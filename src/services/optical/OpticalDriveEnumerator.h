#pragma once
#include "OpticalMediaTypes.h"

#include <QList>
#include <QVariantMap>

namespace uve::optical {

// Optisches Laufwerk mit dem erkannten Medium. Ein Laufwerksbuchstabe/Gerät bedeutet noch kein Medium.
struct DriveInfo
{
    QString device;      // Betriebssystempfad für libmpv: "D:", "/dev/sr0", Mount-Punkt
    QString name;        // Anzeige: "D:", "/dev/sr0"
    QString label;       // Datenträgerbezeichnung (soweit verfügbar)
    DiscKind kind = DiscKind::Unknown;
    DiscError error = DiscError::None;   // NoMedium, NoAccess, Incompatible …
    bool encrypted = false;
    int audioTracks = 0;
    QVariantMap toMap() const;
};

// Laufwerke abfragen. Blockiert (Laufwerk läuft an) – nur aus einem Hintergrund-Thread aufrufen.
// Windows: GetLogicalDrives/GetDriveType/GetVolumeInformation (ohne „Datenträger einlegen“-Meldungen),
// Linux: /dev/sr* mit CDROM_DRIVE_STATUS/CDROM_DISC_STATUS, macOS: eingebundene Volumes (cd9660/udf/cddafs).
QList<DriveInfo> enumerateOpticalDrives();

} // namespace uve::optical
