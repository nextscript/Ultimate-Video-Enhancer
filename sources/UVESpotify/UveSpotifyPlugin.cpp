#include "UveSpotifyPlugin.h"

#include <QJsonArray>
#include <QProcess>
#include <QTimeZone>
#include <QTimer>
#include <QJsonObject>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace uve::spotify {

static const QString kId = QStringLiteral("org.uveplayer.uvespotify");
// Eingebaute Spotify-App (öffentlicher Client mit PKCE, kein Secret). Redirect-URI im Dashboard: http://127.0.0.1:43821/callback
// Im Entwicklungsmodus nur für Konten, die im Spotify-Dashboard unter "User Management" freigeschaltet sind.
static const QString kClientId = QStringLiteral("534dcbc066244fc98250ae0208fea462");

// Anmeldung (Refresh-Token) nur verschlüsselt ablegen: unter Windows an das Benutzerkonto gebunden (DPAPI)
static QString protect(const QString &plain)
{
    if (plain.isEmpty()) return {};
#ifdef Q_OS_WIN
    QByteArray in = plain.toUtf8();
    DATA_BLOB input{DWORD(in.size()), reinterpret_cast<BYTE *>(in.data())};
    DATA_BLOB output{};
    if (CryptProtectData(&input, L"UVESpotify", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        const QByteArray enc(reinterpret_cast<const char *>(output.pbData), int(output.cbData));
        LocalFree(output.pbData);
        return QStringLiteral("dpapi:") + QString::fromLatin1(enc.toBase64());
    }
#endif
    return QStringLiteral("plain:") + QString::fromLatin1(plain.toUtf8().toBase64());
}

static QString unprotect(const QString &stored)
{
    if (stored.startsWith(QLatin1String("plain:"))) return QString::fromUtf8(QByteArray::fromBase64(stored.mid(6).toLatin1()));
#ifdef Q_OS_WIN
    if (stored.startsWith(QLatin1String("dpapi:"))) {
        QByteArray in = QByteArray::fromBase64(stored.mid(6).toLatin1());
        DATA_BLOB input{DWORD(in.size()), reinterpret_cast<BYTE *>(in.data())};
        DATA_BLOB output{};
        if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
            const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(output.pbData), int(output.cbData));
            LocalFree(output.pbData);
            return plain;
        }
    }
#endif
    return {};
}

// ---------------------------------------------------------------- Controller
UveSpotifyController::UveSpotifyController(uve::IPlayerHost *host, QObject *parent)
    : QObject(parent), m_host(host), m_account(new SpotifyAccount(this))
{
    const QJsonObject s = host->loadPluginSettings(kId);
    if (s.value("mode").toString() == QLatin1String("video")) m_mode = QStringLiteral("video");
    m_account->setClientId(kClientId);
    m_account->setRefreshToken(unprotect(s.value("refreshToken").toString()));
    const QString lang = host->call(kId, QStringLiteral("app.info")).toMap().value("language").toString();
    if (!lang.isEmpty()) m_language = lang;

    connect(m_account, &SpotifyAccount::changed, this, &UveSpotifyController::changed);
    connect(m_account, &SpotifyAccount::refreshTokenChanged, this, [this] {
        save();
        if (!m_account->loggedIn()) { m_playlists.clear(); emit playlistsChanged(); }
    });
    connect(m_account, &SpotifyAccount::message, this, [this](const QString &message, bool error) {
        if (error && message.startsWith(QLatin1String("profile: HTTP 403"))) {
            // Spotify-App im Entwicklungsmodus: Konto ist nicht freigeschaltet
            m_account->logout();
            setStatus(text("Dieses Spotify-Konto ist für UVESpotify nicht freigeschaltet. Der Plugin-Autor muss es im Spotify-Dashboard unter „User Management“ eintragen.",
                           "This Spotify account is not enabled for UVESpotify. The plugin author has to add it in the Spotify dashboard under “User Management”."));
            return;
        }
        if (!error && message == QLatin1String("signed in")) {
            setStatus(text("Angemeldet.", "Signed in."));
            refreshPlaylists();
            return;
        }
        m_host->log(kId, error ? QStringLiteral("warning") : QStringLiteral("info"), message);
        setStatus(error ? text("Spotify: %1", "Spotify: %1").arg(message) : message);
    });
    if (m_account->loggedIn()) {
        m_account->loadProfile();   // meldet "signed in" und lädt danach die Playlists
    }
}

