# Architecture

```text
UVEPlayer.exe (app/)
├── main.cpp            Launcher: preload libmpv, single instance, Safe Mode, data folder, load QML
├── PlayerApp           Application core for QML ("app"): opening, playlist, resume, session, updates, self-test
└── qml/                Qt Quick UI (Main, ControlBar, SeekBar, PlaylistPanel, dialogs, context menus)

uvecore (src/, static library)
├── core/       AppSettings (JSON, schema version, atomic), Logger, SafeMode (crash counter), LanguageManager
├── player/     MpvController (client API, state machine), MpvVideoItem (render API → Qt Quick FBO)
├── plugins/    PluginApi.h (contract), PluginManifest (validation before loading), PluginManager (lifecycle,
│               host services, events/commands, contributions), PluginBridge (QML access for plugin pages)
├── rendering/  ShaderChain (mpv "glsl-shaders", error attribution, disabling faulty stages),
│               VideoFilterStage (mpv "vf" stage with frame history, decoder copy mode while active)
└── services/   PlaylistModel, MediaFiles, HotkeyModel, ToolUpdater, AppUpdater, ResumableDownload, SingleInstance, ProxyConfig, ProxyBridge
    └── optical/  OpticalMediaService ("app.disc"), DiscSourceResolver, DiscCapabilities, OpticalDriveEnumerator,
                  DiscNavigationBridge, CdAudioStream (1.1.8)

plugins/
├── uve/                Ultimate Video Enhancer: engine/ (parameters, shader generator, .cube parser), controller, QML panel
└── examples/sleeptimer Reference plugin (host services only)
```

## libmpv integration
- **Thread boundaries:** mpv calls `wakeup` from its own threads; the controller only queues `processEvents` via
  `Qt::QueuedConnection` and then reads with `mpv_wait_event(…, 0)` (non-blocking) on the GUI thread.
- **Commands** are asynchronous (`mpv_command_async`), properties via `mpv_observe_property`.
- **State machine:** `Idle → Opening → Playing ⇄ Paused / Buffering → Ended | Error`
  (`START_FILE`, `FILE_LOADED`, `pause`, `paused-for-cache`, `eof-reached` because of `keep-open=yes`, `END_FILE`).
- **Video output:** `vo=libmpv` + render API (OpenGL). Qt Quick is pinned to the OpenGL RHI;
  `MpvVideoItem` creates the render context on the render thread with the first FBO. Files opened before that are
  deferred (`markRenderReady`); otherwise mpv would disable the video track for lack of an output.
- **Zero-copy:** not promised. Hardware decoding `auto-safe` (NVDEC in testing); whether mpv uses interop or copies
  depends on driver/backend and was not measured.

## Optical media (1.1.8)
Isolated module in `src/services/optical/`; it decodes nothing itself.
- **DiscSourceResolver:** detects DVD/Blu-ray folders (`VIDEO_TS/VIDEO_TS.IFO`, `BDMV/index.bdmv`, `AACS`, `BDMV/JAR`) and images
  (ISO 9660 directory; pure UDF via the volume recognition sequence and the names in the image head). Builds playlist entries
  `uve-disc://<dvd|bluray|cdda>?dev=…&t=…&menu=1&name=…` and the libmpv load plan: `dvd://` / `bd://` / `cdda://` / `uvecdda://`
  with `dvd-device`, `bluray-device`, `cdda-device` and `disc-menu=yes` as **file-local** options of that one `loadfile` (values quoted as
  `%len%value`). Operating-system paths never become MRLs by string concatenation; no shell, no mounting.
- **DiscCapabilities:** asks the loaded libmpv at runtime (`protocol-list`, `option-info/…`, `command-list`, `property-list`,
  `mpv-configuration`). Missing features switch the matching UI off instead of failing later.
- **OpticalDriveEnumerator:** Windows `GetLogicalDrives`/`GetDriveType`/`GetVolumeInformation` (with `SEM_FAILCRITICALERRORS`),
  Linux `/dev/sr*` + `CDROM_DRIVE_STATUS`/`CDROM_DISC_STATUS`, macOS mounted `cd9660`/`udf`/`cddafs` volumes. Runs in a worker thread.
- **CdAudioStream:** for builds without `cdda://` (the Windows build): registers `uvecdda` with `mpv_stream_cb_add_ro`; each track is a
  WAV stream read with `IOCTL_CDROM_READ_TOC`/`IOCTL_CDROM_RAW_READ`. libmpv still demuxes, decodes and outputs.
- **OpticalMediaService:** opens drives/images in the background, adds playlist entries, loads them through `MpvController::load()`
  (same asynchronous queue and deferral until the render context exists), observes `disc-menu-active`, `chapter-list`, `edition-list`
  through the additive `MpvController::observe()`, and forwards `discnav` only through **DiscNavigationBridge** (known actions only,
  disc active, command present). `PlayerApp::loadPath` routes disc entries here before plugin URL resolution, yt-dlp and the proxy.
  A DVD menu shown as a still frame reports end of file in libmpv; the playlist does not advance while the disc menu is active.
- Video stays on `vo=libmpv` → render API → `MpvVideoItem`; ShaderChain/UVE and denoising apply to DVD/Blu-ray frames unchanged.
  Audio CDs have no video track, so no shader runs.

## Frame pipeline (native)
Decode (HW/SW) → optional temporal denoising (video filter) → YUV→RGB (mpv) → **hook MAIN: denoising stage**
(bilateral, adaptive + debanding, NLMeans; optional) → **hook MAIN: UVE passes** (save original · color correction ·
detail/edges/LUT/split) → scaling → mpv color management/tone mapping → output into the Qt FBO.
Details: [DENOISING.md](DENOISING.md).

## Plugins
- Discovery in the user plugin folder (takes precedence) and the program folder: single libraries with an embedded
  manifest, or plugin folders (`manifest.json` + library or QML).
- Name-based events (player → plugin) and commands (plugin → player); contributions from the manifest (pages, menu
  entries, hotkeys, translations, URL handlers). Details: [PLUGIN_API.md](PLUGIN_API.md).

## Error handling
- libmpv cannot be loaded (e.g. missing `vulkan-1.dll`): plain-text message instead of a crash.
- Shader error: `ShaderChain` disables the stage, playback continues, the plugin receives `onShaderError`.
- Plugin crash at startup: Safe Mode after 2 unclean starts in a row (also `--safe-mode`).
- Corrupted `settings.json`: backup `settings.json.corrupt-<time>`, start with defaults.
- Corrupted profile/LUT: rejected with a message, the source file stays unchanged.
- Files on network drives that are temporarily unreadable: automatic retries for about a minute.

## Data
`%APPDATA%\nextscript\UVE Player\` (or `UVE_DATA_DIR`, portable `.\data` with `portable.txt`):
`settings.json`, `crash-guard.json`, `logs\`, `plugins\` (user plugins), `plugin-data\<id>\`,
`tools\<tool>\<version>\` (updates).
