#include "SpotifyCore.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>

#include <functional>

namespace uve::spotify {

QString Track::display() const
{
    const QString who = artists.join(QStringLiteral(", "));
    return who.isEmpty() ? title : QStringLiteral("%1 – %2").arg(who, title);
}

QString Track::query() const
{
    // Erster Interpret genügt, "feat."-Listen verwässern die Suche
    const QString who = artists.isEmpty() ? QString() : artists.first();
    return who.isEmpty() ? title : QStringLiteral("%1 - %2").arg(who, title);
}

Link parseLink(const QString &url)
{
    const QUrl u(url);
    const QString host = u.host().toLower();
    if (host != QLatin1String("open.spotify.com") && host != QLatin1String("play.spotify.com")) return {};
    // "/intl-de/track/…" und "/embed/track/…" wie "/track/…" behandeln
    QStringList parts = u.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    while (!parts.isEmpty() && (parts.first().startsWith(QLatin1String("intl-")) || parts.first() == QLatin1String("embed")))
        parts.removeFirst();
    if (parts.size() >= 2 && parts.at(0) == QLatin1String("collection") && parts.at(1) == QLatin1String("tracks"))
        return {Kind::Liked, {}};
    if (parts.size() < 2) return {};
    static const QRegularExpression idRe(QStringLiteral("^[A-Za-z0-9]{10,40}$"));
    const QString id = parts.at(1);
    if (!idRe.match(id).hasMatch()) return {};
    const QString type = parts.at(0);
    if (type == QLatin1String("track")) return {Kind::Track, id};
    if (type == QLatin1String("album")) return {Kind::Album, id};
    if (type == QLatin1String("playlist")) return {Kind::Playlist, id};
    if (type == QLatin1String("artist")) return {Kind::Artist, id};
    return {};
}

QString trackUrl(const QString &id)
{
    return QStringLiteral("https://open.spotify.com/track/") + id;
}

QString trackUrl(const Track &track)
{
    // Der Player dekodiert den Pfad vor dem Aufteilen: "/" im Namen ("AC/DC") durch das gleich aussehende "∕" ersetzen
    const QString name = track.display().simplified().replace(QLatin1Char('/'), QChar(0x2215));
    if (name.isEmpty()) return trackUrl(track.id);
    return trackUrl(track.id) + QLatin1Char('/') + QString::fromLatin1(QUrl::toPercentEncoding(name));
}

QString nextDataJson(const QString &html)
{
    static const QRegularExpression script(QStringLiteral("<script[^>]*\\bid=\"__NEXT_DATA__\"[^>]*>(.*?)</script>"),
                                           QRegularExpression::DotMatchesEverythingOption);
    return script.match(html).captured(1);
}

QJsonObject embedEntity(const QString &html)
{
    const QJsonObject root = QJsonDocument::fromJson(nextDataJson(html).toUtf8()).object();
    return root.value("props").toObject().value("pageProps").toObject().value("state").toObject()
        .value("data").toObject().value("entity").toObject();
}

static QString idFromUri(const QString &uri)
{
    return uri.section(QLatin1Char(':'), -1);
}

static QStringList splitArtists(const QString &subtitle)
{
    QStringList out;
    for (QString a : subtitle.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        a.replace(QChar(0x00A0), QLatin1Char(' '));
        a = a.trimmed();
        if (!a.isEmpty()) out << a;
    }
    return out;
}

Track embedTrack(const QJsonObject &entity)
{
    Track t;
    t.id = entity.value("id").toString();
    if (t.id.isEmpty()) t.id = idFromUri(entity.value("uri").toString());
    t.title = entity.value("name").toString();
    if (t.title.isEmpty()) t.title = entity.value("title").toString();
    for (const auto &a : entity.value("artists").toArray()) t.artists << a.toObject().value("name").toString();
    if (t.artists.isEmpty()) t.artists = splitArtists(entity.value("subtitle").toString());
    t.durationMs = entity.value("duration").toVariant().toLongLong();
    return t;
}

QList<Track> embedTrackList(const QJsonObject &entity)
{
    QList<Track> out;
    for (const auto &v : entity.value("trackList").toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value("entityType").toString().isEmpty() && o.value("entityType").toString() != QLatin1String("track")) continue;
        Track t;
        t.id = idFromUri(o.value("uri").toString());
        t.title = o.value("title").toString();
        t.artists = splitArtists(o.value("subtitle").toString());
        t.durationMs = o.value("duration").toVariant().toLongLong();
        if (!t.id.isEmpty() && !t.title.isEmpty()) out << t;
    }
    return out;
}

Track apiTrack(const QJsonObject &track)
{
    Track t;
    t.id = track.value("id").toString();
    t.title = track.value("name").toString();
    for (const auto &a : track.value("artists").toArray()) t.artists << a.toObject().value("name").toString();
    t.durationMs = track.value("duration_ms").toVariant().toLongLong();
    return t;
}

