# UVE Player 1.0.0

Nativer Medienplayer für Windows auf Basis von **C++20, Qt 6 und libmpv** – mit dem **Ultimate Video Enhancer** als
GPU-Effekt-Plugin, Netzwerkstreams (YouTube, Twitch, HLS, tcp://) samt Proxy-Unterstützung und sieben Sprachen.

## Download

| Datei | Beschreibung |
|---|---|
| `UVE-Player-1.0.0-setup.exe` | Installation für den aktuellen Benutzer (ohne Administratorrechte), Startmenü, optional Desktop-Symbol und „Öffnen mit“ |
| `UVE-Player-1.0.0-portable.exe` | Eine einzelne EXE, startet ohne Installation; Einstellungen landen im Ordner `UVE Player Data` daneben |
| `UVE-Player-1.0.0-portable.zip` | Portabler Ordner zum Entpacken (Daten in `.\data`) |
| `SHA256SUMS.txt` | Prüfsummen |

Alle Varianten enthalten libmpv, yt-dlp und 7zr – es muss nichts zusätzlich installiert werden.
Voraussetzung: Windows 10/11 x64 mit aktuellem Grafiktreiber (libmpv benötigt `vulkan-1.dll` aus dem Treiber).

## Funktionen

- **Wiedergabe:** fast alle Formate über libmpv/FFmpeg, Hardware-Dekodierung, Wiedergabeliste (M3U/PLS, Ordner, Suche,
  Drag & Drop), Fortsetzen an der letzten Position, Audio-/Untertitelspuren und -Verzögerung, A-B-Schleife, Einzelbild,
  Geschwindigkeit, Bildschirmfoto, Diagnose-Overlay, Medieninfo
- **Bedienung:** Klick = Pause, Doppelklick = Vollbild, Mausrad = Lautstärke, Rechtsklick-Menü, editierbare Tastenkürzel
- **Netzwerk:** YouTube/Twitch (yt-dlp), HTTP(S), HLS, DASH, tcp/udp/rtmp/rtsp/srt; automatische Update-Prüfung für yt-dlp und libmpv
- **Proxy:** kein / Windows-System / manuell (HTTP oder SOCKS5, optional mit Anmeldung) – gilt für Streams, yt-dlp und Downloads
- **Ultimate Video Enhancer:** Farbkorrektur, Schärfe, HDR-like, Rauschfilter, Korn, BL/WL/SL/SR; Presets, Vorher/Nachher,
  Undo; UVE-Profile importieren/exportieren; **Beispielprofil und LUT-Pakete direkt von GitHub**; **LUT-Galerie** mit Vorschau
  auf dem aktuellen Videobild; `.cube`-3D-LUTs
- **Sprachen:** English (Voreinstellung), Deutsch, Español, Italiano, Polski, Ελληνικά, Türkçe – umschaltbar unter
  *Einstellungen → Allgemein*; eigene Sprachen: [lang/README.md](lang/README.md)
- **Plugins:** versionierte Plugin-API mit Manifest-Prüfung und Safe Mode ([docs/PLUGIN_API.md](docs/PLUGIN_API.md))
- **Datenschutz:** keine Telemetrie

## Kurzbedienung

| Taste | Aktion | Taste | Aktion |
|---|---|---|---|
| Leertaste | Wiedergabe/Pause | F / Doppelklick | Vollbild |
| ← / → | Springen | ↑ / ↓ | Lautstärke |
| N / P | Nächster / vorheriger Titel | V / B | Untertitel / Audiospur |
| Strg+Alt+E | Effekte an/aus | Strg+Alt+U | Vorher/Nachher |
| Strg+L | Seitenleiste | F1 | Tastenkürzel ändern |

## Daten

Einstellungen, Protokolle, Presets und Sprachdateien des Benutzers: `%APPDATA%\nextscript\UVE Player\`
(portabel: neben der EXE). Start ohne Plugins: `UVEPlayer.exe --safe-mode`.

## Selbst bauen

Kurzfassung (Details: [docs/BUILD.md](docs/BUILD.md)):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=M:/LAPS/Qt/6.12.0/msvc2022_64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release
powershell -ExecutionPolicy Bypass -File scripts\package.ps1     # Setup, portable EXE, ZIP → release\
```

## Dokumentation

[ARCHITECTURE](docs/ARCHITECTURE.md) · [PLUGIN_API](docs/PLUGIN_API.md) · [BUILD](docs/BUILD.md) ·
[TEST_REPORT](docs/TEST_REPORT.md) · [UVE_PARITY_MATRIX](docs/UVE_PARITY_MATRIX.md) · [FEATURE_COMPARISON](docs/FEATURE_COMPARISON.md) ·
[REA_ANALYSIS](docs/REA_ANALYSIS.md) · [REA_LIMITATIONS](docs/REA_LIMITATIONS.md) · [THIRD_PARTY_LICENSES](docs/THIRD_PARTY_LICENSES.md) ·
[CHANGELOG](CHANGELOG.md)

## Anforderungen – Abschlusstabelle

| Anforderung | implementiert | getestet | Limitierung | Referenz |
|---|---|---|---|---|
| C++20 + Qt 6 (QML) + libmpv, CMake | ja | ja (Build, 7 CTest-Suiten) | nur Windows x64 gebaut | ARCHITECTURE.md, BUILD.md |
| Wiedergabe lokal (MKV/AVI/MP4, HEVC 10-Bit, AC3) inkl. Hardware-Dekodierung | ja | ja (NVDEC) | AMD/Intel nicht getestet | TEST_REPORT.md |
| Wiedergabeliste, Doppelklick, Kontextmenü, Fortsetzen | ja | ja (UI-Selbsttest, tst_playlist) | – | app/qml/PlaylistPanel.qml |
| Menüs Datei … Hilfe, Einstellungen (11 Seiten), Hotkeys mit Konfliktwarnung | ja | ja (tst_hotkeys, UI) | – | SettingsDialog.qml |
| Audio-/Untertitelspuren, Verzögerung, Diagnose (Decoder, VO, FPS, Drops, A/V, Shader) | ja | ja | – | DiagnosticsOverlay.qml |
| Netzwerkstreams YouTube/Twitch/HLS/tcp | ja | ja (echte Quellen) | YouTube-Formate abhängig von yt-dlp | TEST_REPORT.md |
| Proxy (HTTP/SOCKS5, System) für Streams, yt-dlp, Downloads | ja | ja (Live-SOCKS5, lokaler Test-Server) | udp/rtp/rtmp/rtsp/srt nicht über Proxy | src/services/Proxy*.cpp |
| UVE-Effekte nativ (GLSL), Presets, Undo, Vorher/Nachher, Preset je Datei, Standard-Preset | ja | ja (Formeln exakt, Wirkung im Bild gemessen) | Auto-Scene-Match, Scopes, eigene Filtercodes nicht portiert | UVE_PARITY_MATRIX.md |
| UVE-Profile, Beispielprofil + LUT-Pakete von GitHub, LUT-Galerie | ja | ja (echter Download, 83 Profile) | SR ist Radius-Näherung | UVE_PARITY_MATRIX.md |
| Shaderfehler → Rückfall statt Schwarzbild | ja | ja (ShaderChain) | – | src/rendering/ShaderChain.cpp |
| Plugin-API (Manifest, Lebenszyklus, Safe Mode, Fehlerlog) | ja | ja (tst_manifest, Selbsttest) | native Plugins = Vertrauenscode, keine Signatur | PLUGIN_API.md |
| Mehrsprachigkeit (en Standard, de, es, it, pl, el, tr) + eigene Sprachen | ja | ja (tst_i18n, UI en/el) | Übersetzungen maschinell erstellt, Muttersprachler-Prüfung offen | lang/README.md |
| Tool-Updater (yt-dlp, libmpv) mit Prüfsummen | ja | teilweise (Prüfung; Installation von libmpv-Updates nicht end-to-end) | Update wirkt nach Neustart | ToolUpdater.cpp |
| Setup + portable EXE + ZIP mit allen Werkzeugen | ja | ja (Paketbau, Start aus Staging) | Setup nicht auf frischem System geprüft | scripts/package.ps1 |
| REA-Analyse (vlc.exe, libmpv, Electron-Vorgänger) | ja | – | kein Deep-Provider (Ghidra/IDA) verfügbar | REA_ANALYSIS.md, REA_LIMITATIONS.md |
| Telemetrie aus, keine ungefragten Downloads, kein DRM-Umgehen | ja | Code-Review | – | THIRD_PARTY_LICENSES.md |

## Lizenzen

Drittkomponenten (Qt LGPLv3, mpv/libmpv/FFmpeg GPL/LGPL je nach Build, yt-dlp Unlicense, 7-Zip LGPL):
[docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md). Ultimate Video Enhancer © Freak288 / nextscript.
