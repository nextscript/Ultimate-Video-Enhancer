#include "UveTikTokPlugin.h"

#include <QJsonObject>

namespace uve::tiktok {

static const QString kId = QStringLiteral("org.uveplayer.uvetiktok");

UveTikTokController::UveTikTokController(uve::IPlayerHost *host, QObject *parent) : QObject(parent), m_host(host)
{
    const QJsonObject s = host->loadPluginSettings(kId);
    m_maxHeight = s.value("maxHeight").toInt(0);
    if (s.value("liveFormat").toString() == QLatin1String("hls")) m_liveFormat = QStringLiteral("hls");
    const QString lang = host->call(kId, QStringLiteral("app.info")).toMap().value("language").toString();
    if (!lang.isEmpty()) m_language = lang;
}

void UveTikTokController::save()
{
    m_host->savePluginSettings(kId, QJsonObject{{"maxHeight", m_maxHeight}, {"liveFormat", m_liveFormat}});
}

void UveTikTokController::setMaxHeight(int height)
{
    if (height == m_maxHeight) return;
    m_maxHeight = height;
    save();
    emit changed();
}

void UveTikTokController::setLiveFormat(const QString &format)
{
    const QString f = format == QLatin1String("hls") ? QStringLiteral("hls") : QStringLiteral("flv");
    if (f == m_liveFormat) return;
    m_liveFormat = f;
    save();
    emit changed();
}

void UveTikTokController::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

void UveTikTokController::setLanguage(const QString &language)
{
    if (language.isEmpty() || language == m_language) return;
    m_language = language;
    emit changed();
}

bool UveTikTokPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host) { if (error) *error = QStringLiteral("no host"); return false; }
    m_host = host;
    m_controller = new UveTikTokController(host, this);
    return true;
}

void UveTikTokPlugin::deactivate()
{
    // Offene Anfragen an den normalen Weg zurückgeben, damit der Player nicht auf eine Antwort wartet
    const auto pending = m_jobUrls;
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) answer(it.key(), it.value());
    resetPlayback();
}

QString UveTikTokPlugin::text(const char *de, const char *en) const
{
    return QString::fromUtf8(m_controller->language().startsWith(QLatin1String("de")) ? de : en);
}

void UveTikTokPlugin::onEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("media.loading")) {
        // Kopfzeilen und Titel gelten nur für das eigene Video. Neu laden derselben Adresse behält sie.
        if (data.value("url").toString() != m_lastResolved) resetPlayback();
    } else if (name == QLatin1String("media.resolve")) {
        resolve(data.value("requestId").toULongLong(), data.value("url").toString());
    } else if (name == QLatin1String("media.resolveCancelled")) {
        const quint64 id = data.value("requestId").toULongLong();
        m_jobUrls.remove(id);
        if (ExtractJob *job = m_jobs.take(id)) job->deleteLater();
    } else if (name == QLatin1String("language.changed")) {
        m_controller->setLanguage(data.value("code").toString());
    }
}

