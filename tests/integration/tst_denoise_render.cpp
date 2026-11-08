// Live-Denoising über den echten Renderpfad des Players: libmpv-Render-API + OpenGL-FBO, ShaderChain, VideoFilterStage.
// Prüft am ausgegebenen Bild, nicht nur am Shader-Text. Ohne OpenGL (CI ohne GPU) werden die Tests übersprungen.
//
// Messung für docs/DENOISING.md: UVE_DENOISE_CLIPS="a.mp4;b.mp4" setzen, Ergebnis steht im Testprotokoll (Slot measure).
#include "engine/DenoiseShader.h"
#include "engine/ShaderBuilder.h"
#include "player/MpvController.h"
#include "rendering/ShaderChain.h"
#include "rendering/VideoFilterStage.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <clocale>
#include <cmath>

using namespace uve;
using namespace uve::fx;

namespace {

void *procAddr(void *, const char *name)
{
    auto *c = QOpenGLContext::currentContext();
    return c ? reinterpret_cast<void *>(c->getProcAddress(QByteArray(name))) : nullptr;
}

struct Stats { double mean = 0, std = 0; };

Stats regionStats(const QImage &img, const QRect &r)
{
    double sum = 0, sq = 0;
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const double v = qGray(line[x]);
            sum += v; sq += v * v; ++n;
        }
    }
    const double m = sum / n;
    return {m, std::sqrt(std::max(0.0, sq / n - m * m))};
}

// Mittlere Differenz zweier Bilder je Kanal (0 = identisch)
double imageDiff(const QImage &a, const QImage &b)
{
    double sum = 0;
    for (int y = 0; y < a.height(); y += 3) {
        const auto *la = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const auto *lb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); x += 3)
            sum += std::abs(qRed(la[x]) - qRed(lb[x])) + std::abs(qGreen(la[x]) - qGreen(lb[x])) + std::abs(qBlue(la[x]) - qBlue(lb[x]));
    }
    return sum / (3.0 * (a.height() / 3 + 1) * (a.width() / 3 + 1));
}

// Video 16:9 füllt das FBO (1280×720). Clip: links flach (Grau mit Rauschen), ab x = 75 % dunkler Balken (harte Kante).
const QRect kFlat(100, 200, 700, 320);
const QRect kDarkBar(1000, 200, 200, 320);
const QRect kEdgeLeft(940, 200, 12, 320), kEdgeRight(968, 200, 12, 320);   // knapp beiderseits der Kante (x = 960)

} // namespace

class TstDenoiseRender : public QObject
{
    Q_OBJECT
    QOffscreenSurface m_surface;
    QOpenGLContext m_gl;
    std::unique_ptr<QOpenGLFramebufferObject> m_fbo;
    QTemporaryDir m_tmp;
    const QString m_clip = QStringLiteral(UVE_TEST_MEDIA "/denoise-noise-cut.mp4");
    const QString m_longClip = QStringLiteral(UVE_TEST_MEDIA "/test-2audio-sub.mkv");
    int m_seq = 0;
    double m_renderMs = 0;
    int m_renderCount = 0;

    struct Session
    {
        MpvController mpv;
        mpv_render_context *ctx = nullptr;
        QStringList errors;
        ~Session() { if (ctx) mpv_render_context_free(ctx); }
    };

