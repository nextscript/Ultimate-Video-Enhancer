# Changelog

## 1.0.0 – 2026-11-02

Erste Version des nativen Players (C++20 · Qt 6.12 · libmpv). Die bisherige Electron-Fassung liegt unverändert in `legacy-electron/`.

### Wiedergabe
- libmpv (Client- und Render-API, OpenGL) in Qt Quick, Hardware-Dekodierung (`auto-safe`, NVDEC/D3D11VA/DXVA2/Vulkan wählbar)
- Wiedergabeliste: M3U/PLS, Ordner, Suche, Mehrfachauswahl, Doppelklick spielt ab, Kontextmenü, Zufall/Wiederholen, Fortsetzen
- Audio-/Untertitelspuren, Verzögerung, A-B-Schleife, Einzelbild, Geschwindigkeit, Seitenverhältnis, Bildschirmfoto
- Bedienung wie gewohnt: Klick = Pause, Doppelklick = Vollbild, Rad = Lautstärke, Rechtsklick-Menü; 34 editierbare Tastenkürzel mit Konfliktwarnung
- Netzwerkstreams: HTTP(S), HLS, DASH, tcp/udp/rtmp/rtsp/srt, YouTube/Twitch über mitgeliefertes yt-dlp
- **Proxy:** kein / System / manuell (HTTP, SOCKS5 mit Anmeldung). SOCKS5 für mpv über lokale Brücke (nur 127.0.0.1,
  Zufallszugang, Namensauflösung am Proxy), `tcp://` über Weiterleitung, yt-dlp und eigene Downloads ebenfalls; Passwort per DPAPI
- Diagnose-Overlay (Decoder, VO, FPS, verworfene Frames, A/V, Shader-Kette), Medieninformationen

### Ultimate Video Enhancer (Plugin)
- Nativer Port der UVE-Effekte als mpv-GPU-Shader (Grading, Schärfe, HDR-like, Rauschfilter/CAS, Korn, Kanten)
- UVE-Hauptregler **SL/SR/BL/WL** aus der SVG-Kette (BL/WL exakt, SR als Radius-Näherung)
- Presets, Undo/Redo, Zurücksetzen je Regler/Abschnitt, Vorher/Nachher-Split, Preset pro Datei, globales Standard-Preset
- Import/Export von UVE-Profilen (`gvf-settings` 1.10), **Beispielprofil My_Profile.json von GitHub**
- **UVE-LUT-Profile** (4×5-Matrix) und **LUT-Paket-Download von GitHub** (neuestes `LUTsProfiles_v*.zip`, aktuell v3.0 mit 83 Profilen),
  Prüfsummen, ETag-Cache und jsDelivr-Ersatz bei GitHub-Limit
- **LUT-Galerie:** aktuelles Videobild mit jedem LUT-Profil als Vorschau
- 3D-LUT `.cube` (echte 3D-Textur); Shaderfehler deaktivieren nur die Effektstufe, kein schwarzes Bild

### Oberfläche und Sprachen
- Qt Quick, dunkles/helles Schema, Steuerleiste im Stil der Vorgängerversion
- **Sprachen:** Englisch (Voreinstellung), Deutsch, Spanisch, Italienisch, Polnisch, Griechisch, Türkisch – Qt-Übersetzungen
  (`lang/uve_*.ts` → `.qm`), Umschaltung ohne Neustart, eigene Sprachen über Qt Linguist (Anleitung `lang/README.md`)

### Plugin-System
- Versionierte Plugin-API (v1) mit Manifest-Prüfung vor dem Laden, Lebenszyklus, Laufzeit-Aktivierung, Safe Mode nach Abstürzen, Fehlerprotokoll
- Beispiel-Plugin Einschlaf-Timer

### Werkzeuge, Installation
- Update-Prüfung für yt-dlp und libmpv beim Start (Dialog, SHA-256-Prüfung, Überspringen)
- Setup (benutzerbezogen, „Öffnen mit“), portable EXE und portables ZIP – yt-dlp, libmpv und 7zr sind enthalten
- Keine Telemetrie
