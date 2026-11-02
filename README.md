# UVE Player 1.0.1

Native media player for Windows built with **C++20, Qt 6 and libmpv** – with the **Ultimate Video Enhancer** as a
GPU effects plugin, network streams (YouTube, Twitch, HLS, tcp://) including proxy support, and seven languages.

## Download

| File | Description |
|---|---|
| `UVE-Player-1.0.1-setup.exe` | Installer for the current user (no administrator rights), Start menu, optional desktop icon and "Open with" |
| `UVE-Player-1.0.1-portable.exe` | Single EXE that runs without installation; settings are stored in the `UVE Player Data` folder next to it |
| `UVE-Player-1.0.1-portable.zip` | Portable folder to extract (data in `.\data`) |
| `SHA256SUMS.txt` | Checksums |

All variants include libmpv, yt-dlp and 7zr – nothing else needs to be installed.
Requirements: Windows 10/11 x64 with an up-to-date graphics driver (libmpv needs `vulkan-1.dll` from the driver).

## Features

- **Playback:** almost every format via libmpv/FFmpeg, hardware decoding, playlist (M3U/PLS, folders, search,
  drag & drop), resume at the last position, audio/subtitle tracks and delay, A-B loop, frame step,
  speed, screenshot, diagnostics overlay, media info
- **Controls:** click = pause, double-click = fullscreen, mouse wheel = volume, right-click menu, editable shortcuts
- **Window:** position, size and maximized state are remembered between sessions
- **Network:** YouTube/Twitch (yt-dlp), HTTP(S), HLS, DASH, tcp/udp/rtmp/rtsp/srt; automatic update check for yt-dlp and libmpv
- **Proxy:** none / Windows system / manual (HTTP or SOCKS5, optionally with authentication) – applies to streams, yt-dlp and downloads
- **Ultimate Video Enhancer:** color grading, sharpness (SL/SR unsharp mask, detail sharpening), Teal & Orange split toning,
  HDR-like, film grain, edges, BL/WL; presets, before/after, undo; import/export UVE profiles; **example profile and LUT packages
  straight from GitHub**; **LUT gallery** with previews on the current video frame; `.cube` 3D LUTs
- **Languages:** English (default), Deutsch, Español, Italiano, Polski, Ελληνικά, Türkçe – switchable under
  *Settings → General*; custom languages: [lang/README.md](lang/README.md)
- **Plugins:** versioned plugin API with manifest validation and safe mode ([docs/PLUGIN_API.md](docs/PLUGIN_API.md))
- **Privacy:** no telemetry

## Quick reference

| Key | Action | Key | Action |
|---|---|---|---|
| Space | Play/pause | F / double-click | Fullscreen |
| ← / → | Seek | ↑ / ↓ | Volume |
| N / P | Next / previous item | V / B | Subtitle / audio track |
| Ctrl+Alt+E | Effects on/off | Ctrl+Alt+U | Before/after |
| Ctrl+L | Sidebar | F1 | Edit shortcuts |

## Data

Settings, logs, presets and user language files: `%APPDATA%\nextscript\UVE Player\`
(portable: next to the EXE). Start without plugins: `UVEPlayer.exe --safe-mode`.

## Building

Short version (details: [docs/BUILD.md](docs/BUILD.md)):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=M:/LAPS/Qt/6.12.0/msvc2022_64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release
powershell -ExecutionPolicy Bypass -File scripts\package.ps1     # setup, portable EXE, ZIP → release\
```

## Documentation

[ARCHITECTURE](docs/ARCHITECTURE.md) · [PLUGIN_API](docs/PLUGIN_API.md) · [BUILD](docs/BUILD.md) ·
[TEST_REPORT](docs/TEST_REPORT.md) · [UVE_PARITY_MATRIX](docs/UVE_PARITY_MATRIX.md) · [FEATURE_COMPARISON](docs/FEATURE_COMPARISON.md) ·
[REA_ANALYSIS](docs/REA_ANALYSIS.md) · [REA_LIMITATIONS](docs/REA_LIMITATIONS.md) · [THIRD_PARTY_LICENSES](docs/THIRD_PARTY_LICENSES.md) ·
[CHANGELOG](CHANGELOG.md)

## Requirements – summary

| Requirement | Implemented | Tested | Limitation | Reference |
|---|---|---|---|---|
| C++20 + Qt 6 (QML) + libmpv, CMake | yes | yes (build, 7 CTest suites) | only Windows x64 built locally | ARCHITECTURE.md, BUILD.md |
| Local playback (MKV/AVI/MP4, HEVC 10-bit, AC3) incl. hardware decoding | yes | yes (NVDEC) | AMD/Intel not tested | TEST_REPORT.md |
| Playlist, double-click, context menu, resume | yes | yes (UI self-test, tst_playlist) | – | app/qml/PlaylistPanel.qml |
| Menus File … Help, settings (11 pages), shortcuts with conflict warning | yes | yes (tst_hotkeys, UI) | – | SettingsDialog.qml |
| Audio/subtitle tracks, delay, diagnostics (decoder, VO, FPS, drops, A/V, shaders) | yes | yes | – | DiagnosticsOverlay.qml |
| Network streams YouTube/Twitch/HLS/tcp | yes | yes (real sources) | YouTube formats depend on yt-dlp | TEST_REPORT.md |
| Proxy (HTTP/SOCKS5, system) for streams, yt-dlp, downloads | yes | yes (live SOCKS5, local test server) | udp/rtp/rtmp/rtsp/srt not proxied | src/services/Proxy*.cpp |
| Native UVE effects (GLSL), presets, undo, before/after, preset per file, default preset | yes | yes (formulas exact, effect measured in the image) | auto scene match, scopes, custom filter codes not ported | UVE_PARITY_MATRIX.md |
| UVE profiles, example profile + LUT packages from GitHub, LUT gallery | yes | yes (real download, 83 profiles) | SR is a radius approximation | UVE_PARITY_MATRIX.md |
| Shader errors → fallback instead of black image | yes | yes (ShaderChain) | – | src/rendering/ShaderChain.cpp |
| Plugin API (manifest, lifecycle, safe mode, error log) | yes | yes (tst_manifest, self-test) | native plugins are trusted code, no signature | PLUGIN_API.md |
| Multiple languages (en default, de, es, it, pl, el, tr) + custom languages | yes | yes (tst_i18n, UI en/el) | translations machine-generated, native-speaker review pending | lang/README.md |
| Tool updater (yt-dlp, libmpv) with checksums | yes | partially (check; libmpv update installation not end-to-end) | update takes effect after restart | ToolUpdater.cpp |
| Setup + portable EXE + ZIP with all tools | yes | yes (packaging, start from staging) | setup not verified on a fresh system | scripts/package.ps1 |
| REA analysis (vlc.exe, libmpv, Electron predecessor) | yes | – | no deep provider (Ghidra/IDA) available | REA_ANALYSIS.md, REA_LIMITATIONS.md |
| Telemetry off, no unsolicited downloads, no DRM circumvention | yes | code review | – | THIRD_PARTY_LICENSES.md |

## Licenses

Third-party components (Qt LGPLv3, mpv/libmpv/FFmpeg GPL/LGPL depending on the build, yt-dlp Unlicense, 7-Zip LGPL):
[docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md). Ultimate Video Enhancer © Freak288 / nextscript.
