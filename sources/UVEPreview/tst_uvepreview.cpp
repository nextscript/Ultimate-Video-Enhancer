// Headless-Tests für UVEPreview (kein Fenster auf dem Desktop: QT_QPA_PLATFORM=offscreen).
//  1. Decoder mit echter libmpv: Bilder an Anfang/Mitte/Ende, kurzes Video, Hochformat, Unicode-Pfad mit
//     Leerzeichen, HDR (PQ) und Audiodatei (keine Vorschau). Testdateien erzeugt ffmpeg (fehlt es: übersprungen).
//  2. Plugin-Modul (QPluginLoader) mit Fake-Host an der echten SeekBar.qml des Players: Hover zeigt Popup mit
//     Bild + Zeit, Tooltip wird ausgeblendet, Verlassen/Ausschalten/Medienwechsel/Deaktivieren räumen auf.
#include "PreviewWorker.h"
#include "PluginApi.h"

#include <QDir>
#include <QJsonObject>
#include <QPluginLoader>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace uve::preview;

// ---------------------------------------------------------------- Fakes für SeekBar und Host
class FakePlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double duration MEMBER duration NOTIFY changed)
    Q_PROPERTY(double position MEMBER position NOTIFY changed)
    Q_PROPERTY(double bufferedUntil MEMBER bufferedUntil NOTIFY changed)
    Q_PROPERTY(int state MEMBER state NOTIFY changed)
    Q_PROPERTY(bool seekable MEMBER seekable NOTIFY changed)
public:
    double duration = 0, position = 0, bufferedUntil = 0;
    int state = 0;
    bool seekable = true;
    int seeks = 0;
    Q_INVOKABLE void seek(double, bool) { ++seeks; }
signals:
    void changed();
};

class FakeApp : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap liveRewind READ liveRewind CONSTANT)
public:
    QVariantMap liveRewind() const { return {}; }
    Q_INVOKABLE QString formatTime(double s) const
    {
        const int t = int(s);
        return QStringLiteral("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
    }
    Q_INVOKABLE void liveRewindSeek(double) {}
    Q_INVOKABLE void liveRewindGoLive() {}
};

class FakeHost : public uve::IPlayerHost
{
public:
    QJsonObject settings;
    QVariantMap state;
    QString hostVersion() const override { return QStringLiteral("1.3.1"); }
    int apiVersion() const override { return 1; }
    QJsonObject loadPluginSettings(const QString &) const override { return settings; }
    void savePluginSettings(const QString &, const QJsonObject &s) override { settings = s; }
    QString pluginDataDir(const QString &) const override { return QDir::tempPath(); }
    void setShaderStage(const QString &, const QString &) override {}
    void showOsd(const QString &, int) override {}
    void log(const QString &, const QString &level, const QString &message) override { qDebug().noquote() << "[plugin]" << level << message; }
    QString currentMediaPath() const override { return state.value("path").toString(); }
    double currentPosition() const override { return 0; }
    bool isPaused() const override { return true; }
    void setPaused(bool) override {}
    QImage grabVideoFrame(int) override { return {}; }
    QVariant call(const QString &, const QString &command, const QVariantMap &, QString *) override
    {
        if (command == QLatin1String("app.info")) return QVariantMap{{"language", "de"}, {"version", "1.3.1"}};
        if (command == QLatin1String("player.state")) return state;
        return {};   // mpv.get: unbekannt → Decoder entscheidet selbst
    }
    bool hasCommand(const QString &) const override { return true; }
};

// ---------------------------------------------------------------- Test
class TstUvePreview : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void stepGrid();
    void framesStartMiddleEnd();
    void shortPortraitUnicode();
    void hdrIsSdr();
    void audioUnsupported();
    void latestJobWins();
    void uhdTiming();
    void lockedFileRetry();
    void userFile();
    void releasesFileWhenIdle();
    void seekbarIntegration();

private:
    bool makeVideo(const QString &file, const QStringList &args);
    QImage grab(PreviewWorker &w, quint64 media, const QString &path, double t, bool exact = true, QString *err = nullptr);

    QTemporaryDir m_dir;
    bool m_ffmpeg = false;
    QString m_main, m_short, m_portrait, m_unicode, m_hdr, m_audio, m_rotated;
};

