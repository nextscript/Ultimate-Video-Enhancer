# UVE Manager 1.0.4

Desktop video editor and offline renderer built on the Ultimate Video Enhancer effect chain –
for Windows, macOS and Linux. Since 1.0.3 the editor is a multi-track timeline.

## Downloads

All packages are attached to the [GitHub release](https://github.com/nextscript/Ultimate-Video-Enhancer/releases/tag/1.0.4)
and built automatically by `.github/workflows/release.yml`:

| System | Package |
|---|---|
| Windows x64 | `UVE-Manager-1.0.4-setup.exe` (installer) or `UVE-Manager-1.0.4-portable.exe` (portable, ships FFmpeg) |
| macOS Apple Silicon | `UVE-Manager-1.0.4-mac-arm64.dmg` / `.zip` |
| macOS Intel | `UVE-Manager-1.0.4-mac-x64.dmg` / `.zip` |
| Linux x64 | `UVE-Manager-1.0.4-linux-x86_64.AppImage` / `UVE-Manager-1.0.4-linux-x64.tar.gz` |

- **macOS:** the app is not notarized – open it the first time with right click → *Open*
  (or `xattr -cr "/Applications/UVE Manager.app"`). FFmpeg must be installed, e.g. `brew install ffmpeg`.
- **Linux:** FFmpeg must be installed (`sudo apt install ffmpeg` or the package of your distribution).

## Editing

- **Open Video** (or drop files on the window) adds video and audio files to the media pool and to the end of
  the timeline. A video's picture goes on a video track and its sound becomes its own clip on an audio track,
  so picture and sound can be cut and synced independently. Audio files (mp3, wav, m4a, flac, ogg, …) go on
  audio tracks.
- **Tracks:** any number of video and audio tracks with mute, solo, hide, lock, gain and pan. Drag the lower
  edge of a track header to change its height.
- **Clips:** drag to move (snaps to 00:00, clip edges and markers while dragging), drag an edge to trim,
  fade handles, speed, reverse, 13 transitions, per-clip look.
- **Marking a range:** hold the left button and drag over the tracks or in the time ruler (on clips:
  Shift+drag). Delete, ripple delete, copy, cut and paste then work on the marked range.
- **Right-click** a track or clip for every editing command.
- **Mouse wheel** zooms the timeline around the pointer; Shift+wheel scrolls sideways, Alt+wheel changes the
  track height.
- **Scopes:** Enhancement Settings → Basic → Scopes shows histogram, parade, waveform and vectorscope over the
  player.
- **Render timeline** renders all tracks, transitions and the audio mix into one file. Preview and export use
  the same rules on both GPU engines (WebGPU and WebGL2).

## Keyboard shortcuts

| Action | Windows / Linux | macOS |
|---|---|---|
| Undo one step / Redo | Ctrl+Z / Ctrl+Shift+Z (also Ctrl+Y) | ⌘Z / ⇧⌘Z |
| Cut / Copy / Paste (selected clip or marked range) | Ctrl+X / Ctrl+C / Ctrl+V | ⌘X / ⌘C / ⌘V |
| Mark the whole timeline | Ctrl+A | ⌘A |
| Marking start / end at the playhead | I / O | I / O |
| Clear marking, close scopes | Esc | Esc |
| Split at the playhead / ripple trim to the playhead | Q / Shift+Q | Q / ⇧Q |
| Delete (lift) / ripple delete | Delete / Shift+Delete | ⌫ / ⇧⌫ |
| Duplicate clip | Shift+D | ⌘D |
| Move selected clip 1 / 10 frames | Alt+← → / Alt+Shift+← → | ⌥← → / ⌥⇧← → |
| Play / Pause | Space | Space |
| Previous / next frame | ← / → | ← / → |
| 1 second back / forward | Shift+← / Shift+→ | ⇧← / ⇧→ |
| Start / end | Home / End | Home / End |
| Previous / next edit point | PageUp / PageDown | PageUp / PageDown |
| Previous / next marker | Shift+PageUp / Shift+PageDown | ⇧PageUp / ⇧PageDown |
| Add marker / dissolve / toggle snapping | M / T / S | M / T / S |
| Zoom in / out / fit | + / − / Z | + / − / Z |
| Add video / audio track | Ctrl+Shift+V / Ctrl+Shift+A | ⇧⌘V / ⇧⌘A |
| Save / Open / Render timeline | Ctrl+S / Ctrl+O / Ctrl+R | ⌘S / ⌘O / ⌘R |
| Original / Enhanced wipe | Ctrl+Alt+U | ⌃⌥U |
| Look toggles, HUD, Scopes, Auto-Scene-Match … | Ctrl+Alt+key | ⌃⌥key (Control+Option) |

All shortcuts can be changed in Settings → Keyboard shortcuts.

## Documentation

- `docs/README.md`: user and developer documentation
- `docs/BUILD.md`: build instructions per platform and the release workflow
- `docs/PROJECT_SCHEMA.md`: the `.uveproj` project format (schema version 2, timeline)
- `docs/TRANSITIONS.md`: transition geometry shared by preview and export
- `CHANGELOG_1.0.4.md`: changes since 1.0.3 (shortcuts, ruler marking, live snapping)
- `CHANGELOG_1.0.3.md`: changes since 1.0.2 (multi-track timeline)
- `CHANGELOG_1.0.2.md`: changes since 1.0.1
- `CHANGELOG_1.0.1.md`: changes since 1.0.0
- `migration/`: feature-parity inventory, parity comparison, capability probe, benchmark, migration/rollback notes
- `UVE_DESKTOP_V1.0.1_CROSS_PLATFORM_GPU_MIGRATION.md`: the specification of the cross-platform GPU release
