#pragma once
#include <QJsonObject>
#include <QMap>
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
    QJsonValue description;     // Text oder { "<Sprache>": "…" } (seit 1.2.0, siehe localizedText)
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
    // Seit 1.2.9 – ein Paket für mehrere Systeme: Zielschlüssel (z. B. "windows-x64") → Bibliothek relativ zum Ordner.
    // Vorhanden: gilt nur der Eintrag für currentTargetKey(), "library" wird nicht verwendet.
    QMap<QString, QString> libraries;
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

// Zielschlüssel "<Plattform>-<Architektur>" der laufenden Player-Binärdatei (nicht des Rechners: ein emulierter
// x64-Player auf ARM wählt das x64-Modul): windows-x64, windows-arm64, windows-x86, linux-x86_64, linux-aarch64,
// macos-arm64, macos-x86_64
QString currentTargetKey();
QString targetKey(const QString &platform, const QString &qtCpuArchitecture);

// Relativer Pfad, der im Plugin-Ordner bleibt (kein absoluter Pfad, kein "..", kein Laufwerk); Backslash → "/"
bool isSafeRelativePath(const QString &path, QString *cleaned = nullptr);

// Welche Datei der Player für ein Plugin-Ordner-Manifest lädt. Dieselbe Auswahl gilt beim Installieren und beim Laden.
struct ModuleSelection
{
    enum Kind { None, Library, Qml };
    Kind kind = None;
    QString path;       // relativ zum Plugin-Ordner, bereinigt
    QString error;      // leer = ok
    bool fromLibraries = false;
    bool ok() const { return error.isEmpty() && kind != None; }
};
ModuleSelection selectModule(const PluginManifest &m, const QString &targetKey);

} // namespace uve
