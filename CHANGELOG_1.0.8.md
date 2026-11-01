# UVE Manager 1.0.8 – Changelog

Baseline: v1.0.7. Additive extension of the Video2X provider only.

## New

- **Real-ESRGAN and Real-CUGAN through Video2X.** Output Settings → AI Upscale → Video2X has a **Processor** choice:
  - *Anime4K v4 (libplacebo)* – default, exactly the 1.0.7 path (presets, custom GLSL chains, direct-to-resolution).
  - *Real-ESRGAN (ncnn, Vulkan)* – RealESR AnimeVideo v3 (2× / 3× / 4×), Real-ESRGAN+ Anime and Real-ESRGAN+ (4×).
  - *Real-CUGAN (ncnn, Vulkan)* – SE (2× / 3× / 4×), Pro (2× / 3×), No-Denoise (2×), with the noise levels the
    selected model and scale provide (Conservative, No denoise, Denoise 1×–3×).
  The models ship with the installed Video2X (pinned 6.4.0); nothing extra is downloaded.
- Model / Scale / Noise selectors offer only valid combinations; the GPU selector is shared with Anime4K.
- **Test this model** runs a 10-frame 128×72 test job with exactly the selected model, scale, noise level and GPU.
  The same test runs automatically before the first render of a combination. Settings → Video2X lists Real-ESRGAN and
  Real-CUGAN as available / verified separately from the Anime4K verification.
- Neural models have a fixed scale: the output is exactly scale × the UVE render and is verified after Video2X. When
  the Resolution choice differs, the job is refused unless **Resize model output to the Resolution choice** is switched on
  (then the final encode resizes with Lanczos and the job log says so). No silent resize.
- Clear errors for missing model files, GPU out of memory, Vulkan problems and unsupported model/scale combinations
  (checked before rendering), with the advice to change model, scale or GPU. No automatic fallback to another model,
  processor or provider.

## Not included

- **RIFE frame interpolation** is not offered. Its interaction with timeline timing, speed changes, reversed clips,
  transitions and audio sync is not validated, so it stays out of this release instead of risking existing exports.

## Compatibility

- Projects, settings and queued jobs from 1.0.7 have no `video2x.processor` and render with the identical Anime4K
  pipeline (same argv). RVE, non-upscaled exports, effects, timeline, audio, encoders and the shader library are
  unchanged. While Real-ESRGAN / Real-CUGAN is selected, the Anime4K preset stays stored and returns when Anime4K is
  chosen again; the RVE model choice is kept as well.
- The pinned Video2X release (6.4.0) and its installer are unchanged.
- A custom Video2X build without `--realesrgan-model` / `--realcugan-model` / `--noise-level` in its `--help` shows the
  respective processor as unavailable; Anime4K keeps working.

## Implementation

- New adapter `shared/video2x-processors.ts` (`buildNeuralArgs`, capability parsing from `--help`, model table checked
  against the 6.4.0 model files). `buildArgs` for Anime4K is untouched.
- `electron/video2x.ts`: per-combination test jobs and cache, processor status, `prepareJob` dispatch.
- `electron/render.ts`: processor dispatch at the Video2X stage and fixed-scale size handling / verification.
- Optional fields `video2x.processor`, `modelId`, `noiseLevel`, `postResize` (`docs/PROJECT_SCHEMA.md`).

## Tests

- `tests/video2x-processors.test.ts`: capabilities from the 6.4.0 help text and from builds lacking flags, the
  model/scale/noise table, argv snapshots, invalid scale / noise / model ids / paths, unchanged Anime4K argv, 1.0.7
  persistence, unknown values, error classification, and the mock CLI (success, missing model, out of memory, crash,
  Vulkan error, no device, cancel, timeout).
- Self test: Real-ESRGAN single-file render, Real-CUGAN 3× timeline render (exact size and 75 frames, audio kept),
  unsupported combination rejected before rendering, no silent resize, explicit post-resize, missing model files.
  All 1.0.7 Anime4K, RVE and timeline checks still pass.

## Not yet measured

The real Video2X binary was not run for this release (all pipeline tests use the mock CLI with real FFmpeg).
Before quoting speed, measure the same 30-second 1080p60 clip with Anime4K v4 Fast, Real-ESRGAN AnimeVideo v3 2×,
Real-CUGAN SE 2× and RVE AnimeJaNai V2 2× on the same GPU (FPS, peak VRAM, total time, quality on identical frames).
