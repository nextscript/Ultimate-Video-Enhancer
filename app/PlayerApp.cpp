#include "PlayerApp.h"

#include "core/AppSettings.h"
#include "core/Logger.h"
#include "core/SafeMode.h"
#include "player/MpvController.h"
#include "plugins/PluginManager.h"
#include "rendering/ShaderChain.h"
#include "services/HotkeyModel.h"
#include "services/MediaFiles.h"
#include "services/PlaylistModel.h"
#include "services/ToolUpdater.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include "services/ProxyBridge.h"
#include "core/Translation.h"
#include <QQmlEngine>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImage>
#include <QJsonObject>
#include <QMouseEvent>
#include <algorithm>
#include <QProcess>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>

using namespace uve;

PlayerApp::PlayerApp(AppSettings *settings, SafeMode *safe, const QString &mpvLibrary, QObject *parent)
    : QObject(parent), m_settings(settings), m_safe(safe), m_mpvLibrary(mpvLibrary)
{
    m_player = new MpvController(this);
    m_shaders = new ShaderChain(m_player, this);
    m_playlist = new PlaylistModel(this);
    m_hotkeys = new HotkeyModel(m_settings, this);
    m_updater = new ToolUpdater(QCoreApplication::applicationDirPath(), m_settings->dataDir(), this);

    m_playlist->setRepeat(m_settings->value("repeat").toString());
    m_playlist->setShuffle(m_settings->value("shuffle").toBool());
    connect(m_playlist, &PlaylistModel::repeatChanged, this, [this] { m_settings->setValue("repeat", m_playlist->repeat()); });
    connect(m_playlist, &PlaylistModel::shuffleChanged, this, [this] { m_settings->setValue("shuffle", m_playlist->shuffle()); });
    connect(m_playlist, &PlaylistModel::countChanged, this, &PlayerApp::saveSession);

    connect(m_player, &MpvController::fileLoaded, this, &PlayerApp::onFileLoaded);
    connect(m_player, &MpvController::endOfFile, this, &PlayerApp::onEndOfFile);
    connect(m_player, &MpvController::volumeChanged, this, [this] { m_settings->setValue("volume", qRound(m_player->volume())); });
    connect(m_player, &MpvController::mutedChanged, this, [this] { m_settings->setValue("muted", m_player->muted()); });
    connect(m_player, &MpvController::mediaTitleChanged, this, [this] {
        // Netzwerkstreams: lesbaren Titel (z. B. YouTube-Videotitel) übernehmen
        const int i = m_playlist->currentIndex();
        if (i >= 0 && media::isUrl(m_playlist->pathAt(i)) && !m_player->mediaTitle().isEmpty())
            m_playlist->setTitle(i, m_player->mediaTitle());
    });
    connect(m_player, &MpvController::pausedChanged, this, [this] { if (m_player->paused()) saveResume(); });

    m_resumeTimer.setInterval(5000);
    connect(&m_resumeTimer, &QTimer::timeout, this, &PlayerApp::saveResume);
    m_resumeTimer.start();

    connect(m_updater, &ToolUpdater::checkFinished, this, [this](const QVariantList &updates, const QStringList &errors) {
        if (!errors.isEmpty()) Logger::write("update", errors.join("; "));
        // übersprungene Versionen ausfiltern
        const auto skipped = m_settings->object("skippedToolVersions");
        QVariantList visible;
        for (const auto &u : updates) {
            const auto m = u.toMap();
            if (skipped.value(m.value("tool").toString()).toString() != m.value("id").toString()) visible << u;
        }
        if (!visible.isEmpty()) emit toolUpdatesAvailable(visible);
    });
}

PlayerApp::~PlayerApp() = default;

QObject *PlayerApp::playerObject() const { return m_player; }
QObject *PlayerApp::playlistObject() const { return m_playlist; }
QObject *PlayerApp::pluginsObject() const { return m_plugins; }
QObject *PlayerApp::hotkeysObject() const { return m_hotkeys; }
QObject *PlayerApp::updaterObject() const { return m_updater; }
QObject *PlayerApp::shadersObject() const { return m_shaders; }
QObject *PlayerApp::settingsObject() const { return m_settings; }
QString PlayerApp::version() const { return QCoreApplication::applicationVersion(); }
QString PlayerApp::mpvVersion() const { return m_player->mpvVersion(); }
QString PlayerApp::qtVersion() const { return QString::fromLatin1(qVersion()); }
QString PlayerApp::dataDir() const { return m_settings->dataDir(); }
bool PlayerApp::safeMode() const { return m_safe->active(); }
QString PlayerApp::safeModeReason() const { return m_safe->reason(); }
QStringList PlayerApp::recentFiles() const { return m_settings->recentFiles(); }
QString PlayerApp::fileDialogFilter() const { return media::fileDialogFilter(); }
QString PlayerApp::theme() const { return m_settings->value("theme", "dark").toString(); }
QString PlayerApp::logFile() const { return Logger::filePath(); }

void PlayerApp::setTheme(const QString &t)
{
    m_settings->setValue("theme", t == "light" ? "light" : "dark");
    emit themeChanged();
}

QVariant PlayerApp::setting(const QString &key, const QVariant &def) const { return m_settings->value(key, def); }