void UveSpotifyController::save()
{
    m_host->savePluginSettings(kId, QJsonObject{{"mode", m_mode}, {"refreshToken", protect(m_account->refreshToken())}});
}

void UveSpotifyController::setMode(const QString &mode)
{
    const QString m = mode == QLatin1String("video") ? QStringLiteral("video") : QStringLiteral("audio");
    if (m == m_mode) return;
    m_mode = m;
    save();
    emit changed();
}

void UveSpotifyController::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

void UveSpotifyController::setLanguage(const QString &language)
{
    if (language.isEmpty() || language == m_language) return;
    m_language = language;
    emit changed();
}

QString UveSpotifyController::text(const char *de, const char *en) const
{
    return QString::fromUtf8(m_language.startsWith(QLatin1String("de")) ? de : en);
}

void UveSpotifyController::login()
{
    QString error;
    m_account->login(&error);
    if (!error.isEmpty()) {
        setStatus(text("Anmeldung nicht möglich: %1", "Cannot sign in: %1").arg(error));
        return;
    }
    setStatus(text("Bitte im Browser bei Spotify anmelden …", "Please sign in to Spotify in the browser …"));
}

void UveSpotifyController::logout()
{
    m_account->logout();
    setStatus(text("Abgemeldet.", "Signed out."));
}

void UveSpotifyController::refreshPlaylists()
{
    if (!m_account->loggedIn() || m_playlistsLoading) return;
    m_playlistsLoading = true;
    emit playlistsChanged();
    m_account->apiPaged(QStringLiteral("https://api.spotify.com/v1/me/playlists?limit=50"), 500, [this](const QJsonArray &items, int status) {
        m_playlistsLoading = false;
        m_playlists.clear();
        for (const auto &v : items) {
            const QJsonObject p = v.toObject();
            if (p.value("id").toString().isEmpty()) continue;
            // Anzahl: "items.total" (neue API) oder "tracks.total"
            int count = p.value("items").toObject().value("total").toInt(-1);
            if (count < 0) count = p.value("tracks").toObject().value("total").toInt(0);
            // Kleinstes Cover ab 60 px (Liste ist nach Größe absteigend sortiert, Mosaik-Cover haben keine Größe)
            QString image;
            for (const auto &img : p.value("images").toArray()) {
                const QJsonObject o = img.toObject();
                if (image.isEmpty() || o.value("width").toInt(0) >= 60) image = o.value("url").toString();
            }
            m_playlists << QVariantMap{{"id", p.value("id").toString()},
                                       {"image", image},
                                       {"name", p.value("name").toString()},
                                       {"owner", p.value("owner").toObject().value("display_name").toString()},
                                       {"count", count},
                                       {"url", QStringLiteral("https://open.spotify.com/playlist/") + p.value("id").toString()}};
        }
        if (status != 200) setStatus(text("Playlists nicht vollständig geladen (HTTP %1).", "Playlists not fully loaded (HTTP %1).").arg(status));
        emit playlistsChanged();
    });
}

void UveSpotifyController::play(const QString &url)
{
    // Ganze Liste vorab holen und mit Namen in die Wiedergabeliste legen; der erste Titel startet sofort
    if (m_listJob) m_listJob->deleteLater();
    auto *job = new ResolveJob(url, m_account, false, this);
    m_listJob = job;
    job->setListOnly(true);
    setStatus(text("Lade Playlist …", "Loading playlist …"));
    connect(job, &ResolveJob::listed, this, [this, job](const QList<Track> &tracks, const QString &collection) {
        job->deleteLater();
        QStringList urls;
        QVariantList titles, durations;
        for (const Track &t : tracks) {
            urls << trackUrl(t);
            titles << t.display();
            durations << double(t.durationMs) / 1000.0;
        }
        // titles/durations/replace kennt der Player ab 1.2.1; ältere zeigen den Namen aus der Adresse und hängen an
        m_host->call(kId, QStringLiteral("playlist.add"),
                     {{"urls", urls}, {"titles", titles}, {"durations", durations}, {"replace", true}, {"play", true}, {"next", false}});
        setStatus(text("%1: %2 Titel in der Wiedergabeliste", "%1: %2 tracks in the playlist")
                      .arg(collection.isEmpty() ? text("Playlist", "Playlist") : collection).arg(tracks.size()));
    });
    connect(job, &ResolveJob::failed, this, [this, job, url](const QString &error, bool) {
        job->deleteLater();
        // Liste nicht lesbar: wie ein geöffneter Link behandeln (löst beim Abspielen auf)
        m_host->log(kId, QStringLiteral("warning"), QStringLiteral("list %1: %2").arg(url, error));
        m_host->call(kId, QStringLiteral("playlist.add"), {{"urls", QStringList{url}}, {"replace", true}, {"play", true}, {"next", false}});
    });
    job->start();
}

