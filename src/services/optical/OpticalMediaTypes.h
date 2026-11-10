#pragma once
#include <QString>

// Gemeinsame Typen für optische Medien (DVD-Video, Blu-ray, Audio-CD) und Disc-Images.
// Das Modul spielt nichts selbst ab: Es erkennt Quellen, baut libmpv-Adressen und leitet Navigation weiter.
namespace uve::optical {

enum class DiscKind { Unknown, Dvd, Bluray, AudioCd };

// Kurzname für Playlist-Einträge, QML und Protokoll: "dvd", "bluray", "cdda" (unbekannt: "")
QString kindId(DiscKind kind);
DiscKind kindFromId(const QString &id);

// Fehlerbilder (UI-Texte siehe OpticalMediaService::errorText)
enum class DiscError {
    None,
    NoDrive,            // kein optisches Laufwerk
    NoMedium,           // Laufwerk ohne Medium
    Incompatible,       // Medium/Ordner/Image ist keine DVD-Video, Blu-ray oder Audio-CD
    NoAccess,           // kein Lesezugriff
    MissingFeature,     // libmpv-Build kann diesen Disc-Typ nicht öffnen
    MenuUnsupported,    // Disc-Menüs vom Build nicht unterstützt
    BdjNeedsRuntime,    // BD-J-Menü braucht Java + libbluray-j2se
    Encrypted,          // verschlüsselte Blu-ray (AACS/BD+)
    Removed,            // Disc während der Wiedergabe entfernt
    InvalidImage,       // Image beschädigt oder unbekannt
};

// Eine Quelle: Laufwerk, Image oder Ordner. device ist immer ein Betriebssystempfad
// (Laufwerkswurzel "D:\", "/dev/sr0", Mount-Punkt, ISO-Datei oder Disc-Ordner) – nie eine libmpv-Adresse.
struct DiscSource
{
    DiscKind kind = DiscKind::Unknown;
    QString device;
    QString name;          // Anzeige (z. B. "DVD – FILM_2009")
    int title = -1;        // DVD/BD: Titel (-1 = Standard/Haupttitel); Audio-CD: Spur 1 … N (-1 = ganze Disc)
    bool menu = false;     // im Disc-Menü starten
    bool encrypted = false;
};

} // namespace uve::optical
