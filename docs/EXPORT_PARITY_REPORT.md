# Export parity report (2026-10-29)

Environment: Windows 10 IoT Enterprise LTSC 2021, NVIDIA GeForce RTX 5060 Ti, FFmpeg N-116051 (PATH build),
Electron 45 / Chromium. Command: `npm run selftest`.

| Check | Result |
|---|---|
| Neutral settings → output equals source (640×360 H.264, mean abs diff over 320×180 RGB) | **1.81 / 255** (codec loss only) – PASS |
| Orientation (per-quadrant luminance, detects flips) | 147/103/149/105 vs 146/103/149/105 – PASS |
| Frame count / duration | 75 frames, 3.00 s – PASS |
| Audio stream copy | AAC kept – PASS |
| UVE default look baked into the frames (t = 1.5 s) | mean abs diff 13.44 / 255 vs source – PASS |
| Look present on the first frames (t = 0.1 s) | PASS |
| HEVC NVENC output, duration | 3.00 s – PASS |
| Rotated (90°) odd-size source (321×181) → 10-bit HEVC MKV | 180×320, 10-bit – PASS |
| Cancel removes the partial output | PASS |
| Throughput (640×360) | neutral 105.6 fps (h264_nvenc), full UVE look 76.7 fps (hevc_nvenc) |
| Preview processing 1920×1080, default look (14 primitives) | ≈ 27–37 ms per frame |

Preview vs export use the same graph, engine and auto-matrix table. Remaining differences come from the decoder
(Chromium vs FFmpeg YUV→RGB conversion of the same stream) and from the lossy output codec; the 8-bit parity mode
(Settings) quantises every primitive to 8 bit like the browser SVG pipeline for side-by-side comparisons with the
userscript.

Unit tests (`npm test`, 20 tests): curve invariants (black/white fixed, monotonic, contrast slope 1.40 at +10),
`nothingOn` rule, graph structure of the default combo, matrix merging, tone matrix order, user-profile gating,
missing-LUT warning, repository profile/LUT/filter fixtures, `.cube` fitting, expert round trip,
Auto-Scene-Match determinism and scene response, turbulence lattice.


## v1.0.1: WebGPU engine and v1.0.0 vs v1.0.1 exports

The tolerances and per-case results are in `migration/MIGRATION_NOTES.md`; the raw data is in
`migration/parity-v1.0.0-vs-v1.0.1.json`.

- **All 6 reference cases pass** (neutral, default look, heavy stack + LUT, Auto-Scene-Match, LUT + custom GLSL,
  HEVC NVENC 10-bit). PSNR is ≥ 53.3 dB and SSIM ≥ 0.9992, with identical frame counts, codecs, pixel formats,
  colour tags and audio.
- **Selftest, same job on both engines:** WebGPU vs WebGL2 gives PSNR 52.8 dB, a maximum channel-mean
  difference of 0.006 and a worst pixel difference of 12/255.
- **Start-up validation** (85 primitives, covering every primitive type): mean difference 0.014/255, and
  0.010 % of channel values differ by more than 3/255.
