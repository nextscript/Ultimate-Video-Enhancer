# UVE Manager 1.0.7 – Changelog

Baseline: v1.0.6.

## New

- **Fast Upscale with Video2X + Anime4K v4.** Output Settings → **AI Upscale** now chooses the provider explicitly:
  **Off**, **REAL-Video-Enhancer** or **Video2X (Anime4K)**. Video2X (K4YT3X, AGPL-3.0) runs the Anime4K v4 GLSL shaders
  (bloc97, MIT) in libplacebo on any Vulkan GPU. It is an optional external program, not part of UVE Manager.
  - Options: preset, scale (2× / 3× / 4×, or the Resolution choice), Vulkan GPU, status line, estimated temporary disk
    space, source → output size.
  - Pipeline: UVE render (unchanged effects, tracks, transitions, audio mix) → lossless FFV1 RGB intermediate →
    Video2X at the final size, lossless → final encode with your encoder / codec / quality / colour settings (one lossy
    encode) → audio and subtitle mux → verification (size, frame count, fps, audio present, A/V length) → atomic rename.
  - Render queue: phase label (`Rendering UVE (1/4)`, `Upscaling – Video2X … (2/4)`, `Encoding (3/4)`, `Muxing (4/4)`,
    `Verifying output`), monotonic progress across the phases, measured FPS and remaining time.
  - **Render 5-second preview** from the playhead through the exact pipeline, cached by timeline range, effects, shaders
    and output settings. The live player keeps showing the UVE look and does not claim to show Anime4K.
- **Settings → AI Upscaling → Video2X:** *Install Video2X 6.4.0* / *Update* / *Remove managed installation*,
  *Select existing executable…*, *Verify installation*, installed version, executable path, Vulkan status and devices,
  shader support, last verification, *Open logs*, *Reset to default*.
  - Installs only the official GitHub release pinned in `shared/video2x-cli.ts` (Windows: `video2x-windows-amd64.zip`,
    Linux: `Video2X-x86_64.AppImage`). License, vendor, size, release/asset ids and SHA-256 are shown before the download;
    the SHA-256 is checked (upstream publishes no checksums or signatures – shown as such). Staged extraction with
    archive-path checks, atomic move into `tools/video2x/<version>`, cancelable, no admin rights.
  - Detection reads `--help` / `--version` of the installed binary instead of trusting documented flags, lists Vulkan
    devices and runs a short synthetic test job – the only proof that Vulkan, libplacebo and the shader work.
    States: Installed / Not installed / Unsupported / Verification failed (with the exact diagnostic).
- **Shader library** (Settings → AI Upscaling → Video2X → Shaders): import one `.glsl`, a multi-file chain or a folder;
  create, rename, duplicate and remove presets; move passes up / down and enable / disable them; copy files into the
  library or reference them read-only; SHA-256 per file with missing / modified detection; source / author / license per
  preset; *Test with Video2X* shows the real libplacebo diagnostics; *Reset to built-in preset*.
  Files are checked for mpv/libplacebo directives, `//!HOOK` passes, a `hook()` function and `//!BIND` dependencies in
  chain order; binary, oversized, non-UTF-8 or script content is rejected. A `.glsl` extension alone is not accepted as proof.

## Platform support

| Platform | Video2X provider |
|---|---|
| Windows x64 | supported with a Vulkan GPU and current driver |
| Linux x64 | supported (AppImage, runs without FUSE via `APPIMAGE_EXTRACT_AND_RUN`) |
| macOS (Intel / Apple Silicon) | shown as unavailable – no tested, distributable build; REAL-Video-Enhancer stays available |

## Safety

- Video2X is started with `spawn(…, { shell: false })` and an argv array; stdin is ignored. Shader names never reach a
  command line. Custom chains are combined into a hash-named file in the UVE data folder; paths Video2X's libplacebo
  filter string could not quote (quotes, control characters, `..`) are refused.
- Imported shader code runs on the GPU: the import shows a warning, nothing is downloaded or run automatically.
- Cancel terminates the Video2X process tree and FFmpeg and removes only app-owned temp files. Sources, external
  shader files and user-selected executables are never deleted.
- A Video2X failure fails the job with its diagnostic; it never falls back to RVE silently.

## Compatibility

- Projects, settings and queued jobs from 1.0.0–1.0.6 keep their meaning: without the new `upscaleProvider` field a
  model id in `aiUpscale` still selects RVE and an empty one means Off. A missing provider is never read as Video2X.
  The project file version stays 2 (`docs/PROJECT_SCHEMA.md`).
- REAL-Video-Enhancer (TensorRT / NCNN / PyTorch), effects, timeline, transitions, audio, encoders, shortcuts and both
  GPU engines are unchanged. The UVE WebGPU / WebGL2 engines never compile imported mpv shaders.
- `tools/video2x` is excluded from the packages like `tools/rve`.

## Tests

- New unit tests: provider discrimination and legacy migration, settings round trip, platform matrix, `--help` capability
  parser (supported / too old / too new / missing options), device list (Vulkan present / absent), argv assembly
  (spaces, Unicode, quotes, long Windows paths, malicious shader names), progress parser, error classification,
  shader directive checks, chain order / missing dependencies / limits.
- Process tests against a deterministic mock CLI (`tests/fixtures/video2x-mock`): argv arrives verbatim, progress is
  monotonic, Vulkan / shader / crash failures, cancel without orphan process or output.
- Self test (`npm run selftest`): full four-stage Video2X job with real FFmpeg (single file and timeline straight to 1080p),
  Vulkan failure without fallback, a dropped frame caught by verification, cancel during Video2X; all RVE and timeline
  checks unchanged.

## Not yet measured

The real Video2X binary has not been benchmarked for this release. Before publishing speed figures, run the same
30-second 1080p60 clip on one GPU with: RVE AnimeJaNai V2 2× TensorRT, Video2X Anime4K v4 Fast, Real-CUGAN (if
available) and AI upscale off, and record GPU / driver, model or shader, output size / fps, UVE stage, upscale stage,
mux and total time, peak VRAM and intermediate disk size. Anime4K is a shader set, not a large neural network: compare
quality on identical frames and in motion, not only the elapsed time.
