# UVE Player 1.0.0

Ein Medienplayer für Windows, aufgebaut nach dem Vorbild von VLC: schlanker
Launcher, Kern, Plugin-Host und austauschbare Oberfläche. Als Plugin ist
**[Ultimate Video Enhancer](https://github.com/nextscript/Ultimate-Video-Enhancer)**
fest integriert. Es liefert Schärfe, HDR, Farb-Boost, LUTs, Profile, GPU-Modus,
Scopes und den Vorher/Nachher-Vergleich in Echtzeit, auch für lokale Dateien.

## Starten

```powershell
npm install
node node_modules\electron\install.js   # nur nötig, wenn npm Install-Skripte blockiert
npm start                                # Entwicklungsstart
npm run dist                             # Installer + portable EXE nach release\
```

## Funktionen

* **Kompatibilitätsmodus in Echtzeit** – Der Player prüft jede lokale Datei
  per ffprobe.
  * Kann Chromium Container, Video oder Ton nicht, läuft die Datei über FFmpeg:
    Video wird umverpackt (H.264, HEVC inklusive 10-Bit, VP9, AV1) oder nach
    H.264 gewandelt, Ton AC3/E-AC3/DTS/TrueHD → AAC (5.1 bleibt erhalten).
    Typischer Fall: MKV mit AC3-Ton.
  * Die Wiedergabe startet sofort, Spulen geht auch weit entfernt (~2–3 s).
  * Audiospur und eingebettete Untertitel sind wählbar. Erzwungene Untertitel
    werden wie bei VLC automatisch angezeigt.
* **Kontextmenüs**
  * Rechtsklick aufs Bild: Wiedergabe, Audiospur, Untertitel, Geschwindigkeit,
    Bild/Seitenverhältnis, UVE, A-B, Medieninfo
  * Rechtsklick in der Wiedergabeliste: Abspielen, Als Nächstes, Entfernen,
    Verschieben, Explorer, Pfad kopieren usw.
* **Klick aufs Bild** = Wiedergabe/Pause, Doppelklick = Vollbild
* **Wiedergabe**
  * MP4, WebM, MKV, MOV, MP3, AAC, FLAC, Opus u. a. laufen direkt mit
    Hardware-Dekodierung.
  * AVI, WMV, FLV, MPEG-2, AC3 usw. laufen über den FFmpeg-Kompatibilitätsmodus.
    Er verpackt verlustfrei um oder kodiert nach H.264/AAC.
* **Wiedergabeliste**
  * Drag & Drop, Sortieren, Suche, Mehrfachauswahl
  * Zufall und Wiederholen (alle/eins)
  * M3U/PLS laden und speichern
  * Ordner rekursiv öffnen
  * Sitzung wird wiederhergestellt
* **Fortsetzen** – merkt sich die Position pro Datei.
* **A-B-Wiederholung, Einzelbild, Geschwindigkeit** 0,25×–4×, Sprung zu einer Zeit.
* **Untertitel**
  * SRT/VTT werden automatisch erkannt (gleicher Dateiname).
  * ASS/SSA laufen über FFmpeg.
  * Eingebettete Textuntertitel im Kompatibilitätsmodus
  * Verzögerung mit G/H einstellbar
* **Audio**
  * 10-Band-Equalizer mit 16 VLC-Presets
  * Lautstärke bis 200 % mit Limiter
  * Wechsel der Audiospur
* **Bild** – Seitenverhältnis (Einpassen/Füllen/Strecken/Original),
  Bildschirmfoto mit UVE-Effekten (landet in *Bilder\UVE Player*).
* **Vollbild, minimale Oberfläche** mit automatischem Ausblenden,
  *Immer im Vordergrund*.
* **Netzwerkstreams** (*Medien → Netzwerkstream öffnen*, Strg+N):
  * **YouTube und Twitch** (Videos, Livestreams, VODs) und über 1000 weitere
    Seiten über yt-dlp, bevorzugt H.264 bis zur eingestellten Auflösung
  * **tcp://, udp://, rtmp://, rtsp://, srt://** sowie HLS (`.m3u8`) und DASH
    über FFmpeg
  * direkte Medien-URLs (MP4/WebM/MP3) über den Proxy
  * Live-Anzeige, automatisches Aufholen zur Live-Kante, Spulen in VODs
  * UVE-Effekte inklusive GPU-Modus funktionieren bei allen Quellen
* **VLC-kompatible Tastenkürzel** (F1 zeigt die Übersicht).
* **Plugins**
  * Aktivieren/deaktivieren, UVE aktualisieren
  * Weitere Userscripts installieren
  * Plugin-Daten exportieren oder zurücksetzen

## Mitgelieferte Werkzeuge: FFmpeg und yt-dlp

Installer und portable EXE bringen beide Werkzeuge mit (`tools\ffmpeg\`,
`tools\yt-dlp\`). Der Nutzer muss nichts zusätzlich installieren.

* **FFmpeg** (BtbN-Build, GPL) übernimmt Netzwerkstreams, den
  Kompatibilitätsmodus und die Medieninformationen.
* **yt-dlp** löst YouTube, Twitch und über 1000 weitere Seiten auf. Als
  JavaScript-Laufzeit für YouTube dient das in Electron enthaltene Node.js,
  ein zusätzliches Deno ist nicht nötig.

**Automatische Updates:** Nach jedem Start fragt der Player GitHub nach neuen
Versionen. Gibt es eine, öffnet sich ein Dialog: *Jetzt aktualisieren*,
*Später* oder *Diese Versionen überspringen*.

* Downloads werden per SHA-256 gegen die offiziellen Prüfsummen geprüft.
* Updates landen versioniert unter `%APPDATA%\UVE Player\tools\`. So bleiben sie
  auch bei der portablen Version erhalten, die bei jedem Start neu entpackt
  wird. Verwendet wird jeweils die neueste vorhandene Version, ohne Neustart.
* Manuell geht es über *Hilfe → Nach Updates für FFmpeg und yt-dlp suchen…*.
* Abschalten lässt es sich unter *Werkzeuge → Einstellungen*.

Eigene Pfade für beide Werkzeuge lassen sich ebenfalls unter *Werkzeuge →
Einstellungen* setzen. Dort stellst du auch die maximale Stream-Auflösung und
Browser-Cookies ein (für altersbeschränkte oder angemeldete Inhalte).

Netzwerkstreams laufen so: Quelle → (yt-dlp) → FFmpeg → fragmentiertes MP4 →
`uve://app/live` → Media Source Extensions → Video-Element mit UVE.
H.264/AAC wird nur umverpackt, andere Codecs werden nach H.264/AAC umgewandelt.

## Ultimate Video Enhancer

| Kürzel | Funktion |
|---|---|
| Strg+Alt+I | Einstellungen / Import-Export |
| Strg+Alt+G | Grading-HUD |
| Strg+Alt+U | Vorher/Nachher-Vergleich |
| Strg+Alt+S | Scopes-HUD |
| Strg+Alt+P / C / A / X | HDR / Profil / Auto-Scene-Match / GPU-Modus |
| F8 | Nächstes Profil |

Dieselben Funktionen erreichst du über den **UVE**-Button in der Steuerleiste
und über *Video → Ultimate Video Enhancer*. Beispielprofile, `NaturalBoost.cube`
und Beispiel-Filtercodes liegen in `plugins\uve\extras\`.

Mitgelieferte UVE-Version aktualisieren: `npm run update-uve` oder im Player
unter *Werkzeuge → Plugins*.

## Projektstruktur

```
src/main/          Main-Prozess (Launcher, Protokoll, Plugin-Host, GM-Bridge, FFmpeg, Menü)
src/preload/       abgesicherte IPC-Brücke
src/renderer/      Oberfläche, Player-Kern, Playlist, Plugin-Loader, GM-Shim
plugins/uve/       Ultimate Video Enhancer (Userscript + JSZip + Extras)
scripts/           check, update-uve, make-icon
docs/              Architektur & VLC-Analyse
tools/ffmpeg/      optional: ffmpeg.exe / ffprobe.exe
```

Prüfen: `npm run check` (Syntax) und `npm run selftest`. Der Selbsttest lädt UVE
und prüft Wiedergabe, Canvas-Readback und Plugin-Status. Er läuft mit einem
eigenen Datenordner.

Details zur Architektur und zur VLC-Analyse: [docs/ARCHITEKTUR.md](docs/ARCHITEKTUR.md)
