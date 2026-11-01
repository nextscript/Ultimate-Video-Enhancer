# UVE Manager 1.0.2

Desktop video look editor and **offline frame-by-frame renderer** built on the effect chain of
[Ultimate Video Enhancer](https://github.com/nextscript/Ultimate-Video-Enhancer) v1.17.5 (userscript by Freak288).

Import a local video → preview it with the UVE looks, grading, LUTs, Auto-Scene-Match and custom SVG/GLSL filters →
compare Before/After → render a **new** video file. The source is never modified. There is no screen, player or
canvas recording anywhere – export decodes the original file with FFmpeg, runs every frame through the same GPU graph
as the preview and encodes the result.

## Quick start

```powershell
npm install
npm start            # build + launch
npm test             # unit tests of the ported effect core
npm run selftest     # end-to-end: generates clips, renders them, verifies the output
npm run fetch-ffmpeg # fill tools\ffmpeg with the portable FFmpeg build
npm run dist         # NSIS installer + portable self-extractor in release\ (ships the tools folder)
npm run dist:portable  # only release\UVE-Manager-<version>-portable.exe
```

## GPU engines and hardware (1.0.1)

- **Preview and export filter engine:** WebGPU (WGSL; D3D12/Metal/Vulkan through Chromium) or the 1.0.0 WebGL2
  engine.
  - In *Automatic* mode, WebGPU is used only after a start-up validation against the 1.0.0 engine passes.
  - Projects with enabled custom GLSL filters always use WebGL2.
  - Choose the engine in **Settings → Rendering**; the diagnostics panel there shows the adapter, the reason for
    the choice and the validation result.
- **Hardware encoders** (NVENC, AMF, QSV, VideoToolbox, VA-API) and hardware decoders are verified on the actual
  job before use. Software is always the fallback.
- **Export → Frame rate (fps):** empty keeps the source frame rate. Values such as 30, 29.97 or 24000/1001
  duplicate or drop frames; there is no interpolation.

Build instructions per platform: `BUILD.md`. Only Windows/NVIDIA has been tested.

## Portable version

`UVE-Manager-<version>-portable.exe` is a self-extractor: at start it asks for a destination folder (e.g. a USB
drive), unpacks the complete app there and offers to start it. No registry entries, no start menu, no admin rights.
The folder contains `portable.txt`, so the app stores all settings, profiles, LUTs and caches in `<folder>\data`
and the tools in `<folder>\tools`. To update, run a newer portable exe and choose the same folder – `data` and
`tools\ffmpeg` are kept. To remove, delete the folder.

## Portable tools

FFmpeg/FFprobe live in `tools\ffmpeg` next to the app (portable – nothing is installed system-wide).
If the folder is empty, UVE Manager offers to download the portable build (BtbN FFmpeg-Builds, win64 GPL with NVENC,
x264, x265, SVT-AV1, zimg). Order of lookup: custom folder from Settings → `tools\ffmpeg` → FFmpeg on `PATH`.

## Updates

At every start (toggle: **Settings → Updates → Check for updates automatically at startup**) UVE Manager checks

* UVE Manager itself (GitHub releases of the repository configured in Settings, default `nextscript/UVE-Manager`),
* the portable FFmpeg build,
* the UVE LUT package (`LUTsProfiles_v3.0.zip`, newer packages are reported).

A dialog asks **Update now** or **Later – keep current version**; nothing is downloaded without confirmation.
"Don't ask again for these versions" skips exactly those versions.

## LUT profiles

`LUTsProfiles_v3.0.zip` (83 gvf-lut-profile matrices in 20 groups) is downloaded automatically on first start
(toggle in the LUT Library). User LUTs: `.cube`, tiled PNG, gvf-lut-profile JSON and ZIP – converted to UVE's
4×5 color matrix exactly like the userscript.

## Shortcuts (rebindable in Settings, active only inside the app)

On macOS, Ctrl+Alt means Control+Option (⌃⌥). The editor commands use Command there:
⌘Z / ⇧⌘Z (Undo / Redo) and ⌘S / ⌘O / ⌘R (Save / Open / Render).

| Key | Action | Key | Action |
|---|---|---|---|
| Ctrl+Alt+B | Base Tone | Ctrl+Alt+A | Auto-Scene-Match |
| Ctrl+Alt+D | Dark & Moody | Ctrl+Alt+W | Auto White Balance |
| Ctrl+Alt+O | Teal & Orange | Ctrl+Alt+U | Before/After wipe |
| Ctrl+Alt+V | Vibrant | Ctrl+Alt+S | Scopes HUD |
| Ctrl+Alt+P | HDR look | Ctrl+Alt+I | Expert Settings |
| Ctrl+Alt+C | Cycle tone profile | Ctrl+Alt+G | Profile / LUT / filter managers |
| Ctrl+Alt+X | Preview quality | Ctrl+Alt+Y | Bypass (preview) |
| Space, ←/→, Home/End | Transport | I / O | In / Out point |
| Ctrl+Z / Ctrl+Y | Undo / Redo | Ctrl+S / Ctrl+O / Ctrl+R | Save / Open / Render |

## Architecture

```
electron/   main process: IPC allow-list, uve-media:// range protocol, FFmpeg (probe, thumbnails, proxy,
            analysis), render queue + hidden export worker, LUT/tools/update management
shared/uve/ the ported UVE core: settings, expert blocks, curves, matrices, graph (buildFilter port),
            auto-scene-match, LUT fitting, profiles, custom filter parsing
src/engine/ WebGL2 SVG-filter interpreter (EffectEngine) shared by preview and export
src/export/ export worker page (decoded frames → engine → encoder)
src/ui/     React UI (editor, inspector, timeline, presets, LUT library, filter studio, scopes, queue, settings)
tests/      unit tests + repository fixtures
```

See `SOURCE_FEATURE_AUDIT.md`, `UVE_FEATURE_MATRIX.csv`, `RENDER_PIPELINE.md`, `FORMAT_SUPPORT.md`,
`EXPORT_PARITY_REPORT.md`, `PROJECT_SCHEMA.md` and `LICENSES.md`.

## AI upscaling (REAL-Video-Enhancer)

Export → **AI Upscale** selects a model of [REAL-Video-Enhancer](https://github.com/TNTwise/REAL-Video-Enhancer)
(Nomos8k SPAN 4×, OpenProteus 2×, RealESR general 4×, AnimeJaNai V2/V3 2×, Spanimation 2×, AniSD 2×, ESRGAN
AnimeVideo V3 / ESRGAN Plus (NCNN), or a custom model). RVE runs as a separate portable tool in `tools/rve`
(portable Python 3.12.9 + RVE backend + the packages of one inference backend), installed from
Settings → AI Upscaling. Since 1.0.6 the backend can be chosen there:

| Backend | GPUs | Systems | Download |
|---|---|---|---|
| **TensorRT** | NVIDIA RTX 20 or newer | Windows, Linux (x64) | ~8–16 GB (PyTorch 2.9 + CUDA 13, TensorRT 10.12) |
| **NCNN (Vulkan)** | AMD, Intel, NVIDIA, Apple (MoltenVK) | Windows, Linux, macOS Intel + Apple Silicon | ~400 MB (ncnn, upscale-ncnn-py) |
| **PyTorch** | CUDA (NVIDIA), ROCm 6.4 (AMD, Linux), XPU (Intel Arc), MPS (Apple Silicon) | per build | ~3–10 GB |

The recommended backend is preselected: TensorRT on NVIDIA, NCNN everywhere else. Several backends can be
installed side by side (only one PyTorch build at a time); **Use … for rendering** switches between installed ones.
Model ids are the same on all backends, so the export setting keeps working after a switch. NCNN models are
folders `models/<name>/<name>.param + .bin` (custom ones: import the `.param` with its `.bin` next to it),
TensorRT/PyTorch use `.pth` / `.safetensors`.

Rendering has three stages:

1. UVE look → lossless video-only intermediate (FFV1, RGB),
2. RVE upscale (`--backend tensorrt|ncnn|pytorch`) + final video encode with the chosen codec/quality/colours (`--custom_encoder`),
3. audio/subtitles from the original muxed in by stream copy (exact frame count, no A/V drift).

With TensorRT the first job per model and resolution builds a TensorRT engine (minutes), later jobs reuse it.
NCNN and PyTorch need no engine build. NCNN needs a current graphics driver with Vulkan support.

## Fast upscale: Video2X + Anime4K (1.0.7)

Export → **AI Upscale** chooses the provider explicitly: **Off**, **REAL-Video-Enhancer** or **Video2X (Anime4K)**. Only
one provider runs per export; a Video2X problem fails the job with its diagnostic and never falls back to RVE.

* **Install** (Settings → AI Upscaling → Video2X): the pinned official release 6.4.0 from
  `github.com/k4yt3x/video2x/releases` (`video2x-windows-amd64.zip` / `Video2X-x86_64.AppImage`). Version, license, vendor,
  size, release/asset ids and SHA-256 are shown before the download; the SHA-256 is pinned in `shared/video2x-cli.ts`
  (upstream publishes no checksums or signatures). The archive is unpacked in a staging folder with path checks and then
  moved to `tools/video2x/<version>`. *Remove managed installation* deletes only that folder; a user-selected executable is
  never deleted. No admin rights are needed. Works offline after installation.
* **Detection**: platform → executable → `--help` / `--version` (the adapter reads the options the binary offers; versions
  older than 6.1 or newer than 6.x are rejected with an upgrade path) → `--list-devices` → a synthetic 10-frame test job
  (128×72 → 256×144). Status: Installed / Not installed / Unsupported / Verification failed, with the exact diagnostic.
  Results are cached per executable (path, size, modification time) and re-checked after an update or a new path;
  *Verify installation* re-runs them after a driver change.
* **Presets**: built-in Anime4K presets are listed only when the installed binary names them in `--help`. Custom presets
  live in `<data>/video2x/shaders` (`library.json`, `files/<sha256>.glsl` copies, `chains/<sha256>.glsl` combined chains).
  Import either copies a file or references it read-only; every file's SHA-256 is recorded and checked before each use
  (missing / modified files are reported, *Accept change* re-checks and re-hashes). Files are checked for known
  mpv/libplacebo directives, `//!HOOK` passes, a `hook()` function and `//!BIND` dependencies in chain order; libplacebo
  compiles them only in a test run, which happens automatically before the first render with a new chain.
* **Preview**: *Render 5-second preview* renders 5 s from the playhead through the real pipeline into
  `<data>/cache/video2x-preview`, cached by timeline, effects, export settings, range and shader hashes.
* Logs: `<data>/logs/video2x.log` (Settings → *Open logs*).

The job runs: UVE render → lossless FFV1 RGB intermediate (1/4) → Video2X Anime4K at the final size, lossless (2/4)
→ final encode with your encoder settings (3/4) → audio/subtitles mux (4/4) → verification (size, frame count, fps,
audio present and A/V length within 0.25 s) → atomic rename. Cancel kills the Video2X process tree and FFmpeg and
removes only app-owned temp files.

## settings.json

All choices are stored in `data\settings.json` (portable version and development; installed copies use `%APPDATA%\UVE Manager`):
`prefs` (all settings incl. shortcuts, update toggle, tools paths), `window` (position, size, maximized) and `editor`
(last effect settings, export settings, open video with position and In/Out range, view, inspector tab, wipe).
Older `prefs.json` / `session.json` files are migrated automatically.
