# Migration and rollback notes: v1.0.0 → v1.0.1

## Baseline

- v1.0.0 source: `M:\LAPS\UVE Manager`, copied unchanged and committed as tag `v1.0.0-baseline`.
- All v1.0.0 tests were run on the baseline before any change (20/20 unit tests, selftest passed).
- `migration/feature-parity-v1.0.0.json` is generated from the baseline source by `node migration/generate-inventory.mjs`.

  It lists 185 items:
  - every effect/control row of the feature matrix;
  - every shortcut, preference, export setting and main-process API channel;
  - the desktop features.

  The `parity_status` field is derived, not hand-written:

  | Status | Meaning |
  |---|---|
  | `unchanged` | The module has not changed since the baseline. |
  | `unchanged (implementing functions identical to baseline)` | The module changed elsewhere, but the functions that implement this item are byte-identical. |
  | `verified` | The module changed, and a v1.0.1 test covers the item. |
  | `diff-reviewed …` | The change was reviewed in the diff and is described in `differences`. |

## User data

- **Settings:**
  - `settings.json` keeps its schema (`version: 1`); the migration only adds keys.
  - On the first start of a newer version, the old file is copied once to `settings.backup-v<old>.json`.
    `COPYFILE_EXCL` makes sure an existing backup is never overwritten.
  - Verified with `electron . --migration-check=<copy of v1.0.0 data>` (21/21 checks).
- **Projects:** the `.uveproj` schema is unchanged. `export.fps` is optional: projects without it render
  at the source frame rate, as in v1.0.0.
- **Profiles, LUTs and custom filters:** formats and storage locations are unchanged.

## Acceptance tolerances

These were defined before the comparison run (`migration/compare-parity.mjs`):

| Metric | Gate |
|---|---|
| PSNR (Y/U/V average) at matched timestamps | ≥ 45 dB |
| SSIM (All) | ≥ 0.995 |
| Frame count, codec, pixel format, width/height, colour tags, audio stream | identical |
| Duration | within 0.05 s |
| WebGPU start-up validation vs v1.0.0 engine | ≤ 0.1 % of channel values differ by > 3/255, and mean difference < 0.5/255 |

The validation uses an outlier-rate metric rather than a maximum difference. Discrete `feComponentTransfer`
step functions under the auto matrix can flip a value that sits exactly on a step boundary from one level to
the next; this is a float-rounding effect, and the same flips happen between two GPUs running the same engine.

## Results (Windows 10 21H2 LTSC, Ryzen 7 5800X3D, RTX 5060 Ti, driver 617.42, FFmpeg N-116051)

| Case | Engine v1.0.1 | PSNR | SSIM |
|---|---|---|---|
| neutral | WebGPU | ∞ (identical) | 1.00000 |
| default_look | WebGPU | 59.64 dB | 0.99972 |
| heavy_lut | WebGPU | 57.75 dB | 0.99953 |
| auto_scene | WebGPU | 59.63 dB | 0.99972 |
| lut_glsl | WebGL2 (custom GLSL) | ∞ (identical) | 1.00000 |
| hevc_hw_10bit (NVENC) | WebGPU | 53.31 dB | 0.99920 |

## Performance findings

The benchmark is in `migration/benchmark/`.

- **GPU filter time is not the bottleneck.** It takes under 1.2 ms per frame even for the heavy stack at 4K.
- **The limit is delivering decoded RGBA frames into the export worker.** About 16–18 ms per 1080p frame is spent
  waiting for decoded frames, while FFmpeg alone decodes the same file at about 180 fps.
- **A main-process frame server was tried and rejected.** It moved the pipes to the main process and passed
  frames over a MessagePort. Each 8 MB frame cost about 5.6 ms of main-thread copy time, and the job got slower
  (33.4 fps vs 37.9 fps), so v1.0.1 keeps the v1.0.0 pipe path.
- **WebGPU readback is slower than WebGL2's** (mapAsync latency), but it overlaps with the next frame thanks to
  the double-buffered readback.
- **No zero-copy interop is claimed.** Every path is decode → CPU RGBA → GPU upload → GPU filter → readback →
  encoder pipe.

## Rollback

1. **Engine only:** Settings → Rendering → Filter engine → *WebGL2 (v1.0.0)*. Export and preview then run the
   unchanged v1.0.0 engine. Hardware decoding can also be set to *Software*.
2. **Full rollback:**
   - Start v1.0.0 (folder `M:\LAPS\UVE Manager`, or `UVE-Manager-1.0.0-portable.exe`).
   - v1.0.0 ignores the new keys in `settings.json`.
   - To restore the exact old settings, replace `settings.json` with `settings.backup-v1.0.0.json`.
3. **Projects:** a project saved with a custom frame rate opens in v1.0.0 and renders at the source frame rate,
   because v1.0.0 ignores `export.fps`.

## Known limits / untested

- **Untested platforms and GPUs:** AMD AMF, Intel QSV, macOS (Metal/VideoToolbox) and Linux (Vulkan/VA-API)
  have not been run on such hardware. Their candidates are capability-probed and verified at runtime, with
  software fallback.
- **AI upscaling** (REAL-Video-Enhancer TensorRT) needs an NVIDIA GPU on Windows or Linux x64. It is
  unavailable on macOS.
- **Portable FFmpeg download** is Windows-only. macOS/Linux use FFmpeg from `PATH` or a configured folder.
