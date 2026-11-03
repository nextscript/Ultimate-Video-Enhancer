# Changelog

## 1.0.5 – 2026-11-03

### Fixes
- **Network drives:** files on network drives (mapped drives, UNC paths, RaiDrive/WinFsp mounts) sometimes failed with
  "unrecognized file format" right after opening them from Explorer, because the drive briefly refused reads while
  another process (Explorer, virus scanner) was accessing a not yet cached file. UVE Player now retries automatically
  for about a minute (after 1, 2, 3, 4 s, then every 5 s) and shows the progress on the video; choosing another file
  or stopping cancels the retries. Local files still report errors immediately.

## 1.0.4 – 2026-11-03

### Fixes
- **Explorer hang on "Open with":** the video thumbnail provider could deadlock its Explorer helper process
  (`dllhost.exe`): while libmpv was shutting down on the host thread, one of its reader threads called back into that
  same thread through the file stream. The stuck helper then also blocked Explorer's "Open with" menu. libmpv now runs
  on its own worker thread, the file stream is only touched by the calling thread, and a thumbnail request returns
  after at most 15 seconds.

## 1.0.3 – 2026-11-03

### Fixes
- **Tool updates:** updating libmpv under *Update available* failed with "Temporary folder not writable" on installations
  where the user data folder had no `tools` subfolder yet (every fresh installation). The folder is now created first;
  if an update still cannot write, the message names the folder and the reason.

## 1.0.2 – 2026-11-03

### Windows integration
- **"Open with → UVE Player"** is now registered for **all supported formats** on installation (25 video, 17 audio and
  3 playlist extensions – previously only 19 and only as an optional component). UVE Player also appears under
  *Settings → Apps → Default apps* (RegisteredApplications/Capabilities). Existing default apps stay unchanged.
- **Video thumbnails in Explorer** when UVE Player is the default app: new thumbnail provider `UVEThumbnailer.dll`
  (IThumbnailProvider, frame at ~10 % of the duration via the bundled libmpv, also for MKV/WebM/FLV etc.) with the
  **UVE Player icon in the corner** of the thumbnail. Runs in Explorer's isolated thumbnail process.
- Updating or uninstalling while Explorer holds the thumbnail DLL works **without a reboot**: the installer ends only the Explorer thumbnail helper processes (dllhost) that loaded `UVEThumbnailer.dll`; Windows restarts them on demand.
- The installer now writes to the **64-bit registry view**. Before, NSIS (a 32-bit program) registered the thumbnail
  provider under `WOW6432Node`, where the 64-bit Explorer does not look – thumbnails stayed empty
  ("class not registered"). Leftover 32-bit entries of earlier builds are removed on update.

### Ultimate Video Enhancer
- **Auto Scene Match** (Ctrl+Alt+A, *Effects → Auto*) ported 1:1 from the userscript: frame statistics on a 96×54
  image, cut detection, motion threshold, adaptive analysis rate 2–10 fps, smoothing, strength and white-balance tracking.
  The auto matrix is applied via the mpv video equalizer (early, in the raw image – like `uAutoMatrix`), without shader
  recompilation. The analysis image is a 96×54 sample of the video output, scaled down on the GPU and read back
  asynchronously (PBO); the own equalizer is removed again before the statistics. It never blocks playback
  (mpv `screenshot-raw` blocked the mpv core for 7–20 ms per analysis and made video stutter). Profiles import/export `autoOn`, `autoStrength`, `autoLockWB`.
- **Expert Settings** as a new page in *Settings*: all 11 grading blocks of the userscript (contrast, black/white level,
  highlights, shadows, saturation, vibrance, gamma, sharpen, grain, hue) with the same keys, defaults and ranges; search,
  reset per value/block/all, import/export in the userscript format (`gvf-expert-settings`). Changes apply instantly.
- **Before/after slider (Ctrl+Alt+U)** now shows a draggable divider with a ◀▶ knob and BEFORE/AFTER labels directly on
  the video (like the userscript): **left without any filter** (also without the Auto Scene Match correction), right with
  all effects. Dragging is smooth: mpv renders original and result side by side once, the player composes them at the
  divider on the GPU – before, every slider movement rebuilt mpv's render chain (≈50 dropped frames per 3 s of dragging
  at 4K). Switching the comparison on/off holds the last picture until mpv delivers the new one, so no
  intermediate "double picture" flashes. The orange split line of the shader was removed.

### Fixes
- **Plugin folder** button (*Plugins*) opens the folder with the installed plugins; a second button opens the user
  plugin folder (created on demand). Previously the button pointed to a folder that did not exist and nothing happened.
- **Shortcuts page:** German umlauts were displayed incorrectly (ANSI/UTF-8 mix-up in the shortcut labels).
- **Fullscreen:** the play button of the control bar was cut off at the bottom (the overlay bar was 14 px too low).

### Other
- Translations for all languages updated

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
