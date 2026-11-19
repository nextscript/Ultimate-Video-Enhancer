#include "UveSunoPlugin.h"

namespace uve::suno {

static const QString kId = QStringLiteral("org.uveplayer.uvesuno");

UveSunoController::UveSunoController(uve::IPlayerHost *host, QObject *parent) : QObject(parent)
{
    const QString lang = host->call(kId, QStringLiteral("app.info")).toMap().value("language").toString();
    if (!lang.isEmpty()) m_language = lang;
}

void UveSunoController::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

void UveSunoController::setLanguage(const QString &language)
{
    if (language.isEmpty() || language == m_language) return;
    m_language = language;
    emit changed();
}

QString UveSunoController::text(const char *de, const char *en) const
{
    return QString::fromUtf8(m_language.startsWith(QLatin1String("de")) ? de : en);
}

bool UveSunoPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host) { if (error) *error = QStringLiteral("no host"); return false; }
    m_host = host;
    m_controller = new UveSunoController(host, this);
    return true;
}

void UveSunoPlugin::deactivate()
{
    const auto pending = m_jobs.keys();
    for (const quint64 id : pending) answer(id, {}, QStringLiteral("plugin stopped"));
    setTitle({});
}

void UveSunoPlugin::onEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("media.loading")) {
        // Titel gilt nur für den eigenen Song; erneutes Laden derselben Adresse behält ihn
        if (data.value("url").toString() != m_lastResolved) { m_lastResolved.clear(); setTitle({}); }
    } else if (name == QLatin1String("media.resolve")) {
        resolve(data.value("requestId").toULongLong(), data.value("url").toString(), data.value("preload").toBool());
    } else if (name == QLatin1String("media.resolveCancelled")) {
        if (ResolveJob *job = m_jobs.take(data.value("requestId").toULongLong())) job->deleteLater();
    } else if (name == QLatin1String("language.changed")) {
        m_controller->setLanguage(data.value("code").toString());
    }
}

void UveSunoPlugin::resolve(quint64 requestId, const QString &url, bool preload)
{
    auto *job = new ResolveJob(url, this);
    m_jobs.insert(requestId, job);
    if (!preload) m_controller->setStatus(m_controller->text("Suche Song …", "Looking up the song …"));

    connect(job, &ResolveJob::finished, this, [this, requestId, job, preload](const Resolution &result) {
        if (m_jobs.value(requestId) != job) return;
        if (preload) {
            // Vorab für den nächsten Titel (Player ≥ 1.2.2): nichts an der laufenden Wiedergabe ändern
            answer(requestId, result.url, {}, result.title);
            return;
        }
        setTitle(result.title);
        m_lastResolved = result.url;
        answer(requestId, result.url, {}, result.title);
        if (!result.more.isEmpty())
            m_host->call(kId, QStringLiteral("playlist.add"), {{"urls", result.more}, {"titles", result.moreTitles},
                                                               {"durations", result.moreDurations}, {"play", false}, {"next", true}});
        if (result.collection.isEmpty())
            m_controller->setStatus(m_controller->text("Spielt: %1", "Playing: %1").arg(result.title));
        else
            m_controller->setStatus(m_controller->text("%1 (%2 Songs) – spielt: %3", "%1 (%2 songs) – playing: %3")
                                        .arg(result.collection).arg(result.count).arg(result.title));
    });
    connect(job, &ResolveJob::failed, this, [this, requestId, job, url, preload](const QString &error, bool definitive) {
        if (m_jobs.value(requestId) != job) return;
        m_host->log(kId, QStringLiteral("warning"), QStringLiteral("%1: %2").arg(url, error));
        if (preload) { answer(requestId, {}, error); return; }
        if (definitive) {
            m_controller->setStatus(m_controller->text("Nicht abspielbar: %1", "Not playable: %1").arg(error));
            answer(requestId, {}, error);
            return;
        }
        // Eigener Weg gescheitert (z. B. API geändert): unveränderte Adresse geht an yt-dlp
        m_controller->setStatus(m_controller->text("Nicht erkannt (%1), versuche yt-dlp", "Not recognised (%1), trying yt-dlp").arg(error));
        answer(requestId, url);
    });
    job->start();
}

void UveSunoPlugin::answer(quint64 requestId, const QString &url, const QString &error, const QString &title)
{
    if (ResolveJob *job = m_jobs.take(requestId)) job->deleteLater();
    QVariantMap args{{"requestId", QVariant::fromValue<qulonglong>(requestId)}, {"url", url}};
    if (!error.isEmpty()) args.insert(QStringLiteral("error"), error);
    if (!title.isEmpty()) args.insert(QStringLiteral("title"), title);   // ältere Player ignorieren das Feld
    m_host->call(kId, QStringLiteral("media.resolved"), args);
}

void UveSunoPlugin::setTitle(const QString &title)
{
    if (title.isEmpty() && !m_titleSet) return;
    m_titleSet = !title.isEmpty();
    m_host->call(kId, QStringLiteral("mpv.set"), {{"name", QStringLiteral("force-media-title")}, {"value", title}});
}

} // namespace uve::suno
