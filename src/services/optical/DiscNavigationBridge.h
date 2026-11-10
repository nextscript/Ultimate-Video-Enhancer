#pragma once
#include "DiscCapabilities.h"
#include "OpticalMediaTypes.h"

#include <QStringList>

namespace uve::optical::nav {

// Navigationsaktionen, die an "discnav" weitergereicht werden (mpv: DOCS/man/input.rst, discnav)
QStringList actions(DiscKind kind);

// mpv-Befehl für eine Aktion; leer, wenn nichts gesendet werden darf (keine Disc aktiv, Build ohne discnav,
// Audio-CD, unbekannte Aktion oder Aktion für diesen Disc-Typ nicht vorgesehen)
QStringList command(const QString &action, DiscKind kind, bool discActive, const DiscCapabilities &caps);

// Tastenbelegung im Disc-Menü: Pfeiltasten und Enter steuern das Menü statt Spulen/Lautstärke.
// key im Format von QKeySequence::PortableText ("Up", "Return" …); leer = normale Tastenkürzel gelten.
QString keyAction(const QString &key, bool menuActive);
QStringList menuKeys();

} // namespace uve::optical::nav