// ---------------------------------------------------------------- Plugin
bool UveSpotifyPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host) { if (error) *error = QStringLiteral("no host"); return false; }
    m_host = host;
    m_controller = new UveSpotifyController(host, this);
    return true;
}

void UveSpotifyPlugin::deactivate()
{
    const auto pending = m_jobUrls;
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) answer(it.key(), {}, QStringLiteral("plugin stopped"));
    m_controller->account()->cancelLogin();
    for (auto it = m_preloadWaiting.constBegin(); it != m_preloadWaiting.constEnd(); ++it)
        for (const quint64 id : it.value()) answer(id, {}, QStringLiteral("plugin stopped"));
    m_preloadWaiting.clear();
    m_streams.clear();
    resetPlayback();
}

void UveSpotifyPlugin::onEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("media.loading")) {
        if (data.value("url").toString() != m_lastResolved) resetPlayback();
    } else if (name == QLatin1String("media.resolve")) {
        if (data.value("preload").toBool()) preload(data.value("requestId").toULongLong(), data.value("url").toString());
        else resolve(data.value("requestId").toULongLong(), data.value("url").toString());
    } else if (name == QLatin1String("media.resolveCancelled")) {
        const quint64 id = data.value("requestId").toULongLong();
        for (auto it = m_preloadWaiting.begin(); it != m_preloadWaiting.end(); ++it) it->removeAll(id);
        m_jobUrls.remove(id);
        if (ResolveJob *job = m_jobs.take(id)) job->deleteLater();
    } else if (name == QLatin1String("language.changed")) {
        m_controller->setLanguage(data.value("code").toString());
    } else if (name == QLatin1String("media.loaded")) {
        prefetchNext();
    }
}

void UveSpotifyPlugin::resolve(quint64 requestId, const QString &url)
{
    Resolution cached;
    if (takeCached(url, &cached)) {
        // Vorgeladen: sofort abspielen, ohne Suche und ohne yt-dlp
        applyPlayback(cached);
        m_lastResolved = cached.url;
        answer(requestId, cached.url);
        m_controller->setStatus(m_controller->text("Spielt: %1", "Playing: %1").arg(cached.title));
        return;
    }
    auto *job = new ResolveJob(url, m_controller->account(), m_controller->mode() == QLatin1String("video"), this);
    m_jobs.insert(requestId, job);
    m_jobUrls.insert(requestId, url);
    m_controller->setStatus(m_controller->text("Suche Titel …", "Looking up the track …"));

    connect(job, &ResolveJob::finished, this, [this, requestId, job](const Resolution &result) {
        if (m_jobs.value(requestId) != job) return;
        applyPlayback(result);
        m_lastResolved = result.url;
        answer(requestId, result.url);
        if (!result.more.isEmpty())
            m_host->call(kId, QStringLiteral("playlist.add"), {{"urls", result.more}, {"titles", result.moreTitles},
                                                               {"durations", result.moreDurations}, {"play", false}, {"next", true}});
        if (result.collection.isEmpty())
            m_controller->setStatus(m_controller->text("Spielt: %1", "Playing: %1").arg(result.title));
        else
            m_controller->setStatus(m_controller->text("%1 (%2 Titel) – spielt: %3", "%1 (%2 tracks) – playing: %3")
                                        .arg(result.collection).arg(result.count).arg(result.title));
    });
    connect(job, &ResolveJob::failed, this, [this, requestId, job, url](const QString &error, bool) {
        if (m_jobs.value(requestId) != job) return;
        // yt-dlp kann Spotify nicht abspielen (DRM): Fehler direkt melden statt die Adresse weiterzugeben
        m_host->log(kId, QStringLiteral("warning"), QStringLiteral("%1: %2").arg(url, error));
        m_controller->setStatus(m_controller->text("Nicht abspielbar: %1", "Not playable: %1").arg(error));
        answer(requestId, {}, error);
    });
    job->start();
}