void PlayerApp::setSetting(const QString &key, const QVariant &value)
{
    m_settings->setValue(key, value);
    if (key == "streamQualityLimit") applyYtdlOptions();
    if (key == "subAuto") m_player->setProperty("sub-auto", value.toBool() ? "fuzzy" : "no");
    if (key == "subFontSize") m_player->setProperty("sub-font-size", value.toInt());
    if (key == "subLanguages") m_player->setProperty("slang", value.toString());
    if (key == "audioLanguages") m_player->setProperty("alang", value.toString());
    if (key == "rememberHistory" && !value.toBool()) clearRecentFiles();
}

bool PlayerApp::initializePlayer(QString *error)
{
    const QVariantMap opts{
        {"hwdec", m_settings->value("hwdec", "auto-safe")},
        {"volume", m_settings->value("volume", 100)},
        {"mute", m_settings->value("muted").toBool() ? "yes" : "no"},
        {"sub-auto", m_settings->value("subAuto", true).toBool() ? "fuzzy" : "no"},
        {"sub-font-size", m_settings->value("subFontSize", 46)},
        {"slang", m_settings->value("subLanguages", "de,ger,deu,en,eng")},
        {"alang", m_settings->value("audioLanguages", "de,ger,deu,en,eng")},
        {"osd-level", "0"},     // eigenes OSD in QML
        {"audio-client-name", "UVE Player"},
    };
    // Diagnose: ausführliches mpv-Protokoll (z. B. Shader-Kompilierung) über UVE_MPV_LOGFILE
    QVariantMap optsDiag = opts;
    if (qEnvironmentVariableIsSet("UVE_MPV_LOGFILE")) {
        optsDiag.insert("log-file", qEnvironmentVariable("UVE_MPV_LOGFILE"));
        optsDiag.insert("msg-level", "all=v");
    }
    const QVariantMap &opts2 = optsDiag;
    if (!m_player->initialize(opts2, error)) return false;
    m_proxy = uve::ProxyConfig::fromJson(QJsonObject::fromVariantMap(m_settings->value("proxy").toMap()));
    applyYtdlOptions();
    applyProxy();
    const QString dev = m_settings->value("audioDevice").toString();
    if (!dev.isEmpty()) m_player->setProperty("audio-device", dev);
    Logger::write("mpv", "initialisiert: " + m_player->mpvVersion() + " aus " + m_mpvLibrary);
    return true;
}

void PlayerApp::applyYtdlOptions()
{
    // yt-dlp: neueste vorhandene Version (Update im Benutzerordner oder mitgeliefert)
    const QString ytdlp = ToolUpdater::preferredYtdlp(QCoreApplication::applicationDirPath(), m_settings->dataDir());
    if (!ytdlp.isEmpty()) m_player->setProperty("script-opts", QStringLiteral("ytdl_hook-ytdl_path=%1").arg(ytdlp));
    // Standard: immer die beste Qualität (bestes Video + bestes Audio); optional begrenzt (Einstellungen)
    const int h = m_settings->value("streamQualityLimit", 0).toInt();
    m_player->setProperty("ytdl-format", h > 0
        ? QStringLiteral("bv*[height<=?%1]+ba/b[height<=?%1]/bv*+ba/b").arg(h)
        : QStringLiteral("bv*+ba/b"));
}

