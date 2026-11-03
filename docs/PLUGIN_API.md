# Plugin-API (Version 1)

Header: `src/plugins/PluginApi.h`. Ziel seit **1.0.6**: Neue Plugins erweitern den Player, **ohne dass der Player
aktualisiert werden muss**. Dafür gibt es drei Bausteine, die über Namen erweitert werden statt über neue Funktionen:

| Baustein | Richtung | Wie |
|---|---|---|
| **Ereignisse** | Player → Plugin | `IPluginEvents::onEvent(name, data)` (nativ) bzw. `plugin.onEvent(name, data)` (QML) |
| **Befehle** | Plugin → Player | `host->call(pluginId, name, args, &error)` (nativ) bzw. `plugin.call(name, args)` (QML) |
| **Beiträge** | Manifest → Player | `contributes`: Seiten, Menüeinträge, Tastenkürzel, Sprachdateien, Adressmuster |

Neue Ereignisse und Befehle kommen in späteren Versionen einfach hinzu. Die Schnittstelle (und damit `apiVersion 1`)
ändert sich dadurch nicht, alte Plugins laufen weiter. Ob der laufende Player einen Befehl kennt: `hasCommand(name)`.

## Plugin-Formen

### 1. Plugin-Ordner (empfohlen)

```
plugins/<name>/
    manifest.json        Pflicht
    <bibliothek>.dll     native Logik ("library")      – oder –   Main.qml  reine QML-Logik ("main")
    lang/<name>_<code>.qm  eigene Übersetzungen (z. B. beispiel_en.qm)
    *.qml, Bilder …       Seiten und Ressourcen
```

Gesucht wird im Benutzerordner (`%APPDATA%\nextscript\UVE Player\plugins`, hat Vorrang) und im Programmordner
(`<Programm>\plugins`). Das Manifest im Ordner gilt, auch wenn die Bibliothek ein eingebettetes Manifest hat –
Seiten, Menüs oder Sprachen lassen sich so ohne Neukompilieren ändern.

**Installieren:** Plugin-Verwaltung → *Plugin installieren …* → `.zip` (mit `manifest.json` im Wurzelverzeichnis
oder in genau einem Unterordner) oder `manifest.json` eines Ordners wählen. Das Paket wird geprüft
(Manifest, Pfade) und beim **nächsten Start** in `plugins/<id>/` übernommen (geladene DLLs sind unter Windows gesperrt).

### 2. Reines QML-Plugin (ohne Kompilieren)

`"main": "Main.qml"` – ein nicht sichtbares QML-Objekt. Optional: `property var plugin` (Brücke, siehe unten),
Funktionen `activate()`, `suspend()`, `deactivate()`.

```qml
import QtQml
QtObject {
    id: root
    property var plugin
    property Connections events: Connections {
        target: root.plugin
        function onEvent(name, data) {
            if (name === "media.loaded") root.plugin.call("osd.show", { text: "Läuft: " + data.title })
        }
    }
}
```

Netzwerkzugriffe in QML: `XMLHttpRequest`.

### 3. Einzelne Bibliothek (bisheriges Format)

Eine DLL direkt in `plugins/` mit eingebettetem Manifest (`Q_PLUGIN_METADATA(... FILE "manifest.json")`).
Funktioniert weiter; Beiträge (`contributes`) können auch im eingebetteten Manifest stehen, Sprachdateien und
Seiten aus Dateien aber nur im Plugin-Ordner.

## Native Plugins (C++)

```cpp
class MyPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin uve::IPluginEvents)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override; void suspend() override; void deactivate() override;
    QObject *controller() override;    // Objekt für die Seiten (Eigenschaft "controller")
    QUrl panelQml() const override;     // optional, sonst leer → Seiten über contributes.panels
    QString panelTitle() const override;
    void onEvent(const QString &name, const QVariantMap &data) override;   // IPluginEvents (optional)
};
```

Gleicher Compiler und dieselbe Qt-Version wie der Player (1.0.x: Qt 6.12, MSVC 2022, 64 bit).

## Manifest

```json
{
  "id": "org.beispiel.plugin", "name": "…", "version": "1.0.0", "apiVersion": 1, "minHostVersion": "1.0.6",
  "type": "utility", "description": "…", "author": "…", "license": "…",
  "library": "beispiel.dll",
  "main": "Main.qml",
  "supportedPlatforms": ["windows", "linux", "macos"], "requiredBackends": [], "permissions": [],
  "contributes": {
    "panels":   [ { "id": "main", "title": { "de": "Beispiel", "en": "Example" }, "qml": "Panel.qml", "location": "sidebar" },
                  { "id": "settings", "title": "Einstellungen", "qml": "Settings.qml", "location": "settings" } ],
    "menu":     [ { "id": "doit", "title": { "de": "Ausführen", "en": "Run" } } ],
    "hotkeys":  [ { "id": "doit", "title": "Ausführen", "default": "Ctrl+Alt+B" } ],
    "translations": "lang",
    "urlHandlers": [ "^https?://(www\\.)?example\\.com/" ]
  }
}
```

`library` **oder** `main` (nur Plugin-Ordner). Texte im Manifest: Zeichenkette oder `{ "<sprache>": "…" }`
(Rückfall Englisch, dann Deutsch). Verfügbare Backends: `mpv-user-shaders`, `mpv-render-api-opengl`,
`plugin-events`, `plugin-commands`, `qml-plugins`.

