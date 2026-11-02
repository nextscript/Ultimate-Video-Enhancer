#pragma once
#include <QString>

namespace uve {

// Absturzerkennung für Plugins:
//  - Beim Start wird "running" + Phase gesetzt, bei sauberem Ende gelöscht.
//  - Endete der letzte Lauf unsauber während/kurz nach dem Laden der Plugins, zählt das als Plugin-Absturz.
//  - Nach 2 aufeinanderfolgenden Plugin-Abstürzen startet der Player im Safe Mode (ohne Plugins).
class SafeMode
{
public:
    explicit SafeMode(const QString &dataDir);

    // Am Programmstart aufrufen; liefert true, wenn ohne Plugins gestartet werden soll
    bool evaluateOnStartup(bool forcedByUser);
    void markPluginsLoading();
    void markPluginsStable();      // nach erfolgreichem Laden und kurzer Laufzeit
    void markCleanExit();
    void reset();                  // Benutzer reaktiviert Plugins

    bool active() const { return m_active; }
    int crashCount() const { return m_crashes; }
    QString reason() const { return m_reason; }

private:
    void write(bool running, const QString &phase);
    QString m_file;
    bool m_active = false;
    int m_crashes = 0;
    QString m_reason;
};

} // namespace uve