// ---------------------------------------------------------------- Stream-Auflösung
static bool isYtdlStream(const QString &path)
{
    const QUrl u(path);
    if (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https")) return false;
    const QString h = u.host().toLower();
    for (const char *d : {"youtube.com", "youtu.be", "twitch.tv"})
        if (h == QLatin1String(d) || h.endsWith(QLatin1Char('.') + QLatin1String(d))) return true;
    return false;
}

QString PlayerApp::ytdlFormatFor(const QString &id) const
{
    for (const auto &v : m_streamQualities) {
        const auto q = v.toMap();
        if (q.value("id").toString() == id) return q.value("format").toString();
    }
    return {};
}

void PlayerApp::loadPath(const QString &path, double start, bool paused)
{
    prepareStreamQualities(path);
    QStringList fileOpts;
    if (!m_streamQuality.isEmpty()) {
        const QString f = ytdlFormatFor(m_streamQuality);
        if (!f.isEmpty()) fileOpts << QStringLiteral("ytdl-format=") + f;
    }
    m_player->load(routeThroughProxy(path), start, paused, fileOpts);
}

void PlayerApp::prepareStreamQualities(const QString &path)
{
    if (path == m_qualityUrl) return;              // gleicher Stream: Liste und Auswahl behalten
    if (m_formatProc) { m_formatProc->disconnect(this); m_formatProc->kill(); m_formatProc->deleteLater(); m_formatProc = nullptr; }
    m_qualityUrl = path;
    m_streamQuality.clear();
    m_streamQualities.clear();
    if (!isYtdlStream(path)) { emit streamQualitiesChanged(); return; }
    const QString ytdlp = ToolUpdater::preferredYtdlp(QCoreApplication::applicationDirPath(), m_settings->dataDir());
    if (ytdlp.isEmpty()) { emit streamQualitiesChanged(); return; }

    QStringList args{"-J", "--no-playlist", "--no-warnings"};
    if (!m_ytdlProxy.isEmpty()) args << "--proxy" << m_ytdlProxy;
    args << path;
    auto *p = new QProcess(this);
    m_formatProc = p;
    connect(p, &QProcess::finished, this, [this, p, path](int code, QProcess::ExitStatus st) {
        p->deleteLater();
        if (m_formatProc != p) return;
        m_formatProc = nullptr;
        const auto info = QJsonDocument::fromJson(p->readAllStandardOutput()).object();
        if (st != QProcess::NormalExit || code != 0 || info.isEmpty())
            Logger::write("ytdl", "Formatliste nicht ermittelbar: " + QString::fromUtf8(p->readAllStandardError()).trimmed().left(300));
        // je Auflösung + Bildrate die Variante mit der höchsten Bitrate
        struct Q { int h = 0; int fps = 0; double tbr = -1; QString id; bool hasAudio = false; };
        QMap<QPair<int, int>, Q> best;
        for (const auto &v : info.value("formats").toArray()) {
            const auto f = v.toObject();
            const int h = f.value("height").toInt();
            if (h <= 0 || f.value("vcodec").toString() == QLatin1String("none")) continue;
            if (f.value("format_note").toString().contains(QLatin1String("storyboard"), Qt::CaseInsensitive)) continue;
            const int fps = qRound(f.value("fps").toDouble());
            const QPair<int, int> key{h, fps > 30 ? fps : 30};
            const double tbr = f.value("tbr").toDouble();
            auto &b = best[key];
            if (tbr > b.tbr) {
                const QString ac = f.value("acodec").toString();
                b = Q{h, fps, tbr, f.value("format_id").toString(), !ac.isEmpty() && ac != QLatin1String("none")};
            }
        }
        QVariantList list;
        for (auto it = best.constEnd(); it != best.constBegin();) {
            --it;
            const Q &q = it.value();
            if (q.id.isEmpty() || q.id.contains(QLatin1Char(','))) continue;
            QString label = QString::number(q.h) + QLatin1Char('p');
            if (q.fps > 30) label += QString::number(q.fps);
            list << QVariantMap{{"id", label}, {"label", label}, {"height", q.h},
                                {"format", q.hasAudio ? q.id : q.id + QStringLiteral("+ba/") + q.id}};
        }
        m_streamQualities = list;
        Logger::write("ytdl", QStringLiteral("%1 Auflösungen für %2").arg(list.size()).arg(path));
        emit streamQualitiesChanged();
    });
    emit streamQualitiesChanged();
    p->start(ytdlp, args);
}

void PlayerApp::setStreamQuality(const QString &id)
{
    if (id == m_streamQuality || m_qualityUrl.isEmpty()) return;
    m_streamQuality = id;
    emit streamQualitiesChanged();
    // an der aktuellen Stelle neu laden (Livestreams ohne Dauer: live bleiben)
    const double dur = m_player->duration();
    const double pos = dur > 0 ? m_player->position() : 0;
    const bool paused = m_player->paused();
    loadPath(m_qualityUrl, pos > 1 ? pos : 0, paused);
    showOsd(tr("Auflösung: %1").arg(id.isEmpty() ? tr("Beste (automatisch)") : id), 2000);
}

// ---------------------------------------------------------------- Proxy
uve::ProxyConfig PlayerApp::proxyFromMap(const QVariantMap &m) const
{
    uve::ProxyConfig c = m_proxy;
    c.mode = m.value("mode", c.mode).toString();
    c.type = m.value("type", c.type).toString();
    c.host = m.value("host", c.host).toString().trimmed();
    c.port = m.value("port", c.port).toInt();
    c.user = m.value("user", c.user).toString();
    if (m.contains("password") && !m.value("password").isNull()) c.password = m.value("password").toString(); // fehlt → gespeichertes behalten
    if (m.value("clearPassword").toBool()) c.password.clear();
    c.bypass = m.value("bypass", c.bypass).toString();
    return c;
}

QVariantMap PlayerApp::proxySettings() const
{
    const auto sys = uve::ProxyConfig::fromSystem();
    return {{"mode", m_proxy.mode}, {"type", m_proxy.type}, {"host", m_proxy.host}, {"port", m_proxy.port},
            {"user", m_proxy.user}, {"hasPassword", !m_proxy.password.isEmpty()}, {"bypass", m_proxy.bypass},
            {"systemProxy", sys.active() ? sys.url(false) : tr("direkte Verbindung")}};
}

QString PlayerApp::setProxy(const QVariantMap &settings)
{
    const uve::ProxyConfig c = proxyFromMap(settings);
    const QString err = c.validate();
    if (!err.isEmpty()) return err;
    m_proxy = c;
    m_settings->setValue("proxy", m_proxy.toJson().toVariantMap());
    applyProxy();
    showOsd(tr("Proxy: ") + m_proxyStatus, 2200);
    return {};
}

void PlayerApp::applyProxy()
{
    // Effektive Konfiguration (System → aufgelöster Proxy)
    const uve::ProxyConfig eff = m_proxy.mode == QLatin1String("system") ? uve::ProxyConfig::fromSystem() : m_proxy;

    // 1) Eigene Downloads (Updates, LUT-Pakete): Qt-Anwendungsproxy
    if (m_proxy.mode == QLatin1String("system")) {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
    } else {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QNetworkProxy::setApplicationProxy(m_proxy.toQNetworkProxy());
    }

    // 2) FFmpeg: Ausnahmen über no_proxy; "kein Proxy" bedeutet direkt (Umgebungsproxys ignorieren)
    qputenv("no_proxy", eff.bypass.toUtf8());
    qputenv("NO_PROXY", eff.bypass.toUtf8());
    if (m_proxy.mode == QLatin1String("none"))
        for (const char *v : {"http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"}) qunsetenv(v);

    // 3) mpv/FFmpeg: HTTP-Proxy direkt; SOCKS5 über die lokale Brücke (auch für tcp://-Weiterleitungen)
    if (!m_bridge) m_bridge = new uve::ProxyBridge(this);
    m_bridge->stop();
    m_proxiedOriginal.clear();
    QString mpvProxy;
    if (eff.active()) {
        QString err;
        if (m_bridge->start(eff.toQNetworkProxy(), &err)) {
            mpvProxy = eff.isSocks() ? m_bridge->proxyUrl() : eff.url();
        } else {
            Logger::write("proxy", "Brücke konnte nicht starten: " + err);
            mpvProxy = eff.isSocks() ? QString() : eff.url();
        }
    }
    m_player->setProperty("http-proxy", mpvProxy);

    // 4) yt-dlp: dieselbe Adresse wie mpv. Wichtig: mpvs ytdl_hook übernimmt den yt-dlp-Proxy als
    //    file-local "http-proxy" – ein socks5://-Wert würde die Brücke für die Mediendaten aushebeln.
    //    Ohne Proxy: Option nicht setzen.
    QVariantMap raw;
    if (!mpvProxy.isEmpty()) raw.insert("proxy", mpvProxy);
    else if (eff.active()) raw.insert("proxy", eff.url()); // Notfall: Brücke nicht startbar
    m_player->setProperty("ytdl-raw-options", raw);
    m_ytdlProxy = raw.value("proxy").toString();

    updateProxyStatus();
    Logger::write("proxy", m_proxyStatus); // ohne Zugangsdaten
}

void PlayerApp::updateProxyStatus()
{
    const uve::ProxyConfig eff = m_proxy.mode == QLatin1String("system") ? uve::ProxyConfig::fromSystem() : m_proxy;
    if (!eff.active()) m_proxyStatus = m_proxy.mode == QLatin1String("system") ? tr("System – direkte Verbindung") : tr("kein Proxy");
    else m_proxyStatus = QStringLiteral("%1%2 (%3)").arg(m_proxy.mode == QLatin1String("system") ? tr("System: ") : QString(),
                                                          eff.url(false), eff.isSocks() ? tr("SOCKS5 über lokale Brücke") : QStringLiteral("HTTP"));
    emit proxyChanged();
}

// ---------------------------------------------------------------- Sprache
QString PlayerApp::language() const { return m_languages ? m_languages->current() : QStringLiteral("de"); }
QVariantList PlayerApp::languages() const { return m_languages ? m_languages->available() : QVariantList(); }
QStringList PlayerApp::languageFolders() const
{
    QStringList out;
    if (m_languages) for (const auto &d : m_languages->searchDirs()) out << QDir::toNativeSeparators(d);
    return out;
}

QString PlayerApp::setLanguage(const QString &code)
{
    if (!m_languages) return QStringLiteral("no language manager");
    QString err;
    const bool ok = m_languages->apply(code, &err);
    if (ok) m_settings->setValue("language", code);
    // Oberfläche neu übersetzen: QML-Bindungen mit qsTr() und C++-Texte (Signale)
    if (m_engine) m_engine->retranslate();
    updateProxyStatus();
    m_hotkeys->notifyLanguageChanged();
    if (m_plugins) emit m_plugins->pluginsChanged();   // Panel-Titel der Plugins
    emit languageChanged();
    return ok ? QString() : err;
}

QString PlayerApp::routeThroughProxy(const QString &url)
{
    // Rohe TCP-Streams kennen keinen Proxy → über eine lokale Weiterleitung durch den Proxy führen
    if (!m_bridge || !m_bridge->running()) return url;
    const QUrl u(url);
    if (u.scheme() != QLatin1String("tcp") || u.host().isEmpty() || u.port() <= 0) return url;
    const uve::ProxyConfig eff = m_proxy.mode == QLatin1String("system") ? uve::ProxyConfig::fromSystem() : m_proxy;
    for (const auto &b : eff.bypass.split(',', Qt::SkipEmptyParts))
        if (u.host().compare(b.trimmed(), Qt::CaseInsensitive) == 0) return url;
    const quint16 local = m_bridge->forwardTcp(u.host(), quint16(u.port()));
    if (!local) return url;
    QUrl l = u;
    l.setHost(QStringLiteral("127.0.0.1"));
    l.setPort(local);
    m_proxiedOriginal.insert(l.toString(), url);
    return l.toString();
}

QVariantMap PlayerApp::proxyStats() const
{
    if (!m_bridge) return {};
    const auto &s = m_bridge->stats();
    QVariantMap hosts;
    for (auto it = s.hosts.begin(); it != s.hosts.end(); ++it) hosts.insert(it.key(), it.value());
    return {{"running", m_bridge->running()}, {"connections", s.connections}, {"bytesDown", s.bytesDown}, {"bytesUp", s.bytesUp}, {"hosts", hosts}};
}

void PlayerApp::testProxy(const QVariantMap &settings)
{
    uve::ProxyConfig c = proxyFromMap(settings);
    const QString err = c.validate();
    if (!err.isEmpty()) { emit proxyTestFinished(false, err); return; }
    if (c.mode == QLatin1String("system")) c = uve::ProxyConfig::fromSystem();
    auto *nam = new QNetworkAccessManager(this);
    nam->setProxy(c.toQNetworkProxy());
    QNetworkRequest req(QUrl(QStringLiteral("https://www.youtube.com/generate_204")));
    req.setTransferTimeout(12000);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("UVE-Player/1.0.0"));
    auto *timer = new QElapsedTimer;
    timer->start();
    auto *reply = nam->get(req);
    const QString via = c.active() ? c.url(false) : QStringLiteral("direkt");
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, timer, via] {
        const qint64 ms = timer->elapsed();
        delete timer;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && (http == 204 || http == 200))
            emit proxyTestFinished(true, tr("Verbindung OK über %1 – YouTube antwortet (HTTP %2) in %3 ms").arg(via).arg(http).arg(ms));
        else
            emit proxyTestFinished(false, tr("Fehlgeschlagen über %1: %2").arg(via, reply->errorString()));
        reply->deleteLater();
        nam->deleteLater();
    });
}