QList<Track> apiItems(const QJsonArray &items)
{
    QList<Track> out;
    for (const auto &v : items) {
        const QJsonObject entry = v.toObject();
        // Playlist-Einträge: "item" (neue API) oder "track"; Albumtitel kommen direkt
        QJsonObject track = entry.value("item").toObject();
        if (track.isEmpty()) track = entry.value("track").toObject();
        if (track.isEmpty() && entry.contains(QLatin1String("duration_ms"))) track = entry;
        if (track.isEmpty() || track.value("is_local").toBool()) continue;
        if (!track.value("type").toString().isEmpty() && track.value("type").toString() != QLatin1String("track")) continue;   // Podcasts
        const Track t = apiTrack(track);
        if (!t.id.isEmpty() && !t.title.isEmpty()) out << t;
    }
    return out;
}

int parseClock(const QString &text)
{
    int total = 0;
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.size() < 2) return 0;
    for (const QString &p : parts) {
        bool ok = false;
        const int n = p.toInt(&ok);
        if (!ok) return 0;
        total = total * 60 + n;
    }
    return total;
}

QList<YtResult> youtubeResults(const QString &html)
{
    QList<YtResult> out;
    const int marker = html.indexOf(QLatin1String("var ytInitialData = "));
    if (marker < 0) return out;
    const int start = html.indexOf(QLatin1Char('{'), marker);
    const int end = html.indexOf(QLatin1String(";</script>"), start);
    if (start < 0 || end < 0) return out;
    const QJsonObject data = QJsonDocument::fromJson(html.mid(start, end - start).toUtf8()).object();

    std::function<void(const QJsonValue &)> walk = [&](const QJsonValue &v) {
        if (v.isObject()) {
            const QJsonObject o = v.toObject();
            const QJsonObject r = o.value("videoRenderer").toObject();
            if (!r.isEmpty()) {
                YtResult y;
                y.id = r.value("videoId").toString();
                y.title = r.value("title").toObject().value("runs").toArray().first().toObject().value("text").toString();
                y.seconds = parseClock(r.value("lengthText").toObject().value("simpleText").toString());
                if (!y.id.isEmpty()) out << y;
                return;
            }
            for (auto it = o.constBegin(); it != o.constEnd(); ++it) walk(it.value());
        } else if (v.isArray()) {
            for (const auto &e : v.toArray()) walk(e);
        }
    };
    walk(data);
    return out;
}

QString pickYoutube(const QList<YtResult> &results, qint64 durationMs, bool preferVideo)
{
    if (results.isEmpty()) return {};
    const int want = int((durationMs + 500) / 1000);
    const int consider = std::min<int>(int(results.size()), 10);
    const auto isVideo = [](const YtResult &r) {
        static const QRegularExpression v(QStringLiteral("official\\s+(music\\s+)?video|video\\s+oficial|musikvideo|\\(video\\)|\\[video\\]"),
                                          QRegularExpression::CaseInsensitiveOption);
        return v.match(r.title).hasMatch();
    };
    if (preferVideo) {
        for (int i = 0; i < consider; ++i)
            if (isVideo(results.at(i))) return results.at(i).id;
        return results.first().id;
    }
    if (want > 0) {
        // Gleiche Länge = gleiche Fassung (keine Intros, Remixe oder Stundenmixe)
        for (const int tolerance : {3, 8, 20}) {
            for (int i = 0; i < consider; ++i) {
                const YtResult &r = results.at(i);
                if (r.seconds > 0 && std::abs(r.seconds - want) <= tolerance) return r.id;
            }
        }
    }
    for (int i = 0; i < consider; ++i)
        if (results.at(i).seconds > 0 && results.at(i).seconds < 1200) return results.at(i).id;
    return results.first().id;
}

QString directStream(const QStringList &lines)
{
    QStringList urls;
    for (const QString &l : lines) {
        const QString t = l.trimmed();
        if (t.startsWith(QLatin1String("http"))) urls << t;
    }
    if (urls.size() == 1) return urls.first();
    if (urls.size() < 2) return {};
    // Getrennte Spuren: als eine Quelle (mpv-EDL), Längenangabe in Bytes, weil Adressen ";" und "," enthalten
    const auto entry = [](const QString &u) { return QStringLiteral("!no_clip;%%1%%2").arg(u.toUtf8().size()).arg(u); };
    return QStringLiteral("edl://") + entry(urls.at(0)) + QStringLiteral(";!new_stream;") + entry(urls.at(1));
}

qint64 streamExpiry(const QString &url)
{
    static const QRegularExpression re(QStringLiteral("[?&/]expire[=/](\\d{9,11})"));
    return re.match(url).captured(1).toLongLong();
}

QString pkceChallenge(const QString &verifier)
{
    return QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                                   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString randomToken(int length)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i) out += QLatin1Char(chars[QRandomGenerator::system()->bounded(int(sizeof(chars) - 1))]);
    return out;
}

} // namespace uve::spotify
