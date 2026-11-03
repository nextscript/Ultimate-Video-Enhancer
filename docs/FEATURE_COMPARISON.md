# Feature comparison: VLC 3.0.24 / mpv → UVE Player

| Area | VLC (reference) | mpv/libmpv | UVE Player (own implementation) | Status |
|---|---|---|---|---|
| Startup/architecture | Launcher + libvlc + modules (`vlc_entry`) | Library with client API | Launcher (`main.cpp`) + core (C++20) + QML UI + plugin host | implemented |
| Formats/codecs | own modules | FFmpeg in libmpv | via libmpv (HEVC 10-bit, AC3/DTS, MKV, AVI …) | tested: MKV HEVC10+AC3, AVI MPEG-4/MP3, MP4 |
| Hardware decoding | d3d11va/dxva2 | `hwdec` | `auto-safe` + selection in the settings, software fallback | tested: NVDEC |
| Playlist | playlist module, M3U/XSPF | internal playlist | own model: M3U/PLS, folders, shuffle, repeat, context menu, double-click | tested |
| Subtitles | SRT/ASS, delay | libass | track selection, external files, auto-loading, delay | tested (embedded SRT) |
| Audio tracks/delay | yes | yes | track selection, delay ±50 ms, volume up to 150 % | tested (track switch AAC→AC3) |
| Speed/frame step | yes | yes | 0.25×–4×, frame step forward/back, A-B loop | implemented |
| Fullscreen/mouse control | double-click, wheel | – | click = pause, double-click = fullscreen, wheel = volume, auto-hide | UI test with real mouse events |
| Network | HTTP/RTSP/… | + ytdl_hook | HTTP(S), HLS, tcp/udp/rtmp/rtsp/srt, YouTube/Twitch via yt-dlp | tested: YouTube, Twitch, TCP |
| Network drives | – | – | automatic retries for files that are briefly unreadable (UNC, mapped drives, FUSE mounts) | tested: UNC path |
| Proxy | HTTP/SOCKS (per module) | `http-proxy` (HTTP only) | none/system/manual (HTTP, SOCKS5 with credentials); SOCKS5 for mpv via a local bridge (127.0.0.1, random access token), name resolution at the proxy; tcp:// forwarding; yt-dlp and own downloads too; password via DPAPI | tested: Twitch + tcp:// over SOCKS5 (all connections via the bridge), unit test with a local SOCKS5 server |
| Hotkeys | configurable | input.conf | 42 actions plus plugin hotkeys, editable, conflict warning, reset | unit test |
| Effects | video filter modules | user shaders | UVE plugin (mpv user shaders, 3D LUT, presets, before/after) | see parity matrix |
| Extensions | Lua scripts | Lua/JS scripts | C++ and QML plugins with a versioned API, manifest, events/commands, own pages/menus/hotkeys/translations, ZIP install, Safe Mode | unit/self-test |
| Updates | WinINet update check | – | startup check for yt-dlp/libmpv with dialog, SHA-256 verification | implemented (manually verified: check) |
| Privacy | – | – | no telemetry, history can be disabled | implemented |
