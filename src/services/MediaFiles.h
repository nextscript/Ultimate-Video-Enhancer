#pragma once
#include <QStringList>

namespace uve::media {

QStringList videoExtensions();
QStringList audioExtensions();
QStringList subtitleExtensions();
QStringList playlistExtensions();
QString fileDialogFilter();

bool isUrl(const QString &s);              // http(s), tcp, udp, rtmp, rtsp, srt, …
bool isAllowedUrlScheme(const QString &s); // Validierung für Drag & Drop / URL-Dialog
bool isMediaFile(const QString &path);
bool isSubtitleFile(const QString &path);
bool isPlaylistFile(const QString &path);

// Dateien, Ordner (rekursiv, natürlich sortiert), Wiedergabelisten und URLs zu Einträgen auflösen
QStringList expand(const QStringList &inputs);
QStringList parsePlaylistFile(const QString &file);
bool writeM3U(const QString &file, const QStringList &entries, const QStringList &titles);

} // namespace uve::media
