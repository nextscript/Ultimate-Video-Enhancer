#pragma once
// UVESpotify, Teile ohne Netzwerk: Link-Erkennung, Spotify-Metadaten (Embed-Seite und Web-API), YouTube-Trefferwahl.
// Spotify-Ton selbst ist DRM-geschützt und wird nicht angefasst: gespielt wird der passende Titel von YouTube.
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace uve::spotify {

enum class Kind { Unknown, Track, Album, Playlist, Artist, Liked };

struct Link
{
    Kind kind = Kind::Unknown;
    QString id;
};

struct Track
{
    QString id;
    QString title;
    QStringList artists;
    qint64 durationMs = 0;

    QString display() const;   // "Interpret – Titel"
    QString query() const;     // Suchbegriff für YouTube
};

struct YtResult
{
    QString id;
    QString title;
    int seconds = 0;           // 0 = unbekannt (z. B. LIVE)
};

Link parseLink(const QString &url);
QString trackUrl(const QString &id);
// Mit Anzeigename als letztem Pfadteil: Die Wiedergabeliste des Players zeigt so "Interpret – Titel" statt der ID
QString trackUrl(const Track &track);
QString nextDataJson(const QString &html);
// Embed-Seite → entity (data.entity aus __NEXT_DATA__)
QJsonObject embedEntity(const QString &html);
Track embedTrack(const QJsonObject &entity);
QList<Track> embedTrackList(const QJsonObject &entity);
// Web-API: Track-Objekt bzw. Liste von Einträgen ({track|item: {...}})
Track apiTrack(const QJsonObject &track);
QList<Track> apiItems(const QJsonArray &items);

int parseClock(const QString &text);  // "1:02:03" → 3723
QList<YtResult> youtubeResults(const QString &html);
// preferVideo: Musikvideo statt reiner Audiofassung bevorzugen
QString pickYoutube(const QList<YtResult> &results, qint64 durationMs, bool preferVideo);

// yt-dlp -g gibt eine Zeile (Bild+Ton) oder zwei (Bild, Ton) aus: daraus eine abspielbare Adresse für mpv
QString directStream(const QStringList &lines);
// Gültigkeit einer googlevideo-Adresse ("expire=<Unixzeit>"), 0 = unbekannt
qint64 streamExpiry(const QString &url);

QString pkceChallenge(const QString &verifier);
QString randomToken(int length);

} // namespace uve::spotify
