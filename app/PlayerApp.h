#pragma once
#include <QProcess>
#include <QDir>
#include <QImage>
#include <QtConcurrent/QtConcurrentRun>
#include <QFileInfo>
#include <QObject>
#include <QQuickWindow>
#include <QRect>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include "services/ProxyConfig.h"
#include "services/MediaFiles.h"
#include <QHash>
namespace uve { class ProxyBridge; class LanguageManager; }
class QQmlEngine;

namespace uve {
class AppSettings;
class MpvController;
class ShaderChain;
class PluginManager;
class PlaylistModel;
class HotkeyModel;
class ToolUpdater;
class AppUpdater;
class SafeMode;
}
class QQuickWindow;

// Anwendungskern: verbindet Player, Wiedergabeliste, Plugins, Einstellungen und Dienste.
// In QML als Kontext-Eigenschaft "app" verfügbar.
class PlayerApp : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *player READ playerObject CONSTANT)
    Q_PROPERTY(QObject *playlist READ playlistObject CONSTANT)
    Q_PROPERTY(QObject *plugins READ pluginsObject CONSTANT)
    Q_PROPERTY(QObject *hotkeys READ hotkeysObject CONSTANT)
    Q_PROPERTY(QObject *updater READ updaterObject CONSTANT)
    Q_PROPERTY(QObject *appUpdater READ appUpdaterObject CONSTANT)
    Q_PROPERTY(QObject *shaders READ shadersObject CONSTANT)
    Q_PROPERTY(QObject *settings READ settingsObject CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString mpvVersion READ mpvVersion CONSTANT)
    Q_PROPERTY(QString mpvLibrary READ mpvLibrary CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(bool safeMode READ safeMode NOTIFY safeModeChanged)
    Q_PROPERTY(QString safeModeReason READ safeModeReason NOTIFY safeModeChanged)
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(QString fileDialogFilter READ fileDialogFilter CONSTANT)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool selftest READ selftest CONSTANT)
    Q_PROPERTY(QString proxyStatus READ proxyStatus NOTIFY proxyChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages NOTIFY languageChanged)
    // Auflösungen des aktuellen YouTube-/Twitch-Streams (über yt-dlp ermittelt): [{id, label, height, format}]
    Q_PROPERTY(QVariantList streamQualities READ streamQualities NOTIFY streamQualitiesChanged)
    Q_PROPERTY(QString streamQuality READ streamQuality NOTIFY streamQualitiesChanged)      // "" = beste (automatisch)
    Q_PROPERTY(bool streamQualitiesLoading READ streamQualitiesLoading NOTIFY streamQualitiesChanged)

