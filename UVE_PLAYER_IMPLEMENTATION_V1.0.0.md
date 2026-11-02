# UVE Player — Implementation Specification v1.0.0

> **Ziel:** Einen eigenständigen, nativen und plattformübergreifenden Mediaplayer mit VLC-ähnlichem Funktionsumfang entwickeln. Der Ultimate Video Enhancer (UVE) wird als **optional aktivierbares, separat wartbares Plugin** integriert. Die Analyse existierender Player erfolgt **verpflichtend über das bereits installierte REA-MCP**.
>
> **Adressat:** Ein Coding-Agent mit lokalem Projektzugriff, Build-/Testwerkzeugen und einer installierten REA-MCP-Verbindung.
>
> **WICHTIG:** Diese Datei ist ein Implementierungsauftrag, keine Behauptung, dass die Anwendung bereits erstellt oder dass REA bereits ausgeführt wurde. Der Agent muss die Schritte selbst durchführen und Ergebnisse belegen.

## 0. Verbindliche Agenten-Anweisung: REA-MCP verwenden

**REA = Reverse Engineer Anything:** https://github.com/morluto/rea

Der Nutzer hat den REA-MCP-Server bereits installiert. **Vor der Implementierung ist die vorhandene MCP-Verbindung tatsächlich aufzurufen.** Nicht nur das REA-Repository lesen oder REA erwähnen: Seine verfügbaren MCP-Tools abfragen, ihre realen Namen und Schemas verwenden und damit die lokal vorhandene Referenzanwendung untersuchen.

### 0.1 Untersuchungsobjekte

1. **VLC Media Player** (sofern lokal installiert): Bedienverhalten und Architekturmerkmale als Produktreferenz — Playlist, Media-Öffnen, Untertitel, Playback Controls, Wiedergabegeschwindigkeit, Fenster- und Fullscreen-Verhalten, Einstellungen und Hotkeys.
2. **mpv / libmpv** (sofern lokal installiert): Wiedergabe- und Rendering-Integrationspunkte, Config-/Shader-Verhalten und Ereignisverarbeitung. Für Open-Source-Code primär die öffentlichen Quellen und API-Dokumentation verwenden; REA ergänzt sie bei Laufzeit-/Installationsbeobachtungen.
3. **Ultimate Video Enhancer**: Eigene Repository-Quellen statisch prüfen; für die laufende Electron-App REA-JavaScript/Electron-Analyse benutzen, falls Build/Installation verfügbar. Dabei Rendering, WebGPU/WebGL2, Einstellungsprofile und Effektabfolge aufspüren.

**Keine VLC-internen Strukturen kopieren oder undokumentierte Reverse-Engineering-Ergebnisse als gesicherte API ausgeben.** Eigene Implementierung mittels sauberer, veröffentlichter Schnittstellen. Reverse Engineering ausschließlich im rechtlich zulässigen Rahmen und unter Beachtung von Lizenzen und anwendbaren Regeln; keine Schutzmechanismen umgehen oder geschützte Inhalte extrahieren.

### 0.2 Konkreter REA-Ablauf (verpflichtend)

- MCP-Serververfügbarkeit und angebotene Tools abfragen. **Keine Toolnamen halluzinieren**. Exakt die vom Server gelieferten Toolnamen/Schemas verwenden.
- Zielanwendung(en), Versionen, Installationspfade und Plattform erfassen; keine Pfade erfinden. Benutzerinstallationen nur mit zugänglichen Dateien analysieren.
- Zuerst möglichst **read-only und statisch** untersuchen: Anwendungskomponenten, Module, Imports, relevante Binärdateien, eingebettete Konfigurationen; bei Electron zusätzlich Modules, IPC-Grenzen und Renderpfade. Dynamic Inspection nur wenn technisch notwendig und zulässig.
- Für jede untersuchte Funktion dokumentieren: Ziel, REA-Tool/Aufruf, untersuchtes Artefakt samt Pfad und Hash soweit verfügbar, Beobachtung, Beleg (z. B. Symbol/Modul/Offset/Screenshot), Interpretation, Unsicherheiten und Umsetzung als eigene Funktion.
- REA-Fehlfunktionen oder fehlende Abhängigkeiten festhalten; keine erfolgreichen Analysen behaupten. Bei nicht installiertem Ziel erst prüfen, ob Open-Source-Code und dokumentierte APIs genügen. **Fehlender REA-Zugriff ist ein Blocker für den verpflichtenden REA-Nachweis**, nicht für die Formulierung eines Plans.
- Keine blindlings modifizierenden oder destruktiven REA-Aktionen. Eigene App bei Tests isoliert betreiben und nur Testdateien verwenden.

