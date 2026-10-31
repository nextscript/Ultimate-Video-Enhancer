# UVE Manager 1.0.3 – Changelog

Baseline: v1.0.2.

## New

- **Multi-track timeline (NLE).** The editor is now timeline-only: any number of video and audio tracks, and the
  player always shows the timeline as it will be rendered. The separate source monitor (In/Out, before/after
  wipe, bypass) is gone.
  - **Open Video** (and dropping files on the window) adds the files to the media pool and to the end of the
    timeline. The app starts with an empty timeline; the last video is no longer reopened.
  - A video with sound is placed as two clips: the picture on a video track (its own sound muted) and the sound
    as a separate clip on an audio track, so picture and sound can be cut, moved and synced independently.
  - **Audio files** (mp3, wav, m4a, aac, flac, ogg, opus, …) can be imported and placed on audio tracks.
  - Track headers: name, mute, solo, hide, lock, gain (dB), pan, move up/down, remove; add video / audio tracks.
  - **Track height:** drag the lower edge of a track header (double-click resets); Alt+wheel changes all tracks.
    The vertical scrollbar follows the total height.
  - **Mouse wheel zooms** the timeline around the pointer (zoom out = clips get shorter, more fits on screen);
    Shift+wheel scrolls sideways, Ctrl+wheel scrolls the tracks. The px/s button fits the whole timeline.
  - **Right-click menu** on tracks and clips: split here, split at playhead, ripple trim, copy, cut, paste here,
    duplicate, delete (lift), ripple delete, add/remove transition, add marker, add/remove tracks.
  - **Copy / cut / paste clips** with Ctrl+C / Ctrl+X / Ctrl+V (⌘ on macOS); a pasted clip keeps its look,
    levels, fades, speed and automation.
  - Split at the playhead with no clip selected cuts all tracks, so picture and sound stay in sync.
  - **Mark a range:** hold the left button and drag over the tracks: in empty track space a plain drag, on
    clips Shift+drag (a plain drag on a clip still moves it). The range covers every track the drag passes
    over and snaps like clip edges; a click only moves the playhead, Esc clears the marking.
    Delete / Shift+Delete lift or ripple-delete the marked range (clips are cut at its edges), Ctrl+C / Ctrl+X
    copy or cut the parts inside it, and Ctrl+V pastes them back onto their tracks at the playhead. The
    right-click menu inside the range also offers "Split at range edges".
  - Clips: move within and across tracks, trim either edge (Alt on the right edge = ripple trim), razor at the
    playhead, lift or ripple delete, duplicate, picture and audio fades, speed and reverse, per-clip look.
  - Snapping to clip edges, markers and the playhead; zoom (Ctrl+wheel); markers (add, rename, remove, jump).
  - Media pool: import files, show a file in the source monitor, add it at the playhead or drag it onto a track.
- **Transitions:** 13 kinds (dissolve, fade through black/white, wipes, slides, smooth slides, circle
  open/close). Each transition sits on the left edge of a clip; drag its handle to change the length.
- **Timeline playback** of the whole timeline (picture and sound) through the same compositing and audio rules
  the export uses.
- **Render timeline** (toolbar and export panel) renders the whole timeline as one job:
  - one decoder per clip stream; a stream that runs out holds its last frame instead of going black;
  - a lossless pre-pass for reversed clips (FFV1 / PCM temp files, removed after the job);
  - the audio mix (gain, pan, fades, crossfades, automation, speed) is rendered by FFmpeg and muxed in.
- **Both GPU engines (WebGPU and WebGL2)** composite up to 4 layers per frame with per-clip effect graphs.
  Preview and export give the same picture on both engines.
- **Project files** are saved as schema version 2 (`docs/PROJECT_SCHEMA.md`); version 1 projects are migrated
  on open.
- **Shortcuts:**
  - Editing: razor Q, ripple trim to playhead Shift+Q, lift Delete, ripple delete Shift+Delete,
    duplicate Shift+D, dissolve T.
  - Tracks and markers: new video track Ctrl+Shift+V, new audio track Ctrl+Shift+A, marker M.
  - Navigation and view: next/previous edit PageDown/PageUp, next/previous marker Shift+PageDown/PageUp,
    snapping S, zoom + / −.
  - On macOS, ⌘ replaces Ctrl.

- **Scopes** are no longer a separate page: switch them on in Enhancement Settings → Basic → Scopes (or
  Ctrl+Alt+S). They are shown over the player and measure the timeline picture.

## Known limitations

- The preview plays reversed clips without sound.
- Audio formats Chromium cannot decode (e.g. wma, aiff, ac3) render correctly but are silent in the preview.
- Automation of audio pan (`panCurve`) is not applied yet; static pan is.
- At most 4 picture layers are shown per frame; lower layers beyond that are dropped with a warning.

## Tests

- `npm test`: 102 unit tests (timeline model, transitions, engine consistency).
- `npm run selftest` renders timeline jobs (cut, opacity overlay, dissolve, reversed clip) on WebGPU and WebGL2.
  The output must match the source frame for frame, and the two engines must give the same result.
