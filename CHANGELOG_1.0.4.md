# UVE Manager 1.0.4 – Changelog

Baseline: v1.0.3.

## New: keyboard shortcuts for timeline editing

All shortcuts can be changed in Settings → Keyboard shortcuts. On macOS ⌘ replaces Ctrl.

| Shortcut | Action |
|---|---|
| Ctrl+Z | Undo one step |
| Ctrl+Shift+Z | Redo (restore what was undone) – Ctrl+Y still works too |
| Ctrl+X | Cut the selected clip or the marked range |
| Ctrl+C | Copy the selected clip or the marked range |
| Ctrl+V | Paste at the playhead |
| Ctrl+A | Mark the whole timeline on all tracks |
| I / O | Start / end of the marking at the playhead (all tracks) |
| Esc | Clear the marking, close the scopes |
| Alt+← / Alt+→ | Move the selected clip by 1 frame (sync picture and sound) |
| Alt+Shift+← / Alt+Shift+→ | Move the selected clip by 10 frames |
| Shift+← / Shift+→ | Playhead 1 second back / forward |
| Z | Fit the whole timeline into view |

Unchanged: Space play/pause, ← / → frame step, Home / End, Q split, Shift+Q ripple trim, Delete lift,
Shift+Delete ripple delete, Shift+D duplicate, T dissolve, M marker, S snapping, + / − zoom,
PageUp / PageDown edit points, Shift+PageUp / Shift+PageDown markers, Ctrl+Shift+V / Ctrl+Shift+A add tracks.

## Fixed

- **Marking in the time ruler:** holding the left button and dragging in the ruler above the tracks now marks
  that range on all tracks instead of only moving the playhead. A click in the ruler still moves the playhead,
  and dragging the playhead itself still scrubs.

- **Snapping while dragging clips:** the clip now snaps already while it is being dragged, exactly where it will
  land. It can no longer be dragged before 00:00 – it stops at 00:00 and snaps there when it comes close.
  Edge trims stop at the end of the available media and at the neighbouring clip instead of being rejected
  after release. The snap distance grew from 8 to 12 pixels.

- **Adding tracks:** + V / + A (and Ctrl+Shift+V / Ctrl+Shift+A, right-click menu) add the new track below the
  existing ones instead of on top, and the tracks always stay in pairs: V1, A1, V2, A2, V3, A3, … The new
  track is selected and scrolled into view.

- **Original / Enhanced wipe is back** for the timeline: the left side of the handle shows the untouched picture
  of the top clip, the right side the enhanced timeline picture. Drag the handle in the picture; switch it with
  the wipe button in the transport bar or Ctrl+Alt+U.

## Changed

- Redo moved from Ctrl+Y to **Ctrl+Shift+Z** (Ctrl+Y stays as a second key). Saved settings that still had the
  old default are updated automatically; a redo key you chose yourself is kept.
- Shortcuts of the removed source monitor (In/Out point, bypass) are gone from the settings.
- Cut / copy / paste now go through the shortcut settings instead of being fixed keys.