| Beitrag | Wirkung |
|---|---|
| `panels` | `location: "sidebar"` → Reiter in der Seitenleiste; `"settings"` → Knopf in der Plugin-Verwaltung. `qml` relativ zum Ordner oder `qrc:/…` |
| `menu` | Eintrag im Menü *Plugins* → Ereignis `action.triggered {id}` |
| `hotkeys` | Eintrag in *Einstellungen → Tastenkürzel* (änderbar) → Ereignis `action.triggered {id}`. Ein bereits belegtes Standardkürzel wird nicht übernommen |
| `translations` | Ordner mit `*_<code>.qm` (Standard `lang`), wird beim Sprachwechsel mit umgeschaltet; Quellsprache Deutsch, Rückfall Englisch |
| `urlHandlers` | Reguläre Ausdrücke; passende Adressen gehen vor dem Laden an das Plugin (Ereignis `media.resolve`) |

Seiten bekommen die Eigenschaften `theme`, `host` (Player-Objekt), `controller` (Plugin-Objekt bzw. QML-Logik) und
`plugin` (Brücke) – jeweils nur, wenn die Seite sie deklariert.

## Brücke (`plugin` in QML)

`plugin.call(name, args)`, `plugin.hasCommand(name)`, `plugin.commands()`, `plugin.settings()`,
`plugin.saveSettings(obj)`, `plugin.log(text, level)`, `plugin.osd(text, ms)`, `plugin.lastError()`,
`plugin.resolve("bild.png")` (Datei im Plugin-Ordner als URL), Eigenschaften `id`, `folder`, `dataDir`, `language`,
Signal `event(name, data)`.

## Ereignisse

| Name | Daten |
|---|---|
| `media.loading` | `url` – der Nutzer hat ein Medium gewählt (vor einer Auflösung) |
| `media.resolve` | `requestId`, `url` – nur an das Plugin, dessen `urlHandlers` passt; Antwort mit `media.resolved` |
| `media.resolveCancelled` | `requestId` – Ergebnis wird nicht mehr gebraucht |
| `media.loaded` | `path` (geladen), `source` (gewählt), `title`, `duration` |
| `media.ended` | `reason` (`eof`, `stop`, `error`, …), `path`, `source`, `error` |
| `player.stopped` | `path` |
| `playback.paused` | `paused` |
| `language.changed` | `code` |
| `action.triggered` | `id` – Menüeintrag oder Tastenkürzel dieses Plugins |
| `app.quit` | – Programmende |

Unbekannte Ereignisse ignorieren – spätere Versionen können weitere senden.

## Befehle

| Name | Argumente → Ergebnis |
|---|---|
| `commands` | → Liste aller Befehle |
| `app.info` | → `version`, `apiVersion`, `language`, `platform`, `backends` |
| `osd.show` | `text`, `ms` |
| `log` | `message`, `level` |
| `settings.get` / `settings.set` | → Plugin-Einstellungen / `settings` (Objekt) |
| `plugin.dataDir` | → Datenordner des Plugins |
| `panel.open` | `panel` (id aus `contributes.panels`) – Seite zeigen |
| `media.resolved` | `requestId`, `url` (abzuspielen) oder `error` |
| `player.state` | → `path`, `source`, `title`, `position`, `duration`, `paused`, `state`, `playlistIndex` |
| `player.load` | `url` oder `urls`, `next` – zur Wiedergabeliste hinzufügen und abspielen |
| `player.pause` | `paused` (ohne: umschalten) |
| `player.stop`, `player.next`, `player.previous`, `player.reload` | – |
| `player.seek` | `seconds`, `relative` |
| `playlist.add` | `urls`, `play`, `next` |
| `playlist.items` | → `[{path, title, current}]` |
| `playlist.play` | `index` |
| `mpv.get` / `mpv.set` / `mpv.command` | `name` / `name`, `value` / `args` – direkter mpv-Zugriff für alles ohne eigenen Befehl |

## Prüfung vor dem Laden (`validateManifest`)

id-Format, Name, SemVer-Version, `apiVersion == 1`, `minHostVersion ≤ Host`, Plattform, Backends; im Plugin-Ordner
zusätzlich: `library` vorhanden und ein UVE-Player-Plugin bzw. `main` vorhanden. Abgelehnte Plugins werden nicht geladen
und im Plugin-Dialog mit Grund angezeigt (Fehlerprotokoll `logs\plugin-errors.log`).

## Lebenszyklus

`discover → verify → load → initialize → activate ⇄ suspend → deactivate → unload`
Aktivieren/Deaktivieren zur Laufzeit ohne Neustart; abgeschaltete Plugins verlieren Seiten, Menüs, Tastenkürzel und
Adressmuster sofort. Ausnahmen in `initialize`, `onMediaChanged`, `onEvent` und Befehlen werden abgefangen.

## Host-Dienste (`IPlayerHost`)

Einstellungen pro Plugin (JSON), Datenordner, GPU-Shader-Stufe setzen/entfernen, OSD, Protokoll,
aktueller Medienpfad/Position/Pause lesen, Pause setzen, Standbild ohne Effekte (`grabVideoFrame`, threadsicher),
ab 1.0.2 `setVideoEqualizer` und `sampleDisplayFrame`, ab 1.0.6 `call`, `hasCommand`, `commands`.

## Sicherheit

Plugins (nativ und QML) laufen im Hauptprozess und sind **Vertrauenscode**; `mpv.*` erlaubt vollen Zugriff auf mpv.
Es gibt keine Signaturprüfung und keine Prozessisolation; fremde Plugins nur aus vertrauenswürdiger Quelle installieren.

Beispiele: `plugins/examples/sleeptimer/` (nativ, bisheriges Format), `tests/unit/tst_plugins.cpp` (QML-Plugin-Ordner).
