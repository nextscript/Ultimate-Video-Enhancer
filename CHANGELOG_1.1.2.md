# UVE Manager 1.1.2 – Changelog

Baseline: v1.1.1. Fixes missing sound in the preview for MKV files.

## Fixed

- **MKV files played without sound in the preview.** Chromium (the player inside UVE Manager) cannot decode
  AC3, E-AC3, DTS, TrueHD and similar audio codecs that are typical for MKV releases. The picture still plays,
  so no error was raised – the sound was just skipped silently.
  - When the first audio stream uses such a codec, UVE Manager now builds an **audio proxy** for the preview in the
    background: the video stream is copied unchanged (no re-encode, no quality loss, ~25× real time), only the sound
    is converted to AAC with the same channel layout (5.1 stays 5.1, so the timeline mixer down-mix is unchanged).
    Progress is shown in the player; the preview switches to the proxy automatically once it is ready.
  - If the picture codec cannot be stream-copied (e.g. MPEG-2), the full H.264 preview proxy is used instead.
  - Proxies are cached (Settings → Clear cache removes them). **Export always reads the original file** – the
    original audio is copied or encoded as configured, exactly as before.

## Tests

- New unit tests for the codec decision (`shared/browser-codecs.ts`). All unit tests pass.
- Checked in Electron with a German DL 1080p HEVC MKV (2× AC3 5.1): the original file decodes 0 audio bytes in
  Chromium, the audio proxy decodes sound with the HEVC picture copied 1:1.
