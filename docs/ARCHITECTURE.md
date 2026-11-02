# Architektur

```text
UVEPlayer.exe (app/)
├── main.cpp            Starter: libmpv vorladen, Einzelinstanz, Safe Mode, Datenordner, QML laden
├── PlayerApp           Anwendungskern für QML ("app"): Öffnen, Wiedergabeliste, Fortsetzen, Sitzung, Updates, Selbsttest
└── qml/                Qt-Quick-Oberfläche (Main, ControlBar, SeekBar, PlaylistPanel, Dialoge, Kontextmenüs)

uvecore (src/, statische Bibliothek)
├── core/       AppSettings (JSON, Schemaversion, atomar), Logger, SafeMode (Absturzzähler)
├── player/     MpvController (Client-API, Zustandsautomat), MpvVideoItem (Render-API → Qt-Quick-FBO)
├── plugins/    PluginApi.h (Vertrag), PluginManifest (Prüfung vor dem Laden), PluginManager (Lebenszyklus, Host-Dienste)
├── rendering/  ShaderChain (mpv "glsl-shaders", Fehlerzuordnung, Deaktivieren fehlerhafter Stufen)
└── services/   PlaylistModel, MediaFiles, HotkeyModel, ToolUpdater, SingleInstance

plugins/
├── uve/                Ultimate Video Enhancer: engine/ (Parameter, Shader-Generator, .cube-Parser), Controller, QML-Panel
└── examples/sleeptimer Referenz-Plugin (nur Host-Dienste)
```

## libmpv-Anbindung
- **Thread-Grenzen:** mpv ruft `wakeup` aus eigenen Threads; der Controller reiht nur `processEvents` per
  `Qt::QueuedConnection` ein und liest dann im GUI-Thread mit `mpv_wait_event(…, 0)` (nicht blockierend).
- **Befehle** asynchron (`mpv_command_async`), Eigenschaften über `mpv_observe_property`.
- **Zustandsautomat:** `Idle → Opening → Playing ⇄ Paused / Buffering → Ended | Error`
  (`START_FILE`, `FILE_LOADED`, `pause`, `paused-for-cache`, `eof-reached` wegen `keep-open=yes`, `END_FILE`).
- **Videoausgabe:** `vo=libmpv` + Render-API (OpenGL). Qt Quick wird auf das OpenGL-RHI festgelegt;
  `MpvVideoItem` erzeugt den Render-Kontext im Render-Thread beim ersten FBO. Dateien, die vorher geöffnet werden,
  werden zurückgestellt (`markRenderReady`), sonst würde mpv die Videospur mangels Ausgabe deaktivieren.
- **Zero-Copy:** nicht zugesagt. Hardware-Dekodierung `auto-safe` (im Test NVDEC); ob mpv dabei interop oder kopiert,
  hängt von Treiber/Backend ab und wurde nicht gemessen.

## Frame-Pipeline (nativ)
Decode (HW/SW) → YUV→RGB (mpv) → **Hook MAIN: UVE-Pässe** (Original sichern · Rauschfilter+CAS · Farbkorrektur ·
Detail/Kanten/LUT/Split) → Skalierung → mpv-Farbmanagement/Tonemapping → Ausgabe ins Qt-FBO.

## Fehlerbehandlung
- libmpv nicht ladbar (z. B. fehlende `vulkan-1.dll`): Klartextmeldung statt Absturz.
- Shaderfehler: `ShaderChain` deaktiviert die Stufe, Wiedergabe läuft weiter, Plugin erhält `onShaderError`.
- Plugin-Absturz beim Start: nach 2 unsauberen Starts in Folge Safe Mode (auch `--safe-mode`).
- Beschädigte `settings.json`: Sicherung `settings.json.corrupt-<zeit>`, Start mit Standardwerten.
- Beschädigtes Profil/LUT: Ablehnung mit Meldung, Quelldatei unverändert.

## Daten
`%APPDATA%\nextscript\UVE Player\` (bzw. `UVE_DATA_DIR`, portabel `.\data` mit `portable.txt`):
`settings.json`, `crash-guard.json`, `logs\`, `plugin-data\<id>\`, `tools\<werkzeug>\<version>\` (Updates).
