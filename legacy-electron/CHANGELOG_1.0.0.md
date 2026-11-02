# UVE Player 1.0.0 – 2026-11-02

Erste Version.

* Architektur nach VLC 3.0.24, abgeleitet aus der Analyse von `vlc.exe`, `libvlc.dll` und der Module (siehe `docs/ARCHITEKTUR.md`).
* Ultimate Video Enhancer 1.17.5 ist als Plugin integriert:
  * Tampermonkey-kompatible GM-API
  * lokales JSZip, Hash geprüft
  * gemeinsamer Audio-Graph
  * Update-Funktion
* Medien werden über das eigene Schema `uve://` mit HTTP-Range ausgeliefert, sodass Canvas und WebGL die Frames ohne Einschränkung lesen können.
* FFmpeg-Kompatibilitätsmodus mit Remux/Transcode, Fortschrittsanzeige und Cache.
* Wiedergabeliste, Fortsetzen, Sitzungswiederherstellung, A-B-Wiederholung, Einzelbild, Geschwindigkeit.
* Untertitel: SRT/VTT/ASS, automatische Erkennung, Verzögerung.
* Equalizer mit 16 Presets, Lautstärke bis 200 %.
* Bildschirmfoto, Medieninformationen, VLC-Tastenkürzel, deutsches Menü.
* Plugin-Verwaltung mit der Möglichkeit, eigene Userscripts zu installieren.
* Windows-Installer (NSIS) und portable EXE über electron-builder, mit Dateizuordnungen.
* Netzwerkstreams:
  * YouTube und Twitch über yt-dlp, mit Electron als JS-Laufzeit
  * tcp://, udp://, rtmp://, rtsp://, srt://, HLS und DASH über FFmpeg → fMP4 → Media Source Extensions
  * Live-Kante, Spulen in VODs
* Kompatibilitätsmodus in Echtzeit statt Vorab-Umwandlung:
  * MKV mit AC3/DTS-Ton bekommt jetzt Ton
  * HEVC-10-Bit wird direkt kopiert
  * Audiospurwahl, eingebettete Untertitel
  * Spulen über die Keyframe-genaue Startzeit
* Kontextmenüs für Videobild und Wiedergabeliste; Klick aufs Bild pausiert.
* Automatisches Neuverbinden bei Stream-Störungen; bei Decoder-Fehlern (z. B. Twitch) wird neu kodiert.
* Alle FFmpeg-/yt-dlp-Prozesse werden beim Beenden zuverlässig gestoppt.
* Entwicklerwerkzeuge entfernt.
* FFmpeg und yt-dlp werden mitgeliefert. Beim Start sucht der Player nach Updates, fragt per Dialog nach und prüft Downloads per SHA-256.