bool UveSpotifyPlugin::takeCached(const QString &url, Resolution *out)
{
    const Link link = parseLink(url);
    if (link.kind != Kind::Track) return false;
    const auto it = m_streams.constFind(link.id);
    if (it == m_streams.constEnd()) return false;
    const bool fresh = it->mode == m_controller->mode() && QDateTime::currentDateTimeUtc() < it->validUntil;
    if (fresh) *out = it->resolution;
    m_streams.erase(it);   // einmal verwenden: beim nächsten Mal (z. B. Wiederholung) frisch auflösen
    return fresh;
}

void UveSpotifyPlugin::prefetchNext()
{
    const QVariantList items = m_host->call(kId, QStringLiteral("playlist.items")).toList();
    int current = -1;
    for (int i = 0; i < items.size(); ++i)
        if (items.at(i).toMap().value("current").toBool()) { current = i; break; }
    if (current < 0 || current + 1 >= items.size()) return;
    const Link next = parseLink(items.at(current + 1).toMap().value("path").toString());
    if (next.kind == Kind::Track) prefetch(next.id);
}

void UveSpotifyPlugin::prefetch(const QString &trackId)
{
    if (m_prefetching.contains(trackId)) return;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto it = m_streams.constFind(trackId);
    if (it != m_streams.constEnd() && it->mode == m_controller->mode() && now < it->validUntil) return;
    // Abgelaufenes aufräumen, damit der Zwischenspeicher klein bleibt
    for (auto s = m_streams.begin(); s != m_streams.end();)
        s = now >= s->validUntil ? m_streams.erase(s) : std::next(s);

    m_prefetching.insert(trackId);
    const QString mode = m_controller->mode();
    auto *job = new ResolveJob(trackUrl(trackId), m_controller->account(), mode == QLatin1String("video"), this);
    connect(job, &ResolveJob::finished, this, [this, job, trackId, mode](const Resolution &result) {
        job->deleteLater();
        extractStream(trackId, result, mode);
    });
    connect(job, &ResolveJob::failed, this, [this, job, trackId](const QString &error, bool) {
        job->deleteLater();
        m_prefetching.remove(trackId);
        m_host->log(kId, QStringLiteral("info"), QStringLiteral("prefetch %1: %2").arg(trackId, error));
        flushPreloads(trackId, error);
    });
    job->start();
}

void UveSpotifyPlugin::extractStream(const QString &trackId, const Resolution &resolved, const QString &mode)
{
    const auto store = [this, trackId, mode](const Resolution &r, const QDateTime &until) {
        m_prefetching.remove(trackId);
        m_streams.insert(trackId, CachedStream{r, mode, until});
        flushPreloads(trackId);
    };
    // yt-dlp wie der Player: gleicher Pfad (ytdl_hook) und gleiche Zusatzoptionen (z. B. Proxy)
    const QString ytdlp = m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("script-opts")}})
                              .toMap().value(QStringLiteral("ytdl_hook-ytdl_path")).toString();
    if (ytdlp.isEmpty()) {
        // Ohne yt-dlp-Pfad: wenigstens Spotify-Daten und YouTube-Suche sparen
        return store(resolved, QDateTime::currentDateTimeUtc().addSecs(3 * 3600));
    }
    QString format = QStringLiteral("bestaudio/best");
    if (mode == QLatin1String("video")) {
        format = m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("ytdl-format")}}).toString();
        if (format.isEmpty()) format = QStringLiteral("bestvideo+bestaudio/best");
    }
    QStringList args{QStringLiteral("-g"), QStringLiteral("-f"), format, QStringLiteral("--no-playlist"), QStringLiteral("--no-warnings")};
    const QVariantMap raw = m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("ytdl-raw-options")}}).toMap();
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
        args << QStringLiteral("--") + it.key();
        if (!it.value().toString().isEmpty()) args << it.value().toString();
    }
    QString target = resolved.url;
    if (target.startsWith(QLatin1String("ytdl://"))) target = target.mid(7);   // "ytsearch1:…" versteht yt-dlp direkt
    args << QStringLiteral("--") << target;

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc, resolved, store, trackId](int code, QProcess::ExitStatus status) {
        proc->deleteLater();
        const QString direct = code == 0 && status == QProcess::NormalExit
                                   ? directStream(QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n')))
                                   : QString();
        Resolution r = resolved;
        QDateTime until = QDateTime::currentDateTimeUtc().addSecs(3 * 3600);
        if (!direct.isEmpty()) {
            r.url = direct;
            // googlevideo-Adressen laufen ab: mit Reserve vorher verwerfen
            if (const qint64 expire = streamExpiry(direct); expire > 0)
                until = std::min(until, QDateTime::fromSecsSinceEpoch(expire, QTimeZone::UTC).addSecs(-600));
        } else {
            m_host->log(kId, QStringLiteral("info"), QStringLiteral("prefetch %1: yt-dlp exit %2").arg(trackId).arg(code));
        }
        store(r, until);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, trackId](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        proc->deleteLater();
        m_prefetching.remove(trackId);
        flushPreloads(trackId, QStringLiteral("yt-dlp not started"));
    });
    QTimer::singleShot(60000, proc, [proc] { proc->kill(); });
    proc->start(ytdlp, args);
}

