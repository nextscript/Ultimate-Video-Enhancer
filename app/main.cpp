// UVE Player 1.0.0 – Launcher
// Aufgaben (vgl. docs/REA_ANALYSIS.md, VLC: vlc.exe ist ein schlanker Starter vor libvlc):
//   libmpv laden → Einzelinstanz → Safe-Mode-Prüfung → Player initialisieren → QML-Oberfläche → Plugins
#include "PlayerApp.h"

#include "core/AppSettings.h"
#include "core/Translation.h"
#include "core/Logger.h"
#include "core/SafeMode.h"
#include "player/MpvController.h"
#include "player/MpvVideoItem.h"
#include "services/SingleInstance.h"
#include "services/ToolUpdater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>

#include <clocale>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static void fatal(const QString &title, const QString &text)
{
    uve::Logger::write("fatal", title + ": " + text);
#ifdef Q_OS_WIN
    MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(text.utf16()), reinterpret_cast<LPCWSTR>(title.utf16()), MB_ICONERROR | MB_OK);
#else
    fprintf(stderr, "%s: %s\n", qPrintable(title), qPrintable(text));
#endif
}

// Lädt die neueste funktionierende libmpv-2.dll (Benutzer-Update vor mitgelieferter Version).
// Die EXE bindet libmpv per /DELAYLOAD; ist die DLL bereits geladen, nutzt der Delay-Loader sie.
static QString preloadMpv(const QString &appDir, const QString &dataDir, QString *error)
{
#ifdef Q_OS_WIN
    QStringList tried;
    for (const auto &file : uve::ToolUpdater::mpvDllCandidates(appDir, dataDir)) {
        HMODULE h = LoadLibraryExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(file).utf16()), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (h) return file;
        const DWORD err = GetLastError();
        QString hint;
        if (err == ERROR_MOD_NOT_FOUND) hint = QCoreApplication::translate("main", " (abhängige DLL fehlt – libmpv benötigt vulkan-1.dll aus dem Grafiktreiber)");
        tried << QCoreApplication::translate("main", "%1: Fehler %2%3").arg(file).arg(err).arg(hint);
        uve::Logger::write("mpv", "Laden fehlgeschlagen: " + tried.last());
    }
    *error = tried.isEmpty() ? QCoreApplication::translate("main", "libmpv-2.dll wurde nicht gefunden (erwartet unter mpv\\libmpv-2.dll).") : tried.join("\n");
    return {};
#else
    Q_UNUSED(appDir); Q_UNUSED(dataDir); Q_UNUSED(error);
    return QCoreApplication::translate("main", "libmpv (System)");
#endif
}

