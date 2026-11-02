#include "LutLibrary.h"
#include "engine/CubeLut.h"
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
        QCOMPARE(s.count("//!HOOK MAIN"), 3);           // Original + Grading + Detail
        QVERIFY(s.contains("//!SAVE UVEORIG"));
        QVERIFY(s.contains("UVEORIG_tex"));

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
};

QTEST_GUILESS_MAIN(TstUve)
#include "tst_uve.moc"
