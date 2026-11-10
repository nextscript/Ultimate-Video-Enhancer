# UVE Player 1.1.8

Native media player for Windows built with **C++20, Qt 6, and libmpv** – featuring **Ultimate Video Enhancer** as a GPU effects plugin, network streaming (YouTube, Twitch, HLS, tcp://) with proxy support, and seven languages.

## Download

| File | Description |
|---|---|
| `UVE-Player-1.1.8-setup.exe` | Per-user installer (no administrator privileges required), Start Menu integration, optional desktop shortcut, "Open with" for all supported formats, and Explorer video thumbnails with the UVE icon when UVE Player is the default app |
| `UVE-Player-1.1.8-portable.exe` | Standalone executable that runs without installation; settings are stored in the adjacent `UVE Player Data` folder |
| `UVE-Player-1.1.8-portable.zip` | Portable folder archive (data stored in `.\data`) |
| `SHA256SUMS.txt` | SHA-256 checksums |

All versions include libmpv, yt-dlp, and 7zr – no additional software installation is required.

**Requirements:** Windows 10/11 x64 with an up-to-date graphics driver (libmpv requires `vulkan-1.dll` provided by the driver).

## Features

- **Playback:** Supports nearly all media formats through libmpv/FFmpeg, hardware decoding, playlist management (M3U/PLS, folders, search, drag & drop), resume playback from the last position, audio/subtitle tracks and delay adjustment, A-B looping, frame stepping, playback speed control, screenshots, diagnostic overlay, and media information.
- **GPU load (1.0.8):** *Settings → Video & GPU → Picture scaling* selects Balanced (default), High quality (mpv defaults) or Fast.
- **Controls:** Single-click to pause, double-click for fullscreen, mouse wheel for volume adjustment, right-click context menu, and customizable keyboard shortcuts.
- **Window:** Position, size, and maximized state are remembered between sessions.
- **Network Streaming:** YouTube/Twitch (via yt-dlp, YouTube playlists start at the video given by `index=`), HTTP(S), HLS, DASH, TCP/UDP/RTMP/RTSP/SRT; automatic update checks for yt-dlp and libmpv.
- **Optical media (1.1.8):** DVD-Video, unencrypted Blu-ray and audio CDs from a drive, plus DVD/Blu-ray ISO images and `VIDEO_TS`/`BDMV` folders (*File → Open disc…*). DVD/Blu-ray menus with keyboard and on-screen navigation where the bundled libmpv supports them (Windows build: DVD menus and Blu-ray HDMV menus; no BD-J); otherwise the main title plays. Encrypted Blu-rays are not supported. No physical drive was available for testing – see [TEST_REPORT](docs/TEST_REPORT.md#optical-media-118).
- **Updates:** *Help → About UVE Player* checks for updates when it opens (like Chrome) and installs a release from there.
- **Proxy Support:** None / Windows System / Manual (HTTP or SOCKS5, optional authentication) – applies to streams, yt-dlp, and downloads.
- **Ultimate Video Enhancer:** Color correction, sharpening (SL/SR unsharp mask, detail sharpening), Teal & Orange split toning, HDR-like enhancement, film grain, edge enhancement, BL/WL controls; **Auto Scene Match** (Ctrl+Alt+A); **Expert Settings** for every grading curve; presets, before/after comparison with a draggable on-video divider (Ctrl+Alt+U), undo support; UVE profile import/export; **example profiles and LUT packages downloaded directly from GitHub**; **LUT Gallery** with previews based on the current video frame; `.cube` 3D LUT support.
- **Live denoising (1.1.2):** *Effects → Denoising / noise reduction* switches between Bilateral, Adaptive + debanding, Temporal and NLMeans during playback ([docs/DENOISING.md](docs/DENOISING.md)).
- **Languages:** English (default), Deutsch, Español, Italiano, Polski, Ελληνικά, Türkçe – switch languages under *Settings → General*; add custom languages using [lang/README.md](lang/README.md).
- **Plugins:** Versioned Plugin API with manifest validation and Safe Mode; plugins extend the player without player updates – own pages, menu entries, hotkeys, translations, URL handlers, pure QML plugins and ZIP installation ([docs/PLUGIN_API.md](docs/PLUGIN_API.md)). Since 1.1.3: movable plugin windows over the whole player or limited to the video or sidebar, plugin pages in *Settings*, and removing plugins. Since 1.1.5: show or hide single plugin pages in the sidebar, or move them to their own page in *Settings*.
- **Embedded web views (1.0.7):** plugin panels can show web pages inside the player via Qt WebEngine – detected with the host command `webview.available`; `WebEngineProfile` keeps plugin logins (e.g. Twitch chat) persistent. The Twitch Chat plugin shows the official `twitch.tv/embed` chat directly in the player.
- **Privacy:** No telemetry.

## Quick Controls

| Key | Action | Key | Action |
|---|---|---|---|
| Space | Play/Pause | F / Double-click | Fullscreen |
| ← / → | Seek backward/forward | ↑ / ↓ | Volume |
| N / P | Next/Previous track | V / B | Subtitle/Audio track |
| Ctrl+Alt+E | Toggle effects | Ctrl+Alt+U | Before/After comparison |
| Ctrl+Shift+D | Open disc | Ctrl+M | Disc menu (DVD/Blu-ray) |
| Ctrl+L | Toggle sidebar | F1 | Customize keyboard shortcuts |

## Application Data

User settings, logs, presets, and language files are stored in:

`%APPDATA%\nextscript\UVE Player\`

For portable versions, data is stored alongside the executable.

**Launch without plugins (Safe Mode):**

`UVEPlayer.exe --safe-mode`

## Build from Source

Quick instructions (see [docs/BUILD.md](docs/BUILD.md) for details):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=M:/LAPS/Qt/6.12.0/msvc2022_64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release
powershell -ExecutionPolicy Bypass -File scripts\package.ps1     # Setup, portable EXE, ZIP → release\
```

Qt 6.12 with the **WebEngine** component enables plugin web views; build with `-DUVE_WITH_WEBENGINE=OFF` if it is not installed. Mixed Qt 6.12.0 installations (preview WebEngine DLLs + GA base) are reconciled automatically by `scripts\fix-qtwebengine-abi.ps1` during packaging.

## Documentation

[ARCHITECTURE](docs/ARCHITECTURE.md) · [PLUGIN_API](docs/PLUGIN_API.md) · [BUILD](docs/BUILD.md) · [TEST_REPORT](docs/TEST_REPORT.md) · [UVE_PARITY_MATRIX](docs/UVE_PARITY_MATRIX.md) · [FEATURE_COMPARISON](docs/FEATURE_COMPARISON.md) · [THIRD_PARTY_LICENSES](docs/THIRD_PARTY_LICENSES.md) · [CHANGELOG](CHANGELOG.md)

## Requirements – Final Implementation Status

| Requirement | Implemented | Tested | Limitations | Reference |
|---|---|---|---|---|
| C++20 + Qt 6 (QML) + libmpv, CMake | Yes | Yes (build, 10 CTest suites) | Built for Windows x64 only | ARCHITECTURE.md, BUILD.md |
| Local playback (MKV/AVI/MP4, HEVC 10-bit, AC3), including hardware decoding | Yes | Yes (NVDEC) | AMD/Intel not tested | TEST_REPORT.md |
| Playlist, double-click playback, context menu, resume playback | Yes | Yes (UI self-test, tst_playlist) | None | app/qml/PlaylistPanel.qml |
| File through Help menus, settings (11 pages), customizable hotkeys with conflict warnings | Yes | Yes (tst_hotkeys, UI) | None | SettingsDialog.qml |
| Audio/subtitle tracks, delay adjustment, diagnostics (decoder, VO, FPS, dropped frames, A/V sync, shaders) | Yes | Yes | None | DiagnosticsOverlay.qml |
| Network streaming: YouTube/Twitch/HLS/TCP | Yes | Yes (live sources) | YouTube format availability depends on yt-dlp | TEST_REPORT.md |
| Proxy (HTTP/SOCKS5, System) for streams, yt-dlp, and downloads | Yes | Yes (live SOCKS5, local test server) | UDP/RTP/RTMP/RTSP/SRT are not routed through the proxy | src/services/Proxy*.cpp |
| Native UVE effects (GLSL), presets, undo, before/after, per-file presets, default presets | Yes | Yes (exact formulas, visually measured effects) | Auto Scene Match, Scopes, and custom filter codes not ported | UVE_PARITY_MATRIX.md |
| UVE profiles, example profile and LUT packages from GitHub, LUT Gallery | Yes | Yes (real downloads, 83 profiles) | SR uses a radius-based approximation | UVE_PARITY_MATRIX.md |
| Shader error handling with fallback instead of black screen | Yes | Yes (ShaderChain) | None | src/rendering/ShaderChain.cpp |
| Plugin API (manifest, lifecycle, Safe Mode, error logging, events/commands, contributions, QML plugins, ZIP install, embedded web views via `webview.available`) | Yes | Yes (tst_manifest, tst_plugins, self-test) | Native plugins execute trusted code; no digital signature verification; web views require the WebEngine build | PLUGIN_API.md |
| Multilingual support (English default, German, Spanish, Italian, Polish, Greek, Turkish) and custom languages | Yes | Yes (tst_i18n, English/Greek UI) | Machine-generated translations; native-speaker review pending | lang/README.md |
| Tool updater (yt-dlp, libmpv) with checksum verification | Yes | Partially (checks verified; libmpv update installation not tested end-to-end) | Updates take effect after restarting | ToolUpdater.cpp |
| Optical media: DVD/Blu-ray from drive, ISO or folder, DVD/HDMV menus, audio CD | Yes (Windows; on Linux/macOS it depends on the distribution's/Homebrew libmpv) | Partially: synthetic DVD (menu start, title menu, titles, chapters, ISO and folder) and synthetic audio CD with the real libmpv; no physical drive, no Blu-ray test disc | With the synthetic DVD, selecting a menu button did not jump into the title (cause open); no BD-J; no encrypted Blu-rays | TEST_REPORT.md |
| Installer, portable EXE, and ZIP with all required tools | Yes | Yes (package build, launch from staging) | Installer not tested on a clean system | scripts/package.ps1 |
| No telemetry, no unsolicited downloads, no DRM circumvention | Yes | Code review | None | THIRD_PARTY_LICENSES.md |

## Licenses

Third-party components (Qt LGPLv3, mpv/libmpv/FFmpeg GPL/LGPL depending on build configuration, yt-dlp Unlicense, 7-Zip LGPL):

[docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md)

Ultimate Video Enhancer © Freak288 / nextscript.
