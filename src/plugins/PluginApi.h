#pragma once
// UVE Player – Plugin-Host-Schnittstelle, API-Version 1.
//
// Plugins sind Qt-Plugins (DLL/SO/DYLIB) mit eingebettetem Manifest (Q_PLUGIN_METADATA FILE "manifest.json").
// Der Host liest das Manifest, BEVOR die Bibliothek geladen wird, und prüft apiVersion, minHostVersion,
// supportedPlatforms und Pflichtfelder (siehe PluginManifest). Erst danach wird geladen.
//
// Lebenszyklus: discover → verify → load → initialize → activate ⇄ suspend → deactivate → unload
//
// Plugins erhalten ausschließlich die Host-Dienste aus IPlayerHost – keinen direkten Zugriff auf
// mpv- oder GUI-Objekte. Native Plugins laufen im Hauptprozess und sind daher Vertrauenscode.

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtPlugin>

namespace uve {

inline constexpr int kPluginApiVersion = 1;

// Dienste des Hosts für Plugins (vom Host implementiert, Lebensdauer ≥ Plugin)
class IPlayerHost
{
public:
    virtual ~IPlayerHost() = default;

    virtual QString hostVersion() const = 0;
    virtual int apiVersion() const = 0;

    // Persistente, plugin-eigene Einstellungen (JSON) und Datenordner
    virtual QJsonObject loadPluginSettings(const QString &pluginId) const = 0;
    virtual void savePluginSettings(const QString &pluginId, const QJsonObject &settings) = 0;
    virtual QString pluginDataDir(const QString &pluginId) const = 0;

    // GPU-Shader-Stufe (mpv user shader, //!HOOK-Syntax). Leerer Pfad = Stufe entfernen.
    // Mehrere Dateien einer Stufe durch '\n' trennen (Reihenfolge = Ausführungsreihenfolge).
    // WICHTIG: mpv speichert Shader nach Pfad zwischen – geänderte Inhalte immer unter neuem Dateinamen ablegen.
    // Fehler beim Kompilieren meldet der Host über onShaderError() an das Plugin zurück.
    virtual void setShaderStage(const QString &pluginId, const QString &shaderFile) = 0;

    // Benutzerhinweise und Protokoll
    virtual void showOsd(const QString &text, int durationMs = 1500) = 0;
    virtual void log(const QString &pluginId, const QString &level, const QString &message) = 0;

    // Lesender Zugriff auf den Wiedergabezustand und wenige erlaubte Aktionen
    virtual QString currentMediaPath() const = 0;
    virtual double currentPosition() const = 0;
    virtual bool isPaused() const = 0;
    virtual void setPaused(bool paused) = 0;
    // Standbild des aktuellen Videos (dekodiert, ohne Effekte), max. maxWidth Pixel breit; leer ohne Video
    virtual QImage grabVideoFrame(int maxWidth) = 0;
};

// Vom Plugin implementiert
class IPlayerPlugin
{
public:
    virtual ~IPlayerPlugin() = default;

    // Einmalig nach dem Laden. Rückgabe false (+ error) → Plugin wird als fehlerhaft markiert und entladen.
    virtual bool initialize(IPlayerHost *host, QString *error) = 0;
    // Wirkung einschalten / vorübergehend aussetzen / vollständig ausschalten (vor dem Entladen)
    virtual void activate() = 0;
    virtual void suspend() = 0;
    virtual void deactivate() = 0;

    // Optionale Oberfläche: QObject (Q_PROPERTY/Q_INVOKABLE) + QML-Datei aus den Plugin-Ressourcen
    virtual QObject *controller() = 0;
    virtual QUrl panelQml() const = 0;
    virtual QString panelTitle() const = 0;

    // Ereignisse des Hosts
    virtual void onMediaChanged(const QString &path) { Q_UNUSED(path); }
    virtual void onShaderError(const QString &message) { Q_UNUSED(message); }
};

} // namespace uve

#define UVE_PLAYER_PLUGIN_IID "org.nextscript.uveplayer.IPlayerPlugin/1"
Q_DECLARE_INTERFACE(uve::IPlayerPlugin, UVE_PLAYER_PLUGIN_IID)