bool TstUvePreview::makeVideo(const QString &file, const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), QStringList{"-hide_banner", "-loglevel", "error", "-y"} + args + QStringList{file});
    if (!p.waitForFinished(120000) || p.exitCode() != 0) {
        qWarning().noquote() << "ffmpeg:" << p.readAllStandardError();
        return false;
    }
    return QFileInfo::exists(file);
}

void TstUvePreview::initTestCase()
{
#ifdef Q_OS_WIN
    // Wie im Player: libmpv-2.dll aus dem Player-Build laden (das Plugin findet sie dann als geladenes Modul)
    const QString dll = QStringLiteral(UVE_PLAYER_DIR "/build/bin/mpv/libmpv-2.dll");
    if (!GetModuleHandleW(L"libmpv-2.dll"))
        LoadLibraryExW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(dll).utf16()), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#endif
    QString err;
    QVERIFY2(PreviewWorker::mpvAvailable(&err), qPrintable(err));
    QVERIFY(m_dir.isValid());
    m_ffmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    if (!m_ffmpeg) return;
    // 20 s: Farbe wechselt jede Sekunde (hue), dazu ein Zähler → unterschiedliche Stellen = unterschiedliche Bilder
    m_main = m_dir.filePath("main.mp4");
    QVERIFY(makeVideo(m_main, {"-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25:duration=20,hue=h=t*18",
                               "-c:v", "libx264", "-g", "50", "-pix_fmt", "yuv420p"}));
    m_short = m_dir.filePath("short.mp4");
    QVERIFY(makeVideo(m_short, {"-f", "lavfi", "-i", "testsrc2=size=320x240:rate=25:duration=0.8",
                                "-c:v", "libx264", "-pix_fmt", "yuv420p"}));
    m_portrait = m_dir.filePath("portrait.mp4");
    QVERIFY(makeVideo(m_portrait, {"-f", "lavfi", "-i", "testsrc2=size=360x640:rate=25:duration=4",
                                   "-c:v", "libx264", "-pix_fmt", "yuv420p"}));
    // Unicode, Leerzeichen und langer Ordnername
    const QString deep = m_dir.filePath(QStringLiteral("Ordner mit Leerzeichen ü ß é 日本語 Ελληνικά ") + QString(120, QLatin1Char('x')));
    QVERIFY(QDir().mkpath(deep));
    m_unicode = deep + QStringLiteral("/Vorschau – Тест 動画 ✓.mp4");
    QVERIFY(QFile::copy(m_main, m_unicode));
    // HDR10 (PQ, BT.2020, 10 bit)
    m_hdr = m_dir.filePath("hdr.mkv");
    QVERIFY(makeVideo(m_hdr, {"-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25:duration=3",
                              "-vf", "format=yuv420p,zscale=tin=bt709:min=bt709:pin=bt709:rin=tv:t=smpte2084:p=bt2020:m=bt2020nc:r=tv:npl=100,format=yuv420p10le",
                              "-c:v", "libx265", "-x265-params", "log-level=error",
                              "-color_primaries", "bt2020", "-color_trc", "smpte2084", "-colorspace", "bt2020nc"}));
    // Hochkant gefilmt: 640x360 gespeichert, Anzeige um 90 Grad gedreht
    m_rotated = m_dir.filePath("rotated.mp4");
    QVERIFY(makeVideo(m_rotated, {"-display_rotation", "90", "-i", m_main, "-c", "copy"}));
    m_audio = m_dir.filePath("audio.mp3");
    QVERIFY(makeVideo(m_audio, {"-f", "lavfi", "-i", "sine=frequency=440:duration=3", "-c:a", "libmp3lame"}));
}

QImage TstUvePreview::grab(PreviewWorker &w, quint64 media, const QString &path, double t, bool exact, QString *err)
{
    static quint64 id = 0;
    QSignalSpy ok(&w, &PreviewWorker::frameReady);
    QSignalSpy fail(&w, &PreviewWorker::frameFailed);
    PreviewJob job;
    job.id = ++id;
    job.media = media;
    job.path = path;
    job.time = t;
    job.index = int(t * 10);
    job.exact = exact;
    job.duration = 600;   // lang genug, damit große Videos die Keyframe-Regel nutzen
    w.submit(job);
    if (!QTest::qWaitFor([&] { return ok.size() + fail.size() > 0; }, 20000)) { if (err) *err = QStringLiteral("timeout"); return {}; }
    if (!fail.isEmpty()) { if (err) *err = fail.first().at(3).toString(); return {}; }
    return ok.first().at(3).value<QImage>();
}

