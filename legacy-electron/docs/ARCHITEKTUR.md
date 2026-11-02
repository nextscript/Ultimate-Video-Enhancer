# UVE Player – Architektur & VLC-Analyse

## 1. Analyse von VLC 3.0.24 (`C:\Program Files\VideoLAN\VLC`)

Die Analyse lief statisch, also ohne VLC auszuführen:

* **REA MCP** `inspect_pe_resources` lieferte den Ressourcenbaum von `vlc.exe`
  (Evidence `ev_e335bb82…`, SHA-256 `63d0d02d…80d7`).
* Die PE-Import- und Exporttabellen (`pefile`) habe ich zusätzlich ausgewertet.
  Den Deep-Decompiler (Ghidra/IDA) konnte REA nicht nutzen, weil kein Provider
  konfiguriert ist.

### 1.1 `vlc.exe` – schlanker Launcher (1 MB, PE32+, x86-64, GUI-Subsystem)

| Bereich | Befund |
|---|---|
| Export | nur `WinMain` |
| Import aus `libvlc.dll` | **genau 7 Funktionen:** `libvlc_new`, `libvlc_set_app_id`, `libvlc_set_user_agent`, `libvlc_add_intf`, `libvlc_playlist_play`, `libvlc_wait`, `libvlc_release` |
| Weitere Importe | `WININET` (Update-Prüfung), `SHELL32` (`CommandLineToArgvW`, `SHGetFolderPathW`), `ADVAPI32` (Registry, CryptGenRandom), `USER32` (nur `MessageBoxW`) |
| Ressourcen | 6 Icons (16–256 px) als Gruppe `VLC_ICON`, `RT_VERSION` (FileVersion 3.0.24), `RT_MANIFEST` |

Daraus ergibt sich der Ablauf: Argumente parsen → `libvlc_new(argc, argv)` →
`libvlc_add_intf(NULL)` (Standard-Oberfläche) → `libvlc_playlist_play` →
`libvlc_wait` blockiert bis zum Beenden → `libvlc_release`. Die Datei enthält
keine Wiedergabe-Logik.

### 1.2 Schichten

```
vlc.exe            Launcher (WinMain, Argumente, Single-Instance)
  └─ libvlc.dll    Öffentliche API (316 Exporte: libvlc_media_*, libvlc_audio_*, libvlc_video_*, …)
      └─ libvlccore.dll   Kern: input, es_out, playlist, vout/aout, config, Module-Bank
          └─ plugins\<kategorie>\lib*_plugin.dll   ~420 Module, je 3 Exporte:
                vlc_entry__3_0_0f, vlc_entry_copyright__3_0_0f, vlc_entry_license__3_0_0f
             (Index-Cache: plugins\plugins.dat, erzeugt von vlc-cache-gen.exe)
```

Modulkategorien (Anzahl): access 37 · codec 56 · demux 47 · video_filter 41 ·
audio_filter 25 · stream_out 20 · video_chroma 19 · video_output 15 ·
packetizer 14 · stream_filter 9 · mux 9 · control 8 · gui 2 (Qt, Skins2) · u. a.
Dazu kommen Lua-Erweiterungen (`lua\extensions`, `intf`, `playlist`, `sd`, `http`).

Die Oberfläche ist selbst nur ein Modul: `libqt_plugin.dll` importiert 172
Funktionen aus `libvlccore` (playlist, var_*, config_*, input_*), also nicht
aus `libvlc`.

## 2. Übertragung auf UVE Player