void PlayerApp::startPlugins()
{
    const QStringList dirs{
        QDir(m_settings->dataDir()).filePath("plugins"),                 // Benutzer-Plugins (Vorrang)
        QDir(QCoreApplication::applicationDirPath()).filePath("plugins"), // mitgelieferte Plugins
    };
    m_plugins = new PluginManager(m_settings, m_player, m_shaders, dirs, m_safe->active(), this);
    connect(m_plugins, &PluginManager::osdRequested, this, &PlayerApp::osd);
    if (!m_safe->active()) m_safe->markPluginsLoading();
    m_plugins->discoverAndStart();
    // Nach 10 s stabiler Laufzeit gilt der Plugin-Start als erfolgreich (Absturzerkennung)
    QTimer::singleShot(10000, this, [this] { if (!m_safe->active()) m_safe->markPluginsStable(); });

    if (m_settings->value("checkToolUpdates", true).toBool() && !m_selftest)
        QTimer::singleShot(2500, m_updater, &ToolUpdater::check);
}

void PlayerApp::resetSafeMode()
{
    m_safe->reset();
    emit safeModeChanged();
    showOsd(tr("Safe Mode beendet – Plugins werden beim nächsten Start geladen"), 3000);
}

void PlayerApp::restoreSessionAndOpen(const QStringList &args)
{
    QStringList files;
    for (const auto &a : args) if (!a.startsWith("--")) files << a;
    if (!files.isEmpty()) { openPaths(files, true); return; }
    if (!m_settings->value("restoreSession", true).toBool()) return;
    QStringList session;
    for (const auto &v : m_settings->value("session").toList()) session << v.toString();
    const auto entries = media::expand(session);
    if (entries.isEmpty()) return;
    m_playlist->add(entries);
    const int idx = m_settings->value("sessionIndex", -1).toInt();
    if (idx >= 0 && idx < m_playlist->count()) {
        m_playlist->setCurrentIndex(idx);
        // Sitzung pausiert an der gemerkten Position wiederherstellen
        const QString path = m_playlist->pathAt(idx);
        const double pos = m_settings->value("resumePlayback", true).toBool() ? m_settings->resumePosition(path) : 0;
        loadPath(path, pos, true);
    }
}

