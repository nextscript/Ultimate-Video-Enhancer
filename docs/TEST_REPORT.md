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

Result: **10/10 passed** (1.1.8: 15 suites incl. `tst_optical_sources`, `tst_disc_navigation`, `tst_optical_playback` – see below).

## 1.1.3 self-tests (2026-11-08, real mouse events)

| Check | Result |
|---|---|
| `UVE_SELFTEST_ONLY=language`: all 42 language pairs, every menu title (hover and idle) and every enabled menu entry checked by its pixels | 0 failures; before the fix 36 failures on *Audio* and *Plugins* |
| `UVE_SELFTEST_ONLY=plugins` with `plugins/examples/modaldemo`: scopes player/video/sidebar, modes movable/fixed/fill, dragging and limits at the edges, player usable next to a limited window, `modal.closed`, settings entry and page, removal with undo | ok |
| Second start after the removal: plugin folder and plugin data deleted | ok |
| 1.1.7: YouTube playlist links (`list=`, `index=`, `youtu.be`, mix capped at 200) parsed and start video chosen (`tst_playlist`) | ok |
| 1.1.6: `defaultPlacement` starts a sidebar page hidden; the user's choice wins and survives a restart | ok |
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

## Optical media (1.1.8)

Run on 2026-11-10 on the test system above. **No optical drive is attached to this system** – nothing below was tested with a physical
DVD, Blu-ray or CD. Labels: **PASS** = executed with real data, **SIMULATED** = executed with a synthetic disc or mock,
**NOT TESTED** = not executed, **BLOCKED** = missing hardware/tool/feature.

Synthetic DVD: `tests/fixtures/make-dvd-fixture.sh` (ffmpeg + dvdauthor/spumux 0.7.2 + genisoimage from Ubuntu 24.04, run in WSL without
root): VMGM menu with two buttons (first play → menu), title 1 = 12 s PAL test picture with AC-3 sound and chapters at 0:00 and 0:06,
as `VIDEO_TS` folder and as ISO. Synthetic audio CD: `createSyntheticCd()` / `UVE_TEST_FAKE_CD=1` (3 tracks of sine tones, 9–11 s).

| Check | How | Result |
|---|---|---|
| Capabilities of the bundled libmpv (mpv v0.41.0-1386, shinchiro 20261102) | `tst_optical_playback` against the real DLL | PASS: `dvd`/`dvdnav`, `bd`/`bluray`, `disc-menu`, `disc-menu-active`, `discnav` present; `cdda` absent; BD-J not available |
| Source detection: DVD/BD folders, `VIDEO_TS`/`BDMV` chosen directly, AACS, BD-J (`JAR`), ISO 9660, UDF, garbage/truncated images, schemes rejected | `tst_optical_sources` (in-memory images, temp folders) | SIMULATED, passed |
| Playlist entries (special characters, Unicode), session restore, load plans incl. `%len%` quoting, degradation without menus/libraries | `tst_optical_sources` | SIMULATED, passed |
| `discnav` only with an active DVD/BD and the command present, unknown actions refused; menu keys (arrows, Enter, Backspace) only while a menu is shown | `tst_disc_navigation` | SIMULATED, passed |
| DVD folder: start in the disc menu, `disc-menu-active`, titles from `edition-list`, title selection leaves the menu, `discnav title-menu` returns to the menu (53 ms) | `tst_optical_playback` (vo=null) | SIMULATED, passed |
| DVD ISO: main title, 720×576, 12.0 s, 2 chapters, chapter jump to 0:06 | `tst_optical_playback` | SIMULATED, passed |
| DVD in the real player (`UVEPlayer.exe` with the folder as argument): menu picture shown, disc bar visible, Enter → `discnav select`, ← → `discnav left` (not seek), Ctrl+M → `discnav menu` | real window, keys sent to the player | SIMULATED, passed |
| Selecting a **menu button** (`discnav select`) jumps into the title | synthetic DVD, headless and in the player | **not passed**: libmpv accepts the command and the disc resyncs, but stays in the menu; button highlights are not visible. Cause open (synthetic disc or libmpv). Needs a real DVD |
| Audio CD via `uvecdda://`: WAV header, byte-exact data, seeking across sector/cache boundaries, data tracks refused, read error when the medium disappears | `tst_optical_sources` | SIMULATED, passed |
| Audio CD in libmpv: `pcm_s16le`, length 9.0 s, seek, track change without early end, no navigation/video | `tst_optical_playback` | SIMULATED, passed |
| Audio CD in the real player: *Open disc* dialog lists the test CD with badge and 3 tracks, *Open* → 3 playlist entries with lengths, playback, N → next track | real window | SIMULATED, passed |
| Unknown `.iso` refused with message, nothing added; missing device → libmpv error, no crash; removed image → "disc was removed" | `tst_optical_playback` | SIMULATED, passed |
| Windows drive enumeration (`GetDriveType`, empty drive without system dialog), raw CD reading via `IOCTL_CDROM_RAW_READ` | – | BLOCKED (no drive) |
| Blu-ray titles and HDMV menus (folder/ISO) | – | BLOCKED (no open-source tool here to author a valid BDMV with playlists/menus); not claimed |
| BD-J | – | BLOCKED (not shipped) |
| Encrypted Blu-ray | – | refused by design (AACS folder → message); NOT TESTED with a real disc |
| Linux (`/dev/sr*`, distribution libmpv) and macOS (mounted volumes, Homebrew mpv) | – | NOT TESTED (only built by CI) |
| Physical DVD, Blu-ray, audio CD on any platform | – | NOT TESTED |

Regression (same day): all 15 CTest suites passed, including the existing ones (`tst_mpv_integration`, `tst_denoise_render`, `tst_i18n`
with 6 languages × 1079 strings, …). UI self-test (`UVE_SELFTEST_UI=1`, real mouse events: playlist, click/double-click, context menu,
effects, LUT gallery, before/after, Auto Scene Match, transitions): passed in 3 consecutive runs; it failed in 3 earlier runs on
`autoInfo` (Auto Scene Match text empty) and passed with 1.1.7 in between – after a clean rebuild 1.1.8 passed again, so this is noted
as possibly timing-dependent, not as solved.

## About UVE Player – update check (1.1.8)

| Check | Result |
|---|---|
| *Help* menu contains only *Keyboard shortcuts* and *About UVE Player* | PASS (real window) |
| Opening *About* starts the check and shows the result in the dialog: "New commits on GitHub (no release yet)" with *Details*, "Tool updates available: libmpv" with *Show* | PASS (real GitHub answer at the time of the test) |
| Automatic start-up check does not open a second window while *About* is open | code review |
| *Update now* inside the dialog (download, SHA-256, restart) | NOT TESTED (no newer release existed); uses the unchanged `AppUpdater::install()` covered by `tst_appupdater` |

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