static double meanDiff(const QImage &a, const QImage &b)
{
    if (a.size() != b.size()) return 255;
    double sum = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            sum += qAbs(qRed(p) - qRed(q)) + qAbs(qGreen(p) - qGreen(q)) + qAbs(qBlue(p) - qBlue(q));
        }
    return sum / (3.0 * a.width() * a.height());
}

void TstUvePreview::stepGrid()
{
    QCOMPARE(previewStep(0.8), 0.04);
    QVERIFY(previewStep(7200) == 10.0);
    QVERIFY(qAbs(previewStep(400) - 1.0) < 1e-9);
}

void TstUvePreview::framesStartMiddleEnd()
{
    if (!m_ffmpeg) QSKIP("ffmpeg missing");
    PreviewWorker w;
    QElapsedTimer t; t.start();
    const QImage a = grab(w, 1, m_main, 0.0);
    qInfo() << "first frame (open + seek):" << t.elapsed() << "ms";
    QVERIFY(!a.isNull());
    QCOMPARE(a.size(), QSize(240, 135));   // 16:9 in 240×180
    t.restart();
    const QImage b = grab(w, 1, m_main, 10.0);
    qInfo() << "exact seek in open instance:" << t.elapsed() << "ms";
    const QImage c = grab(w, 1, m_main, 19.75);   // Ende
    const QImage d = grab(w, 1, m_main, 10.0, false);
    QVERIFY(!b.isNull() && !c.isNull() && !d.isNull());
    QVERIFY2(meanDiff(a, b) > 8, "start and middle look the same");
    QVERIFY2(meanDiff(b, c) > 8, "middle and end look the same");
    a.save(m_dir.filePath("f_start.png")); b.save(m_dir.filePath("f_mid.png")); c.save(m_dir.filePath("f_end.png"));
    // Keine schwarzen Bilder
    QVERIFY(qGray(b.pixel(b.width() / 2, b.height() / 4)) > 5 || qGray(b.pixel(10, 10)) > 5);
    w.shutdown();
}

void TstUvePreview::shortPortraitUnicode()
{
    if (!m_ffmpeg) QSKIP("ffmpeg missing");
    PreviewWorker w;
    QString err;
    const QImage s0 = grab(w, 1, m_short, 0.0, true, &err);
    QVERIFY2(!s0.isNull(), qPrintable(err));
    const QImage s1 = grab(w, 1, m_short, 0.8 - 0.016, true, &err);   // so wie der Controller das Ende anfragt
    QVERIFY2(!s1.isNull(), qPrintable(err));
    QCOMPARE(s0.size(), QSize(240, 180));
    const QImage p = grab(w, 2, m_portrait, 2.0, true, &err);
    QVERIFY2(!p.isNull(), qPrintable(err));
    QCOMPARE(p.size(), QSize(101, 180));   // 9:16 in 240×180
    const QImage r = grab(w, 4, m_rotated, 2.0, true, &err);
    QVERIFY2(!r.isNull(), qPrintable(err));
    QCOMPARE(r.size(), QSize(101, 180));   // Drehung beachtet
    r.save(m_dir.filePath("f_rotated.png"));
    const QImage u = grab(w, 3, m_unicode, 5.0, true, &err);
    QVERIFY2(!u.isNull(), qPrintable(err));
    w.shutdown();
}

void TstUvePreview::hdrIsSdr()
{
    if (!m_ffmpeg) QSKIP("ffmpeg missing");
    PreviewWorker w;
    QString err;
    const QImage h = grab(w, 1, m_hdr, 1.5, true, &err);
    QVERIFY2(!h.isNull(), qPrintable(err));
    h.save(m_dir.filePath("f_hdr.png"));
    // Mittlere Helligkeit plausibel (weder schwarz noch ausgebrannt)
    double sum = 0;
    for (int y = 0; y < h.height(); y += 4) for (int x = 0; x < h.width(); x += 4) sum += qGray(h.pixel(x, y));
    const double mean = sum / ((h.height() / 4 + 1) * (h.width() / 4 + 1));
    qInfo() << "HDR preview mean luma" << mean;
    QVERIFY(mean > 30 && mean < 230);
    w.shutdown();
}

void TstUvePreview::audioUnsupported()
{
    if (!m_ffmpeg) QSKIP("ffmpeg missing");
    PreviewWorker w;
    QSignalSpy un(&w, &PreviewWorker::mediaUnsupported);
    QString err;
    QVERIFY(grab(w, 7, m_audio, 1.0, true, &err).isNull());
    QCOMPARE(un.size(), 1);
    qInfo() << "audio:" << err;
    w.shutdown();
}