// ---------------------------------------------------------------- Öffnen
void PlayerApp::openPaths(const QStringList &pathsOrUrls, bool play, bool insertNext)
{
    QStringList subs, rest;
    for (const auto &p : pathsOrUrls) {
        const QString local = p.startsWith("file:", Qt::CaseInsensitive) ? QUrl(p).toLocalFile() : p;
        if (media::isSubtitleFile(local) && !media::isUrl(local)) subs << local; else rest << local;
    }
    for (const auto &s : subs) {
        if (!m_player->path().isEmpty()) { m_player->addSubtitle(s); showOsd(tr("Untertitel: ") + QFileInfo(s).fileName()); }
    }
    const auto entries = media::expand(rest);
    if (entries.isEmpty()) {
        if (subs.isEmpty() && !rest.isEmpty()) showOsd(tr("Nichts Abspielbares gefunden"), 2500);
        return;
    }
    const int at = m_playlist->add(entries, insertNext);
    if (play || m_playlist->currentIndex() < 0) playIndex(at);
}

void PlayerApp::openUrls(const QList<QUrl> &urls, bool play)
{
    QStringList list;
    for (const auto &u : urls) list << (u.isLocalFile() ? u.toLocalFile() : u.toString());
    openPaths(list, play);
}

QString PlayerApp::validateUrl(const QString &text) const
{
    const QString t = text.trimmed();
    if (t.isEmpty()) return tr("Bitte eine Adresse eingeben");
    if (!media::isUrl(t)) return tr("Adresse muss mit einem Schema beginnen (z. B. https://, tcp://)");
    if (!media::isAllowedUrlScheme(t)) return tr("Nicht unterstütztes Adressformat oder fehlender Host");
    return {};
}

