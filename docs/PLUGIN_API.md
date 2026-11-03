# Plugin-API (Version 1)

Header: `src/plugins/PluginApi.h`. Plugins sind Qt-Plugins (DLL) mit eingebettetem Manifest:

```cpp
class MyPlugin : public QObject, public uve::IPlayerPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override; void suspend() override; void deactivate() override;
    QObject *controller() override;   // optional: Objekt für das QML-Panel
    QUrl panelQml() const override;    // optional: qrc:/…/Panel.qml (Eigenschaften controller, theme, host)
    QString panelTitle() const override;
};
```

## Manifest
```json
{
  "id": "org.beispiel.plugin", "name": "…", "version": "1.0.0", "apiVersion": 1, "minHostVersion": "1.0.0",
  "type": "gpu-effect | utility", "entrypoint": "Klassenname", "description": "…", "author": "…", "license": "…",
  "capabilities": [], "supportedPlatforms": ["windows", "linux", "macos"],
  "requiredBackends": ["mpv-user-shaders"], "permissions": [], "configurationSchema": {}
}
```
`entrypoint` und `requiredBackends` sind **eigene Konventionen** dieses Players (keine mpv-Felder).
Verfügbare Backends des Hosts: `mpv-user-shaders`, `mpv-render-api-opengl`.

## Prüfung vor dem Laden (`validateManifest`)
id-Format, Name, SemVer-Version, `apiVersion == 1`, `minHostVersion ≤ Host`, Plattform, Backends.
Abgelehnte Plugins werden nicht geladen und im Plugin-Dialog mit Grund angezeigt (Fehlerprotokoll `logs\plugin-errors.log`).

## Lebenszyklus
`discover → verify → load → initialize → activate ⇄ suspend → deactivate → unload`
Aktivieren/Deaktivieren zur Laufzeit ohne Neustart. Ausnahmen in `initialize`/`onMediaChanged` werden abgefangen.

## Host-Dienste (`IPlayerHost`)
Einstellungen pro Plugin (JSON), Datenordner, GPU-Shader-Stufe setzen/entfernen, OSD, Protokoll,
aktueller Medienpfad/Position/Pause lesen, Pause setzen, Standbild ohne Effekte (`grabVideoFrame`, threadsicher).
Ab 1.0.2: `setVideoEqualizer(pluginId, brightness, contrast, saturation, hue)` – mpv-Video-Equalizer (je −100 … 100),
wird beim Deaktivieren/Entladen des Plugins zurückgesetzt (an das Ende der Schnittstelle angehängt, API-Version bleibt 1).
Kein direkter Zugriff auf mpv- oder GUI-Objekte.

## Sicherheit
Native Plugins laufen im Hauptprozess und sind **Vertrauenscode** (gleicher Compiler/Qt-Build nötig).
Es gibt keine Signaturprüfung und keine Prozessisolation in 1.0.0; fremde Plugins nur aus vertrauenswürdiger Quelle installieren.
Für Drittentwickler wäre eine C-ABI oder IPC stabiler – nicht Teil von 1.0.0.

Beispiel: `plugins/examples/sleeptimer/`.