static QString resolveDataDir()
{
    const QByteArray env = qgetenv("UVE_DATA_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    // Portables Profil: Daten neben der EXE, wenn portable.txt vorhanden ist
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(QDir(appDir).filePath("portable.txt"))) return QDir(appDir).filePath("data");
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

int main(int argc, char *argv[])
{
    // Eigener Datenordner (%APPDATA%\nextscript\UVE Player) – getrennt von der Electron-Version,
    // die %APPDATA%\UVE Player mit einem anderen settings.json-Format nutzt
    QCoreApplication::setOrganizationName(QStringLiteral("nextscript"));
    QCoreApplication::setApplicationName(QStringLiteral("UVE Player"));
    QCoreApplication::setApplicationVersion(QStringLiteral(UVE_PLAYER_VERSION));
    // libmpv rendert über die OpenGL-Render-API → Qt Quick muss OpenGL nutzen
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/UvePlayer/resources/icon.png")));
    // libmpv verlangt die C-Locale für Zahlen (sonst verweigert mpv_create die Arbeit)
    std::setlocale(LC_NUMERIC, "C");

    const QString dataDir = resolveDataDir();
    QDir().mkpath(dataDir);
    uve::Logger::init(dataDir);
    uve::Logger::installQtMessageHandler();
    uve::Logger::write("start", QStringLiteral("UVE Player %1, Qt %2, Daten: %3").arg(app.applicationVersion(), QString::fromLatin1(qVersion()), dataDir));

    // Sprache (Voreinstellung Englisch) – vor allen sichtbaren Meldungen
    uve::AppSettings settings(dataDir);
    uve::LanguageManager languages({QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lang")),
                                    QDir(dataDir).filePath(QStringLiteral("lang"))});
    if (!languages.apply(settings.value(QStringLiteral("language"), QString::fromLatin1(uve::LanguageManager::kDefaultLanguage)).toString()))
        languages.apply(QString::fromLatin1(uve::LanguageManager::kDefaultLanguage));

    // Argumente
    QStringList args = app.arguments().mid(1);
    bool forceSafe = args.removeAll(QStringLiteral("--safe-mode")) > 0;
    QString selftestOut;
    const int st = args.indexOf(QStringLiteral("--selftest"));
    if (st >= 0) {
        selftestOut = args.value(st + 1);
        args = args.mid(st + 2);
    }

    // Einzelinstanz (nicht im Selbsttest)
    uve::SingleInstance single(QStringLiteral("UVEPlayer-") + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER")));
    if (selftestOut.isEmpty()) {
        QStringList forward;
        for (const auto &a : args) forward << (QFileInfo::exists(a) ? QFileInfo(a).absoluteFilePath() : a);
        if (!single.claim(forward)) return 0;
    }

    QString mpvError;
    const QString mpvLib = preloadMpv(QCoreApplication::applicationDirPath(), dataDir, &mpvError);
    if (mpvLib.isEmpty()) {
        fatal(QCoreApplication::translate("main", "UVE Player – libmpv fehlt"), QCoreApplication::translate("main", "Die Wiedergabe-Engine libmpv konnte nicht geladen werden.\n\n") + mpvError);
        return 3;
    }

    uve::SafeMode safe(dataDir);
    safe.evaluateOnStartup(forceSafe);

    auto *player = new PlayerApp(&settings, &safe, mpvLib);
    player->setLanguageManager(&languages);
    if (!selftestOut.isEmpty()) player->setSelftest(selftestOut, args);
    QString err;
    if (!player->initializePlayer(&err)) {
        fatal(QCoreApplication::translate("main", "UVE Player – mpv"), QCoreApplication::translate("main", "libmpv konnte nicht initialisiert werden: ") + err);
        delete player;
        return 4;
    }

    qmlRegisterType<uve::MpvVideoItem>("UvePlayer.Native", 1, 0, "MpvVideo");
    qmlRegisterUncreatableType<uve::MpvController>("UvePlayer.Native", 1, 0, "MpvController", QStringLiteral("nur über app.player"));

    auto *engine = new QQmlApplicationEngine;
    engine->rootContext()->setContextProperty(QStringLiteral("app"), player);
    player->setEngine(engine);
    player->startPlugins();
    QObject::connect(engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        fatal(QCoreApplication::translate("main", "UVE Player"), QCoreApplication::translate("main", "Die Oberfläche konnte nicht geladen werden (siehe Protokoll)."));
        QCoreApplication::exit(5);
    }, Qt::QueuedConnection);
    engine->loadFromModule(QStringLiteral("UvePlayer"), QStringLiteral("Main"));
    if (!engine->rootObjects().isEmpty()) player->setWindow(qobject_cast<QQuickWindow *>(engine->rootObjects().first()));

    QObject::connect(&single, &uve::SingleInstance::messageReceived, player, [player](const QStringList &a) {
        if (!a.isEmpty()) player->openPaths(a, true);
        emit player->requestRaise();
    });
    QObject::connect(player, &PlayerApp::selftestScreenshot, &app, [engine](const QString &file) {
        if (engine->rootObjects().isEmpty()) return;
        if (auto *win = qobject_cast<QQuickWindow *>(engine->rootObjects().first())) win->grabWindow().save(file);
    });

    if (selftestOut.isEmpty()) player->restoreSessionAndOpen(args);

    const int rc = app.exec();

    player->shutdown();
    delete engine;   // erst Oberfläche (Render-Kontext) freigeben, dann mpv
    delete player;
    safe.markCleanExit();
    uve::Logger::write("exit", QStringLiteral("Beendet (Code %1)").arg(rc));
    return rc;
}