void PlayerApp::playIndex(int index, double startAt)
{
    const QString path = m_playlist->pathAt(index);
    if (path.isEmpty()) return;
    saveResume();
    m_stoppedByUser = false;
    m_playlist->setCurrentIndex(index);
    double start = startAt;
    if (start < 0) {
        start = 0;
        if (m_settings->value("resumePlayback", true).toBool() && !media::isUrl(path)) {
            start = m_settings->resumePosition(path);
            if (start > 10) showOsd(tr("Fortgesetzt bei %1").arg(formatTime(start)), 2200);
            else start = 0;
        }
    }
    loadPath(path, start, false);
    saveSession();
}

void PlayerApp::next()
{
    const int i = m_playlist->nextIndex(false);
    if (i >= 0) playIndex(i);
}

void PlayerApp::previous()
{
    if (m_player->position() > 5) { m_player->seek(0); return; }
    const int i = m_playlist->previousIndex();
    if (i >= 0) playIndex(i);
}

void PlayerApp::stop()
{
    saveResume();
    m_stoppedByUser = true;
    m_player->stop();
}

void PlayerApp::onFileLoaded()
{
    const int i = m_playlist->currentIndex();
    // Direkt bei mpv abfragen: die beobachtete Eigenschaft "path" kann erst nach FILE_LOADED eintreffen
    m_loadedPath = m_player->getProperty(QStringLiteral("path")).toString();
    m_loadedPath = m_proxiedOriginal.value(m_loadedPath, m_loadedPath);
    m_playlist->setError(i, false);
    if (m_player->duration() > 0) m_playlist->setDuration(i, m_player->duration());
    QTimer::singleShot(1500, this, [this, i] { if (m_player->duration() > 0) m_playlist->setDuration(i, m_player->duration()); });
    m_settings->addRecentFile(m_playlist->pathAt(i));
    emit recentFilesChanged();
    if (m_plugins) m_plugins->notifyMediaChanged(m_loadedPath);
    Logger::write("play", "geladen: " + m_loadedPath);
}

void PlayerApp::onEndOfFile(const QString &reason)
{
    const int i = m_playlist->currentIndex();
    if (reason == "error") {
        m_playlist->setError(i, true);
        showOsd(tr("Nicht abspielbar: %1 (%2)").arg(m_playlist->titleAt(i), m_player->errorString()), 4000);
        Logger::write("play", "Fehler: " + m_playlist->pathAt(i) + " – " + m_player->errorString());
        const int n = m_playlist->nextIndex(true);
        if (n >= 0 && n != i) QTimer::singleShot(1500, this, [this, n] { playIndex(n); });
        return;
    }
    if (reason != "eof" || m_stoppedByUser) return;
    m_settings->clearResumePosition(m_playlist->pathAt(i));
    const int n = m_playlist->nextIndex(true);
    if (n >= 0) playIndex(n, 0);
}

void PlayerApp::saveResume()
{
    if (!m_settings->value("resumePlayback", true).toBool()) return;
    const QString path = m_player->path();
    if (path.isEmpty() || media::isUrl(path)) return;
    const double d = m_player->duration(), t = m_player->position();
    if (d < 60) return;
    if (t > 10 && t < d - 10) m_settings->setResumePosition(path, t, d);
    else m_settings->clearResumePosition(path);
}

void PlayerApp::saveSession()
{
    m_settings->setValue("session", m_playlist->paths());
    m_settings->setValue("sessionIndex", m_playlist->currentIndex());
}

void PlayerApp::shutdown()
{
    saveResume();
    saveSession();
    if (m_plugins) m_plugins->shutdown();
    m_settings->flush();
}

bool PlayerApp::savePlaylist(const QUrl &file)
{
    const bool ok = m_playlist->saveM3U(toLocalFile(file));
    showOsd(ok ? tr("Wiedergabeliste gespeichert") : tr("Speichern fehlgeschlagen"));
    return ok;
}

void PlayerApp::addSubtitle(const QUrl &file)
{
    m_player->addSubtitle(toLocalFile(file));
    showOsd(tr("Untertitel: ") + QFileInfo(toLocalFile(file)).fileName());
}

QString PlayerApp::snapshot()
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath("UVE Player");
    const QString file = m_player->screenshot(dir);
    showOsd(tr("Bildschirmfoto: ") + QFileInfo(file).fileName(), 2500);
    return file;
}

QString PlayerApp::formatTime(double s) const
{
    if (!(s > 0)) s = 0;
    const qint64 t = qint64(s);
    const int h = int(t / 3600), m = int((t % 3600) / 60), sec = int(t % 60);
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'))
                 : QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}