void UveSpotifyPlugin::preload(quint64 requestId, const QString &url)
{
    const Link link = parseLink(url);
    if (link.kind != Kind::Track) {
        // Listen (Playlist-Link als Eintrag) löst erst die normale Wiedergabe auf
        answer(requestId, {}, QStringLiteral("only single tracks are preloaded"));
        return;
    }
    Resolution cached;
    if (takeCached(url, &cached)) {
        answer(requestId, cached.url, {}, cached.title);
        return;
    }
    m_preloadWaiting[link.id] << requestId;
    prefetch(link.id);   // läuft schon (z. B. eigenes Vorladen): die Anfrage wartet mit
}

void UveSpotifyPlugin::flushPreloads(const QString &trackId, const QString &error)
{
    const QList<quint64> waiting = m_preloadWaiting.take(trackId);
    if (waiting.isEmpty()) return;
    Resolution r;
    const bool ok = error.isEmpty() && takeCached(trackUrl(trackId), &r);
    for (const quint64 id : waiting)
        answer(id, ok ? r.url : QString(), ok ? QString() : (error.isEmpty() ? QStringLiteral("not resolved") : error), ok ? r.title : QString());
}

void UveSpotifyPlugin::answer(quint64 requestId, const QString &url, const QString &error, const QString &title)
{
    m_jobUrls.remove(requestId);
    if (ResolveJob *job = m_jobs.take(requestId)) job->deleteLater();
    QVariantMap args{{"requestId", QVariant::fromValue<qulonglong>(requestId)}, {"url", url}};
    if (!error.isEmpty()) args.insert(QStringLiteral("error"), error);
    if (!title.isEmpty()) args.insert(QStringLiteral("title"), title);   // ältere Player ignorieren das Feld
    m_host->call(kId, QStringLiteral("media.resolved"), args);
}

void UveSpotifyPlugin::applyPlayback(const Resolution &result)
{
    m_playbackChanged = true;
    m_host->call(kId, QStringLiteral("mpv.set"), {{"name", QStringLiteral("force-media-title")}, {"value", result.title}});
    if (m_controller->mode() == QLatin1String("audio")) {
        // Nur Ton: spart Bandbreite, die Länge passt zur Spotify-Fassung
        if (!m_formatChanged) m_savedFormat = m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("ytdl-format")}}).toString();
        m_formatChanged = true;
        m_host->call(kId, QStringLiteral("mpv.set"), {{"name", QStringLiteral("ytdl-format")}, {"value", QStringLiteral("bestaudio/best")}});
    }
}

void UveSpotifyPlugin::resetPlayback()
{
    m_lastResolved.clear();
    if (m_formatChanged) {
        m_formatChanged = false;
        m_host->call(kId, QStringLiteral("mpv.set"), {{"name", QStringLiteral("ytdl-format")}, {"value", m_savedFormat}});
    }
    if (!m_playbackChanged) return;
    m_playbackChanged = false;
    m_host->call(kId, QStringLiteral("mpv.set"), {{"name", QStringLiteral("force-media-title")}, {"value", QString()}});
}

} // namespace uve::spotify
