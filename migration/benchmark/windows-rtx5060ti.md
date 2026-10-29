# Benchmark – UVE Manager 1.0.1

Measured 2026-10-29T13:42:21.316Z · win32 · AMD Ryzen 7 5800X3D 8-Core Processor (16 threads) · 63.9 GB RAM
GPU NVIDIA GeForce RTX 5060 Ti, driver 617.42 · FFmpeg N-116051-g0b330d8642-20240629

| Resolution | Workload | Engine | Encoder | Status | FPS | Wall [s] | decode-wait | upload | gpu-filter | readback | encode-wait | CPU % | Peak app RAM [MB] | Peak VRAM total [MB] |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1080p | simple | webgl2 | hevc_nvenc | done | 34.5 | 5.26 | 17 | 0.66 | 0.16 | 4.03 | 6.52 | 35.9 | 878 | 1675 |
| 1080p | simple | webgpu | hevc_nvenc | done | 37.7 | 4.95 | 17.31 | 0.65 | 0.11 | 10.64 | 7.54 | 32.6 | 949 | 1841 |
| 1080p | heavy | webgl2 | hevc_nvenc | done | 30.4 | 5.77 | 16.46 | 0.61 | 0.4 | 8.56 | 6.34 | 28.5 | 937 | 1751 |
| 1080p | heavy | webgpu | hevc_nvenc | done | 37.1 | 4.96 | 17.06 | 0.61 | 0.53 | 23.9 | 7.46 | 36.7 | 976 | 1796 |
| 1080p | lut_glsl | webgl2 | hevc_nvenc | done | 34.4 | 5.21 | 16.85 | 0.63 | 0.34 | 4.37 | 6.4 | 30 | 890 | 1744 |
| 4K | simple | webgl2 | hevc_nvenc | done | 8.8 | 12.21 | 65.2 | 2.66 | 0.36 | 13.44 | 28.33 | 33 | 1577 | 2364 |
| 4K | simple | webgpu | hevc_nvenc | done | 8.6 | 12.66 | 65.1 | 2.51 | 0.43 | 39.15 | 40.48 | 41.1 | 1230 | 2305 |
| 4K | heavy | webgl2 | hevc_nvenc | done | 7.5 | 14.29 | 65.04 | 2.47 | 1.41 | 35.54 | 26.24 | 32.4 | 1449 | 2601 |
| 4K | heavy | webgpu | hevc_nvenc | done | 9.2 | 12.01 | 70.44 | 2.33 | 0.7 | 80.1 | 30.61 | 35.8 | 1292 | 2711 |
| 4K | lut_glsl | webgl2 | hevc_nvenc | done | 8.8 | 12.24 | 66.19 | 2.4 | 0.84 | 15.43 | 26.23 | 31 | 1467 | 2368 |
| 1080p | heavy | webgl2 | libx265 | done | 24.7 | 6.55 | 18.79 | 0.74 | 0.54 | 9.05 | 7.12 | 60.4 | 839 | 1703 |
| 1080p | heavy | webgpu | libx265 | done | 28.2 | 6.06 | 20.77 | 0.71 | 0.66 | 21.95 | 8.38 | 58.7 | 891 | 1630 |

Per-frame stage times in milliseconds (averages over the job).

## Notes

- engineRequested webgl2 = the unchanged v1.0.0 filter engine inside v1.0.1; webgpu = the new WGSL engine.
- Encoder "auto" = verified hardware HEVC encoder when available; "cpu" = libx265 (separates filter speed from encoder speed).
- systemCpuPercent is whole-system CPU usage during the job (includes the FFmpeg decoder/encoder processes).
- peakVramMB is total GPU memory in use (nvidia-smi), not per-process; null where unavailable.
- "webgl2" in v1.0.1 runs the unchanged v1.0.0 filter engine, so the webgl2 rows are the v1.0.0 baseline for the filter stage on the same machine and FFmpeg build.
- Stage times overlap (decoder, GPU and encoder run concurrently); they are not additive.