public:
    PlayerApp(uve::AppSettings *settings, uve::SafeMode *safe, const QString &mpvLibrary, QObject *parent = nullptr);
    ~PlayerApp() override;

    bool initializePlayer(QString *error);
    void startPlugins();
    void restoreSessionAndOpen(const QStringList &args);
    void shutdown();

    QObject *playerObject() const;
    QObject *playlistObject() const;
    QObject *pluginsObject() const;
    QObject *hotkeysObject() const;
    QObject *updaterObject() const;
    QObject *appUpdaterObject() const;
    QObject *shadersObject() const;
    QObject *settingsObject() const;
    uve::MpvController *player() const { return m_player; }

    QString version() const;
    QString mpvVersion() const;
    QString mpvLibrary() const { return m_mpvLibrary; }
    QString qtVersion() const;
    QString dataDir() const;
    bool safeMode() const;
    QString safeModeReason() const;
    QStringList recentFiles() const;
    QString fileDialogFilter() const;
    QString theme() const;
    void setTheme(const QString &t);
    bool selftest() const { return m_selftest; }
    void setSelftest(const QString &outFile, const QStringList &files);

    // Öffnen / Wiedergabe
    Q_INVOKABLE void openPaths(const QStringList &pathsOrUrls, bool play = true, bool insertNext = false);
    Q_INVOKABLE void openUrls(const QList<QUrl> &urls, bool play = true);
    Q_INVOKABLE QString validateUrl(const QString &text) const; // leer = gültig, sonst Fehlermeldung
    Q_INVOKABLE void playIndex(int index, double startAt = -1);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool savePlaylist(const QUrl &file);
    Q_INVOKABLE void addSubtitle(const QUrl &file);
    Q_INVOKABLE QString snapshot();
    Q_INVOKABLE QVariantMap mediaInfo() const;
    Q_INVOKABLE void clearRecentFiles();
    Q_INVOKABLE void clearResumePositions();
    Q_INVOKABLE QString toLocalFile(const QUrl &url) const { return url.isLocalFile() ? url.toLocalFile() : url.toString(); }
    Q_INVOKABLE void openFolder(const QString &path) const;
    // Plugin-Ordner öffnen: user = false → mitgelieferte Plugins (plugins\ neben der EXE),
    // user = true → eigene Plugins im Benutzerordner (wird bei Bedarf angelegt)
    Q_INVOKABLE void openPluginFolder(bool user) const;
    Q_INVOKABLE void showInExplorer(const QString &path) const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE void resetSafeMode();
    // Beendet den Player und startet ihn neu (z. B. nach einem Plugin-Update)
    Q_INVOKABLE void restartApp();
    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &def = {}) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void applyHwdec(const QString &mode);
    Q_INVOKABLE void setRenderQuality(const QString &mode);   // balanced | high | fast
    Q_INVOKABLE QVariantList audioDevices() const;
    Q_INVOKABLE void setAudioDevice(const QString &name);
    Q_INVOKABLE void showOsd(const QString &text, int ms = 1500) { emit osd(text, ms); }
    Q_INVOKABLE QString logFile() const;
    Q_INVOKABLE void reportSelftestUi(const QVariantMap &ui);
    // Selbsttest der Oberfläche: echte Mausereignisse an das Fenster senden (Fensterkoordinaten)
    Q_INVOKABLE void simulateClick(qreal x, qreal y, int button = 1, bool doubleClick = false);
    Q_INVOKABLE void simulateMove(qreal x, qreal y);   // Selbsttest: Mausbewegung ohne Taste
    Q_INVOKABLE void simulateDrag(qreal x1, qreal y1, qreal x2, qreal y2);   // Selbsttest: Ziehen mit gedrückter linker Taste
    // Selbsttest: mittlere Farbe/Sättigung eines Fensterbereichs (echte Pixel aus dem Fenster-Framebuffer)
    Q_INVOKABLE QString selftestPath(const QString &name) const { return QFileInfo(m_selftestOut).absoluteDir().filePath(name); }
    Q_INVOKABLE bool selftestOnly(const QString &part) const { return m_selftest && qEnvironmentVariable("UVE_SELFTEST_ONLY") == part; }
    Q_INVOKABLE bool saveWindowShot(const QString &file) const { return m_window && !m_selftestOut.isEmpty() && m_window->grabWindow().save(file); }
    // Selbsttest: Fenster aufnehmen, verkleinert im Hintergrund speichern (schnelle Bildfolgen, z. B. Übergänge)
    Q_INVOKABLE void saveWindowShotAsync(const QString &file) const
    {
        if (!m_window || m_selftestOut.isEmpty()) return;
        const QImage img = m_window->grabWindow();
        (void)QtConcurrent::run([img, file] { img.scaled(img.width() / 4, img.height() / 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).save(file); });
    }
    Q_INVOKABLE QVariantMap sampleWindow(qreal x, qreal y, qreal w, qreal h) const;
    // Selbsttest: Anteil der Pixel eines Bereichs, die sich deutlich vom Hintergrund (Ecke oben links) abheben
    Q_INVOKABLE double selftestInkShare(qreal x, qreal y, qreal w, qreal h) const;
    void setWindow(QQuickWindow *w);   // stellt Fensterposition/-größe wieder her und zeigt das Fenster
    bool selftestUiMode() const { return m_selftestUiMode; }
    Q_PROPERTY(bool selftestUiMode READ selftestUiMode CONSTANT)
    Q_INVOKABLE QString formatTime(double seconds) const;

    // Proxy für Netzwerkstreams (mpv/FFmpeg über lokale Brücke bei SOCKS5, yt-dlp, eigene Downloads)
    QString proxyStatus() const { return m_proxyStatus; }

    // Sprache (JSON-Dateien in <App>/lang und <Daten>/lang)
    void setLanguageManager(uve::LanguageManager *lm) { m_languages = lm; }
    void setEngine(QQmlEngine *engine) { m_engine = engine; }
    QString language() const;
    QVariantList languages() const;
    Q_INVOKABLE QString setLanguage(const QString &code);   // "" = ok, sonst Fehlertext
    Q_INVOKABLE QStringList languageFolders() const;
    Q_INVOKABLE QVariantMap proxySettings() const;              // ohne Passwort (nur hasPassword)
    Q_INVOKABLE QString setProxy(const QVariantMap &settings);  // "" = übernommen, sonst Fehlertext
    Q_INVOKABLE void testProxy(const QVariantMap &settings);    // Ergebnis: proxyTestFinished
    Q_INVOKABLE QVariantMap proxyStats() const;
    QVariantList streamQualities() const { return m_streamQualities; }
    QString streamQuality() const { return m_streamQuality; }
    bool streamQualitiesLoading() const { return m_formatProc != nullptr; }
    Q_INVOKABLE void setStreamQuality(const QString &id);       // "" = beste; lädt den Stream an der aktuellen Stelle neu

