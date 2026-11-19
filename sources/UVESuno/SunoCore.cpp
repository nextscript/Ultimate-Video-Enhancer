#include "SunoCore.h"

#include <QRegularExpression>
#include <QUrl>

namespace uve::suno {

QString Clip::display() const
{
    return artist.isEmpty() ? title : QStringLiteral("%1 – %2").arg(artist, title);
}

Link parseLink(const QString &url)
{
    const QUrl u(url);
    const QString host = u.host().toLower();
    static const QStringList hosts{"suno.com", "www.suno.com", "suno.ai", "www.suno.ai", "app.suno.ai"};
    if (!hosts.contains(host)) return {};
    const QStringList parts = u.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return {};
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    const QString first = parts.at(0);
    const QString second = parts.value(1);
    if ((first == QLatin1String("song") || first == QLatin1String("embed")) && uuid.match(second).hasMatch())
        return {Kind::Song, second.toLower()};
    if (first == QLatin1String("playlist") && uuid.match(second).hasMatch()) return {Kind::Playlist, second.toLower()};
    if (first == QLatin1String("s") && !second.isEmpty()) return {Kind::Short, second};
    if (first.startsWith(QLatin1Char('@')) && first.size() > 1) return {Kind::Profile, first.mid(1)};
    return {};
}

QString apiBase()
{
    return QStringLiteral("https://studio-api.prod.suno.com/api");
}

QString clipApi(const QString &id)
{
    return apiBase() + QStringLiteral("/clip/") + id;
}

QString playlistApi(const QString &id, int page)
{
    return apiBase() + QStringLiteral("/playlist/%1?page=%2").arg(id).arg(page);
}

QString profileApi(const QString &handle, int page)
{
    return apiBase() + QStringLiteral("/profiles/%1?page=%2&clips_sort_by=created_at&playlists_sort_by=created_at")
                           .arg(QString::fromLatin1(QUrl::toPercentEncoding(handle)))
                           .arg(page);
}

Clip clipFromJson(const QJsonObject &clip)
{
    Clip c;
    c.id = clip.value("id").toString();
    c.title = clip.value("title").toString().simplified();
    c.artist = clip.value("display_name").toString();
    if (c.artist.isEmpty()) c.artist = clip.value("handle").toString();
    c.duration = clip.value("metadata").toObject().value("duration").toVariant().toDouble();
    c.cover = clip.value("image_large_url").toString();
    if (c.cover.isEmpty()) c.cover = clip.value("image_url").toString();
    if (clip.value("status").toString() != QLatin1String("complete")) return c;   // in Arbeit oder fehlgeschlagen: nicht abspielbar
    // MP3 nur, wenn der Ersteller sie freigibt (sonst zeigt audio_url auf ".../api/forbidden")
    const QString audio = clip.value("audio_url").toString();
    if (audio.startsWith(QLatin1String("https://")) && !audio.contains(QLatin1String("/forbidden"))) c.url = audio;
    else { c.url = clip.value("video_url").toString(); c.isVideo = !c.url.isEmpty(); }   // offizielles MP4 zum Teilen: Ton + Video
    if (c.title.isEmpty()) c.title = QStringLiteral("Suno");
    return c;
}

static QList<Clip> collect(const QJsonArray &list, const char *wrapper)
{
    QList<Clip> out;
    for (const auto &v : list) {
        QJsonObject o = v.toObject();
        if (wrapper) o = o.value(QLatin1String(wrapper)).toObject();
        const Clip c = clipFromJson(o);
        if (!c.id.isEmpty() && !c.url.isEmpty()) out << c;
    }
    return out;
}

QList<Clip> playlistClips(const QJsonObject &playlist)
{
    return collect(playlist.value("playlist_clips").toArray(), "clip");
}

QList<Clip> profileClips(const QJsonObject &profile)
{
    return collect(profile.value("clips").toArray(), nullptr);
}

QString playbackUrl(const Clip &clip)
{
    if (clip.url.isEmpty() || clip.cover.isEmpty()) return clip.url;
    // Zwei Spuren als eine Quelle: Bild nur aus dem Cover, Ton nur aus der Suno-Datei (delay_open,media_type wählt
    // genau eine Spur je Teil). Längenangabe in Bytes, weil Adressen ";" und "," enthalten dürfen.
    const auto part = [](const char *type, const QString &url) {
        return QStringLiteral("!no_clip;!delay_open,media_type=%1;%%2%%3").arg(QLatin1String(type)).arg(url.toUtf8().size()).arg(url);
    };
    return QStringLiteral("edl://") + part("video", clip.cover) + QStringLiteral(";!new_stream;") + part("audio", clip.url);
}

QString songUrl(const Clip &clip)
{
    const QString base = QStringLiteral("https://suno.com/song/") + clip.id;
    // Der Player dekodiert den Pfad vor dem Aufteilen: "/" im Namen durch das gleich aussehende "∕" ersetzen
    const QString name = clip.display().simplified().replace(QLatin1Char('/'), QChar(0x2215));
    return name.isEmpty() ? base : base + QLatin1Char('/') + QString::fromLatin1(QUrl::toPercentEncoding(name));
}

} // namespace uve::suno
