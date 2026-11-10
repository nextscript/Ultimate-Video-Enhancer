#pragma once
#include "DiscCapabilities.h"
#include "OpticalMediaTypes.h"

#include <QStringList>

namespace uve::optical {

// Erkennung von Disc-Ordnern und -Images, Playlist-Einträge und libmpv-Ladeplan.
// Betriebssystempfade bleiben strikt getrennt von libmpv-Adressen; es wird nie eine Kommandozeile gebaut.
namespace resolver {

// Ergebnis der Erkennung eines Pfads (Ordner oder Image)
struct Detection
{
    DiscKind kind = DiscKind::Unknown;
    QString device;            // kanonischer Pfad, den libmpv als dvd-device/bluray-device bekommt
    DiscError error = DiscError::None;
    bool encrypted = false;    // AACS-Ordner gefunden
    bool bdj = false;          // Blu-ray mit BD-J-Anteil (BDMV/JAR)
    bool image = false;
};

// Ordner: DVD (VIDEO_TS/VIDEO_TS.IFO, auch der VIDEO_TS-Ordner selbst), Blu-ray (BDMV/index.bdmv, auch BDMV selbst)
Detection detectFolder(const QString &path);
// Image (.iso/.img/.udf): ISO 9660-Verzeichnis lesen, bei reinem UDF die Kennungen im Kopf suchen.
// Ein unbekanntes Image wird abgelehnt (InvalidImage) und nicht als Videodatei geraten.
Detection detectImage(const QString &file);
// Ordner oder Image
Detection detect(const QString &path);
// Daten eines Images im Speicher (für Tests und detectImage)
Detection detectImageData(const QByteArray &head, qint64 fileSize);

// Pfad bereinigen: lokale Datei, kein Schema, kanonisch; leer bei ungültigem Pfad
QString canonicalPath(const QString &path);

// Playlist-Eintrag "uve-disc://<typ>?dev=…&t=…&menu=1&name=…" (Prozentkodierung, keine Rohpfade im Schema)
QString toEntry(const DiscSource &source);
bool fromEntry(const QString &entry, DiscSource *out);
bool isEntry(const QString &entry);

// libmpv-Ladeplan: Adresse (dvd://, bd://, cdda://, uvecdda://) und Datei-Optionen für genau diesen Ladevorgang
// (dvd-device usw. als file-local Option, nicht dauerhaft). error ≠ None: nicht ladbar.
struct LoadPlan
{
    QString url;
    QStringList fileOptions;
    DiscError error = DiscError::None;
    bool menu = false;         // mit disc-menu=yes geladen
};
LoadPlan loadPlan(const DiscSource &source, const DiscCapabilities &caps);

// Wert für eine mpv-Datei-Option sicher kodieren (%Länge%Wert, Länge in UTF-8-Bytes): Kommas und
// Gleichheitszeichen im Pfad können so keine weiteren Optionen einschleusen
QString quoteOptionValue(const QString &value);

// Adresse des eigenen CD-Lesers: uvecdda://<gerät>/<spur>
QString cdReaderUrl(const QString &device, int track);
bool parseCdReaderUrl(const QString &url, QString *device, int *track);

} // namespace resolver
} // namespace uve::optical
