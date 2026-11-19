#pragma once
// UVESuno, Teile ohne Netzwerk: Link-Erkennung und Auswertung der öffentlichen Suno-API (studio-api.prod.suno.com).
// Gespielt wird nur, was Suno offen ausliefert: die MP3 (wenn der Ersteller Downloads erlaubt) oder das MP4 zum Teilen.
// Die verschlüsselte m4a-Fassung wird nicht angefasst.
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace uve::suno {

enum class Kind { Unknown, Song, Playlist, Profile, Short };

struct Link
{
    Kind kind = Kind::Unknown;
    QString id;   // Song-/Playlist-UUID, Profil-Handle (ohne "@") oder Kurzcode
};

struct Clip
{
    QString id;
    QString title;
    QString artist;      // display_name
    double duration = 0; // Sekunden
    QString url;         // abspielbare Adresse, leer wenn nichts offen ausgeliefert wird
    QString cover;       // Coverbild (1024×1024, sonst kleiner)
    bool isVideo = false;   // url ist Sunos MP4 (Ton + Video mit eingebranntem Titel/Text)

    QString display() const;   // "Interpret – Titel"
};

Link parseLink(const QString &url);
QString apiBase();
QString clipApi(const QString &id);
QString playlistApi(const QString &id, int page);
QString profileApi(const QString &handle, int page);

Clip clipFromJson(const QJsonObject &clip);
QList<Clip> playlistClips(const QJsonObject &playlist);   // playlist_clips[].clip
// Abspieladresse mit dem Cover als Bild und nur dem Ton der Suno-Datei (mpv-EDL). Sunos eigenes Video zeigt
// nicht-lateinische Titel und Texte (Griechisch, Koreanisch, Kyrillisch …) als Kästchen. Ohne Cover: die Datei selbst.
QString playbackUrl(const Clip &clip);
QList<Clip> profileClips(const QJsonObject &profile);     // clips[]

// Link für die Wiedergabeliste: Name als letzter Pfadteil, damit ältere Player "Interpret – Titel" statt der ID zeigen
QString songUrl(const Clip &clip);

} // namespace uve::suno
