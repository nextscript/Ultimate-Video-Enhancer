# UVE Manager 1.0.1 – Changelog

Baseline: v1.0.0, git tag `v1.0.0-baseline`. All v1.0.0 features are kept; see
`migration/feature-parity-v1.0.0.json` (185 inventoried items).

## New

- **WebGPU filter engine** (`src/engine/GpuEngine.ts`, WGSL). Chromium's Dawn runs it on D3D12 (Windows),
  Metal (macOS) or Vulkan (Linux). It implements all 85 SVG-filter primitive types of the UVE graph with the
  v1.0.0 semantics: premultiplied alpha, rotated kernels, per-primitive clamping, the W3C turbulence lattice and
  8-bit parity mode.
- **Engine facade** (`src/engine/createEngine.ts`). Preview and export use the same selection:
  - **Automatic:** WebGPU, but only after a start-up validation against the v1.0.0 engine on a graph that
    covers every primitive type. Otherwise the v1.0.0 WebGL2 engine is used.
  - Projects with enabled custom GLSL filters always use the WebGL2 engine.
  - Settings → Rendering → Filter engine: Automatic / WebGPU / WebGL2 (v1.0.0). Includes a diagnostics panel
    showing the selected adapter, the reason for the choice and the validation result.
- **Hardware encoders beyond NVIDIA.** Candidates are NVENC, AMD AMF, Intel QSV, Apple VideoToolbox and VA-API
  (`shared/hw.ts`). Each one is verified with a scratch encode, at start-up and again per job at the real
  size and bit depth. Software is always the fallback.
- **Hardware decoding verified per input file** (cuda / d3d11va / dxva2 / qsv / vaapi / videotoolbox), with
  software fallback. Setting: Settings → Rendering → Hardware decoding (Automatic / Hardware / Software).
- **Output frame rate** in the export settings ("Frame rate (fps)"):
  - Empty keeps the source rate, as in v1.0.0.
  - Accepts e.g. `30`, `29.97`, `59.94` or `24000/1001`, with a preset list.
  - Frames are duplicated or dropped, never interpolated. The duration and the audio stay unchanged.
- **Diagnostics:**
  - Each job logs the engine and adapter, its decoder/encoder decision with the reason, the decoder delivery
    rate and the per-frame stage times (decode-wait, upload, gpu-filter, readback, encode-wait).
  - The render queue shows the engine.
  - The status bar shows the engine and the verified hardware encoder families.
- **Test harness:**
  - `--probe-capabilities=<json>`, `--parity-export=<dir>`, `--benchmark=<json>` and
    `--migration-check=<copy of a data folder>`.
  - `migration/compare-parity.mjs`, `migration/generate-inventory.mjs` and `migration/report-benchmark.mjs`.
- **Cross-platform groundwork** (`electron/platform.ts`): process-tree kill, executable names, tar, and Python
  paths for REAL-Video-Enhancer. There are electron-builder targets for macOS (dmg/zip) and Linux (AppImage/tar.gz).
  **These builds are untested** (see `docs/BUILD.md`).

## Changed

- **"Prefer Hardware" no longer fails.** If no hardware encoder works for the job, v1.0.1 uses the software
  encoder and logs a warning. v1.0.0 stopped the job with an error.
- The export readback is pipelined (two alternating buffers).
- The preview proxy uses any verified hardware H.264 encoder instead of only NVENC.
- On first start, settings migration writes an untouched backup `settings.backup-v1.0.0.json` next to
  `settings.json`. New keys get defaults (`gpuEngine: auto`, `hwDecode: auto`, `defaultExport.fps: ''`).
  Existing values are not changed.

## Fixed

- An export no longer hangs if the encoder process dies mid-job (`writeFrame` used to wait for `drain`
  forever). The job now fails with the encoder's error message.
- A lost GPU device during export is reported as such. A lost device in the preview recreates the canvas and engine.

## Not changed

- The UI layout, shortcuts, project format (`.uveproj`; `export.fps` is optional), profiles, LUT formats,
  custom filters, the AI upscaling pipeline, the portable self-extractor and the update flow.
- No capture-based export of any kind.

## Evidence (Windows 10, RTX 5060 Ti, driver 617.42)

- Unit tests: 27/27 passing. The 20 tests from v1.0.0 are unmodified; 7 are new.
- Selftest: passes, including WebGPU vs v1.0.0 engine export parity (PSNR 52.8 dB), 30 / 29.97 fps output,
  preview proxy, AI upscale, 10-bit/rotation and cancel.
- v1.0.0 vs v1.0.1 exports (6 cases): all pass (PSNR ≥ 53.3 dB, SSIM ≥ 0.9992). Frame counts, codecs, pixel
  formats, colour tags and audio are identical (`migration/parity-v1.0.0-vs-v1.0.1.json`).
- Settings migration on a copy of real v1.0.0 data: 21/21 checks pass.
- Benchmark: `migration/benchmark/windows-rtx5060ti.md`.

**Untested:** AMD and Intel GPUs, macOS and Linux. The code paths exist and are capability-gated, but no
claim is made for them until they have been run on such hardware.
