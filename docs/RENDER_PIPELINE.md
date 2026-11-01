# Render pipeline

```
source file (read-only)
   │ ffprobe → MediaInfo (streams, fps, rotation, color metadata, HDR)
   ├──────────────── preview ─────────────────┐            ┌──────────── export (hidden worker) ───────────┐
   │ <video> via uve-media:// (range protocol) │            │ ffmpeg decode → scale (explicit YUV→RGB       │
   │ or cached H.264 proxy for codecs Chromium │            │ matrix/range, tone map for PQ/HLG) → rgba or  │
   │ cannot decode                             │            │ rgba64le raw frames on stdout                 │
   │ texImage2D(video) → EffectEngine          │            │ uploadRGBA8/16 → EffectEngine                 │
   └───────────────────┬───────────────────────┘            └───────────────────┬───────────────────────────┘
                       │ same UveGraph (shared/uve/graph.ts) + custom GLSL passes + auto matrix(t)
                       ▼                                                        ▼
            present (wipe/bypass) + scopes                       readPixels RGBA8 / RGBA16UI → ffmpeg stdin
                                                                 → BT.709 limited-range YUV → NVENC / x264 / x265 /
                                                                   SVT-AV1 / VP9 / ProRes → mux audio/subtitles
                                                                 → <name>.uvepart.<ext> → atomic rename
```

* **Timing**: frames are decoded with `-fps_mode cfr -r <avg fps>`; frame *n* is processed at `t = start + n/fps`
  (Auto-Scene-Match matrix and GLSL `u_time` use this media time).
* **Audio**: stream copy when the container allows it, otherwise AAC/Opus re-encode (reported in the job log).
* **Cancel / errors**: the worker kills both FFmpeg processes, the partial `.uvepart` file is deleted; the source is never opened for writing and the output path can never equal the input.
* **Memory**: bounded decoded-frame queue (4 frames) with stdout back-pressure, encoder stdin back-pressure, texture pool.
* **GPU**: the worker uses its own WebGL2 context; context loss aborts the job with an actionable error.


## v1.0.1 engine facade

```text
Player / export worker
  → createEngine(pref, customFilters)   src/engine/createEngine.ts
      ├─ GpuEngine     WebGPU/WGSL      (validated once against the v1.0.0 engine)
      └─ Webgl2Engine  wraps the unchanged v1.0.0 EffectEngine (fallback; custom GLSL)
FFmpeg decode (hwaccel verified per file, else software) → RGBA/RGBA64 pipe → upload → graph → readback
(double-buffered) → FFmpeg encode (hardware encoder verified per job, else software)
```

There is no zero-copy surface sharing: frames pass through CPU memory between FFmpeg and the GPU engine.

## v1.0.7 AI upscale providers

```text
timeline (tracks, transitions, per-clip looks, audio mix)
   │ UVE export worker (unchanged effect parity)
   ├─ provider off ───────▶ final encode + mux (unchanged)
   ├─ provider rve ───────▶ FFV1 RGB intermediate → RVE upscale + final encode (--custom_encoder) → mux (unchanged)
   └─ provider video2x ──▶ FFV1 RGB intermediate                                                         (stage 1)
                             → Video2X -p libplacebo --libplacebo-shader <Anime4K | custom chain> -w W -h H
                               -c ffv1 --pix-fmt gbrp (lossless, final size; fallback libx264rgb qp 0)    (stage 2)
                             → ffprobe: exact size and frame count
                             → FFmpeg final encode with the job's encoder (one lossy encode)              (stage 3)
                             → audio/subtitle mux (single file: from the source; timeline: the mixed wav)  (stage 4)
                             → ffprobe verification → atomic rename
```

* The provider is `ExportSettings.upscaleProvider`; when it is absent (1.0.6 projects and jobs) `aiUpscale` decides
  (model id = RVE, empty = off). It is never guessed from a shader or model name.
* Video2X is started with `spawn(exe, argv, { shell: false })`, stdin ignored. Imported shader file names never reach a
  command line: custom chains are written to a hash-named file in the UVE data folder, and that path is checked for
  characters Video2X's libplacebo filter string could not quote.
* Progress is weighted over the phases (UVE 35 %, Video2X 50 %, encode 12 %, mux/verify 3 %) and never goes backwards;
  the remaining time is shown only once Video2X reports measured frames.
* Not zero-copy: total time = UVE render + Video2X + encode + mux. The lossless intermediates need roughly
  `frames × 3 bytes × 0.5 × (source pixels + output pixels)` of free space; they are deleted after success, cancel or failure.
* Colour: the intermediate is 8-bit RGB (as for RVE); the final encode writes BT.709 limited range. HDR sources are tone
  mapped by the UVE decode stage exactly as in the other export paths – PQ/HLG is not passed through the upscaler.
* The UVE WebGPU / WebGL2 engines never compile imported mpv shaders; only Video2X/libplacebo executes them.