**Erforderlicher Output:** `docs/REA_ANALYSIS.md` mit nachvollziehbaren Ergebnissen, `docs/REA_EVIDENCE/` mit zulässigen Text-/Bildbelegen, `docs/FEATURE_COMPARISON.md` (VLC/mpv → eigenes Design), `docs/REA_LIMITATIONS.md` (nicht verifizierte Aussagen). Vor dem ersten Implementierungs-Commit mindestens REA-Status und Analyseplan dokumentieren; vor Freigabe v1.0.0 muss die Analyse vollständig sein oder als nicht erfüllte Anforderung markiert werden.

REA-Dokumentation: https://github.com/morluto/rea ; https://github.com/morluto/rea/blob/main/docs/mcp-contracts.md ; https://github.com/morluto/rea/blob/main/docs/desktop-applications.md

## 1. Produkte und Abgrenzung

**Neuer Name (Arbeitsname):** UVE Player. Separates Programm, **keine Umbenennung oder Ersetzung des UVE Managers**. Eigener Source-Tree/Repository empfohlen. Bestehende UVE-Dateien, Profile und Apps werden **nicht überschrieben**.

### Muss-Kriterien für v1.0.0

- Video- und Audio-Wiedergabe von lokalen Dateien und unterstützten HTTP(S)-Adressen mit libmpv.
- Hardware-Decoding, sinnvolle Fallbacks, stabile Audio-/Video-Synchronisation, Pause/Seek/Frame-Step.
- Playlists, Untertitel-/Audiospurwahl, Drag & Drop, Vollbild, Tastenkürzel, Verlauf optional abschaltbar.
- Native moderne Oberfläche mit Qt 6/QML; adaptive Skalierung und dunkles/helles Theme.
- Optionale Plugins über definierte, versionierte Plugin-Schnittstelle; fehlerhaftes Plugin darf den Start nicht dauerhaft blockieren.
- **UVE Plugin:** Effects On/Off, Presets, einstellbare Effekte, Before/After, Konfiguration persistieren. Zunächst nachweisbare Teilmenge; fehlende Effekte in Parity-Matrix klar kennzeichnen — keine falschen Paritätsbehauptungen.
- Windows 10/11 x64, Linux x64, macOS arm64/x64 als **Zielplattformen**. Plattformen nur bei erfolgreichen Builds/Tests als unterstützt deklarieren.
- Keine vorausgesetzte NVIDIA-GPU; AMD/Intel/Apple und Software-Fallback berücksichtigen.

### Nicht Teil von v1.0.0

- Vollwertiger Multi-Track-Videoeditor oder Export-Rendering (bleibt Aufgabe des UVE Managers).
- DRM-Umgehung, Streamingplattform-Schutzmechanismen umgehen oder Kopieren lizenzgeschützter Inhalte.
- Garantie für Echtzeit-KI-Upscaling mit beliebigen neuronalen Modellen.
- Blindes Ausführen ungeprüfter, untrusted nativer Fremd-Plugins im Hauptprozess.

## 2. Technologieentscheidungen

| Bereich | Auswahl | Begründung |
| --- | --- | --- |
| Sprache | **C++20** | Native Anbindung, Rendering, Plugin-ABI |
| GUI | **Qt 6 + Qt Quick/QML** | Desktop-Oberfläche auf Windows/Linux/macOS |
| Playback | **libmpv (Client API)** | Bewährte Demux-/Decoder-/Untertitel-/Playback-Funktionen |
| GPU-Filter | **mpv Shader Hook / libplacebo, soweit unterstützt** | GPU-nahes Rendering, Shader-Ketten |
| Build | **CMake + vcpkg oder geprüftes Paketmanagement** | Reproduzierbare native Builds |
| Tests | **CTest + Qt Test + End-to-End-Testmatrix** | Wiederholbar |
| Plugins | **Qt-Plugin-Layer + versioniertes Host-Interface** | Discovery, Lifecycle, Settings |
| Daten | **JSON mit versioniertem Schema, QSettings nach Bedarf** | Profile und Einstellungen |
| Packaging | **windeployqt / macdeployqt / Linux-Bundling** | Plattformpakete |