void TstUvePreview::latestJobWins()
{
    if (!m_ffmpeg) QSKIP("ffmpeg missing");
    PreviewWorker w;
    QVERIFY(!grab(w, 1, m_main, 1.0).isNull());   // Instanz offen
    QSignalSpy ok(&w, &PreviewWorker::frameReady);
    // Viele Aufträge schnell hintereinander: höchstens der laufende und der letzte werden fertig
    for (int i = 0; i < 40; ++i) {
        PreviewJob j; j.id = 1000 + i; j.media = 1; j.path = m_main; j.time = i * 0.45; j.index = i;
        w.submit(j);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!ok.isEmpty() && ok.last().at(0).toULongLong() == 1039, 20000);
    QVERIFY(ok.size() <= 3);
    // Medienwechsel während eines Auftrags: schließt, keine Blockade
    PreviewJob j; j.id = 2000; j.media = 1; j.path = m_main; j.time = 15; j.index = 150;
    w.submit(j);
    QElapsedTimer t; t.start();
    w.closeMedia();
    QVERIFY(!grab(w, 2, m_portrait, 1.0).isNull());
    qInfo() << "media switch + new frame:" << t.elapsed() << "ms";
    w.shutdown();
}

void TstUvePreview::uhdTiming()
{
    if (!m_ffmpeg) QSKIP("ffmpeg missing");
    // 4K HEVC, 10 bit, Keyframe nur alle 5 s: ungünstiger Fall für exakte Sprünge
    const QString uhd = m_dir.filePath("uhd.mkv");
    QVERIFY(makeVideo(uhd, {"-f", "lavfi", "-i", "testsrc2=size=3840x2160:rate=25:duration=12", "-pix_fmt", "yuv420p10le",
                            "-c:v", "libx265", "-preset", "ultrafast", "-x265-params", "log-level=error:keyint=125:min-keyint=125"}));
    PreviewWorker w;
    for (bool exact : {false, true}) {
        QElapsedTimer t; t.start();
        QVERIFY(!grab(w, exact ? 2 : 1, uhd, 0.5, exact).isNull());
        qInfo() << (exact ? "exact" : "keyframe") << "open+first:" << t.elapsed() << "ms";
        for (double pos : {4.8, 9.9, 2.4}) {
            t.restart();
            QVERIFY(!grab(w, exact ? 2 : 1, uhd, pos, exact).isNull());
            qInfo() << (exact ? "exact" : "keyframe") << pos << ":" << t.elapsed() << "ms";
        }
    }
    w.shutdown();
}

void TstUvePreview::lockedFileRetry()
{
#ifndef Q_OS_WIN
    QSKIP("Windows-Freigabemodus");
#else
    if (!m_ffmpeg) QSKIP("ffmpeg missing");
    // Wie ein Netzlaufwerk (RaiDrive/WinFsp), das die gerade geladene Datei kurz exklusiv hält
    const QString locked = m_dir.filePath("locked.mp4");
    QVERIFY(QFile::copy(m_main, locked));
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(locked).utf16()), GENERIC_READ, 0,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(h != INVALID_HANDLE_VALUE);
    QTimer::singleShot(1500, [h] { CloseHandle(h); });
    PreviewWorker w;
    QSignalSpy un(&w, &PreviewWorker::mediaUnsupported);
    QElapsedTimer t; t.start();
    QString err;
    const QImage img = grab(w, 1, locked, 5.0, true, &err);
    QVERIFY2(!img.isNull(), qPrintable(err));
    QVERIFY(t.elapsed() >= 1400);
    QCOMPARE(un.size(), 0);
    qInfo() << "locked file: picture after" << t.elapsed() << "ms";
    w.shutdown();
#endif
}

void TstUvePreview::userFile()
{
    // Optional: echte Datei prüfen, z. B. UVEPREVIEW_FILE=O:\movies\film.mkv
    const QString f = qEnvironmentVariable("UVEPREVIEW_FILE");
    if (f.isEmpty()) QSKIP("UVEPREVIEW_FILE not set");
    PreviewWorker w;
    for (double pos : {1.0, 30.0, 60.0}) {
        QElapsedTimer t; t.start();
        QString err;
        const QImage img = grab(w, 1, f, pos, true, &err);
        qInfo() << pos << "s:" << (img.isNull() ? err : QStringLiteral("%1x%2").arg(img.width()).arg(img.height())) << t.elapsed() << "ms";
        QVERIFY2(!img.isNull(), qPrintable(err));
        img.save(m_dir.filePath(QStringLiteral("user_%1.png").arg(int(pos))));
    }
    w.shutdown();
}

