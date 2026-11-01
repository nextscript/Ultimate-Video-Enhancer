# UVE Manager 1.1.1

Desktop video editor and offline renderer built on the Ultimate Video Enhancer effect chain –
for Windows, macOS and Linux. Since 1.0.3 the editor is a multi-track timeline.

## Downloads

All packages are attached to the [GitHub release](https://github.com/nextscript/Ultimate-Video-Enhancer/releases/tag/1.1.1)
and built automatically by `.github/workflows/release.yml`:

| System | Package |
|---|---|
| Windows x64 | `UVE-Manager-1.1.1-setup.exe` (installer) or `UVE-Manager-1.1.1-portable.exe` (portable, ships FFmpeg) |
| macOS Apple Silicon | `UVE-Manager-1.1.1-mac-arm64.dmg` / `.zip` |
| macOS Intel | `UVE-Manager-1.1.1-mac-x64.dmg` / `.zip` |
| Linux x64 | `UVE-Manager-1.1.1-linux-x86_64.AppImage` / `UVE-Manager-1.1.1-linux-x64.tar.gz` |

- **macOS:** the app is not notarized – open it the first time with right click → *Open*
  (or `xattr -cr "/Applications/UVE Manager.app"`). FFmpeg must be installed, e.g. `brew install ffmpeg`.
- **Linux:** FFmpeg must be installed (`sudo apt install ffmpeg` or the package of your distribution).

## AI Upscaling (REAL-Video-Enhancer)

Settings → AI Upscaling installs [REAL-Video-Enhancer](https://github.com/TNTwise/REAL-Video-Enhancer) as a
separate portable tool. Since 1.0.6 it works with **NVIDIA, AMD, Intel and Apple GPUs** – choose the backend there:

| Backend | GPUs | Systems | Download |
|---|---|---|---|
| TensorRT | NVIDIA RTX 20 or newer (fastest on NVIDIA) | Windows, Linux | ~8–16 GB |
| NCNN (Vulkan) | AMD, Intel, NVIDIA, Apple | Windows, Linux, macOS (Intel + Apple Silicon) | ~400 MB |
| PyTorch | CUDA (NVIDIA), ROCm (AMD, Linux), XPU (Intel Arc), MPS (Apple Silicon) | depends on the build | ~3–10 GB |

The recommended backend for your graphics card is preselected. Then choose **REAL-Video-Enhancer** under Output
Settings → **AI Upscale** and pick a model; models are downloaded on the first render.

## Fast Upscale with Video2X + Anime4K (1.0.7)

Output Settings → **AI Upscale** has a second provider: **Video2X (Anime4K)**. [Video2X](https://github.com/k4yt3x/video2x)
runs the [Anime4K v4](https://github.com/bloc97/Anime4K) GLSL shaders in libplacebo on any Vulkan GPU – much faster
than the neural-network models of RVE and well suited to animation, but Anime4K is a set of real-time shaders and
restores less detail than a large model. No speed figures are claimed until they are measured.

| System | Video2X provider |
|---|---|
| Windows x64 | supported (Vulkan GPU + current driver) |
| Linux x64 | supported (AppImage, Vulkan) |
| macOS | **not available** in 1.0.7 (no tested build) – REAL-Video-Enhancer stays available |

- **Settings → AI Upscaling → Video2X**: *Install Video2X 6.4.0* downloads the official release (SHA-256 checked) into
  `tools/video2x`, or *Select existing executable…* uses your own build. *Verify installation* runs a short test job –
  only that proves Vulkan, libplacebo and the shaders work.
- **Presets:** *Fast – Anime4K v4 (Mode A)* (default), *Balanced – Anime4K v4 (Mode A+A)* and the other Anime4K modes
  the installed Video2X offers, plus **custom GLSL**: import mpv-compatible `.glsl` shaders (one file, a multi-file chain or
  a folder), order and enable passes, test them with Video2X.
- **Render 5-second preview** renders from the playhead through the exact pipeline (the live player does not show Anime4K).
- **1.0.8 – more Video2X processors:** the Video2X section has a **Processor** choice: *Anime4K v4* (default, unchanged),
  **Real-ESRGAN** (RealESR AnimeVideo v3 2×/3×/4×, Real-ESRGAN+ / + Anime 4×) and **Real-CUGAN** (SE 2×/3×/4×, Pro 2×/3×,
  No-Denoise 2×, with noise level: no denoise or denoise 1×–3×). They run as ncnn models on any Vulkan GPU through the installed Video2X – no extra
  download. Only combinations the installed Video2X offers are selectable; each is checked with a 10-frame test job
  before its first render (*Test this model*). The model scale is fixed: a different Resolution is only applied when
  *Resize model output to the Resolution choice* is switched on. RIFE frame interpolation is not included.
- Render time = UVE render + Video2X + final encode (not zero-copy). The intermediates are lossless; the free disk space
  needed is shown. Your encoder, codec, quality and frame rate settings are kept; Video2X renders straight to the chosen
  resolution.

## Editing

- **Open Video** (or drop files on the window) adds video and audio files to the media pool and to the end of
  the timeline. A video's picture goes on a video track and its sound becomes its own clip on an audio track,
  so picture and sound can be cut and synced independently. Audio files (mp3, wav, m4a, flac, ogg, …) go on
  audio tracks.
- **Tracks:** any number of video and audio tracks with mute, solo, hide, lock, gain and pan. Drag the lower
  edge of a track header to change its height.
- **Clips:** drag to move (snaps to 00:00, clip edges and markers while dragging), drag an edge to trim,
  fade handles, speed, reverse, 13 transitions, per-clip look.
- **Marking a range:** hold the left button and drag over the tracks or in the time ruler (on clips:
  Shift+drag). Delete, ripple delete, copy, cut and paste then work on the marked range.
- **Right-click** a track or clip for every editing command.
- **Mouse wheel** zooms the timeline around the pointer; Shift+wheel scrolls sideways, Alt+wheel changes the
  track height.
- **Scopes:** Enhancement Settings → Basic → Scopes shows histogram, parade, waveform and vectorscope over the
  player.
- **Render timeline** renders all tracks, transitions and the audio mix into one file. Preview and export use
  the same rules on both GPU engines (WebGPU and WebGL2).

## Keyboard shortcuts

| Action | Windows / Linux | macOS |
|---|---|---|
| Undo one step / Redo | Ctrl+Z / Ctrl+Shift+Z (also Ctrl+Y) | ⌘Z / ⇧⌘Z |
| Cut / Copy / Paste (selected clip or marked range) | Ctrl+X / Ctrl+C / Ctrl+V | ⌘X / ⌘C / ⌘V |
| Mark the whole timeline | Ctrl+A | ⌘A |
| Marking start / end at the playhead | I / O | I / O |
| Clear marking, close scopes | Esc | Esc |
| Split at the playhead / ripple trim to the playhead | Q / Shift+Q | Q / ⇧Q |
| Delete (lift) / ripple delete | Delete / Shift+Delete | ⌫ / ⇧⌫ |
| Duplicate clip | Shift+D | ⌘D |
| Move selected clip 1 / 10 frames | Alt+← → / Alt+Shift+← → | ⌥← → / ⌥⇧← → |
| Play / Pause | Space | Space |
| Previous / next frame | ← / → | ← / → |
| 1 second back / forward | Shift+← / Shift+→ | ⇧← / ⇧→ |
| Start / end | Home / End | Home / End |
| Previous / next edit point | PageUp / PageDown | PageUp / PageDown |
| Previous / next marker | Shift+PageUp / Shift+PageDown | ⇧PageUp / ⇧PageDown |
| Add marker / dissolve / toggle snapping | M / T / S | M / T / S |
| Zoom in / out / fit | + / − / Z | + / − / Z |
| Add video / audio track | Ctrl+Shift+V / Ctrl+Shift+A | ⇧⌘V / ⇧⌘A |
| Save / Open / Render timeline | Ctrl+S / Ctrl+O / Ctrl+R | ⌘S / ⌘O / ⌘R |
| Original / Enhanced wipe | Ctrl+Alt+U | ⌃⌥U |
| Look toggles, HUD, Scopes, Auto-Scene-Match … | Ctrl+Alt+key | ⌃⌥key (Control+Option) |

All shortcuts can be changed in Settings → Keyboard shortcuts.

## Documentation

- `docs/README.md`: user and developer documentation
- `docs/BUILD.md`: build instructions per platform and the release workflow
- `docs/PROJECT_SCHEMA.md`: the `.uveproj` project format (schema version 2, timeline)
- `docs/TRANSITIONS.md`: transition geometry shared by preview and export
- `CHANGELOG_1.1.1.md`: changes since 1.1.0 (Real-ESRGAN / Real-CUGAN fixes for the real Video2X build)
- `CHANGELOG_1.1.0.md`: changes since 1.0.9 (Real-CUGAN noise-level fix; summary of the Video2X releases)
- `CHANGELOG_1.0.9.md`: changes since 1.0.8 (Video2X: Install as the first step)
- `CHANGELOG_1.0.8.md`: changes since 1.0.7 (Real-ESRGAN and Real-CUGAN through Video2X)
- `CHANGELOG_1.0.7.md`: changes since 1.0.6 (Video2X / Anime4K fast upscale provider, custom GLSL shader library)
- `CHANGELOG_1.0.6.md`: changes since 1.0.5 (AI Upscaling for AMD, Intel and macOS: NCNN / PyTorch backends)
- `CHANGELOG_1.0.5.md`: changes since 1.0.4 (AI Upscaling installation fix)
- `CHANGELOG_1.0.4.md`: changes since 1.0.3 (shortcuts, ruler marking, live snapping)
- `CHANGELOG_1.0.3.md`: changes since 1.0.2 (multi-track timeline)
- `CHANGELOG_1.0.2.md`: changes since 1.0.1
- `CHANGELOG_1.0.1.md`: changes since 1.0.0
- `migration/`: feature-parity inventory, parity comparison, capability probe, benchmark, migration/rollback notes
- `UVE_DESKTOP_V1.0.1_CROSS_PLATFORM_GPU_MIGRATION.md`: the specification of the cross-platform GPU release