Wichtiger Architekturhinweis: libmpv läuft als Wiedergabe-Engine in der Anwendung. `libplacebo` ist keine beliebig austauschbare Erweiterungsschicht; für Custom GPU Filters ist zu prüfen, welche Shader-Hooks die **verwendete mpv-Version und der gewählte Video-Output** tatsächlich bereitstellen. Nicht unterstellen, dass beliebige WebGPU-, GLSL- oder TypeScript-Effekte unverändert ausführbar sind. Bei Bedarf für erweiterte GPU-Plugins einen eigenen Renderer-Pfad planen; nicht schon v1 mit einer unbewiesenen Zero-Copy-Architektur festlegen.

**Quellen:** https://mpv.io/manual/master/ , https://github.com/mpv-player/mpv , https://github.com/haasn/libplacebo , https://doc.qt.io/qt-6/qtqml-index.html

## 3. Vor Implementierung: Repositories vollständig inventarisieren

**Quellen:**
- Browser-Userscript: https://github.com/nextscript/Ultimate-Video-Enhancer (Branch `main`)
- Desktop-Editor: https://github.com/nextscript/Ultimate-Video-Enhancer/tree/uve-manager
- REA: https://github.com/morluto/rea

Zum Zeitpunkt der Spezifikation weist `uve-manager` laut README/`package.json` **v1.1.2** mit Electron, React, TypeScript, WebGPU/WebGL2, FFmpeg und optionalen REAL-Video-Enhancer- und Video2X-Backends aus. Dies ist **keine** native C++-Plugin-Implementierung. Aktuellen Stand zu Beginn erneut prüfen; kein statisches Dateilayout voraussetzen.

Erstelle `docs/UVE_PARITY_MATRIX.md` anhand **echter Source-Stellen**:

| Funktion | Source-Datei / Stelle | WebGPU/WebGL/JS-Implementierung | Native Umsetzung | Status | Test |
|---|---|---|---|---|---|
| Sharpen | zu ermitteln | zu ermitteln | Shader / CPU-Fallback | open | Vorher/Nachher |
| HDR-like / Tonemapping | zu ermitteln | zu ermitteln | Shader | open | SDR/HDR |
| Brightness / Contrast / Saturation | zu ermitteln | zu ermitteln | Shader | open | Pixelvergleich |
| Denoise / Texture | zu ermitteln | zu ermitteln | Shader oder Backend | open | Temporal Stability |
| LUT Import | zu ermitteln | zu ermitteln | Parser + GPU LUT | open | `.cube` |
| Before/After | zu ermitteln | zu ermitteln | Hotkey + Split/Wipe | open | Live A/B |
| Preset Profile | zu ermitteln | JSON | Versionierter Import | open | Roundtrip |
| Auto Scene Match | zu ermitteln | zu ermitteln | gesondert evaluieren | open | Szenenwechsel |
| Scopes | zu ermitteln | zu ermitteln | optional separate GPU/CPU-Auswertung | open | Genauigkeit |
| AI Upscale | zu ermitteln | externe Backends | optionales Provider-Plugin | future | Durchsatz |

**Abnahmeregel:** Nicht einfach alle Browser-Effekte als „übernommen“ markieren. Für jeden Effekt müssen die Shader-/Algorithmenparameter, Farbraumkonventionen, Grenzwerte, Pixelvergleich, Plattformfähigkeit und Performance dokumentiert sein. Die Plugin-UI darf nur tatsächlich implementierte Effekte anbieten. UVE-Profile dürfen ihre Originaldateien nicht beschädigen.

## 4. Zielarchitektur

