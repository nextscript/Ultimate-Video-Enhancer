#include "LutLibrary.h"
#include "engine/AutoSceneMatch.h"
#include "engine/CubeLut.h"
#include "engine/DenoiseShader.h"
#include "engine/ExpertSettings.h"
#include "engine/LutProfile.h"
#include "engine/MiniZip.h"
#include "engine/ShaderBuilder.h"
#include "engine/UveParams.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace uve::fx;

class TstUve : public QObject
{
    Q_OBJECT
    QJsonObject m_ref;
private slots:
    void initTestCase()
    {
        QFile f(QStringLiteral(UVE_TEST_REF));
        QVERIFY2(f.open(QIODevice::ReadOnly), "tests/reference/uve_reference.json fehlt (scripts/uve-reference.mjs)");
        m_ref = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(m_ref.contains("table"));
    }

    // Jede portierte Umrechnungsfunktion gegen die Werte des Original-JavaScripts
    void mappingMatchesOriginal_data()
    {
        QTest::addColumn<QString>("fn");
        for (const auto &k : m_ref.value("table").toObject().keys()) QTest::newRow(qPrintable(k)) << k;
    }
    void mappingMatchesOriginal()
    {
        QFETCH(QString, fn);
        const QHash<QString, std::function<double(double)>> impl{
            {"normU", normU}, {"normSL", normSL}, {"normDN", normDN}, {"normHDR", normHDR}, {"normEDGE", normEDGE},
            {"userGammaValue", userGammaValue}, {"userContrastAmount", userContrastAmount},
            {"userSaturationAmount", userSaturationAmount}, {"userVibranceAmount", userVibranceAmount},
            {"userHueDegrees", userHueDegrees}, {"userGrainAmount", userGrainAmount},
            {"userHighlightsAmount", userHighlightsAmount}, {"userShadowsAmount", userShadowsAmount},
            {"userBlackOffset", userBlackOffset}, {"userWhiteOffset", userWhiteOffset},
        };
        QVERIFY2(impl.contains(fn), qPrintable("nicht portiert: " + fn));
        for (const auto &pair : m_ref.value("table").toObject().value(fn).toArray()) {
            const double in = pair.toArray().at(0).toDouble(), expected = pair.toArray().at(1).toDouble();
            const double got = impl[fn](in);
            QVERIFY2(std::fabs(got - expected) < 1e-9, qPrintable(QStringLiteral("%1(%2) = %3, UVE = %4").arg(fn).arg(in).arg(got).arg(expected)));
        }
    }

    void curveConstantsMatchOriginal()
    {
        const auto c = m_ref.value("constants").toObject();
        const auto &k = curveConstants();
        QCOMPARE(k.contrastPivotExp, c.value("contrastPivotExp").toDouble());
        QVERIFY(std::fabs(k.highlightsPeak - c.value("highlightsCurvePeak").toDouble()) < 1e-12);
        QVERIFY(std::fabs(k.shadowsPeak - c.value("shadowsCurvePeak").toDouble()) < 1e-12);
        QVERIFY(std::fabs(k.grainPeakExp - c.value("grainPeakExp").toDouble()) < 1e-12);
    }

    void shaderValuesFollowUpdateParams()
    {
        Params p; // Standard: Grundton an
        auto s = computeShaderValues(p);
        QCOMPARE(s.contrast, 1.05);
        QCOMPARE(s.saturation, 1.15);
        QCOMPARE(s.brightness, 1.02);
        p.baseTone = false; p.profile = "anime"; p.darkMoody = true; p.vibrantSat = true;
        s = computeShaderValues(p);
        QVERIFY(std::fabs(s.saturation - 1.16 * 0.92 * 1.35) < 1e-12);
        QVERIFY(std::fabs(s.brightness - 1.03 * 0.96) < 1e-12);
        p = Params(); p.hdr = 2.0;
        QCOMPARE(computeShaderValues(p).hdr, 0.65); // gpuMaxSingleVideo
        p.hdr = -0.5;
        QVERIFY(computeShaderValues(p).hdr < 0);    // negativ: im GPU-Shader ohne Wirkung (clamp 0..1)
        p = Params(); p.sl = 1.0; p.sharpen = 2;
        s = computeShaderValues(p);
        QVERIFY(std::fabs(s.sharpen - 0.3) < 1e-12);       // Detail-Schärfe: 0.15 je Schritt
        QCOMPARE(s.usmAmount, 1.0);                         // SL > 0: Unschärfemaske
        QCOMPARE(s.usmBlur, 0.0);
        p.sl = -0.5;
        s = computeShaderValues(p);
        QCOMPARE(s.usmAmount, 0.0);
        QCOMPARE(s.usmBlur, 0.5);                           // SL < 0: weichzeichnen
        p = Params(); p.tealOrange = true;
        s = computeShaderValues(p);
        QCOMPARE(s.hueDeg, 0.0);                            // kein Farbton-Trick mehr, echtes Split-Toning
        QVERIFY(s.tealOrange);
        QVERIFY(buildShader({p, nullptr}).contains("float tsh"));
        QVERIFY(!buildShader({}).contains("float tsh"));
        p = Params(); p.black = 5; p.white = -5;
        s = computeShaderValues(p);
        QVERIFY(std::fabs(s.inBlack - 0.06) < 1e-12);
        QVERIFY(std::fabs(s.outWhite - 0.94) < 1e-12);
    }

