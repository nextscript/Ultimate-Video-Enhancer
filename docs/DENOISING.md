# Live denoising

*Effects → Denoising / noise reduction* (since 1.1.2). The mode applies immediately during playback, without reopening the
file or stream and without changing the playback position.

| Mode | Where it runs | Implementation |
|---|---|---|
| **Off** (default) | – | No stage in the pipeline, the picture is identical to the picture without this feature |
| **1. Bilateral filter** | GPU, `//!HOOK MAIN` | Edge-preserving 5×5 filter on luma; chroma follows with its own limit so colours do not smear |
| **2. Adaptive bilateral + debanding** | GPU, two separate passes | Directional 5×5 slope controls the strength (flat areas stronger, edges weaker), 32 samples; separate debanding pass with a narrow threshold and fine dither |
| **3. Temporal denoising** | CPU, mpv video filter before the renderer | FFmpeg `atadenoise` with a fixed window of 5 or 7 frames; intended for light noise |
| **4. NLMeans** | GPU, `//!HOOK MAIN` | Own non-local means shader (GLSL): 32 search positions, cross-shaped patch of 5 samples compared on luma |

Controls: *Strength* and *Preserve details* apply to all modes, *Debanding* only to mode 2. The presets 480p, 720p, 1080p and
4K set moderate values for the current mode. Mode 2 at 720p is the standard preset (strength 40, details 60, debanding 35).

Denoising cannot restore information a video has already lost. Heavy compression artefacts are reduced, not repaired.

## Architecture

- **Order:** the plugin registers the stage `org.nextscript.ultimate-video-enhancer#denoise` before its UVE stage, so denoising
  runs before colour and sharpening. Stages of other plugins are not reordered. `ShaderChain` treats `<pluginId>#<name>` as
  a further stage of the same plugin; a new file re-enables such a stage after an error.
- **Why Temporal is not a shader:** `vo=libmpv` uses mpv's `vo_gpu` renderer, and its user shaders have no texture that survives
  a frame. A stateless shader cannot mix frames, so Temporal uses a real frame window in the video filter chain.
- **Host stage:** `VideoFilterStage` (commands `video.filter.set`, `video.filter.status`) inserts the filter under its own label, leaves
  other `vf` entries alone and accepts temporal denoisers only.
- **Hardware decoding:** a software filter cannot read GPU surfaces such as direct `nvdec`. While Temporal is active the player
  uses `auto-copy-safe` and then restores the previous mode. `settings.json` is not changed by this.
- **Frame history:** mpv rebuilds the filter on seek and on format or resolution changes. Scene cuts and motion above the threshold end
  the window for that pixel. Memory is bounded by the window size. The filter works per frame, so a different playback speed does not
  change which frames are mixed. Timestamps stay with their frames, so A/V sync is kept.
- **Veil behind moving objects:** `atadenoise` compares single pixels, so motion with less contrast than the threshold is averaged like
  noise and leaves a faint trail. The threshold is therefore capped at about 10 of 255 levels and the window at 7 frames. A motion mask built
  from FFmpeg filters was tested and rejected: the trail stayed at low contrast and 4K fell to 21 fps.
- **Before/after:** with a GPU mode the denoising stage saves the original, so the left side shows the untreated picture. Temporal runs
  before the renderer and is therefore active on both sides.
- **Settings:** own block `denoising` in the plugin settings (`schemaVersion`, `mode`, `strength`, `detailPreservation`,
  `debanding`). Presets, UVE profiles, undo and the profile values `dn` and `gpuBase` are not involved. A missing block means Off.
- **Switching the feature off completely:** environment variable `UVE_DISABLE_DENOISING=1`, or `"featureEnabled": false` in the block.

## Fallbacks

| Situation | Behaviour |
|---|---|
| Shader does not compile or the file cannot be written | Back to **Off** with a message in *Status*; UVE effects and playback continue |
| Temporal filter rejected or dropped by mpv | Back to **Bilateral** with a message |
| Temporal overloaded: more than 15 % of frames dropped for 5 seconds | Back to **Bilateral** with a message |
| Effects switched off | All denoising stages are removed, the mode is kept |