```text
UVE Player
│
├── Qt 6 / QML UI
│   ├── Player Window / Controls / Seekbar
│   ├── Playlist / Subtitle & Audio Tracks
│   ├── Effects Panel / Before-After / Presets
│   ├── Plugin Manager / Settings / Hotkeys
│   └── Diagnostics
│
├── App Core (C++20)
│   ├── PlayerController / Playback State
│   ├── Media & Playlist Services
│   ├── Plugin Registry / Lifecycle / Settings Schema
│   ├── Profile Migration / Config Persistence
│   └── Logging, Crash Recovery, Compatibility Checks
│
├── libmpv Adapter
│   ├── Event Loop / Thread Boundaries
│   ├── Video Output + Qt Integration
│   ├── HW Decode / Software Fallback
│   └── Subtitle & Audio Track Control
│
└── Plugins
    ├── UVE Enhancement Plugin
    │   ├── Shader Chains / LUT / Effect Parameters
    │   ├── Presets / Before-After / Capability Detection
    │   └── Optional AI Provider Abstraction (later)
    └── Other supported plugins
```

### 4.1 Threading und Rendering

- mpv-Events **nicht** im GUI-Thread blockierend poll-en. Thread-sichere Nachrichten und klare QObject-Lifetime festlegen.
- Videoeinbettung anhand aktuell dokumentierter `libmpv` Render-API/Qt-Integrationsmöglichkeiten evaluieren. QML-Scenegraph und native API-Ownership sauber koordinieren.
- Frame-Pipeline: Decode → korrekte Farb-/Range-Konvertierung → UVE Shader → Tone Mapping/Presentation → Display (exakte Reihenfolge je nach Render-Backend validieren).
- Zero-Copy **nur dann** zusagen, wenn für gewählte Backend-/Betriebssystemkombination nachgewiesen. Sonst Kopierkosten messen.
- Synchronisierung für HDR10, SDR, High-DPI, variable Framerate und 10-bit-Video explizit testen; OS-seitige HDR-Ausgabe nicht ohne Nachweis versprechen.
- Kein synchroner Netzwerk-/Modell-Download im Render- oder UI-Thread.

## 5. Plugin-System (verbindlicher Vertrag)

Versioniertes Host-Interface mit `apiVersion`, `pluginId`, `version`, `minHostVersion`, `capabilities`, `supportedPlatforms`, `requiredBackends`, `configurationSchema`, `permissions`. Kompatibilitätsprüfung **vor** Plugin-Aktivierung.

Lifecycle: `discover → verify → load → initialize → activate → suspend → deactivate → unload`. Zu jedem Zustand definierte Fehlerbehandlung, Telemetrie standardmäßig **aus**, persistentes Error-Log, Safe Mode ohne Plugins nach wiederholtem Crash. Eigene Host-Services statt direktem Zugriff auf globale GUI-/mpv-Objekte. Einfache Parameter-Plugin-API zuerst; native Filterplugins sind Vertrauenscode und erfordern separate Sicherheits-/Stabilitätsstrategie (optional isolierter Prozess, wo technisch möglich). Dynamisch ladbare C++-Plugins müssen mit kompatiblen Qt-/Compiler-/ABI-Builds ausgeliefert werden; binär stabile C-ABI oder IPC für externe Drittentwickler prüfen.

**Beispielmanifest (vorgeschlagen, noch nicht implementiert):**

```json
{
  "id": "org.nextscript.ultimate-video-enhancer",
  "name": "Ultimate Video Enhancer",
  "version": "1.0.0",
  "apiVersion": 1,
  "type": "gpu-effect",
  "entrypoint": "uve-plugin",
  "capabilities": ["video-effects", "presets", "before-after"],
  "requiredBackends": ["supported-mpv-shader-path"],
  "permissions": ["read-user-selected-presets"]
}
```

`entrypoint` und `requiredBackends` stehen hier für **eigene zu implementierende Schema-Konventionen**, nicht für existierende mpv-API-Felder. Manifestvalidierung/Signierung optional; keine stillen Plugin-Updates oder Hintergrunddownloads.

## 6. UVE-Plugin: Funktionsweise