void TstUvePreview::releasesFileWhenIdle()
{
#ifndef Q_OS_WIN
    QSKIP("Windows-Freigabemodus");
#else
    if (!m_ffmpeg) QSKIP("ffmpeg missing");
    // Nach einer Vorschau gibt der Decoder die Datei nach kurzer Ruhe wieder frei (der Player kann sie dann
    // exklusiv/neu öffnen); closeMedia(wait) gibt sie sofort frei
    PreviewWorker w;
    QVERIFY(!grab(w, 1, m_main, 3.0).isNull());
    const auto exclusive = [this] {
        HANDLE h = CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(m_main).utf16()), GENERIC_READ, 0,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        CloseHandle(h);
        return true;
    };
    QVERIFY(!exclusive());               // Instanz hält die Datei gerade offen
    QTRY_VERIFY_WITH_TIMEOUT(exclusive(), 4000);
    QVERIFY(!grab(w, 1, m_main, 6.0).isNull());
    QVERIFY(!exclusive());
    QElapsedTimer t; t.start();
    w.closeMedia(1000);
    QVERIFY(exclusive());
    qInfo() << "closeMedia(wait) released the file after" << t.elapsed() << "ms";
    w.shutdown();
#endif
}

void TstUvePreview::seekbarIntegration()
{
    if (!m_ffmpeg) QSKIP("ffmpeg missing");
    // Gebautes Plugin-Modul laden wie der Player
    const QString lib = QCoreApplication::applicationDirPath() +
#ifdef Q_OS_WIN
        QStringLiteral("/uve_uvepreview.dll");
#elif defined(Q_OS_MACOS)
        QStringLiteral("/libuve_uvepreview.dylib");
#else
        QStringLiteral("/libuve_uvepreview.so");
#endif
    QPluginLoader loader(lib);
    QObject *obj = loader.instance();
    QVERIFY2(obj, qPrintable(loader.errorString()));
    auto *plugin = qobject_cast<uve::IPlayerPlugin *>(obj);
    auto *events = qobject_cast<uve::IPluginEvents *>(obj);
    QVERIFY(plugin && events);

    FakePlayer player;
    FakeApp app;
    QVariantMap theme{{"line", "#444"}, {"accent", "#3b82f6"}, {"dark", true}};
    // SeekBar des Players unten in einem 800×450-Fenster, wie in der ControlBar
    const QString wrapper = m_dir.filePath("wrapper.qml");
    {
        QFile f(wrapper);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("import QtQuick\nimport \"%1\"\n"
                               "Rectangle { width: 800; height: 450; color: \"#202020\"\n"
                               "  SeekBar { objectName: \"bar\"; x: 10; y: 400; width: 780; height: 20; player: fakePlayer; theme: fakeTheme } }\n")
                    .arg(QUrl::fromLocalFile(QStringLiteral(UVE_PLAYER_DIR "/app/qml")).toString()).toUtf8());
    }
    QQuickView win;
    win.engine()->rootContext()->setContextProperty("app", &app);
    win.engine()->rootContext()->setContextProperty("fakePlayer", &player);
    win.engine()->rootContext()->setContextProperty("fakeTheme", theme);
    win.setSource(QUrl::fromLocalFile(wrapper));
    QVERIFY2(win.status() == QQuickView::Ready, qPrintable(win.errors().isEmpty() ? QString() : win.errors().first().toString()));
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QQuickItem *bar = win.rootObject()->findChild<QQuickItem *>("bar");
    QVERIFY(bar);

    FakeHost host;
    QString err;
    QVERIFY(plugin->initialize(&host, &err));
    player.duration = 20; player.state = 2; player.position = 3; emit player.changed();
    host.state = {{"path", m_main}, {"duration", 20.0}};
    plugin->activate();

    QQuickItem *layer = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((layer = bar->findChild<QQuickItem *>("uvePreviewLayer")) != nullptr, 5000);
    QQuickItem *popup = layer->findChild<QQuickItem *>("uvePreviewPopup");
    QVERIFY(popup);
    QVERIFY(!popup->isVisible());

    // Hover bei 25 % → Bild + Zeit 0:05, Tooltip ausgeblendet
    QQuickItem *tip = qvariant_cast<QQuickItem *>(layer->property("tip"));
    QVERIFY(tip);
    QTest::mouseMove(&win, QPoint(10 + 195, 410));
    QTRY_VERIFY_WITH_TIMEOUT(popup->isVisible(), 15000);
    QTRY_COMPARE(tip->opacity(), 0.0);
    QQuickItem *time = layer->findChild<QQuickItem *>("uvePreviewTime");
    QCOMPARE(time->property("text").toString(), QStringLiteral("0:05"));
    const QPointF topLeft = popup->mapToScene(QPointF(0, 0));
    QVERIFY(topLeft.y() >= 0 && topLeft.y() + popup->height() <= 400);
    QCOMPARE(popup->width(), 240.0 + 4);
    win.grabWindow().save(m_dir.filePath("seekbar_hover.png"));

    // Ganz links und ganz rechts: Popup bleibt im Fenster
    QTest::mouseMove(&win, QPoint(11, 410));
    QTRY_VERIFY(popup->isVisible());
    QTRY_VERIFY(popup->mapToScene(QPointF(0, 0)).x() >= 0);
    QTest::mouseMove(&win, QPoint(789, 410));
    QTRY_COMPARE(time->property("text").toString(), QStringLiteral("0:19"));
    QTRY_VERIFY(popup->mapToScene(QPointF(popup->width(), 0)).x() <= 800);
    win.grabWindow().save(m_dir.filePath("seekbar_end.png"));
    QCOMPARE(player.seeks, 0);   // Hover allein springt nicht

    // Verlassen → weg, Tooltip wieder sichtbar
    QTest::mouseMove(&win, QPoint(400, 100));
    QTRY_VERIFY(!popup->isVisible());
    QTRY_COMPARE(tip->opacity(), 1.0);

    // Ausschalten in den Einstellungen → normale Zeitanzeige
    QObject *ctl = plugin->controller();
    ctl->setProperty("enabled", false);
    QCOMPARE(host.settings.value("enabled").toBool(true), false);
    QTest::mouseMove(&win, QPoint(300, 410));
    QTest::qWait(600);
    QVERIFY(!popup->isVisible());
    QCOMPARE(tip->opacity(), 1.0);
    ctl->setProperty("enabled", true);
    QTest::mouseMove(&win, QPoint(320, 410));
    QTRY_VERIFY_WITH_TIMEOUT(popup->isVisible(), 15000);

    // Medienwechsel während Hover: Bild verschwindet sofort, neues Medium (Audio) → keine Vorschau
    events->onEvent("media.loading", {{"url", m_audio}});
    QTRY_VERIFY(!popup->isVisible());
    events->onEvent("media.loaded", {{"path", m_audio}, {"duration", 3.0}});
    player.duration = 3; emit player.changed();
    QTest::mouseMove(&win, QPoint(200, 410));
    QTest::qWait(1500);
    QVERIFY(!popup->isVisible());
    QTRY_COMPARE(ctl->property("available").toBool(), false);

    // Netzwerkadresse → keine Vorschau
    events->onEvent("media.loaded", {{"path", "https://example.com/video.mp4"}, {"duration", 60.0}});
    QCOMPARE(ctl->property("available").toBool(), false);

    // Hochformat-Video, dann Plugin deaktivieren: Ebene weg, Tooltip normal
    events->onEvent("media.loaded", {{"path", m_portrait}, {"duration", 4.0}});
    player.duration = 4; emit player.changed();
    QTest::mouseMove(&win, QPoint(400, 410));
    QTRY_VERIFY_WITH_TIMEOUT(popup->isVisible(), 15000);
    QVERIFY(popup->width() < popup->height());
    win.grabWindow().save(m_dir.filePath("seekbar_portrait.png"));
    plugin->deactivate();
    QTRY_VERIFY(bar->findChild<QQuickItem *>("uvePreviewLayer") == nullptr);
    QCOMPARE(tip->opacity(), 1.0);
    qInfo() << "screenshots in" << m_dir.path();
    m_dir.setAutoRemove(qEnvironmentVariableIsEmpty("UVEPREVIEW_KEEP"));
}

QTEST_MAIN(TstUvePreview)
#include "tst_uvepreview.moc"