QVariantMap PlayerApp::mediaInfo() const
{
    QVariantMap m;
    const auto g = [this](const char *p) { return m_player->getProperty(QString::fromLatin1(p)); };
    m.insert(tr("Datei / Adresse"), m_player->path());
    m.insert(tr("Titel"), m_player->mediaTitle());
    m.insert(tr("Container"), g("file-format"));
    m.insert(tr("Dauer"), formatTime(m_player->duration()));
    m.insert(tr("Größe"), g("file-size").isValid() ? QString::number(g("file-size").toDouble() / 1048576.0, 'f', 1) + " MB" : QString("–"));
    m.insert(tr("Video-Codec"), g("video-codec"));
    m.insert(tr("Auflösung"), QStringLiteral("%1 × %2").arg(g("video-params/w").toInt()).arg(g("video-params/h").toInt()));
    m.insert(tr("Pixelformat"), g("video-params/pixelformat"));
    m.insert(tr("Farbraum"), QStringLiteral("%1 / %2").arg(g("video-params/primaries").toString(), g("video-params/gamma").toString()));
    m.insert(tr("Bildrate"), QString::number(g("container-fps").toDouble(), 'f', 3));
    m.insert(tr("Hardware-Dekodierung"), g("hwdec-current").toString().isEmpty() ? tr("nein (Software)") : g("hwdec-current"));
    m.insert(tr("Audio-Codec"), g("audio-codec-name"));
    m.insert(tr("Audio"), tr("%1 Kanäle · %2 Hz").arg(g("audio-params/channel-count").toInt()).arg(g("audio-params/samplerate").toInt()));
    m.insert(tr("Audiospuren"), m_player->audioTracks().size());
    m.insert(tr("Untertitelspuren"), m_player->subtitleTracks().size());
    m.insert(tr("Aktive Shader"), m_shaders->activeShaders().join(", "));
    return m;
}

void PlayerApp::clearRecentFiles() { m_settings->clearRecentFiles(); emit recentFilesChanged(); }

void PlayerApp::clearResumePositions()
{
    m_settings->setObject("resume", QJsonObject());
    showOsd(tr("Gemerkte Positionen gelöscht"));
}

void PlayerApp::openFolder(const QString &path) const { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }

void PlayerApp::showInExplorer(const QString &path) const
{
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

void PlayerApp::copyText(const QString &text) const { QGuiApplication::clipboard()->setText(text); }

void PlayerApp::applyHwdec(const QString &mode)
{
    m_settings->setValue("hwdec", mode);
    m_player->setProperty("hwdec", mode);
    showOsd(tr("Hardware-Dekodierung: ") + mode);
}

QVariantList PlayerApp::audioDevices() const
{
    QVariantList out;
    for (const auto &d : m_player->getProperty("audio-device-list").toList()) {
        const auto m = d.toMap();
        out << QVariantMap{{"name", m.value("name")}, {"description", m.value("description")}};
    }
    return out;
}

void PlayerApp::setAudioDevice(const QString &name)
{
    m_settings->setValue("audioDevice", name);
    m_player->setProperty("audio-device", name.isEmpty() ? QStringLiteral("auto") : name);
}

// ---------------------------------------------------------------- Selbsttest
// Aufruf: uve-player --selftest <ausgabe.json> [dateien/urls …]
// Spielt jede Quelle an, prüft Zustand, Dekodierung, Ton, Shader-Kette und einen Sprung; schreibt JSON.
void PlayerApp::setSelftest(const QString &outFile, const QStringList &files)
{
    m_selftest = true;
    m_selftestOut = outFile;
    m_selftestFiles = files;
    // UI-Modus (UVE_SELFTEST_UI=1): Oberfläche mit echten Mausereignissen prüfen (Main.qml steuert)
    m_selftestUiMode = qEnvironmentVariableIntValue("UVE_SELFTEST_UI") == 1;
    QTimer::singleShot(1500, this, [this] {
        for (const auto &f : m_selftestFiles) m_playlist->add(media::expand({f}));
        m_selftestIndex = -1;
        if (m_selftestUiMode) { playIndex(0, 0); return; }
        runSelftestStep();
    });
    QTimer::singleShot(240000, this, [this] { Logger::write("selftest", "Zeitüberschreitung"); QCoreApplication::exit(2); });
}

void PlayerApp::reportSelftestUi(const QVariantMap &ui)
{
    m_selftestUi = ui;
    if (m_selftestUiMode && ui.value("done").toBool()) finishSelftest();
}

void PlayerApp::simulateClick(qreal x, qreal y, int button, bool doubleClick)
{
    if (!m_window) return;
    const QPointF local(x, y);
    const QPointF global = m_window->mapToGlobal(local);
    const Qt::MouseButton b = button == 2 ? Qt::RightButton : Qt::LeftButton;
    auto send = [&](QEvent::Type t, Qt::MouseButtons buttons) {
        QMouseEvent e(t, local, local, global, b, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(m_window, &e);
    };
    send(QEvent::MouseButtonPress, b);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
    if (doubleClick) {
        send(QEvent::MouseButtonPress, b);
        send(QEvent::MouseButtonDblClick, b);
        send(QEvent::MouseButtonRelease, Qt::NoButton);
    }
}

QVariantMap PlayerApp::sampleWindow(qreal x, qreal y, qreal w, qreal h) const
{
    if (!m_window) return {};
    const QImage img = m_window->grabWindow();
    const qreal dpr = img.devicePixelRatio();
    const QRect r = QRectF(x * dpr, y * dpr, w * dpr, h * dpr).toRect().intersected(img.rect());
    double sr = 0, sg = 0, sb = 0, sat = 0;
    qint64 n = 0;
    for (int py = r.top(); py <= r.bottom(); py += 4) {
        for (int px = r.left(); px <= r.right(); px += 4) {
            const QColor c = img.pixelColor(px, py);
            sr += c.redF(); sg += c.greenF(); sb += c.blueF();
            const double mx = std::max({c.redF(), c.greenF(), c.blueF()}), mn = std::min({c.redF(), c.greenF(), c.blueF()});
            sat += mx - mn;
            ++n;
        }
    }
    if (n == 0) return {};
    return {{"r", sr / n}, {"g", sg / n}, {"b", sb / n}, {"chroma", sat / n}, {"samples", n}};
}

void PlayerApp::finishSelftest()
{
    // Abschluss: Ergebnis schreiben; Screenshot übernimmt main.cpp über selftestScreenshot
    bool ok = !m_selftestResults.isEmpty() || m_playlist->count() == 0 || m_selftestUiMode;
    for (const auto &r : m_selftestResults) ok = ok && r.toMap().value("ok").toBool();
    if (m_selftestUiMode) ok = ok && m_selftestUi.value("ok").toBool();
    {
        QJsonObject out{
            {"ok", ok}, {"version", version()}, {"mpv", mpvVersion()}, {"mpvLibrary", m_mpvLibrary}, {"qt", qtVersion()},
            {"safeMode", safeMode()}, {"results", QJsonArray::fromVariantList(m_selftestResults)},
            {"plugins", QJsonArray::fromVariantList([this] {
                 QVariantList l;
                 if (m_plugins) for (const auto &p : m_plugins->plugins()) {
                     auto m = p.toMap(); m.remove("controller");
                     l << QVariantMap{{"id", m.value("id")}, {"state", m.value("state")}, {"error", m.value("error")}, {"version", m.value("version")}};
                 }
                 return l; }())},
            {"activeShaders", QJsonArray::fromStringList(m_shaders->activeShaders())},
            {"shaderError", m_shaders->lastError()},
            {"ui", QJsonObject::fromVariantMap(m_selftestUi)},
            {"proxyStatus", m_proxyStatus},
            {"proxyStats", QJsonObject::fromVariantMap(proxyStats())},
        };
        QSaveFile f(m_selftestOut);
        if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(out).toJson()); f.commit(); }
        emit selftestScreenshot(QFileInfo(m_selftestOut).absolutePath() + "/selftest-" + (m_selftestUiMode ? QStringLiteral("ui") : QString::number(m_selftestResults.size())) + ".png");
        QTimer::singleShot(800, this, [ok] { QCoreApplication::exit(ok ? 0 : 1); });
    }
}

