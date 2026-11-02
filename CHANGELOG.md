# Changelog

## 1.0.1 – 2026-11-02

### Ultimate Video Enhancer
- **SL – Sharpness** now works as a real unsharp mask, like the UVE SVG path (separable Gaussian, two passes, applied
  before grading): positive = sharper, negative = softer. Previously SL had only a very weak effect and negative values were ignored.
- **SR – Radius** now controls the radius of the unsharp mask (σ 0.7–2.0 px) and of detail sharpening across the whole range.
  Previously it only had an effect for |SR| > 1.
- **Detail sharpening** is clearly visible (3×3 Gaussian detail at radius SR, 0.15 per step); negative values soften the image.
- **Teal & Orange** is now a real split toning: shadows → teal, highlights → orange, midtones stay neutral, skin tones
  are protected. Previously it was only a −5° hue rotation that was barely visible.
- **Denoise section removed** (bilateral noise filter + CAS and the DN grain slider). The noise filter visibly weakened
  the sharpening controls. *Film grain* moved to the *Sharpness* section. Profiles keep `dn`/`gpuBase` for compatibility.

### User interface
- The **window position and size** are saved and restored on the next start, including the maximized state
  (restored only if the window is visible on a connected screen).

### Other
- Translations for all languages updated

## 1.0.0 – 2026-11-02

First release of the native player (C++20 · Qt 6.12 · libmpv). The previous Electron version is kept unchanged in `legacy-electron/`.

### Playback
- libmpv (client and render API, OpenGL) in Qt Quick, hardware decoding (`auto-safe`; NVDEC/D3D11VA/DXVA2/Vulkan selectable)
- Playlist: M3U/PLS, folders, search, multi-selection, double-click to play, context menu, shuffle/repeat, resume
- Audio/subtitle tracks, delay, A-B loop, frame step, speed, aspect ratio, screenshot
- Familiar controls: click = pause, double-click = fullscreen, wheel = volume, right-click menu; 34 editable shortcuts with conflict warning
- Network streams: HTTP(S), HLS, DASH, tcp/udp/rtmp/rtsp/srt, YouTube/Twitch via bundled yt-dlp
- **Proxy:** none / system / manual (HTTP, SOCKS5 with authentication). SOCKS5 for mpv via a local bridge (127.0.0.1 only,
  random access token, name resolution at the proxy), `tcp://` via forwarding, yt-dlp and own downloads as well; password protected with DPAPI
- Diagnostics overlay (decoder, VO, FPS, dropped frames, A/V, shader chain), media information

### Ultimate Video Enhancer (plugin)
- Native port of the UVE effects as mpv GPU shaders (grading, sharpness, HDR-like, noise filter/CAS, grain, edges)
- UVE main controls **SL/SR/BL/WL** from the SVG chain (BL/WL exact, SR as radius approximation)
- Presets, undo/redo, reset per control/section, before/after split, preset per file, global default preset
- Import/export of UVE profiles (`gvf-settings` 1.10), **example profile My_Profile.json from GitHub**
- **UVE LUT profiles** (4×5 matrix) and **LUT package download from GitHub** (latest `LUTsProfiles_v*.zip`, currently v3.0 with 83 profiles),
  checksums, ETag cache and jsDelivr fallback when the GitHub rate limit is hit
- **LUT gallery:** the current video frame previewed with every LUT profile
- 3D LUT `.cube` (real 3D texture); shader errors only disable the effect stage, no black image

### User interface and languages
- Qt Quick, dark/light theme, control bar in the style of the previous version
- **Languages:** English (default), German, Spanish, Italian, Polish, Greek, Turkish – Qt translations
  (`lang/uve_*.ts` → `.qm`), switching without restart, custom languages via Qt Linguist (guide: `lang/README.md`)

### Plugin system
- Versioned plugin API (v1) with manifest validation before loading, lifecycle, runtime activation, safe mode after crashes, error log
- Example plugin: sleep timer

### Tools, installation
- Update check for yt-dlp and libmpv at startup (dialog, SHA-256 verification, skip)
- Setup (per user, "Open with"), portable EXE and portable ZIP – yt-dlp, libmpv and 7zr included
- No telemetry