    void importsShippedUveProfiles()
    {
        QFile f(QStringLiteral(UVE_TEST_DATA "/presets/UVE_My_Profile.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray original = f.readAll();
        const auto r = paramsFromProfile(QJsonDocument::fromJson(original).object());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.params.sl, 1.2);
        QCOMPARE(r.params.darkMoody, true);
        QCOMPARE(r.params.profile, QString("user"));
        QCOMPARE(r.params.lutProfile, QString("Fujifilm||DCI-P3 Fujifilm 3513DI D55"));
        // Roundtrip: Export → Import liefert dieselben Parameter, Original-Felder bleiben erhalten
        const auto exported = profileFromParams(r.params, r.passthrough);
        const auto back = paramsFromProfile(exported);
        QVERIFY(back.ok);
        QVERIFY(back.params == r.params);
        QCOMPARE(exported.value("lutProfile").toString(), QString("Fujifilm||DCI-P3 Fujifilm 3513DI D55"));
        QCOMPARE(profileFromParams(Params()).value("lutProfile").toString(), QString("none")); // wie UVE
        QCOMPARE(exported.value("autoStrength").toDouble(), 0.65);
        // Quelldatei unverändert (Import liest nur)
        QFile again(f.fileName());
        QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(again.readAll(), original);
    }

    void rejectsBrokenProfiles()
    {
        QVERIFY(!paramsFromProfile(QJsonObject{{"schema", "something-else"}}).ok);
        QVERIFY(!paramsFromProfile(QJsonObject{{"schema", "gvf-settings"}, {"sl", "abc"}}).ok);
        const auto r = paramsFromProfile(QJsonObject{{"schema", "gvf-settings"}, {"sl", 99}, {"user", QJsonObject{{"contrast", -50}}}});
        QVERIFY(r.ok); // Bereichsüberschreitung wird begrenzt, nicht abgelehnt
        QCOMPARE(r.params.sl, 2.0);
        QCOMPARE(r.params.contrast, -10.0);
    }

    void parsesShippedLut()
    {
        const auto r = loadCubeFile(QStringLiteral(UVE_TEST_DATA "/luts/NaturalBoost.cube"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.lut.size, 33);
        QCOMPARE(r.lut.title, QString("NaturalBoost"));
        QCOMPARE(r.lut.rgb.size(), 33 * 33 * 33 * 3);
    }

    void rejectsBrokenLuts()
    {
        QVERIFY(!parseCube("LUT_3D_SIZE 2\n0 0 0\n1 1 1\n").ok);                       // zu wenige Werte
        QVERIFY(!parseCube("LUT_1D_SIZE 16\n").ok);                                     // 1D nicht unterstützt
        QVERIFY(!parseCube("LUT_3D_SIZE 2\n0 0 x\n").ok);                               // keine Zahl
        QVERIFY(!parseCube("0 0 0\n").ok);                                              // Größe fehlt
        QByteArray ok = "LUT_3D_SIZE 2\n";
        for (int i = 0; i < 8; ++i) ok += QByteArray::number(i & 1) + " " + QByteArray::number((i >> 1) & 1) + " " + QByteArray::number((i >> 2) & 1) + "\n";
        QVERIFY(parseCube(ok).ok);
    }

    void halfFloatConversion()
    {
        QCOMPARE(floatToHalf(0.0f), quint16(0x0000));
        QCOMPARE(floatToHalf(1.0f), quint16(0x3C00));
        QCOMPARE(floatToHalf(-2.0f), quint16(0xC000));
        QCOMPARE(floatToHalf(0.5f), quint16(0x3800));
    }

    void shaderStructure()
    {
        ShaderBuildInput in;
        QString s = buildShader(in);
        QCOMPARE(s.count("//!HOOK MAIN"), 2);           // Grading + Detail (kein Vergleich, SL = 0)
        QVERIFY(s.contains("//!DESC org.nextscript.ultimate-video-enhancer:"));
        QVERIFY(!s.contains("nan") && !s.contains("inf"));
        QVERIFY(!s.contains("UVEORIG"));
        QVERIFY(!s.contains("UVEBLUR"));
        in.params.sl = 1.5;
        s = buildShader(in);
        QCOMPARE(s.count("//!HOOK MAIN"), 4);           // Unschärfemaske (2 Pässe) + Grading + Detail
        QVERIFY(s.contains("//!SAVE UVEBLUR") && s.contains("UVEBLUR_texOff"));
        QVERIFY(s.contains("src.rgb + 1.500000 * (src.rgb - bl.rgb)"));
        in.params.sl = 0;
        in.params.compare = true;
        s = buildShader(in);
        QCOMPARE(s.count("//!HOOK MAIN"), 4);           // Original + Grading + Detail + Vergleich (doppelt breit)
        QVERIFY(s.contains("//!SAVE UVEORIG"));
        QVERIFY(s.contains("UVEORIG_tex"));
        QVERIFY(s.contains("//!WIDTH HOOKED.w 2 *"));
        // Die Schieberposition steckt nicht im Shader: Ziehen darf mpvs Render-Kette nicht neu aufbauen
        in.params.compareSplit = 0.2;
        const QString a = buildShader(in);
        in.params.compareSplit = 0.8;
        QCOMPARE(buildShader(in), a);

        const auto lut = loadCubeFile(QStringLiteral(UVE_TEST_DATA "/luts/NaturalBoost.cube"));
        in.lut = &lut.lut;
        s = buildShader(in);
        QVERIFY(s.contains("//!BIND UVELUT"));       // Pässe binden die Textur …
        QVERIFY(!s.contains("//!TEXTURE"));          // … die in einer eigenen Datei liegt
        QVERIFY(s.size() < 20000);                   // Regler-Änderungen erzeugen nur kleine Dateien
        const QString t = buildLutShader(lut.lut);
        QVERIFY(t.contains("//!TEXTURE UVELUT"));
        QVERIFY(t.contains("//!SIZE 33 33 33"));
        const int hexStart = t.indexOf("//!BORDER CLAMP\n") + 16;
        const int hexEnd = t.indexOf('\n', hexStart);
        QCOMPARE(hexEnd - hexStart, 33 * 33 * 33 * 4 * 2 * 2); // rgba16f = 8 Byte/Texel = 16 Hex-Zeichen
    }

    void inflateAndCrc()
    {
        QCOMPARE(crc32("123456789"), 0xCBF43926u);           // Standard-Prüfwert
        QByteArray out;
        QVERIFY(!inflateRaw(QByteArray("\xff\xff\xff", 3), 1024, &out)); // ungültiger Blocktyp
        // Testpaket (Python zipfile): deflate + stored, ein kaputtes Profil, eine Fremddatei
        QFile f(QStringLiteral(UVE_TEST_REF_DIR "/lut_package_test.zip"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto z = readZip(f.readAll(), [](const QString &n) { return n.endsWith(".json"); });
        QVERIFY2(z.ok, qPrintable(z.error));
        QCOMPARE(z.entries.size(), 3);
        QVERIFY(QJsonDocument::fromJson(z.entries[0].data).isObject());
        QVERIFY(!readZip("PK\x03\x04garbage", [](const QString &) { return true; }).ok);
    }

    void lutProfilesFromRealPackage()
    {
        // Echtes UVE-Paket LUTsProfiles_v3.0.zip (aus dem Repository, unverändert)
        QFile f(QStringLiteral(UVE_TEST_REF_DIR "/LUTsProfiles_v3.0.zip"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray zip = f.readAll();
        QTemporaryDir dir;
        LutLibrary lib(dir.path());
        QString msg;
        QCOMPARE(lib.importPackage(zip, "LUTsProfiles_v3.0.zip", &msg), 83); // 83 Profile + _lut_groups.json
        const auto *p = lib.find("Fujifilm||DCI-P3 Fujifilm 3513DI D55"); // vom Beispielprofil My_Profile.json verlangt
        QVERIFY(p);
        QCOMPARE(p->matrix.size(), 20);
        QVERIFY(lib.find("Fresh LUTs||Agfa Ultra 100"));
        // Persistenz
        LutLibrary again(dir.path());
        QCOMPARE(again.count(), 83);
        QVERIFY(!msg.contains("ungültig"));
        QCOMPARE(again.source(), QString("LUTsProfiles_v3.0.zip"));
        // Prüfsummen wie in den Dateilisten von GitHub (Git-Blob-SHA-1) und jsDelivr (SHA-256, Base64)
        QVERIFY(LutLibrary::verify(zip, {"LUTsProfiles_v3.0.zip", "o8qfsfcHDjwfcNGXlRZ+N+4UlgyFcQiMQGcTrNBvfR0=", "sha256-b64", 32166}));
        QVERIFY(!LutLibrary::verify(zip + "x", {"LUTsProfiles_v3.0.zip", "o8qfsfcHDjwfcNGXlRZ+N+4UlgyFcQiMQGcTrNBvfR0=", "sha256-b64", 0}));
        QCOMPARE(LutLibrary::gitBlobSha1(""), QByteArray("e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"));

        QTemporaryDir dir2;
        LutLibrary small(dir2.path());
        QFile t(QStringLiteral(UVE_TEST_REF_DIR "/lut_package_test.zip"));
        QVERIFY(t.open(QIODevice::ReadOnly));
        QCOMPARE(small.importPackage(t.readAll(), "test.zip", &msg), 2);
        QVERIFY(msg.contains("1 ungültige"));
        QVERIFY(small.find("Test Gruppe||Deflate Look")); // Gruppe aus dem Dateinamen "Gruppe__Name.json"
        QVERIFY(small.find("Self||Stored"));
    }

    void packageListingAndSelection()
    {
        const QByteArray gh = R"([{"type":"file","name":"LUTsProfiles_v2.0.zip","sha":"a","size":1},
                                  {"type":"file","name":"LUTsProfiles_v3.0.zip","sha":"b","size":2},
                                  {"type":"file","name":"LUTsProfiles_v10.zip.bak","sha":"c","size":3},
                                  {"type":"dir","name":"LUTsProfiles_v9.0.zip","sha":"d","size":0},
                                  {"type":"file","name":"My_Profile.json","sha":"e","size":797}])";
        const auto files = LutLibrary::parseGithubListing(gh);
        QCOMPARE(files.size(), 4);
        QCOMPARE(files[LutLibrary::pickLatestPackage(files)].name, QString("LUTsProfiles_v3.0.zip")); // aktuell: v3.0
        const QByteArray js = R"({"files":[{"name":"/LUTsProfiles_v2.0.zip","hash":"x","size":1},
                                            {"name":"/LUTsProfiles_v3.0.zip","hash":"y","size":2},
                                            {"name":"/sub/LUTsProfiles_v9.0.zip","hash":"z","size":3}]})";
        const auto jf = LutLibrary::parseJsdelivrListing(js);
        QCOMPARE(jf.size(), 2);
        QCOMPARE(jf[LutLibrary::pickLatestPackage(jf)].checksum, QString("y"));
        QCOMPARE(LutLibrary::pickLatestPackage({}), -1);
    }

    void lutProfileMatrixInShader()
    {
        LutProfile lp;
        QVERIFY(!parseLutProfile(QJsonObject{{"name", "x"}, {"matrix4x5", QJsonArray{1, 2}}}).ok);
        QVERIFY(!parseLutProfile(QJsonObject{{"schema", "other"}}).ok);
        QVector<double> m(20, 0.0);
        m[0] = 0.5; m[6] = 0.25; m[12] = 0.125; m[4] = 0.01; m[18] = 1;
        ShaderBuildInput in;
        in.lutMatrix = &m;
        const QString s = buildShader(in);
        QVERIFY(s.contains("vec3 lm = vec3("));
        QVERIFY(s.contains("0.500000 * color.r"));
        QVERIFY(!buildShader({}).contains("vec3 lm"));
    }

    void svgLevelsFromExampleProfile()
    {
        // My_Profile.json: SR 1.1, BL 0.7, WL 0.2 (UVE-Hauptregler „Basis“)
        QFile f(QStringLiteral(UVE_TEST_DATA "/presets/UVE_My_Profile.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto r = paramsFromProfile(QJsonDocument::fromJson(f.readAll()).object());
        QVERIFY(r.ok);
        QCOMPARE(r.params.bl, 0.7);
        QCOMPARE(r.params.wl, 0.2);
        QCOMPARE(r.params.sr, 1.1);
        const auto s = computeShaderValues(r.params);
        QVERIFY(std::fabs(s.levelBlackOffset - (-0.028)) < 1e-12);   // −0.7·0.04
        QVERIFY(std::fabs(s.levelWhiteAdj - 0.012) < 1e-12);         // 0.2·0.06
        QVERIFY(std::fabs(s.sharpenRadius - 1.55) < 1e-12);         // 1 + |SR|·0.5
        QVERIFY(std::fabs(s.usmSigma - (0.7 + 1.1 * 0.65)) < 1e-12);
        const auto t = whiteLevelTable(s.levelWhiteAdj);
        QCOMPARE(t.size(), 17);
        QCOMPARE(t[12], 0.75);                                       // unterhalb des Knies 0.78 unverändert
        QCOMPARE(t[13], 0.8143);                                     // 0.8125 + 0.012·(0.0325/0.22), toFixed(4)
        QCOMPARE(t[16], 1.0);                                        // begrenzt
        const QString sh = buildShader({r.params, nullptr});
        QVERIFY(sh.contains("color = clamp(color + vec3(-0.028000), 0.0, 1.0);"));
        QVERIFY(sh.contains("const float WLT[17]"));
        QVERIFY(sh.contains("//!SAVE UVEBLUR"));                     // SL 1.2 → Unschärfemaske
        // Neutral: keine Pegel-Blöcke
        const QString neutral = buildShader({});
        QVERIFY(!neutral.contains("WLT") && !neutral.contains("color + vec3("));
    }

    void neutralBlocksAreOmitted()
    {
        Params p; p.baseTone = false;
        const QString s = buildShader({p, nullptr});
        QVERIFY(!s.contains("applyHueRotate(color,"));  // Farbton 0 → kein Rotationsaufruf
        QVERIFY(!s.contains("tonemapACES(lin)"));       // HDR 0 → kein HDR-Block
        QVERIFY(!s.contains("lumaDelta"));              // Schärfe 0 → kein Detailblock
    }

    // ---------------------------------------------------------------- Auto-Scene-Match
    static QImage solid(QRgb c, int w = 96, int h = 54) { QImage i(w, h, QImage::Format_RGB32); i.fill(c); return i; }

    void autoFrameStats()
    {
        const auto s = AutoSceneMatch::computeFrameStats(solid(qRgb(128, 128, 128)));
        QVERIFY(std::fabs(s.mY - 128.0 / 255) < 1e-9);
        QCOMPARE(s.sdY, 0.0);
        QCOMPARE(s.mCh, 0.0);
        QCOMPARE(s.mSat, 0.0);
        QCOMPARE(s.skin, 0.0);
        const auto d = AutoSceneMatch::computeFrameStats(solid(qRgb(0, 0, 0)));
        QCOMPARE(d.mSat, 0.25);    // keine hellen Pixel → Standardwert wie im Userscript
    }

    void autoDarkFlatSceneIsLifted()
    {
        AutoSceneMatch a;
        // Dunkle, flache, wechselnde Szene → nach Bewegungsphase: Helligkeit und Kontrast steigen
        qint64 t = 0;
        a.reset(t, false);
        for (int i = 0; i < 400; ++i) {
            t += 100;
            a.step(solid(i % 2 ? qRgb(40, 40, 46) : qRgb(46, 44, 40)), t);
        }
        QVERIFY(a.current().br > 1.05);
        QVERIFY(a.current().ct > 1.0);
        QVERIFY(a.current().br <= 1.22 && a.current().ct <= 1.30 && a.current().sat <= 1.15);
    }

    void autoStaticFrameDoesNotUpdate()
    {
        AutoSceneMatch a;
        a.reset(0, false);
        // Standbild: nach dem Abklingen der Bewegungs-EMA keine Anpassung mehr (motionThresh/motionMinFrames)
        for (int i = 1; i <= 40; ++i) a.step(solid(qRgb(30, 30, 30)), i * 100);
        const AutoGrade before = a.current();
        for (int i = 41; i <= 60; ++i) QVERIFY(!a.step(solid(qRgb(30, 30, 30)), i * 100));
        QCOMPARE(a.current().br, before.br);
        QCOMPARE(a.nextIntervalMs(), 500);   // Mindestrate 2 fps
    }

    void autoMpvEqMapping()
    {
        // UVE: out = br·ct·x + br·0.5·(1−ct)  ↔  mpv: out = (1 + contrast/100)·x + brightness/100
        AutoGrade g; g.br = 1.1; g.ct = 1.2; g.sat = 1.1; g.hue = 6;
        const MpvEq e = AutoSceneMatch::toMpvEq(g);
        QVERIFY(std::fabs(e.contrast - 32.0) < 0.01);
        QVERIFY(std::fabs(e.brightness - (-11.0)) < 0.01);
        QVERIFY(std::fabs(e.saturation - 10.0) < 0.01);
        QVERIFY(std::fabs(e.hue - (-6.0 / 180 * 100)) < 0.01);   // Drehrichtung von mpv umgekehrt
        QCOMPARE(AutoSceneMatch::toMpvEq(AutoGrade()), MpvEq());
    }

    void autoRemoveEqInvertsMpv()
    {
        // Vorwärts wie mpv csputils: [U,V] um −θ drehen und mit s skalieren, dann out = c·rgb + b
        const MpvEq eq{6.0, 18.0, 12.0, -4.0};   // brightness, contrast, saturation, hue
        const double c = 1.18, b = 0.06, s = 1.12, th = -4.0 / 100 * 3.14159265358979323846;
        const double LR = 0.2126, LG = 0.7152, LB = 0.0722;
        QImage in(4, 1, QImage::Format_RGB32), out(4, 1, QImage::Format_RGB32);
        const QRgb cols[4] = {qRgb(150, 90, 60), qRgb(60, 120, 170), qRgb(100, 100, 100), qRgb(90, 140, 70)};
        for (int i = 0; i < 4; ++i) {
            in.setPixel(i, 0, cols[i]);
            const double r = qRed(cols[i]) / 255.0, g = qGreen(cols[i]) / 255.0, bl = qBlue(cols[i]) / 255.0;
            const double Y = LR * r + LG * g + LB * bl, u = (bl - Y) / 1.8556, v = (r - Y) / 1.5748;
            const double u2 = s * (std::cos(th) * u + std::sin(th) * v), v2 = s * (-std::sin(th) * u + std::cos(th) * v);
            const double r2 = Y + 1.5748 * v2, b2 = Y + 1.8556 * u2, g2 = (Y - LR * r2 - LB * b2) / LG;
            auto q = [&](double t) { return int(std::lround(std::clamp(c * t + b, 0.0, 1.0) * 255)); };
            out.setPixel(i, 0, qRgb(q(r2), q(g2), q(b2)));
        }
        const QImage back = AutoSceneMatch::removeEq(out, eq);
        for (int i = 0; i < 4; ++i) {
            const QRgb a = back.pixel(i, 0), e = in.pixel(i, 0);
            QVERIFY2(std::abs(qRed(a) - qRed(e)) <= 2 && std::abs(qGreen(a) - qGreen(e)) <= 2 && std::abs(qBlue(a) - qBlue(e)) <= 2,
                     qPrintable(QStringLiteral("Pixel %1").arg(i)));
        }
        QCOMPARE(AutoSceneMatch::removeEq(in, MpvEq()), in);
    }

    void autoProfileRoundtrip()
    {
        Params p; p.autoOn = false; p.autoStrength = 0.4; p.autoLockWB = false;
        const auto o = profileFromParams(p);
        QCOMPARE(o.value("autoOn").toBool(), false);
        QCOMPARE(o.value("autoStrength").toDouble(), 0.4);
        const auto r = paramsFromProfile(o);
        QVERIFY(r.ok);
        QCOMPARE(r.params.autoOn, false);
        QCOMPARE(r.params.autoStrength, 0.4);
        QCOMPARE(r.params.autoLockWB, false);
        QVERIFY(!r.passthrough.contains("autoOn"));
    }

    // ---------------------------------------------------------------- Expert Settings
    void expertDefaultsMatchPreviousBehaviour()
    {
        setExpert(ExpertSettings());
        QVERIFY(expertDiff(ExpertSettings()).isEmpty());
        Params p; p.sharpen = 2;
        QVERIFY(std::fabs(computeShaderValues(p).sharpen - 0.3) < 1e-12);   // 0.1 × 1.5 je Schritt
    }

    void expertOverridesApply()
    {
        ExpertSettings e;
        e.contrast.perStep = 0.08;
        e.hue.reverse = true;
        e.gamma.maxShadowSlope = 0;
        setExpert(e);
        QVERIFY(std::fabs(userContrastAmount(5) - 0.40) < 1e-12);
        QCOMPARE(userHueDegrees(2), -6.0);
        QCOMPARE(curveConstants().gammaShadowSlope, 0.0);   // 0 = aus
        setExpert(ExpertSettings());
        QVERIFY(std::fabs(userContrastAmount(5) - 0.20) < 1e-12);
    }

    void expertExportImportRoundtrip()
    {
        ExpertSettings e;
        e.black.softness = 0.1;
        e.saturation.desaturateEvenly = false;
        const QJsonObject exp = expertExport(e, "1.0.2");
        QCOMPARE(exp.value("type").toString(), QString("gvf-expert-settings"));
        const auto diff = exp.value("settings").toObject();
        QCOMPARE(diff.size(), 2);
        QCOMPARE(diff.value("GVF_BLACK_SETTINGS").toObject().value("softness").toDouble(), 0.1);
        ExpertSettings back;
        QCOMPARE(expertImport(back, exp), 2);
        QCOMPARE(back.black.softness, 0.1);
        QCOMPARE(back.saturation.desaturateEvenly, false);
        // Fremdes JSON wird abgelehnt, unbekannte Schlüssel/Typen ignoriert
        QCOMPARE(expertImport(back, QJsonObject{{"type", "gvf-settings"}}), -1);
        ExpertSettings plain;
        QCOMPARE(expertImport(plain, QJsonObject{{"GVF_HUE_SETTINGS", QJsonObject{{"perStep", "2.5"}, {"reverse", 1}, {"bogus", 3}}}}), 1);
        QCOMPARE(plain.hue.perStep, 2.5);
        QCOMPARE(plain.hue.reverse, false);
    }

    void denoiseSettingsAreSeparateAndSafe()
    {
        // Ältere Installationen haben keinen Block: Aus
        QCOMPARE(denoiseFromJson({}).mode, DenoiseMode::Off);
        // Unbekannter Modus (z. B. aus einer neueren Version) und Werte außerhalb des Bereichs
        const auto odd = denoiseFromJson({{"mode", "gibtesnicht"}, {"strength", 900}, {"detailPreservation", -4}, {"debanding", 50}});
        QCOMPARE(odd.mode, DenoiseMode::Off);
        QCOMPARE(odd.strength, 100);
        QCOMPARE(odd.detail, 0);
        // Rundlauf aller Modi unter den vereinbarten Schlüsseln
        for (const auto mode : {DenoiseMode::Off, DenoiseMode::Bilateral, DenoiseMode::AdaptiveDeband, DenoiseMode::Temporal, DenoiseMode::NLMeans}) {
            DenoiseParams p; p.mode = mode; p.strength = 12; p.detail = 34; p.debanding = 56;
            const QJsonObject j = denoiseToJson(p);
            QCOMPARE(j.value("schemaVersion").toInt(), kDenoiseSchemaVersion);
            QCOMPARE(j.value("detailPreservation").toInt(), 34);
            QVERIFY(denoiseFromJson(j) == p);
        }
        // dn/gpuBase behalten ihre Bedeutung: ohne Wirkung auf den Shader, weiter im Profil-Rundlauf
        Params p; p.dn = 1.5; p.gpuBase = true;
        QCOMPARE(buildShader({p, nullptr}), buildShader({}));
        QCOMPARE(paramsFromJson(paramsToJson(p)).dn, 1.5);
        QVERIFY(!paramsToJson(p).contains("denoising"));
    }

    void denoiseStagesByMode()
    {
        DenoiseParams p;
        QVERIFY(buildDenoiseShader(p, true).isEmpty());      // Aus: keine Stufe, auch nicht für den Vergleich
        QVERIFY(buildTemporalGraph(p).isEmpty());

        p.mode = DenoiseMode::Bilateral;
        QString s = buildDenoiseShader(p, false);
        QVERIFY(s.contains("Bilateral"));
        QVERIFY(!s.contains("UVEORIG"));
        QVERIFY(buildDenoiseShader(p, true).contains("//!SAVE UVEORIG"));
        QVERIFY(buildTemporalGraph(p).isEmpty());
        // Jede //!DESC-Zeile trägt die Stufe, die ShaderChain nutzt die Zuordnung bei Fehlern
        for (const auto &line : s.split('\n'))
            if (line.startsWith("//!DESC")) QVERIFY(line.contains(QString::fromLatin1(kDenoiseStageOwner) + ":"));

        p.mode = DenoiseMode::AdaptiveDeband;
        s = buildDenoiseShader(p, false);
        QCOMPARE(s.count("//!HOOK MAIN"), 2);                // Entrauschen und Debanding sind getrennte Pässe
        QVERIFY(s.contains("Adaptive Bilateral") && s.contains(": Deband"));
        p.debanding = 0;
        QVERIFY(!buildDenoiseShader(p, false).contains(": Deband"));
        p.debanding = 40; p.strength = 0;
        QVERIFY(!buildDenoiseShader(p, false).contains("Adaptive Bilateral"));
        p.debanding = 0;
        QVERIFY(buildDenoiseShader(p, false).isEmpty());

        p = DenoiseParams(); p.mode = DenoiseMode::NLMeans;
        QVERIFY(buildDenoiseShader(p, false).contains(": NLMeans"));
        p.strength = 0;
        QVERIFY(buildDenoiseShader(p, false).isEmpty());

        // Temporal: keine Shader-Stufe (eine stateless Mischung wäre kein zeitlicher Filter), kleines festes Fenster
        p = DenoiseParams(); p.mode = DenoiseMode::Temporal;
        QVERIFY(buildDenoiseShader(p, false).isEmpty());
        QVERIFY(buildTemporalGraph(p).startsWith("atadenoise="));
        p.strength = 100; p.detail = 0;
        const QString strongest = buildTemporalGraph(p);
        QVERIFY(strongest.endsWith(":s=7"));
        // Obergrenze gegen den Schleier hinter bewegten Objekten: Schwelle unter 12 von 255 Stufen
        const double threshold = strongest.section("0a=", 1).section(':', 0, 0).toDouble();
        QVERIFY(threshold > 0 && threshold * 255.0 < 12.0);
        p.strength = 1;
        QVERIFY(buildTemporalGraph(p).endsWith(":s=5"));
        p.strength = 0;
        QVERIFY(buildTemporalGraph(p).isEmpty());
    }

    void denoisePresetsKeepMode()
    {
        DenoiseParams p;
        QVERIFY(denoisePreset(p, "720p") == p);              // Aus bleibt unverändert
        p.mode = DenoiseMode::AdaptiveDeband;
        const auto hd = denoisePreset(p, "720p");
        QCOMPARE(hd.mode, DenoiseMode::AdaptiveDeband);
        QCOMPARE(hd.strength, 40);
        QVERIFY(denoisePreset(p, "4k").strength < denoisePreset(p, "480p").strength);
    }

    // Vergleich: die Denoising-Stufe sichert das Original, die UVE-Stufe darf es nicht überschreiben
    void compareWithDenoiseOriginal()
    {
        ShaderBuildInput in;
        in.params.compare = true;
        QVERIFY(buildShader(in).contains("//!SAVE UVEORIG"));
        in.originalSaved = true;
        const QString s = buildShader(in);
        QVERIFY(!s.contains("//!SAVE UVEORIG"));
        QVERIFY(s.contains("//!BIND UVEORIG"));
        in.params.compare = false;
        ShaderBuildInput plain;
        plain.params.compare = false;
        QCOMPARE(buildShader(in), buildShader(plain));       // ohne Vergleich ohne jede Wirkung
    }
};

QTEST_GUILESS_MAIN(TstUve)
#include "tst_uve.moc"