- Installier- und deaktivierbar über **Settings → Plugins**; bei nicht installiertem Plugin spielt der Player vollständig weiter.
- Effektsektionen: **Basic**, **Color**, **Sharpen**, **HDR-like**, **Denoise**, **LUT**, **Advanced**, **Presets** (jeweils nur tatsächliche Features anzeigen).
- Effektpipeline mit expliziter Reihenfolge und aktivem GPU-Backend; einzelne Effekte einzeln abschaltbar.
- Beibehaltung von Eingabeprofilen / Slider-Semantik soweit nachweislich kompatibel. Profile importieren, validieren, migrationieren, exportieren.
- Before/After (Toggle, Split/Wipe), Reset per Effect und Reset All, Undo für Einstellungsänderungen.
- Pro Medien-Datei optional eigenes Preset, globales Default-Preset, „Original“ als jederzeit verfügbarer Fallback.
- Shaderfehler: Meldung mit Dateipfad/Pass/Compilelog, fehlerhaften Pass deaktivieren, Playback fortsetzen.
- WebGPU- oder WebGL-Shader nicht per Dateiendung als mpv-/libplacebo-kompatibel behandeln. Shader-Dialekt, Uniforms, Textur-/Farbraumzugriffe und Multi-Pass-Logik portieren und testen.
- Upcaling-KI-Provider (`TensorRT`, `NCNN/Vulkan`, `PyTorch`) nur als **spätere optionale Live-Integration**, mit Capability- und Frame-Budget-Prüfung. Batch-/Offline-Renderer aus UVE Manager wird dadurch nicht automatisch livefähig.

## 7. Player-Funktionen und UI

### Hauptfenster

- Minimalistische dunkle Oberfläche als Default, Light-Theme optional, frei skalierbar.
- Oberes Menü: **File | Playback | Video | Audio | Subtitles | Effects | Plugins | View | Help**.
- Center: Videooberfläche ohne unnötige Overlays; Mausbewegung blendet Controls ein.
- Unteres Steuerfeld: Back/Play/Pause/Next, präzise Seekbar mit Zeitangabe, Volume/Mute, Subtitle Selector, Audio Selector, Speed, Snapshot, Playlist, Fullscreen.
- Seitenleiste ein-/ausblendbar für Playlist und Effektpanel.
- UVE Plugin mit eigenem Tab **Enhancements** und Live-Vorschau; Plugin deaktiviert → Tab verschwindet, Rest des Players unverändert.
- Drag & Drop von Datei/Ordner/URL (URLs nur mit Validierung), Open Recent, Multi-File Queue, Wiederhol-/Shuffle-Optionen.
- Hotkeys editierbar, Konfliktwarnung, Reset to Defaults; Untertitel-Delay und Audio-Delay anpassbar.

### Settings

`General`, `Playback`, `Video & GPU`, `Audio`, `Subtitles`, `Plugins`, `Enhancements`, `Shortcuts`, `Privacy`, `Advanced`.

### Playback-Abdeckung und Diagnose

Erst Kernpfad implementieren, dann: lokale MP4/MKV/WebM/MOV/AVI/MP3/FLAC, Untertitel SRT/ASS, Netzwerkdateien über HTTPS soweit von libmpv unterstützt, HLS ohne Umgehung von Zugangs-/Werbeschutzsystemen. Tatsächliche Codec-Fähigkeit hängt von kompilierten Dependencies und deren Lizenzen ab. Video-HW-Decode mit Auto-Auswahl und Benutzer-Override; Anzeige von Decoder, Video-Out, durchschnittlicher FPS, Dropped Frames, A/V-Delay und aktiver Shader-Kette.

## 8. Repo-Struktur (Vorschlag)

```text
uve-player/
  CMakeLists.txt
  cmake/
  app/main.cpp
  src/core/
  src/player/            # libmpv adapter, event handling
  src/plugins/           # API, registry, validation, lifecycle
  src/rendering/         # shader adapter and capabilities
  src/services/          # playlists, files, settings
  src/ui/                # Qt / QML
  plugins/uve/           # optional UVE plugin
  plugins/examples/      # reference plugin
  resources/
  tests/unit/
  tests/integration/
  tests/media/            # legally redistributable short samples
  docs/
    REA_ANALYSIS.md
    REA_LIMITATIONS.md
    REA_EVIDENCE/
    FEATURE_COMPARISON.md
    UVE_PARITY_MATRIX.md
    ARCHITECTURE.md
    PLUGIN_API.md
    BUILD.md
    TEST_REPORT.md
    THIRD_PARTY_LICENSES.md
  .github/workflows/
```

