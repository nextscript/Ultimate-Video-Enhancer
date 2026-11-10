#pragma once
#include "OpticalMediaTypes.h"

#include <QStringList>
#include <QVariantMap>

namespace uve { class MpvController; }

namespace uve::optical {

// Was die tatsächlich geladene libmpv kann – ermittelt zur Laufzeit (protocol-list, option-info, command-list,
// property-list), nicht aus der mpv-Dokumentation. Fehlende Fähigkeiten schalten die passenden Funktionen ab.
struct DiscCapabilities
{
    bool dvd = false;            // dvd:// (libdvdnav/libdvdread)
    bool bluray = false;         // bd:// (libbluray)
    bool cddaNative = false;     // cdda:// (libcdio im libmpv-Build)
    bool cddaReader = false;     // eigener CD-Leser über die libmpv-Stream-Schnittstelle (uvecdda://)
    bool dvdDevice = false;      // Option dvd-device
    bool blurayDevice = false;   // Option bluray-device
    bool cddaDevice = false;     // Option cdda-device
    bool discMenuOption = false; // Option disc-menu
    bool discMenuActive = false; // Eigenschaft disc-menu-active
    bool discnav = false;        // Befehl discnav
    bool bdj = false;            // BD-J: Java-Laufzeit und libbluray-j2se-*.jar gefunden (nur mit UVE_WITH_BDJ)
    QString bdjReason;           // warum BD-J fehlt
    QString mpvVersion;
    QStringList buildFlags;      // aus mpv-configuration: "dvdnav=enabled", "libbluray=enabled", "cdda=enabled" …

    bool audioCd() const { return cddaNative || cddaReader; }
    bool supports(DiscKind kind) const;
    // DVD- bzw. Blu-ray-Menüs (HDMV) – nur wenn Menüstart und Navigation beide vorhanden sind
    bool menus(DiscKind kind) const;

    // probe: Schlüssel "protocols" (QStringList), "options" (QStringList), "commands" (QStringList),
    // "properties" (QStringList), "configuration" (QString), "mpvVersion" (QString), "cddaReader" (bool),
    // "bdj" (bool), "bdjReason" (QString) – reine Funktion, damit sie ohne libmpv testbar ist
    static DiscCapabilities fromProbe(const QVariantMap &probe);
    // Fragt die laufende libmpv ab
    static QVariantMap probe(const MpvController *player);
    // BD-J-Voraussetzungen im Programmordner suchen (keine Downloads)
    static bool findBdjRuntime(const QString &appDir, QString *reason);

    QVariantMap toMap() const;
};

} // namespace uve::optical
