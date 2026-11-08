# Test report – UVE Player

Test system: Windows 10 IoT Enterprise LTSC 2021 (19044), x64, NVIDIA GPU, MSVC 19.51, Qt 6.12.0, libmpv 20261102 (mpv v0.41.0-1386).
All results come from runs that were actually executed (1.0.0 on 2026-11-02, additions up to 1.0.6 on 2026-11-03).
Anything not tested is explicitly marked as such.

## Automated tests (CTest, `ctest --test-dir build -C Release`)

| Suite | Content | Result |
|---|---|---|
| tst_manifest | Manifest validation (id, version, apiVersion, minHostVersion, platform, backends) | passed |
| tst_plugins | Plugin folders, pure QML plugin, contributions (pages, menu, hotkeys, localized titles), commands, events, URL resolution, enabling/disabling at runtime, package installation from a folder and from ZIP (incl. backslash paths and path-traversal rejection), `modal.open`/`modal.close` (defaults, overrides, rejected input), removing plugins (bundled refused, undo, deletion at the next start, data kept or deleted, forged entries outside the user folder ignored) | passed |
| tst_playlist | Adding, moving, sorting, repeat/shuffle, M3U/PLS, network path detection | passed |
| tst_hotkeys | Default bindings, conflict detection, reset | passed |
| tst_proxy | URL encoding, validation, DPAPI storage; bridge against a local SOCKS5 server (407 without access, CONNECT tunnel, resolution at the proxy, tcp forwarding); optional live proxy | passed (live: public IP direct 84.118.8.160, via proxy 88.151.59.152) |
| tst_toolupdater | Tool updater (data folder creation, error messages) | passed |
| tst_i18n | all `lang/uve_*.ts`: complete, placeholders, edge whitespace, unique menu accelerators; switching en/el/tr/de with built `.qm`, fallback, broken file | passed (6 languages × 860 strings) |
| tst_uve | 15 UVE conversions exact against the original JS (Node vm), profile round trip, `.cube` parser, ZIP/inflate/CRC, real `LUTsProfiles_v3.0.zip` (83 profiles), GitHub/jsDelivr checksums, BL/WL/SR values, shader structure | passed |
| tst_mpv_integration | Load libmpv, open a file, switch tracks, seek, EOF | passed |
| tst_thumbnailer | Explorer thumbnail provider: file stream, memory stream, STA-bound stream (no access from other threads) | passed |

Result: **10/10 passed**.

## 1.1.3 self-tests (2026-11-08, real mouse events)

| Check | Result |
|---|---|
| `UVE_SELFTEST_ONLY=language`: all 42 language pairs, every menu title (hover and idle) and every enabled menu entry checked by its pixels | 0 failures; before the fix 36 failures on *Audio* and *Plugins* |
| `UVE_SELFTEST_ONLY=plugins` with `plugins/examples/modaldemo`: scopes player/video/sidebar, modes movable/fixed/fill, dragging and limits at the edges, player usable next to a limited window, `modal.closed`, settings entry and page, removal with undo | ok |
| Second start after the removal: plugin folder and plugin data deleted | ok |
| 1.1.5: hide and move sidebar pages (*Timer*, *Effects*), sidebar and current tab stay, effects command and LUT gallery from *Settings*, *View → Plugin pages* | ok |
| 1.1.4: *Plugins → Manage plugins…* opens *Settings → Plugins*; settings button, removal and undo on that page | ok |

## UI self-test (`UVE_SELFTEST_UI=1 UVEPlayer.exe --selftest …`, real mouse events)

| Check | Result |
|---|---|
| Double-click in the playlist plays row 2 | ok |
| Click on the video pauses / resumes; double-click → fullscreen without pausing | ok |
| Right-click context menu | ok |
| Effects are visible in the picture (saturation −10: color saturation measured at 41 %) | ok |
| Example profile My_Profile.json from GitHub: BL 0.7, WL 0.2, SR 1.1, SL 1.2, Dark & Moody, LUT profile Fujifilm DCI-P3 | ok |
| LUT package v3.0 downloaded automatically (83 profiles), LUT profile measurable in the picture (black-and-white LUT: saturation → 0) | ok |
| LUT gallery with 83 previews from the current frame (53–64 ms) | ok |
| Settings → Network | ok |
| English (default) and Greek language, 0 warnings in the log | ok |
| Clean exit (exit code 0) | ok |

## Playback of real sources

| Source | Result |
|---|---|
| MKV HEVC 10-bit + AC3, 2 audio / 2 subtitle tracks | NVDEC, start 3.0 s, seek 212 ms |
| AVI MPEG-4/MP3, MKV H.264 + AAC/AC3 + SRT (test media) | ok |
| YouTube 1080p (yt-dlp) | start 6.5 s, seek 200 ms |
| Twitch mihanaplays (live) | 1080p |
| `tcp://hls.ts3x.cc:8113` (live) | 1080p, start 0.7 s |
| **Via SOCKS5 proxy** albcloud.ts3x.cc:2888: Twitch + tcp:// | 1080p/NVDEC; all connections via the bridge (gql.twitch.tv, usher, playlist, 4× segments, hls.ts3x.cc), 7.5 MB |
| File via a network path that becomes readable only after a few seconds (UNC) | opened automatically after 2 retries |

## Not tested
- Linux, macOS (built by CI only, not tested manually)
- AMD/Intel GPUs, HDR10 output, multi-monitor setups
- Installing the setup on a clean system without the Visual C++ runtime (runtime DLLs are bundled by windeployqt)
- Pixel-exact comparison browser UVE ↔ native (see UVE_PARITY_MATRIX.md)
