# Changelog

## 1.1.3 – 2026-11-08

### Plugins
- **Plugin windows over the whole player:** plugins can open windows over the entire player, or limit them to the video or the sidebar, where
  the rest of the player stays usable. Sizes can also be limited.
- **Three window modes:** *movable* (drag the title bar, the default), *fixed* and *fill*. Windows always stay inside their area.
- **Plugin settings in Settings:** a plugin's settings page now has its own entry in *Settings*.
- **Remove plugins:** *Plugins → Manage plugins → Remove* asks for confirmation, disables the plugin immediately and deletes it at the next start,
  optionally with its settings and data. This can be undone until then. Bundled plugins can only be disabled.
- For plugin authors: `docs/PLUGIN_API.md` and the example `plugins/examples/modaldemo/`.

### Language switching
- **Menu titles no longer disappear:** after switching the language, *Plugins* or *Audio* could turn blank in the menu bar when the mouse
  moved over them, and stayed blank until the next start. This affected English, German, Spanish, Italian and Polish, depending on the switch.
- **Consistent underlines:** the shortcut letters in the menu bar now stay underlined after a language switch.
- **No more German words in other languages:** the *Repeat* entry in the *Playback* menu and the shuffle message were partly German.
- **Close and Cancel buttons follow the language:** they kept the language the player was started with.
- **Settings stay on the current page** when the list of pages is rebuilt, for example after a language switch.

## 1.1.2 – 2026-11-08

### Live denoising
- **New section *Denoising / noise reduction* under *Effects*:** a dropdown switches between *Off* (default) and four methods while a
  video or stream is playing, without reopening it and without changing the position:
  **Bilateral filter**, **Adaptive bilateral + debanding**, **Temporal denoising** and **NLMeans** (GLSL shader).
- **Controls:** *Strength*, *Preserve details*, *Debanding* (only for the adaptive mode), presets for 480p, 720p, 1080p and 4K, and a
  *Status* line.
- **Works with all effects:** denoising runs before colour and sharpening. *Off* restores the identical picture. In *before/after*
  the left side stays untreated with the GPU modes; Temporal is active on both sides.
- **Temporal uses real neighbouring frames and is meant for light noise:** its strength is limited so that moving objects do not leave a
  visible veil; NLMeans and Adaptive are better for strong noise. It runs as a video filter on the CPU. Hardware decoding uses copy mode while
  it is active and returns afterwards; switching it on or off costs a short moment of picture (about 0.15 s with local files,
  up to about 0.6 s with live streams). The other modes switch without restarting the decoder.
- **Safe fallbacks:** if a shader or filter fails or Temporal is overloaded, the mode falls back to *Bilateral* or *Off* and the
  status says why. Playback and the other effects continue.
- **Your settings stay as they are:** denoising has its own settings and is not part of presets or UVE profiles. Details and
  measurements: `docs/DENOISING.md`.

## 1.1.1 – 2026-11-08

### Player updates through GitHub
- **Check for updates now includes the player:** *Help → Check for updates* compares the installed version and commit
  with GitHub and shows the new version and the new commits.
- **You decide when:** choose *Install now* or *Later*, or skip a version. Commits that are not published as a release yet
  are only shown, because there is no package to install for them.
- **Works beyond the GitHub limit:** the player check does not use the limited GitHub API. yt-dlp and libmpv fall back to
  the release pages when the API limit is reached. Interrupted or rate-limited downloads retry and continue where they
  stopped, and every player package is checked against its SHA-256 sum.

### Plugin updates
- **A new version replaces the old one:** installing a plugin package updates the installed version, even if the old
  version lies in a differently named folder or is a single library. Plugin settings are kept. If the folder is locked,
  the old version stays usable and the update is retried at the next start.
- **New dialog after installing:** it confirms the update and offers *Restart now* or *Later*.

### Picture format
- **"Fill" removed:** the picture format no longer offers *Fill*. *Fit* is the default, and a saved *Fill* setting
  changes to *Fit*.

## 1.1.0 – 2026-11-07

### Stream resolutions with plugins
- **Resolution menu for plugin streams:** when a plugin took over a Twitch or YouTube address (or passed it on unchanged),
  the player emptied the *Resolution* menu. A plugin with the new capability `stream-quality` now keeps the list of
  resolutions the streamer offers and receives the selection in `media.resolve` (see `docs/PLUGIN_API.md`).