void PlayerApp::runSelftestStep()
{
    ++m_selftestIndex;
    if (m_selftestIndex >= m_playlist->count()) { finishSelftest(); return; }
    playIndex(m_selftestIndex, 0);
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    auto *poll = new QTimer(this);
    poll->setInterval(250);
    connect(poll, &QTimer::timeout, this, [this, poll, started] {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - started;
        const bool playing = m_player->state() == MpvController::Playing && m_player->position() > 2.0;
        const bool failed = m_player->state() == MpvController::Error;
        if (!playing && !failed && elapsed < 45000) return;
        poll->stop();
        poll->deleteLater();
        QVariantMap r{
            {"source", m_playlist->pathAt(m_selftestIndex)}, {"state", QVariant::fromValue(m_player->state()).toString()},
            {"error", m_player->errorString()}, {"position", m_player->position()}, {"duration", m_player->duration()},
            {"audioTracks", m_player->audioTracks().size()}, {"subtitleTracks", m_player->subtitleTracks().size()},
            {"title", m_player->mediaTitle()}, {"startMs", elapsed},
            {"hwdec", m_player->getProperty("hwdec-current")}, {"vo", m_player->getProperty("current-vo")},
            {"videoCodec", m_player->getProperty("video-codec")}, {"audioCodec", m_player->getProperty("audio-codec-name")},
            {"width", m_player->getProperty("video-params/w")}, {"height", m_player->getProperty("video-params/h")},
            {"glslShaders", m_player->getProperty("glsl-shaders")}, {"droppedFrames", m_player->getProperty("frame-drop-count")},
            {"audioPts", m_player->getProperty("audio-pts")},
        };
        if (!playing) { r.insert("ok", false); m_selftestResults << r; runSelftestStep(); return; }
        // Sprung auf 40 % (bei Livestreams übersprungen)
        const double d = m_player->duration();
        if (d > 20 && m_player->seekable()) {
            const double target = d * 0.4;
            m_player->seek(target);
            const qint64 seekStart = QDateTime::currentMSecsSinceEpoch();
            auto *wait = new QTimer(this);
            wait->setInterval(200);
            connect(wait, &QTimer::timeout, this, [this, wait, r, target, seekStart]() mutable {
                const qint64 el = QDateTime::currentMSecsSinceEpoch() - seekStart;
                const bool reached = std::abs(m_player->position() - target) < 5 && m_player->state() == MpvController::Playing;
                if (!reached && el < 20000) return;
                wait->stop(); wait->deleteLater();
                r.insert("seekTarget", target);
                r.insert("seekReached", reached);
                r.insert("seekMs", el);
                r.insert("positionAfterSeek", m_player->position());
                r.insert("ok", reached);
                m_selftestResults << r;
                QTimer::singleShot(1500, this, &PlayerApp::runSelftestStep);
            });
            wait->start();
        } else {
            r.insert("ok", true);
            r.insert("live", d <= 0);
            m_selftestResults << r;
            QTimer::singleShot(1500, this, &PlayerApp::runSelftestStep);
        }
    });
    poll->start();
}