## 9. Implementierungsphasen (Reihenfolge verbindlich)

### Phase A — Untersuchung und Nachweis
1. Repos und lokale Installation erfassen; REA-MCP verbinden und tatsächlich verwenden.
2. REA-Befunde für VLC/mpv/UVE festhalten, Funktionen mit Quelle und Beleg annotieren.
3. UVE-Effektinventar und Lizenz-/Abhängigkeitsprüfung erstellen.
4. Rendering-Spike: Qt 6 + libmpv, 1080p60 Wiedergabe, kontrollierte Eventloop; Probe eines funktionierenden mpv-GPU-Shaders.

**Gate A:** REA-Bericht vorhanden; Video läuft; gewählter Rendering-Integrationstyp nachweisbar; ungeklärte Shader-Fragen benannt.

### Phase B — Solider Grundplayer
1. PlayerController mit Zustandsautomat `Idle/Opening/Playing/Paused/Buffering/Ended/Error`.
2. Datei/Ordner/URL/Playlist, Seek, Fullscreen, OSD, Hotkeys, Track-Auswahl.
3. Untertitel, Audio-Delay/Speed, Fehlerbilder und Hardware-Decode-Fallbacks.
4. Settings/Session-Verwaltung, optionale Recent Files.

**Gate B:** Lange Wiedergabe, Playlistwechsel und Seek ohne Crash; nutzbar **ohne** Plugins.

### Phase C — Plugin API
1. Versionierung und Manifest-Schema, Settings API, Lifecycle, Plugin Manager.
2. Beispielplugin sowie Testfall für ungültige/inkompatible Plugins.
3. Safe Mode und gezielte Wiederherstellung nach Plugin-Absturz.

**Gate C:** Plugins aktivieren/deaktivieren; Fehler führen zu dokumentiertem Fallback.

### Phase D — UVE Plugin
1. Effektquelle und Reihenfolge anhand Parity-Matrix portieren, nicht schätzen.
2. Erst Color/Sharpen/LUT, danach weitere nachgewiesene kompatible Effekte.
3. Slider, Presets, Before/After, Fehleranzeige, Profile Roundtrip.
4. Vergleich mit Browser-/UVE-Manager-Referenz auf denselben Quelldateien.

**Gate D:** Jedes als unterstützt markierte Feature hat belegten Vergleichstest; Plugin Off → Referenzwiedergabe.

### Phase E — Releases
1. Windows, Linux, macOS bauen; Host- und Plugin-Dependencies korrekt bündeln.
2. Lizenztexte, Checksums, Debug-Symbole separat, Update- und Privacy-Einstellungen.
3. E2E-Tests + reale GPU-Matrix + Portabilität testen, Release-Notes schreiben.

**Gate E:** Nur getestete Plattformen offiziell ausloben. Keine ungemessenen Geschwindigkeitsversprechen.

## 10. Test-/Qualitätsplan

**Mindestszenarien:**

| Test | Erwartung |
|---|---|
| 1080p60 H.264 und HEVC | stabile Wiedergabe; Decode-Backend angezeigt |
| 4K30 / 4K60, sofern Testgerät leistungsfähig | keine unbegründete Erfolgsgarantie, FPS messen |
| MKV mit mehreren Sprachen + SRT/ASS | Trackwechsel korrekt, Ton synchron |
| Schnelles Seek, Loop, Playlist Next | Playerzustand konsistent |
| UVE On/Off während Playback | kein Neustart notwendig, Bild sichtbar umschaltbar |
| LUT/Shader-Fehler | sauberer Fallback statt schwarzem Bildschirm |
| Fensterskalierung, Multi-Monitor, Fullscreen | keine QML-Überlappungen |
| GPU-Wechsel/Fallback | AMD/NVIDIA/Intel/Apple je nach vorhandenem Testgerät |
| CPU-only-Umgebung | Abspielen ohne UVE-GPU-Effekte soweit möglich |
| Plugin-Manifest falsche API-Version | kontrollierte Ablehnung |
| Beschädigtes Profil / falscher Shader | verständliche Fehlermeldung, keine Datenkorruption |
| Bereits aktive Video-Wiedergabe nach Plugin-Crash | Wiederanlauf/Deaktivierung dokumentiert |
| Profile import/export | reversible semantische Parameterwerte |
| Datenschutz | keine Telemetrie oder Netzwerkanfragen ohne Funktion/Einwilligung |