## Measurements

Test system: GeForce RTX 5060 Ti, driver 617.42, Windows 10, player render path (render API + OpenGL FBO, 1280×720 output,
`ao=null`), 6 s per mode. Reproduce with `UVE_DENOISE_CLIPS="a.mp4;b.mp4"` and `tst_denoise_render`.
These values only apply to this system; weaker GPUs and CPUs will differ.

All modes: **0 dropped frames**, A/V difference 0.0 ms at 25, 30 and 60 fps.

Total render time per frame in ms (`glFinish` included):

| Clip | Off | Bilateral | Adaptive + debanding | NLMeans | Temporal |
|---|---|---|---|---|---|
| H.264 360p25 (real video) | 0.61 | 0.80 | 0.99 | 1.06 | 0.71 |
| 720p30 | 0.57 | 1.77 | 1.71 | 0.74 | 0.63 |
| 720p60 | 0.61 | 0.64 | 0.88 | 1.97 | 0.58 |
| 1080p60 | 0.76 | 1.00 | 1.34 | 1.33 | 1.03 |
| 4K60 | 1.11 | 1.92 | 3.49 | 3.40 | 3.28 |

Single values vary between runs by up to about 1 ms (for example 720p30 Bilateral: 0.61 and 1.77 ms), so small differences mean nothing.
The synthetic clips are `testsrc2` with temporal noise. Temporal's main cost is on the CPU (copy-back decoding and the filter) and is not
fully part of the render time; its render time includes uploading the frames.

Noise on a flat grey area, standard deviation in 8-bit levels, before → after. GPU modes: `tests/media/denoise-noise-cut.mp4` with strength 60,
details 40. Temporal: `tests/media/denoise-light-noise.mp4` with strength 100, details 0.

| Mode | Noise | Edge height |
|---|---|---|
| Bilateral | 12.9 → 9.6 | 63.6 → 64.6 |
| Adaptive + debanding | 12.9 → 6.8 | 63.6 → 64.5 |
| NLMeans | 12.9 → 7.4 | 63.6 → 65.1 |
| Temporal (light noise only) | 4.5 → 3.3 | – |

Scene cut with Temporal: flat area 94.8 two frames before the cut (scene value 94.4) and 175.0 two frames after it (scene value 174.9).

Moving bar with 12 levels of contrast, Temporal at standard, 720p and maximum settings: background 95.4, area behind the bar 95.4 (no trail).

Switching Temporal on or off restarts the decoder once (no reload and no backward jump). Measured picture loss per switch: about 0.15 s
with local files and 0.3–0.6 s with a local HLS live stream; playback then continues in sync. The GPU modes do not restart the decoder.

## Tests

`tests/integration/tst_denoise_render.cpp` runs on the real render path and checks the output picture: all modes compile and reduce
noise, the edge remains, *Off* restores the identical picture, before/after, broken shaders, failing filters, scene cuts, seeking, live
switching, decoder restore. It is skipped without OpenGL. Unit tests: `tst_uve` (settings, shader structure, presets), `tst_plugins` (stage order).

The running application can be checked with `UVE_SELFTEST_UI=1`, `UVE_SELFTEST_ONLY=denoise` and `UVEPlayer --selftest result.json <file or URL>`: it
switches through all modes during playback, tests before/after and writes results and screenshots next to `result.json`. Run it twice with the same
`UVE_DATA_DIR` to check that the settings are restored after a restart. Checked with a local file and a local HLS live stream.

## Limitations

- Temporal in *before/after* affects both sides.
- Temporal delays the filter chain by half its window. Timing stays correct, but after a seek while paused the frames behind the position
  are not yet part of the window, so the first picture is filtered less than during playback.
- Temporal has little effect on strong noise, because the threshold that prevents the veil ends the window early. Use NLMeans or Adaptive for
  strong noise; these modes have no frame history and cannot leave a trail.
- At full strength a faint trail remains possible for moving objects with very low contrast (below about 10 of 255 levels), mostly at most 3 to 4 levels.
- Measured on one NVIDIA system only. AMD, Intel, macOS and Linux have not been measured.
