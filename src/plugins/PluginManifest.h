#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace uve {

// Manifest eines Plugins (eingebettet über Q_PLUGIN_METADATA, Feld "MetaData")
struct PluginManifest
{
    QString id;                 // z. B. "org.nextscript.ultimate-video-enhancer"
    QString name;
    QString version;            // Plugin-Version (SemVer)
    int apiVersion = 0;         // muss == kPluginApiVersion sein
    QString minHostVersion;     // SemVer, Host muss >= sein
    QString type;               // "gpu-effect", "utility", …
    QString entrypoint;         // eigene Konvention: Name der Plugin-Klasse
    QString description;
    QString author;
    QString license;
    QStringList capabilities;
    QStringList supportedPlatforms; // "windows", "linux", "macos"; leer = alle
    QStringList requiredBackends;   // eigene Konvention, z. B. "mpv-user-shaders"
    QStringList permissions;
    QJsonObject configurationSchema;
    // Seit 1.0.6 – Plugin-Ordner (plugins/<name>/manifest.json):
    QString library;            // Bibliothek im Ordner (z. B. "beispiel.dll"); leer bei reinen QML-Plugins
    QString main;               // QML-Datei der Plugin-Logik (reine QML-Plugins), relativ zum Ordner
    QJsonObject contributes;    // panels, menu, hotkeys, translations, urlHandlers (siehe docs/PLUGIN_API.md)

    static PluginManifest fromJson(const QJsonObject &o);
    QJsonObject toJson() const;
};

struct ManifestCheck
{
    bool ok = false;
    QStringList errors;
};

// Prüft Pflichtfelder und Kompatibilität mit diesem Host (ohne die Bibliothek zu laden)
// Text aus dem Manifest: Zeichenkette oder {"de": "…", "en": "…"} → passende Sprache (Rückfall en, de, erste)
QString localizedText(const QJsonValue &v, const QString &language);

ManifestCheck validateManifest(const PluginManifest &m, const QString &hostVersion, int hostApiVersion,
                               const QString &platform, const QStringList &availableBackends);

// SemVer-Vergleich (major.minor.patch, fehlende Teile = 0): <0, 0, >0
int compareVersions(const QString &a, const QString &b);

QString currentPlatformId();

} // namespace uve