    std::unique_ptr<Session> open(const QString &hwdec, const QString &clip, bool paused = true)
    {
        auto s = std::make_unique<Session>();
        QString err;
        if (!s->mpv.initialize({{"vo", "libmpv"}, {"ao", "null"}, {"hwdec", hwdec}}, &err)) return {};
        connect(&s->mpv, &MpvController::logMessage, this, [p = s.get()](const QString &pre, const QString &lv, const QString &tx) {
            if (lv == QLatin1String("error") || lv == QLatin1String("fatal")) p->errors << pre + ": " + tx;
        });
        mpv_opengl_init_params init{&procAddr, nullptr};
        mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                                     {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init}, {MPV_RENDER_PARAM_INVALID, nullptr}};
        if (mpv_render_context_create(&s->ctx, s->mpv.handle(), params) < 0) return {};
        s->mpv.markRenderReady();
        QSignalSpy loaded(&s->mpv, &MpvController::fileLoaded);
        s->mpv.load(clip, 0, paused);
        QElapsedTimer t; t.start();
        while (loaded.isEmpty() && t.elapsed() < 10000) pump(*s, 20);
        if (loaded.isEmpty()) return {};
        pump(*s, 300);
        return s;
    }

    void pump(Session &s, int ms)
    {
        QElapsedTimer t; t.start();
        do {
            QCoreApplication::processEvents();
            if (mpv_render_context_update(s.ctx) & MPV_RENDER_UPDATE_FRAME) {
                mpv_opengl_fbo mfbo{int(m_fbo->handle()), m_fbo->width(), m_fbo->height(), 0};
                int flip = 0;
                // Nicht auf die Anzeigezeit warten: so misst m_renderMs die reine Renderzeit (mit glFinish)
                int block = 0;
                mpv_render_param p[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &mfbo}, {MPV_RENDER_PARAM_FLIP_Y, &flip},
                                        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block}, {MPV_RENDER_PARAM_INVALID, nullptr}};
                QElapsedTimer rt; rt.start();
                mpv_render_context_render(s.ctx, p);
                m_gl.functions()->glFinish();
                m_renderMs += rt.nsecsElapsed() / 1e6;
                ++m_renderCount;
            } else {
                QThread::yieldCurrentThread();   // msleep(1) schläft unter Windows bis zu 15 ms: Frames kämen zu spät
            }
        } while (t.elapsed() < ms);
    }

    // Standbild an Position t (exakt angesprungen) mit dem aktuellen Filterstand
    QImage frameAt(Session &s, double t)
    {
        s.mpv.seek(t + 0.5 / 30.0, true);    // Framemitte: rundet immer auf denselben Frame (Clip: 30 fps)
        pump(s, 700);
        return m_fbo->toImage().convertToFormat(QImage::Format_RGB32);
    }

    QString shaderFile(const QString &text)
    {
        const QString file = m_tmp.filePath(QStringLiteral("t%1.glsl").arg(++m_seq));
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly)) return {};
        f.write(text.toUtf8());
        return file;
    }

    bool hasGl() const { return m_gl.isValid() && m_fbo; }

    static DenoiseParams params(DenoiseMode mode, int strength = 60, int detail = 40, int deband = 40)
    {
        DenoiseParams p;
        p.mode = mode; p.strength = strength; p.detail = detail; p.debanding = deband;
        return p;
    }