| VLC | UVE Player | Datei |
|---|---|---|
| `vlc.exe` (Launcher, Single-Instance, Argumente) | Electron-Main-Prozess | `src/main/main.js` |
| `vlcrc` (Konfiguration) | `settings.json` im userData-Ordner | `src/main/settings.js` |
| Access-Module (file, http) | eigenes Schema `uve://app/media` / `/remote` mit HTTP-Range | `src/main/protocol.js` |
| Demux/Codec (libavcodec) | Chromium-Decoder (Hardware); FFmpeg-Fallback für Remux/Transcode | `src/main/transcode.js` |
| Playlist (`vlc_playlist_t`), M3U/PLS | Playlist-Kern und Parser | `src/renderer/js/core/playlist.js`, `src/main/media-registry.js` |
| input/vout/aout, Equalizer, Untertitel | Player-Kern (Video-Element, WebAudio-Graph, TextTracks) | `src/renderer/js/core/player.js` |
| Module-Bank + `vlc_entry` + `plugins.dat` | Plugin-Host + `plugin.json`-Manifeste | `src/main/plugin-host.js` |
| Lua-Erweiterungen | Userscript-/ES-Modul-Plugins | `src/renderer/js/plugins/loader.js` |
| `libqt_plugin` (GUI) | HTML-Oberfläche | `src/renderer/index.html`, `js/app.js` |
| Video-Filter-Module (sharpen, adjust …) | **Ultimate Video Enhancer** (Plugin) | `plugins/uve/` |

## 3. UVE als Plugin

Ultimate Video Enhancer ist ein Tampermonkey-Userscript. UVE Player stellt
dafür eine kompatible Laufzeit bereit:

* **GM-API** (`gm-shim.js` im Renderer, `gm-bridge.js` im Main-Prozess):
  `GM_getValue/SetValue/deleteValue/listValues`, `GM_addValueChangeListener`,
  `GM_info`, `GM_xmlhttpRequest` (läuft über den Main-Prozess, prüft `@connect`,
  unterstützt `responseType` arraybuffer/blob/json), `GM_download`,
  `GM_addElement`, `GM_addStyle`, `unsafeWindow` und `GM.*` (Promise-Variante).
* **`@require`** – JSZip 3.10.1 liegt lokal bei. Der SHA-256-Hash
  `acc7e414…d59e` stimmt mit dem Wert im Userscript-Header überein.
  `requireMap` biegt CDN-URLs auf die lokale Kopie um.
* **Gleicher Ursprung** – Oberfläche, Plugin und Medien laufen alle unter
  `uve://app`. Deshalb werden Video-Frames für Canvas/WebGL nicht „tainted“.
  Ohne das funktionieren GPU-Modus, Scopes und Auto-Scene-Match nicht.
* **Gemeinsamer Audio-Graph** – UVE prüft `if (unsafeWindow.__gvfAudio) return;`.
  Der Player stellt `__gvfAudio` bereit, sodass `createMediaElementSource` nur
  einmal aufgerufen wird. Equalizer, Lautstärke-Boost bis 200 % und die
  UVE-Audiofilter hängen am selben Graphen.
* **Hotkeys** – UVE belegt `Strg+Alt+…` und `F8`. Der Player wertet diese
  Kombinationen nicht aus und ignoriert Tasten, die UVE schon verarbeitet hat
  (`defaultPrevented`). Menü und UVE-Button lösen die UVE-Funktionen über
  synthetische Tastenereignisse aus.
* **Updates** – *Werkzeuge → Plugins → Nach Update suchen* lädt die aktuelle
  Version von GitHub in den Benutzer-Plugin-Ordner. Die mitgelieferte Version
  bleibt als Rückfallebene erhalten.

## 4. Eigene Plugins

```
%APPDATA%\UVE Player\plugins\<id>\plugin.json
{
  "id": "mein-plugin",
  "name": "Mein Plugin",
  "type": "script",            // oder "userscript"
  "main": "main.js",
  "require": []
}
```

Ein `script`-Plugin exportiert `export default function activate(api) { … }`.
`api` enthält `video`, `player`, `playlist`, `osd(msg)`,
`registerAction(name, fn)`, `run(action)` und `version`. Beliebige `*.user.js`
lassen sich auch über *Plugins → Userscript installieren…* einbinden.
