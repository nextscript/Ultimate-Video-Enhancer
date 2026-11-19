#include "SunoNet.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace uve::suno {

namespace {
constexpr auto kChromeUa = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";
constexpr int kTimeoutMs = 30000;
constexpr int kMaxClips = 500;     // Profile können tausende Songs haben
constexpr int kMaxPages = 60;
}

ResolveJob::ResolveJob(const QString &url, QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_url(url.trimmed())
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

void ResolveJob::start()
{
    const Link link = parseLink(m_url);
    if (link.kind == Kind::Unknown) return fail(QStringLiteral("unsupported Suno link (song, playlist, @profile or /s/ short link)"), true);
    handle(link);
}

void ResolveJob::handle(const Link &link)
{
    switch (link.kind) {
    case Kind::Song: return song(link.id);
    case Kind::Playlist:
    case Kind::Profile: return pages(link.kind, link.id, 1, {});
    case Kind::Short:
        // Kurzlink leitet auf /song/<id> weiter
        get(m_url, [this](int status, const QByteArray &, const QUrl &finalUrl) {
            const Link target = parseLink(finalUrl.toString());
            if (target.kind == Kind::Unknown || target.kind == Kind::Short)
                return fail(QStringLiteral("short link leads nowhere (HTTP %1)").arg(status), status == 404);
            handle(target);
        });
        return;
    case Kind::Unknown: break;
    }
    fail(QStringLiteral("unsupported Suno link"), true);
}

void ResolveJob::get(const QString &url, const std::function<void(int, const QByteArray &, const QUrl &)> &cb)
{
    QNetworkRequest req(QUrl::fromEncoded(url.toUtf8()));
    req.setTransferTimeout(kTimeoutMs);
    req.setRawHeader("User-Agent", kChromeUa);
    req.setRawHeader("Accept", "application/json, text/html;q=0.9");
    QNetworkReply *reply = m_nam->get(req);
    reply->setParent(this);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
        reply->deleteLater();
        if (m_done) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0) return fail(reply->errorString());
        cb(status, reply->readAll(), reply->url());
    });
}

void ResolveJob::fail(const QString &error, bool definitive)
{
    if (m_done) return;
    m_done = true;
    emit failed(error, definitive);
}

void ResolveJob::song(const QString &id)
{
    get(clipApi(id), [this](int status, const QByteArray &body, const QUrl &) {
        if (status == 404) return fail(QStringLiteral("song not found or private"), true);
        if (status != 200) return fail(QStringLiteral("HTTP %1").arg(status));
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const Clip c = clipFromJson(o);
        if (c.url.isEmpty()) {
            const QString state = o.value("status").toString();
            return fail(state == QLatin1String("complete") ? QStringLiteral("Suno offers no open audio for this song")
                                                           : QStringLiteral("song is not finished (%1)").arg(state),
                        true);
        }
        begin({c});
    });
}

void ResolveJob::pages(Kind kind, const QString &id, int page, QList<Clip> clips)
{
    const QString url = kind == Kind::Playlist ? playlistApi(id, page) : profileApi(id, page);
    get(url, [this, kind, id, page, clips](int status, const QByteArray &body, const QUrl &) mutable {
        if (status == 404) return fail(kind == Kind::Playlist ? QStringLiteral("playlist not found or private")
                                                              : QStringLiteral("profile @%1 not found").arg(id),
                                       true);
        if (status != 200) {
            if (!clips.isEmpty()) return begin(clips);   // spätere Seite gescheitert: Bisheriges spielen
            return fail(QStringLiteral("HTTP %1").arg(status));
        }
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const QList<Clip> found = kind == Kind::Playlist ? playlistClips(o) : profileClips(o);
        clips += found;
        if (m_result.collection.isEmpty())
            m_result.collection = kind == Kind::Playlist ? o.value("name").toString()
                                                         : QStringLiteral("@") + o.value("handle").toString(id);
        const int total = (kind == Kind::Playlist ? o.value("num_total_results") : o.value("num_total_clips")).toInt();
        const int listed = (kind == Kind::Playlist ? o.value("playlist_clips") : o.value("clips")).toArray().size();
        // Weiter, solange die Seite voll war und noch Songs fehlen
        const bool more = listed > 0 && page < kMaxPages && clips.size() < kMaxClips && (total <= 0 || page * listed < total);
        if (more) return pages(kind, id, page + 1, clips);
        if (clips.isEmpty()) return fail(QStringLiteral("no playable songs in %1").arg(m_result.collection), true);
        begin(clips.mid(0, kMaxClips));
    });
}

void ResolveJob::begin(const QList<Clip> &clips)
{
    const Clip &first = clips.first();
    m_result.url = playbackUrl(first);   // Cover als Bild, Ton aus der Suno-Datei
    m_result.title = first.display();
    m_result.count = int(clips.size());
    for (int i = 1; i < clips.size(); ++i) {
        m_result.more << songUrl(clips.at(i));
        m_result.moreTitles << clips.at(i).display();
        m_result.moreDurations << clips.at(i).duration;
    }
    m_done = true;
    emit finished(m_result);
}

} // namespace uve::suno