- **Plugin switched off:** addresses a plugin passes on unchanged show their resolutions again and honour the selection.
- **HLS2TCP 1.1.0 is required for Twitch through HLS2TCP:** older versions of the plugin keep the previous behaviour.

## 1.0.9 – 2026-11-07

### Audio and subtitle tracks
- **Right-click menu fixed:** choosing an audio track in the video's right-click menu did nothing. The same error also
  affected the subtitle tracks, playback speed, picture format and effect presets in that menu.
- **Preferred languages are respected:** with tracks tagged with a region (e.g. `de-DE`), a file could start with the
  wrong track: the list `de,en` chose an English track over `de-DE` because mpv rates an exact language match above the
  order of the list. The order under *Settings → Preferred audio/subtitle languages* now decides, whichever way a
  language is written (`de`, `ger`, `deu`, `de-DE`). A track you choose yourself stays selected, external subtitle files
  are not replaced.

## 1.0.8 – 2026-11-07

### Lower GPU load during video playback
- **No more permanent copy of the video picture:** the compare transition kept a live copy of the whole video area and
  blended it over the picture on every frame. The copy is now only taken when switching the before/after view and only
  runs while that view is open.
- **New setting *Settings → Video & GPU → Picture scaling*:** *Balanced* (new default) drops mpv's linear-light and
  sigmoid passes and uses lighter scalers, *High quality* restores the mpv defaults (the previous behaviour), *Fast* uses
  bilinear scaling only. *Balanced* changes the picture slightly; choose *High quality* to keep the previous scaling.
- **New decoder option *Direct3D 11 with copy (d3d11va-copy)*:** NVDEC work shows up in Task Manager as high "3D" load of
  the player. This option lowers that value, but does not lower the GPU's power draw and can raise its load at 4K, so it
  is not the default.

## 1.0.7 – 2026-11-06

### Plugins: embedded web views (WebView interface)
- The player now embeds **Qt WebEngine** (Qt 6.12 extension, MSVC x64). Build option `UVE_WITH_WEBENGINE` (on by default;
  build with `-DUVE_WITH_WEBENGINE=OFF` if the component is not installed).
- **New host command `webview.available`** (`hasCommand()` / `call()`): plugins detect embedded web support and may then
  use `import QtWebEngine` in their panel pages (`WebEngineView` for web content, `WebEngineProfile` with `storageName`
  for persistent logins). Existing plugins and the plugin API (version 1) are unchanged.
- The Twitch Chat plugin (`cc.ts3x.twitch-chat` 1.2.0) uses this interface: the official Twitch **embed chat**
  (`twitch.tv/embed/<channel>/chat` – the same chat as play.php/embed pages) is the default view inside the player;
  the native IRC view stays as fallback for older players.

## 1.0.6 – 2026-11-03

### Plugin system: extensions without player updates
New plugins can now extend the player without needing a new player version. The plugin API stays at version 1;
existing plugins keep working.
- **Events and commands by name:** the player sends events (`media.loading`, `media.loaded`, `media.ended`,
  `player.stopped`, `playback.paused`, `language.changed`, `action.triggered`, `app.quit`, …) and plugins call
  commands (`player.load`, `player.state`, `player.seek`, `playlist.add`, `osd.show`, `panel.open`, `mpv.get/set/command`,
  …). New events and commands can be added later without changing the interface; `hasCommand()` lets a plugin check
  what the running player supports.
- **Contributions in `manifest.json`:** pages in the sidebar or as settings pages in the plugin manager, entries in the
  *Plugins* menu, hotkeys (editable under *Settings → Shortcuts*), the plugin's own translations (`lang/*.qm`, switched
  with the player language) and URL handlers (a plugin can take over matching addresses before they are loaded, e.g.
  turn a Twitch link into a `tcp://` stream).
- **Plugin folders** (`plugins/<name>/manifest.json` + library or QML, translations, pages, images) and **pure QML
  plugins** that need no compiler.
- **Install plugin …** in the plugin manager: installs a `.zip` package (also created by Windows PowerShell) or a
  folder; checked for a valid manifest and safe paths, applied on the next start.
- Documentation: `docs/PLUGIN_API.md` lists all events, commands and manifest fields.

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
