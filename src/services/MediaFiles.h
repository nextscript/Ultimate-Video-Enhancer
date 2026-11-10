#pragma once
#include <QStringList>

namespace uve::media {

QStringList videoExtensions();
QStringList audioExtensions();
QStringList subtitleExtensions();
QStringList playlistExtensions();
QString fileDialogFilter();

bool isUrl(const QString &s);
// Playlist-Eintrag einer optischen Disc ("uve-disc://…", siehe services/optical) – kein Netzwerkstream
bool isDiscEntry(const QString &s);              // http(s), tcp, udp, rtmp, rtsp, srt, …
bool isAllowedUrlScheme(const QString &s); // Validierung für Drag & Drop / URL-Dialog
bool isMediaFile(const QString &path);
bool isSubtitleFile(const QString &path);
bool isPlaylistFile(const QString &path);
// Lokaler Pfad auf einem Netzlaufwerk (UNC, verbundenes Laufwerk, NFS/SMB/FUSE-Mount)
bool isNetworkPath(const QString &path);

// Dateien, Ordner (rekursiv, natürlich sortiert), Wiedergabelisten und URLs zu Einträgen auflösen
QStringList expand(const QStringList &inputs);
QStringList parsePlaylistFile(const QString &file);
bool writeM3U(const QString &file, const QStringList &entries, const QStringList &titles);

// YouTube-Wiedergabelisten (…watch?v=ID&list=LISTE&index=N, youtu.be/ID?list=…, …/playlist?list=…)
struct YoutubePlaylistLink { QString listId; QString videoId; int index = 0; };   // index: 1-basiert, 0 = keiner
bool parseYoutubePlaylist(const QString &url, YoutubePlaylistLink *out);
QString youtubeVideoUrl(const QString &videoId);           // https://www.youtube.com/watch?v=ID (ohne Liste)
// Startzeile in der aufgelösten Liste: index= hat Vorrang, sonst das Video aus v=, sonst die erste
int youtubePlaylistStart(const QStringList &videoIds, const YoutubePlaylistLink &link);

} // namespace uve::media
