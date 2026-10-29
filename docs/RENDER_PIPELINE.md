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
