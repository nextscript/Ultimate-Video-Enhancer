# Funktionsvergleich: VLC 3.0.24 / mpv → UVE Player 1.0.0

| Bereich | VLC (Referenz) | mpv/libmpv | UVE Player (eigene Umsetzung) | Status |
|---|---|---|---|---|
| Start/Architektur | Starter + libvlc + Module (`vlc_entry`) | Bibliothek mit Client-API | Starter (`main.cpp`) + Kern (C++20) + QML-Oberfläche + Plugin-Host | umgesetzt |
| Formate/Codecs | eigene Module | FFmpeg in libmpv | über libmpv (HEVC 10-Bit, AC3/DTS, MKV, AVI …) | getestet: MKV HEVC10+AC3, AVI MPEG-4/MP3, MP4 |
| Hardware-Dekodierung | d3d11va/dxva2 | `hwdec` | `auto-safe` + Auswahl in den Einstellungen, Software-Fallback | getestet: NVDEC |
| Wiedergabeliste | Playlist-Modul, M3U/XSPF | interne Playlist | eigenes Modell: M3U/PLS, Ordner, Zufall, Wiederholen, Kontextmenü, Doppelklick | getestet |
| Untertitel | SRT/ASS, Verzögerung | libass | Spurwahl, externe Dateien, Auto-Laden, Verzögerung | getestet (eingebettet SRT) |
| Audiospuren/-verzögerung | ja | ja | Spurwahl, Verzögerung ±50 ms, Lautstärke bis 150 % | getestet (Spurwechsel AAC→AC3) |
| Geschwindigkeit/Frame-Step | ja | ja | 0.25×–4×, Einzelbild vor/zurück, A-B-Schleife | umgesetzt |
| Vollbild/Mausbedienung | Doppelklick, Rad | – | Klick = Pause, Doppelklick = Vollbild, Rad = Lautstärke, Auto-Ausblenden | UI-Test mit echten Mausereignissen |
| Netzwerk | HTTP/RTSP/… | + ytdl_hook | HTTP(S), HLS, tcp/udp/rtmp/rtsp/srt, YouTube/Twitch über yt-dlp | getestet: YouTube, Twitch, TCP |
| Proxy | HTTP/SOCKS (je Modul) | `http-proxy` (nur HTTP) | Kein/System/Manuell (HTTP, SOCKS5 mit Anmeldung); SOCKS5 für mpv über lokale Brücke (127.0.0.1, Zufallszugang), Auflösung am Proxy; tcp://-Weiterleitung; yt-dlp und eigene Downloads ebenfalls; Passwort per DPAPI | getestet: Twitch + tcp:// über SOCKS5 (alle Verbindungen über Brücke), Unit-Test mit lokalem SOCKS5-Server |
| Tastenkürzel | konfigurierbar | input.conf | 42 Aktionen, editierbar, Konfliktwarnung, Zurücksetzen | Unit-Test |
| Effekte | Videofilter-Module | User-Shader | UVE-Plugin (mpv-User-Shader, 3D-LUT, Presets, Vorher/Nachher) | siehe Parity-Matrix |
| Erweiterungen | Lua-Skripte | Lua/JS-Skripte | C++-Plugins mit versionierter API, Manifest, Safe Mode | Unit-/Selbsttest |
| Updates | WinINet-Updateprüfung | – | Start-Prüfung yt-dlp/libmpv mit Dialog, SHA-256-Prüfung | umgesetzt (manuell geprüft: Check) |
| Datenschutz | – | – | keine Telemetrie, Verlauf abschaltbar | umgesetzt |