private slots:
    void initTestCase()
    {
        std::setlocale(LC_NUMERIC, "C");
        QSurfaceFormat fmt;
        fmt.setRenderableType(QSurfaceFormat::OpenGL);
        m_gl.setFormat(fmt);
        if (!m_gl.create()) QSKIP("kein OpenGL-Kontext");
        m_surface.setFormat(m_gl.format());
        m_surface.create();
        if (!m_gl.makeCurrent(&m_surface)) QSKIP("OpenGL-Kontext nicht nutzbar");
        m_fbo = std::make_unique<QOpenGLFramebufferObject>(1280, 720);
        std::setlocale(LC_NUMERIC, "C");
    }

    // Jeder GPU-Modus kompiliert unter dem echten Renderer, entrauscht messbar, erhält die Kante.
    // Danach liefert "Aus" exakt das Ausgangsbild.
    void gpuModesFilterAndOffRestoresImage_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("bilateral") << int(DenoiseMode::Bilateral);
        QTest::newRow("adaptive+deband") << int(DenoiseMode::AdaptiveDeband);
        QTest::newRow("nlmeans") << int(DenoiseMode::NLMeans);
    }
    void gpuModesFilterAndOffRestoresImage()
    {
        QFETCH(int, mode);
        auto s = open("auto-safe", m_clip);
        QVERIFY(s);
        ShaderChain chain(&s->mpv);
        QSignalSpy chainErrors(&chain, &ShaderChain::errorOccurred);
        const QString owner = QString::fromLatin1(kDenoiseStageOwner);

        // Alle Bilder vom selben pausierten Frame: Shader-Änderungen dekodieren nichts neu
        s->mpv.seek(1.0, true);
        pump(*s, 700);
        const auto grab = [&] { pump(*s, 400); return m_fbo->toImage().convertToFormat(QImage::Format_RGB32); };
        const QImage off = grab();
        const Stats offFlat = regionStats(off, kFlat);
        const double offEdge = regionStats(off, kEdgeLeft).mean - regionStats(off, kEdgeRight).mean;
        QVERIFY2(offFlat.std > 4, "Testclip enthält kein Rauschen");

        chain.setStage(owner, shaderFile(buildDenoiseShader(params(DenoiseMode(mode)), false)));
        const QImage on = grab();
        const Stats onFlat = regionStats(on, kFlat);
        const double onEdge = regionStats(on, kEdgeLeft).mean - regionStats(on, kEdgeRight).mean;
        qInfo("Rauschen %.2f -> %.2f, Kante %.1f -> %.1f", offFlat.std, onFlat.std, offEdge, onEdge);
        QVERIFY2(s->errors.isEmpty(), qPrintable(s->errors.join('\n')));
        QCOMPARE(chainErrors.size(), 0);
        QVERIFY(onFlat.std < offFlat.std * 0.8);                  // Rauschen deutlich reduziert
        QVERIFY(std::abs(onFlat.mean - offFlat.mean) < 2.0);      // Helligkeit bleibt
        QVERIFY(onEdge > offEdge * 0.9);                          // kein pauschaler Weichzeichner

        chain.setStage(owner, QString());
        const QImage again = grab();
        QCOMPARE(chain.activeShaders().size(), 0);
        QVERIFY2(imageDiff(off, again) < 0.01, "Aus zeigt nicht mehr das Ausgangsbild");
    }

    // Vorher/Nachher: links das unbehandelte Bild, rechts entrauscht, beide Stufen laufen zusammen
    void compareKeepsOriginalOnLeftSide()
    {
        auto s = open("auto-safe", m_clip);
        QVERIFY(s);
        ShaderChain chain(&s->mpv);
        const QString owner = QString::fromLatin1(kDenoiseStageOwner), uve = QStringLiteral("org.nextscript.ultimate-video-enhancer");
        chain.setStage(owner, QString());     // Reihenfolge festlegen: Denoising vor UVE
        Params p; p.compare = true; p.baseTone = false;
        ShaderBuildInput in; in.params = p; in.originalSaved = true;
        chain.setStage(owner, shaderFile(buildDenoiseShader(params(DenoiseMode::Bilateral), true)));
        chain.setStage(uve, shaderFile(buildShader(in)));
        const auto list = chain.activeShaders();
        QCOMPARE(list.size(), 2);
        const QImage img = frameAt(*s, 1.0);
        QVERIFY2(s->errors.isEmpty(), qPrintable(s->errors.join('\n')));
        // Doppelt breites Bild im 16:9-FBO: Höhe halbiert, zentriert
        const Stats left = regionStats(img, QRect(60, 300, 340, 120)), right = regionStats(img, QRect(700, 300, 340, 120));
        qInfo("links %.2f, rechts %.2f", left.std, right.std);
        QVERIFY(left.std > 4);
        QVERIFY(right.std < left.std * 0.8);
    }

    // Kaputter Shader: Stufe wird abgeschaltet, Wiedergabe und andere Stufen laufen weiter, neue Datei läuft wieder
    void brokenShaderIsContained()
    {
        auto s = open("no", m_longClip, false);
        QVERIFY(s);
        ShaderChain chain(&s->mpv);
        QSignalSpy chainErrors(&chain, &ShaderChain::errorOccurred);
        const QString owner = QString::fromLatin1(kDenoiseStageOwner);
        chain.setStage(owner, shaderFile(QStringLiteral("//!HOOK MAIN\n//!BIND HOOKED\n//!DESC %1: kaputt\nvec4 hook() { return gibtEsNicht; }\n").arg(owner)));
        const double p0 = s->mpv.position();
        QElapsedTimer t; t.start();
        while (chainErrors.isEmpty() && t.elapsed() < 4000) pump(*s, 50);
        QVERIFY(!chainErrors.isEmpty());
        QCOMPARE(chainErrors.first().at(0).toString(), owner);
        QVERIFY(chain.activeShaders().isEmpty());
        pump(*s, 500);
        QVERIFY(s->mpv.position() > p0);       // spielt weiter
        QVERIFY(s->mpv.state() == MpvController::Playing);

        chain.setStage(owner, shaderFile(buildDenoiseShader(params(DenoiseMode::Bilateral), false)));
        QCOMPARE(chain.activeShaders().size(), 1);
        const int before = chainErrors.size();
        pump(*s, 500);
        QCOMPARE(chainErrors.size(), before);
    }

    // Temporal entrauscht wirklich über Frames: gleiches Standbild, weniger Rauschen (Clip mit leichtem Rauschen)
    void temporalReducesLightNoise()
    {
        auto s = open("no", QStringLiteral(UVE_TEST_MEDIA "/denoise-light-noise.mp4"));
        QVERIFY(s);
        VideoFilterStage stage(&s->mpv);
        const Stats off = regionStats(frameAt(*s, 1.0), kFlat);
        QString err;
        QVERIFY2(stage.set("t", buildTemporalGraph(params(DenoiseMode::Temporal, 100, 0)), &err), qPrintable(err));
        const Stats on = regionStats(frameAt(*s, 1.0), kFlat);
        qInfo("Rauschen %.2f -> %.2f", off.std, on.std);
        QVERIFY2(s->errors.isEmpty(), qPrintable(s->errors.join('\n')));
        QVERIFY(on.std < off.std * 0.8);
        QVERIFY(std::abs(on.mean - off.mean) < 2.0);
    }

    // Kein Schleier: hinter einem bewegten Objekt mit geringem Kontrast (12 von 255 Stufen) bleibt keine Spur,
    // auch nicht bei voller Stärke. Clip: Balken x = 20 + 2·n (240 px breit), Breite 40, 30 fps.
    void temporalLeavesNoTrailBehindLowContrastMotion_data()
    {
        QTest::addColumn<int>("strength");
        QTest::addColumn<int>("detail");
        QTest::newRow("standard") << 35 << 60;
        QTest::newRow("720p") << 45 << 55;
        QTest::newRow("maximum") << 100 << 0;
    }
    void temporalLeavesNoTrailBehindLowContrastMotion()
    {
        QFETCH(int, strength);
        QFETCH(int, detail);
        auto s = open("no", QStringLiteral(UVE_TEST_MEDIA "/denoise-low-contrast-motion.mp4"));
        QVERIFY(s);
        VideoFilterStage stage(&s->mpv);
        QVERIFY(stage.set("t", buildTemporalGraph(params(DenoiseMode::Temporal, strength, detail))));
        const QImage img = frameAt(*s, 1.0);   // Balken bei x = 80 … 120 von 240
        const double px = img.width() / 240.0;
        const double trail = regionStats(img, QRect(int(45 * px), 200, int(30 * px), 320)).mean;
        const double background = regionStats(img, QRect(int(170 * px), 200, int(50 * px), 320)).mean;
        const double box = regionStats(img, QRect(int(88 * px), 200, int(24 * px), 320)).mean;
        qInfo("Hintergrund %.1f, Spur %.1f, Balken %.1f", background, trail, box);
        QVERIFY2(s->errors.isEmpty(), qPrintable(s->errors.join('\n')));
        QVERIFY(box - background > 8);                       // Balken ist da
        QVERIFY(std::abs(trail - background) < 1.5);         // keine Spur dahinter
    }

    // Szenenschnitt und Seek: keine Frames der anderen Szene
    void temporalRespectsSceneCutAndSeek()
    {
        auto s = open("no", m_clip);
        QVERIFY(s);
        VideoFilterStage stage(&s->mpv);
        const QImage off = frameAt(*s, 1.0);
        const Stats offFlat = regionStats(off, kFlat);
        const QImage offBright = frameAt(*s, 3.0);
        const double brightMean = regionStats(offBright, kFlat).mean;
        QString err;
        QVERIFY2(stage.set("t", buildTemporalGraph(params(DenoiseMode::Temporal, 100, 0)), &err), qPrintable(err));

        // Szenenschnitt bei t = 2 s: kein Frame mischt die Helligkeit der anderen Szene ein
        const double dark = offFlat.mean;
        const double before = regionStats(frameAt(*s, 2.0 - 2.0 / 30.0), kFlat).mean;
        const double posBefore = s->mpv.position();
        const double after = regionStats(frameAt(*s, 2.0 + 2.0 / 30.0), kFlat).mean;
        const double posAfter = s->mpv.position();
        qInfo("Schnitt: dunkel %.1f, davor %.1f (t=%.3f), danach %.1f (t=%.3f), hell %.1f", dark, before, posBefore, after, posAfter, brightMean);
        QVERIFY(std::abs(before - dark) < 2.5);
        QVERIFY(std::abs(after - brightMean) < 2.5);

        // Seek zurück in die andere Szene: Historie ist leer, kein alter Frame sichtbar
        const double seekBack = regionStats(frameAt(*s, 1.0), kFlat).mean;
        QVERIFY(std::abs(seekBack - dark) < 2.0);

        stage.clear("t");
        const QImage restored = frameAt(*s, 1.0);
        QVERIFY2(imageDiff(off, restored) < 0.01, "Aus zeigt nicht mehr das Ausgangsbild");
    }

    // Live-Umschaltung während der Wiedergabe: kein Neustart, Position läuft stetig weiter, Decoder kehrt zurück
    void temporalSwitchesLiveAndRestoresHardwareDecoding()
    {
        auto s = open("auto-safe", m_longClip, false);
        QVERIFY(s);
        VideoFilterStage stage(&s->mpv);
        QSignalSpy reloaded(&s->mpv, &MpvController::fileLoaded);
        pump(*s, 800);
        const QString hwBefore = s->mpv.getProperty("hwdec-current").toString();
        qInfo("Decoder vorher: %s", qPrintable(hwBefore));

        const double p0 = s->mpv.position();
        QElapsedTimer wall; wall.start();
        QVERIFY(stage.set("t", buildTemporalGraph(params(DenoiseMode::Temporal))));
        double last = p0;
        double maxBack = 0;
        auto follow = [&](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms) {
                pump(*s, 50);
                const double p = s->mpv.position();
                maxBack = std::max(maxBack, last - p);
                last = p;
            }
        };
        follow(1800);
        const QString hwDuring = s->mpv.getProperty("hwdec-current").toString();
        qInfo("Decoder während Temporal: %s", qPrintable(hwDuring));
        QVERIFY(hwDuring.isEmpty() || hwDuring == "no" || hwDuring.endsWith("-copy"));
        QVERIFY(stage.status().value("active").toBool());
        QVERIFY(stage.status().value("error").toString().isEmpty());

        // Reglerwert geändert: ersetzt den Eintrag, stapelt nicht (andere vf-Einträge bleiben)
        s->mpv.command({"vf", "add", "@andere:lavfi=[hflip]"});
        QVERIFY(stage.set("t", buildTemporalGraph(params(DenoiseMode::Temporal, 30, 40))));
        follow(700);
        QCOMPARE(s->mpv.getProperty("vf").toList().size(), 2);
        s->mpv.command({"vf", "remove", "@andere"});
        follow(300);

        stage.clear("t");
        follow(1200);
        QVERIFY2(s->errors.isEmpty(), qPrintable(s->errors.join('\n')));
        qInfo("Position %.2f -> %.2f in %.2f s, größter Rücksprung %.3f s", p0, last, wall.elapsed() / 1000.0, maxBack);
        QCOMPARE(s->mpv.getProperty("hwdec").toStringList().join(','), QString("auto-safe"));
        QCOMPARE(s->mpv.getProperty("hwdec-current").toString(), hwBefore);
        QCOMPARE(reloaded.size(), 0);                         // Datei wurde nicht neu geöffnet
        QVERIFY(std::abs((last - p0) - wall.elapsed() / 1000.0) < 0.5);   // Wiedergabezeit folgt der Uhr
        QVERIFY(maxBack < 0.25);                              // kein Sprung zurück
        QCOMPARE(s->mpv.state(), MpvController::Playing);
    }

    // Filter scheitert: Wiedergabe läuft weiter, die Stufe meldet den Fehler und räumt auf
    void failingFilterIsReported()
    {
        auto s = open("auto-safe", m_longClip, false);
        QVERIFY(s);
        VideoFilterStage stage(&s->mpv);
        const QString hwBefore = s->mpv.getProperty("hwdec").toStringList().join(',');
        QVERIFY(stage.set("t", QStringLiteral("atadenoise=gibtesnicht=1")));
        QElapsedTimer t; t.start();
        QVariantMap st = stage.status();
        while (st.value("active").toBool() && t.elapsed() < 4000) { pump(*s, 50); st = stage.status(); }
        qInfo("Fehlermeldung: %s", qPrintable(st.value("error").toString()));
        QVERIFY(!st.value("active").toBool());
        QVERIFY(!st.value("error").toString().isEmpty());
        pump(*s, 300);
        QCOMPARE(s->mpv.getProperty("hwdec").toStringList().join(','), hwBefore);
        const double p0 = s->mpv.position();
        pump(*s, 500);
        QVERIFY(s->mpv.position() > p0);
        QCOMPARE(s->mpv.state(), MpvController::Playing);
    }

    void ownershipAndValidation()
    {
        MpvController mpv;
        QString err;
        QVERIFY(mpv.initialize({{"vo", "null"}, {"ao", "null"}}, &err));
        VideoFilterStage stage(&mpv);
        QVERIFY(!stage.set("a", "movie=c\\:/x.mp4", &err));
        QVERIFY(!stage.set("a", "atadenoise,movie=x", &err));
        QVERIFY(stage.set("a", "atadenoise=s=5"));
        QVERIFY(!stage.set("b", "hqdn3d", &err));              // fremder Besitzer
        stage.clear("b");
        QVERIFY(stage.status().value("active").toBool());      // nur der Besitzer räumt ab
        stage.clear("a");
        QVERIFY(!stage.status().value("active").toBool());
        QVERIFY(stage.set("b", "hqdn3d=0:0:4:4"));
        stage.clear("b");
    }

    // Messwerte je Modus für docs/DENOISING.md
    void measure()
    {
        const QStringList clips = qEnvironmentVariable("UVE_DENOISE_CLIPS").split(';', Qt::SkipEmptyParts);
        if (clips.isEmpty()) QSKIP("UVE_DENOISE_CLIPS nicht gesetzt");
        struct Row { const char *name; DenoiseParams p; };
        const Row rows[] = {{"off", {}}, {"bilateral", params(DenoiseMode::Bilateral, 35, 60)},
                            {"adaptive", params(DenoiseMode::AdaptiveDeband, 40, 60, 35)},
                            {"nlmeans", params(DenoiseMode::NLMeans, 40, 60)}, {"temporal", params(DenoiseMode::Temporal, 45, 55)}};
        for (const QString &clip : clips) {
            for (const auto &r : rows) {
                auto s = open("auto-safe", clip, false);
                QVERIFY(s);
                ShaderChain chain(&s->mpv);
                VideoFilterStage stage(&s->mpv);
                const QString text = buildDenoiseShader(r.p, false), graph = buildTemporalGraph(r.p);
                if (!text.isEmpty()) chain.setStage(QString::fromLatin1(kDenoiseStageOwner), shaderFile(text));
                if (!graph.isEmpty()) QVERIFY(stage.set("m", graph));
                pump(*s, 1500);   // Einschwingen, Decoder ggf. neu gestartet
                const auto drops = [&] { return s->mpv.getProperty("frame-drop-count").toLongLong() + s->mpv.getProperty("decoder-frame-drop-count").toLongLong(); };
                const qint64 d0 = drops();
                const double p0 = s->mpv.position();
                QElapsedTimer t; t.start();
                double maxSync = 0;
                m_renderMs = 0;
                m_renderCount = 0;
                while (t.elapsed() < 6000) {
                    pump(*s, 200);
                    maxSync = std::max(maxSync, std::abs(s->mpv.getProperty("avsync").toDouble()));
                }
                qInfo("%s | %-9s | fps %.1f | Drops %lld | Pos +%.2f/%.2fs | max A/V %.1f ms | Render %.2f ms/Frame | hw %s",
                      qPrintable(QFileInfo(clip).fileName()), r.name, s->mpv.getProperty("estimated-vf-fps").toDouble(),
                      drops() - d0, s->mpv.position() - p0, t.elapsed() / 1000.0, maxSync * 1000.0, m_renderMs / qMax(1, m_renderCount),
                      qPrintable(s->mpv.getProperty("hwdec-current").toString()));
                QVERIFY2(s->errors.isEmpty(), qPrintable(s->errors.join('\n')));
                // Standbilder zum Sichtvergleich (optional): gleiche Position, je Modus eine Datei
                const QString shots = qEnvironmentVariable("UVE_DENOISE_SHOTS");
                if (!shots.isEmpty()) {
                    s->mpv.setPaused(true);
                    s->mpv.seek(qEnvironmentVariable("UVE_DENOISE_SHOT_T", "20").toDouble(), true);
                    pump(*s, 1200);
                    m_fbo->toImage().save(QDir(shots).filePath(QStringLiteral("%1-%2.png").arg(QFileInfo(clip).completeBaseName().left(12), QLatin1String(r.name))));
                }
            }
        }
    }
};

QTEST_MAIN(TstDenoiseRender)
#include "tst_denoise_render.moc"
