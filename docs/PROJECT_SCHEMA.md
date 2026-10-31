# .uveproj schema (version 2)

Saving always writes version 2. Version 1 files are still readable and are migrated on open
(`migrateProjectV1toV2` in `shared/timeline.ts`); they are never rewritten in place, and a migrated
project keeps the original look, in/out points and export settings.

```jsonc
{
  "schema": "uve-project",
  "version": 2,
  "app": "UVE Manager 1.0.4",
  "uveSource": { "name": "...", "version": "1.17.5", "repo": "...", "commit": "38b2ed1…", "sha256": "28b37df…" },
  "savedAt": "2026-10-31T12:00:00.000Z",

  // project-wide look: used by every clip whose `effect` is null
  "effect": {
    "settings": { /* identical keys to the userscript user-profile "settings" object */ },
    "expert":   { /* full GVF_*_SETTINGS blocks */ },
    "customFilters": [ /* gvf custom filter records (id, label, code, type, enabled, blendMode, uniforms, …) */ ],
    "lut": { "name": "...", "group": "...", "matrix4x5": [20 numbers] } // embedded copy, or null
  },

  "timeline": {
    "schema": "uve-timeline",
    "version": 1,              // timeline sub-schema version (the FILE version is 2)
    "frameRate": 25,           // editing grid: every position is quantised to 1/frameRate
    "fps": "",                 // export frame rate argument for ffmpeg -r; '' = frameRate as integer
    "sources": [
      { "id": "S1", "path": "D:\\Videos\\holiday.mkv", "media": { /* full MediaInfo probe snapshot */ } }
    ],
    "tracks": [                // UI order top -> bottom; compositing draws the LAST video track first
      { "id": "T1", "name": "V1", "kind": "video", "locked": false, "muted": false,
        "hidden": false, "solo": false, "gainDb": 0, "pan": 0 },
      { "id": "T2", "name": "A1", "kind": "audio", /* same fields */ }
    ],
    "clips": [
      {
        "id": "C1", "trackId": "T1", "sourceId": "S1",
        "start": 0, "dur": 4.2, "srcIn": 1.5,          // seconds, all on the frameRate grid
        "speed": 1, "reverse": false,                  // dur is TIMELINE seconds; source span = dur * speed
        "opacity": 1, "level": 1, "fadeIn": 0, "fadeOut": 0, "hidden": false,   // picture
        "audioMuted": false, "gainDb": 0, "pan": 0,                              // sound (-1..1)
        "audioFadeIn": 0, "audioFadeOut": 0,
        "opacityCurve": [], "levelCurve": [], "gainCurve": [], "panCurve": [],  // AutoPoint {t,v}, t = seconds from clip start
        "transition": { "kind": "dissolve", "dur": 1 },  // centred on this clip's LEFT boundary; null = none
        "effect": null                                    // null = use the project look
      }
    ],
    "markers": [ { "id": 1, "t": 12.4, "name": "Title" } ],
    "masterGainDb": 0
  },

  "export": { "container": "mp4", "videoCodec": "hevc", "encoder": "auto", "quality": 20, "preset": "medium",
              "tenBit": false, "resolution": "source", "audio": "aac", "audioBitrate": 192, "audioTrack": -1,
              "subtitles": "none", "outputDir": "", "fileName": "{name}_UVE", "useInOut": false,
              "aiUpscale": "", "fps": "" },

  "ui": { "wipe": 0.5, "compare": true, "timelineZoom": 120 }
}
```

## Field notes

* **Positions are seconds**, always multiples of `1/frameRate`. A clip whose `dur * frameRate` is not a
  whole frame is rejected by `validateTimeline` (`unaligned`), so the grid decides precision. Changing
  `frameRate` later only changes the snap grid and the export rate; nothing is re-quantised.
* `fps` (string, e.g. `"30"`, `"29.97"`, `"24000/1001"`) is the ffmpeg `-r` argument; empty means
  `frameRate`. Output frames are duplicated/dropped to that rate, never interpolated.
* `speed` is source seconds per timeline second: `speed: 2` plays a clip twice as fast, and the clip
  reads `dur * speed` source seconds. `reverse: true` reads the source span backwards.
* `transition` sits on the **left boundary of the right clip** and needs media handles on both sides of
  the cut; `dur` is seconds (`max(1, round(dur * frameRate))` frames, split `floor(D/2)` / `D - floor(D/2)`).
  The kind semantics are in `docs/TRANSITIONS.md`.
* `gainDb` on a clip is a **sound** fader (decibels); the picture faders are `opacity` (0..1) and `level`
  (linear multiplier). Track `gainDb` is a video brightness gain on a video track and a fader gain on an
  audio track; `masterGainDb` is the audio master. `pan` is `-1..1` (left..right) and never boosts:
  `panLaw` clamps each side at unity, so panning only attenuates the opposite channel.
* `muted` affects sound only, `hidden` affects picture only; a `solo` track on any track silences all
  others. `locked` is an editing guard, not a playback state.
* `kind: 'audio'` tracks contribute to the mix but never to the picture; video tracks are composited in
  `tracks` order (last drawn on top).

## Validation and migration behaviour

Loading never fails on a bad timeline: `sanitizeTimeline` drops unknown keys, clamps numbers, rebuilds
missing ids/defaults, and `validateTimeline` reports problems as issues (`frameRate`, `missingTrack`,
`missingSource`, `zeroDur`, `negStart`, `speed`, `negIn`, `unaligned`, `mediaShort`, `curve`, `overlap`,
`trNoLeft`, `trGap`, `trTooLong`, `trHandles`, `trOverlap`) which surface as toasts; transitions that
cannot be honoured are dropped rather than silently distorted.

`project:open` returns `{ path, project, mediaOk, missing }`; `missing` lists source paths whose file is
not present, and `project:locateMedia` re-probes a replacement file for them (`setSourcePath`). Sources
carry their `MediaInfo` snapshot so a project stays reproducible even before the file is re-located.

A v1 file migrates to a timeline with the project's default frame rate: its single media becomes one
source, its `inPoint`/`outPoint` and export settings are kept, and its look stays the project look, so a
v1 project behaves like a one-clip timeline. `inPoint`/`outPoint` remain app state for the source
monitor; timeline render jobs always render the timeline (or the selected range) and ignore them.
`useInOut` stays meaningful only for the per-file batch path.

User profiles keep the userscript format (`{id,name,createdAt,updatedAt,settings}`) plus an optional
`desktop` object (expert overrides, custom filter stack, embedded LUT) that the userscript ignores on import.