void UveTikTokPlugin::resolve(quint64 requestId, const QString &url)
{
    auto *job = new ExtractJob(url, m_controller->maxHeight(), m_controller->liveFormat(), this);
    m_jobs.insert(requestId, job);
    m_jobUrls.insert(requestId, url);
    m_controller->setStatus(text("Suche Stream …", "Looking for the stream …"));

    connect(job, &ExtractJob::finished, this, [this, requestId, job](const Extraction &result) {
        if (m_jobs.value(requestId) != job) return;
        applyPlayback(result);
        m_lastResolved = result.url;
        answer(requestId, result.url, {}, result.title);
        if (!result.more.isEmpty()) {
            // Profil: übrige Videos direkt hinter dem laufenden einreihen; jedes wird erst beim Abspielen aufgelöst
            QVariantList durations;
            for (const double d : result.moreDurations) durations << d;
            m_host->call(kId, QStringLiteral("playlist.add"), {{"urls", result.more}, {"titles", result.moreTitles},
                                                               {"durations", durations}, {"play", false}, {"next", true}});
        }
        if (!result.collection.isEmpty()) {
            m_controller->setStatus(text("%1 (%2 Videos) – spielt: %3", "%1 (%2 videos) – playing: %3")
                                        .arg(result.collection).arg(result.count).arg(result.title));
            return;
        }
        m_controller->setStatus((result.live ? text("Spielt LIVE: %1", "Playing LIVE: %1") : text("Spielt als Stream: %1", "Streaming: %1"))
                                    .arg(result.title));
    });
    connect(job, &ExtractJob::failed, this, [this, requestId, job, url](const QString &error, bool definitive) {
        if (m_jobs.value(requestId) != job) return;
        if (definitive) {
            // z. B. Kanal nicht live oder Video privat: yt-dlp würde nur lange suchen und dasselbe melden
            m_host->log(kId, QStringLiteral("info"), QStringLiteral("%1: %2").arg(url, error));
            m_controller->setStatus(text("Nicht abspielbar: %1", "Not playable: %1").arg(error));
            answer(requestId, QString(), error);
            return;
        }
        // Eigener Weg gescheitert: Unveränderte Adresse geht im Player an yt-dlp
        m_host->log(kId, QStringLiteral("warning"), QStringLiteral("%1: %2 - using yt-dlp").arg(url, error));
        m_controller->setStatus(text("Nicht erkannt (%1), versuche yt-dlp", "Not recognised (%1), trying yt-dlp").arg(error));
        answer(requestId, url);
    });
    job->start();
}

void UveTikTokPlugin::answer(quint64 requestId, const QString &url, const QString &error, const QString &title)
{
    m_jobUrls.remove(requestId);
    if (ExtractJob *job = m_jobs.take(requestId)) job->deleteLater();
    QVariantMap args{{"requestId", QVariant::fromValue<qulonglong>(requestId)}, {"url", url}};
    if (!error.isEmpty()) args.insert(QStringLiteral("error"), error);
    if (!title.isEmpty()) args.insert(QStringLiteral("title"), title);   // ältere Player ignorieren das Feld
    m_host->call(kId, QStringLiteral("media.resolved"), args);
}

void UveTikTokPlugin::applyPlayback(const Extraction &result)
{
    if (!m_playbackChanged) m_savedUserAgent = m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("user-agent")}}).toString();
    m_playbackChanged = true;
    const auto set = [this](const char *name, const QString &value) {
        m_host->call(kId, QStringLiteral("mpv.set"), {{"name", QString::fromLatin1(name)}, {"value", value}});
    };
    set("referrer", result.referer);
    set("user-agent", result.userAgent);
    set("force-media-title", result.title);
    // Einzeln anhängen: "set" würde Werte an Kommas trennen, und Cookies dürfen Kommas enthalten
    const auto command = [this](const QStringList &args) { m_host->call(kId, QStringLiteral("mpv.command"), {{"args", args}}); };
    command({QStringLiteral("set"), QStringLiteral("http-header-fields"), QString()});
    if (!result.cookie.isEmpty())
        command({QStringLiteral("change-list"), QStringLiteral("http-header-fields"), QStringLiteral("append"), QStringLiteral("Cookie: ") + result.cookie});
    command({QStringLiteral("set"), QStringLiteral("sub-files"), QString()});
    for (const QString &subtitle : result.subtitles)
        command({QStringLiteral("change-list"), QStringLiteral("sub-files"), QStringLiteral("append"), subtitle});
}

void UveTikTokPlugin::resetPlayback()
{
    m_lastResolved.clear();
    if (!m_playbackChanged) return;
    m_playbackChanged = false;
    const auto set = [this](const char *name, const QString &value) {
        m_host->call(kId, QStringLiteral("mpv.set"), {{"name", QString::fromLatin1(name)}, {"value", value}});
    };
    set("referrer", QString());
    set("user-agent", m_savedUserAgent);
    set("force-media-title", QString());
    m_host->call(kId, QStringLiteral("mpv.command"), {{"args", QStringList{QStringLiteral("set"), QStringLiteral("http-header-fields"), QString()}}});
    m_host->call(kId, QStringLiteral("mpv.command"), {{"args", QStringList{QStringLiteral("set"), QStringLiteral("sub-files"), QString()}}});
}

} // namespace uve::tiktok