Benchmarks: Render FPS, Dropped Frames, GPU-Auslastung, RAM/VRAM, Frame-Pacing (p50/p95/p99), Startzeit und Seek-Latenz. Getrennt für **Original**, **UVE Basic**, **UVE High** messen; Warm-up, Treiber und Hardware protokollieren. **Keine willkürlichen Prozent-Performance-Gewinne versprechen.**

## 11. Lizenzen und Distribution

- Vor Distribution die **konkreten** Lizenzen und Build-Konfigurationen von mpv, FFmpeg, libplacebo, Qt, Shadern, optionalen Codecs, Modellen und UVE-Quellen prüfen.
- Qt-Lizenzmodell, LGPL/GPL-Kompatibilität, dynamische Verlinkung, Source-Offerten und erforderliche Notices rechtlich prüfen. Proprietäre Distribution nicht pauschal als möglich bezeichnen.
- VLC/REA ausschließlich als Analyse- und Referenzmaterial, **keinen** fremden Code blind übernehmen.
- Kein ungefragter externer Download von Plugins/Modellen. Externe native Plugins nur nach transparenter Nutzeraktion installieren.

## 12. Finaler Lieferumfang des Coding-Agenten

- Baubares, lauffähiges **eigenes** `uve-player` Projekt mit C++20, Qt 6/QML und libmpv.
- Plugin API + Beispielplugin + optionales UVE Plugin mit **tatsächlich funktionierender** Feature-Teilmenge und belegter Parität.
- REA-Nachweis und Quellen-/Funktionsanalyse, inkl. Limitierungen und Tests.
- Build-Anleitung, Dependencies, Testberichte, Lizenzübersicht, Release-Artefakte für erfolgreich getestete Systeme.
- Eine finale Tabelle: **Anforderung | implementiert | getestet | Limitierung | Referenz**.
- Kein Dummy-UI, keine nur dekorativen Buttons, keine `TODO`-Funktionen als abgeschlossen deklarieren.

## 13. Unverhandelbare Arbeitsregeln

1. **REA MCP tatsächlich benutzen**; bei fehlendem Zugriff den technischen Grund dokumentieren und den Gate-Status offen lassen.
2. **Keine bestehende UVE Manager-Installation verändern oder zerstören.** Neuen Player unabhängig bauen.
3. API- und Render-Details gegen echte Versionen prüfen; technisch Unmögliches offen benennen.
4. Erst die abgenommenen Grundfunktionen stabilisieren, dann teure GPU-/KI-Features hinzufügen.
5. Für jedes aktive UI-Element vollständige Funktion und zugehörigen Test implementieren.
6. Fallbacks und portable Profile gehören zur Version 1.0.0, nicht erst zur Fehlerbehebung.
7. **Keine fingierten Benchmarks, REA-Ergebnisse, Plugin-Kompatibilität oder Plattformtests.**

---

### Referenzlinks

- REA: https://github.com/morluto/rea
- REA MCP Contracts: https://github.com/morluto/rea/blob/main/docs/mcp-contracts.md
- UVE Userscript: https://github.com/nextscript/Ultimate-Video-Enhancer
- UVE Manager: https://github.com/nextscript/Ultimate-Video-Enhancer/tree/uve-manager
- mpv API: https://mpv.io/manual/master/#client-api
- mpv Code: https://github.com/mpv-player/mpv
- Qt 6: https://doc.qt.io/qt-6/
- libplacebo: https://github.com/haasn/libplacebo
- VideoLAN: https://www.videolan.org/
