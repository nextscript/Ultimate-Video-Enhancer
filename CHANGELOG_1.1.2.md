# UVE Manager 1.1.2 – Changelog

Baseline: v1.1.1. Instant sound for MKV files in the preview, crackle-free 5.1 down-mix, and a new
one-click **Sofort-Render** (Raw + Filter).

## New

- **Sofort-Render (Raw + Filter)** – button next to *Render Video*. Renders the whole original file of the
  current source 1:1 with the look you set in the app (filters, profile, LUT, Auto-Scene-Match) and nothing else:
  - same resolution, frame rate, video codec (HEVC stays HEVC, H.264 stays H.264, …) and bit depth (10-bit stays
    10-bit), container like the source (MKV stays MKV), very high quality (CRF/CQ 16, GPU encoder when available);
  - **every audio track and every subtitle track is copied unchanged** (e.g. AC3 5.1 German + English and both
    German subtitle tracks), with their language and title tags;
  - no AI upscaling, timeline cuts and transitions are ignored. Output folder and file-name pattern come from the
    Output Settings.

## Fixed

- **MKV files played without sound in the preview.** Chromium (the player inside UVE Manager) cannot decode
  AC3, E-AC3, DTS, TrueHD and similar codecs that are typical for MKV releases; the picture still played, so no
  error was shown. The sound of such files is now streamed live next to the picture: the main process decodes
  it on demand with FFmpeg into a PCM stream that the player can seek in. Sound starts **immediately** (no
  conversion step, no proxy file) and follows play, pause, seeks and speed changes in the timeline and the
  source monitor. Export always reads the original file.
- **Crackling sound with 5.1 / 7.1 sources** (preview and export). The stereo fold mixed both surround channels
  into both sides and was not normalized, so loud scenes summed far above full scale and clipped. Each surround now
  feeds only its own side and every fold is normalized like FFmpeg's own stereo down-mix – no more clipping.
- The preview sound no longer adjusts its speed in tiny steps to stay in sync (that is audible as crackling); it is
  corrected only by a single jump when picture and sound really drift apart (e.g. a seek while playing).

## Tests

- New unit tests for the codec decision, the Sofort-Render settings and the normalized 5.1 / 7.1 folds.
  All 153 unit tests pass.
- Checked in the app with a German DL 1080p HEVC MKV (2× AC3 5.1, 2× SRT): sound plays at once, stays in sync
  after a seek to 50:00 while playing; a Sofort-Render keeps HEVC 10-bit 1920×804 25 fps, both AC3 tracks and
  both subtitle tracks, with the look applied.
