#pragma once
#include <QString>

namespace uve {

// Einfaches Datei-Protokoll (logs/uve-player.log im Datenordner), Rotation bei 2 MB.
// Telemetrie gibt es nicht; das Protokoll bleibt lokal.
class Logger
{
public:
    static void init(const QString &dataDir);
    static void write(const QString &category, const QString &message);
    static QString filePath();
    static void installQtMessageHandler();
};

} // namespace uve