signals:
    void osd(const QString &text, int ms);
    void recentFilesChanged();
    void safeModeChanged();
    void themeChanged();
    void toolUpdatesAvailable(const QVariantList &updates);
    void requestRaise();
    void selftestScreenshot(const QString &file);
    void proxyChanged();
    void languageChanged();
    void proxyTestFinished(bool ok, const QString &message);
    void streamQualitiesChanged();
    void pluginPanelRequested(const QString &panelKey);   // Plugin-Befehl "panel.open"

private:
    void onFileLoaded();
    void onEndOfFile(const QString &reason);
    void saveResume();
    void saveSession();
    void applyYtdlOptions();
    void openYoutubePlaylist(const QString &url, const uve::media::YoutubePlaylistLink &link, bool play, bool insertNext);
    void applyRenderQuality(const QString &mode);
    void loadPath(const QString &path, double start, bool paused); // inkl. gewählter Stream-Auflösung
    void prepareStreamQualities(const QString &path);
    void clearStreamQualities();
    QStringList streamFileOptions() const;   // mpv-Dateioptionen der gewählten Auflösung (yt-dlp)
    QString resolverQuality() const;         // gewählte Auflösung für das auflösende Plugin
    void onMediaResolved(quint64 requestId, const QString &resolvedUrl, const QString &error, const QString &pluginName);
    void cancelPendingResolve();
    void registerPluginCommands();   // player.* / playlist.* für Plugins (docs/PLUGIN_API.md)
    QString ytdlFormatFor(const QString &id) const;
    void applyProxy();
    uve::ProxyConfig proxyFromMap(const QVariantMap &m) const;
    QString routeThroughProxy(const QString &url);             // tcp:// → lokale Weiterleitung
    void runSelftestStep();

    uve::AppSettings *m_settings;
    uve::SafeMode *m_safe;
    QString m_mpvLibrary;
    uve::ProxyConfig m_proxy;
    uve::ProxyBridge *m_bridge = nullptr;
    uve::LanguageManager *m_languages = nullptr;
    QQmlEngine *m_engine = nullptr;
    void updateProxyStatus();
    QString m_proxyStatus;
    QString m_ytdlProxy;                                         // Proxy-URL für eigene yt-dlp-Aufrufe
    QVariantList m_streamQualities;
    QString m_streamQuality;
    bool m_streamQualityChosen = false;   // im Menü gewählt (auch "Beste"), nicht nur Voreinstellung
    // Adressauflösung durch ein Plugin (z. B. Twitch → tcp://): wartender Ladevorgang
    struct PendingResolve { quint64 id = 0; QString path; double start = 0; bool paused = false; };
    PendingResolve m_resolve;
    quint64 m_resolveSeq = 0;
    QString m_sourcePath;            // vom Nutzer gewählte Adresse (vor einer Plugin-Auflösung)
    QTimer m_resolveTimeout;
    QString m_qualityUrl;                                        // Stream, zu dem Liste/Auswahl gehören
    QProcess *m_formatProc = nullptr;
    QHash<QString, QString> m_proxiedOriginal;                  // lokale URL → Original
    uve::MpvController *m_player = nullptr;
    uve::ShaderChain *m_shaders = nullptr;
    uve::PluginManager *m_plugins = nullptr;
    uve::PlaylistModel *m_playlist = nullptr;
    uve::HotkeyModel *m_hotkeys = nullptr;
    uve::ToolUpdater *m_updater = nullptr;
    uve::AppUpdater *m_appUpdater = nullptr;
    QTimer m_resumeTimer;
    QString m_loadedPath;
    bool m_stoppedByUser = false;
    // Netzlaufwerke (z. B. RaiDrive): Öffnen schlägt kurzzeitig fehl, solange ein anderer Prozess
    // (Explorer, Virenscanner) die noch nicht zwischengespeicherte Datei liest → automatisch erneut versuchen
    QString m_retryPath;
    int m_retryCount = 0;
    bool m_retrying = false;
    quint64 m_loadGeneration = 0;                               // jedes neue playIndex() verwirft alte Wiederholungen

    // Selbsttest
    bool m_selftest = false;
    QString m_selftestOut;
    QStringList m_selftestFiles;
    int m_selftestIndex = -1;
    QVariantList m_selftestResults;
    QVariantMap m_selftestUi;
    QVariantMap m_renderDefaults;   // mpv-Standardwerte der Skalierungsoptionen ("high")
    bool m_selftestUiMode = false;
    QQuickWindow *m_window = nullptr;
    QRect m_normalGeometry;          // letzte Geometrie im Fenstermodus (ohne Maximiert/Vollbild)
    bool m_windowMaximized = false;
    QTimer m_geometryTimer;
    void captureWindowGeometry();
    void saveWindowGeometry();
    void finishSelftest();
};
