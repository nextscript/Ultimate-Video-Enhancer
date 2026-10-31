// Platform-aware shortcut labels for the UI (v1.0.2): ⌃⌥⇧⌘ symbols on macOS, "Ctrl+Alt+B" elsewhere.
import { defaultShortcuts, formatCombo } from '../../shared/shortcuts';
import { addMarker, clipById, clipEnd, moveClip, rippleTrimEnd, setTransition, snap, timelineDuration } from '../../shared/timeline';
import {
    copyClip, duplicateSelectedClip, editTimeline, insertTrack, pasteClip, razorAtPlayhead, removeSelectedClip, setMonitor, setRangeSel,
    setSnapping, setTimelineZoom
} from '../state/actions';
import { getState, setState, toast } from '../state/store';
import { player } from './Player';
import { defaultTransitionDurations, editPoints, nextPoint } from './timelineMath';

export const PLATFORM = /Mac/i.test(navigator.platform) ? 'darwin' : /Win/i.test(navigator.platform) ? 'win32' : 'linux';
export const IS_MAC = PLATFORM === 'darwin';

/** Display text of a literal combo, e.g. kbd('Ctrl+Alt+B') → "⌃⌥B" on macOS. */
export const kbd = (combo: string) => formatCombo(combo, PLATFORM);

/** Display text of the (possibly user-changed) shortcut of an action, e.g. sc('save') → "⌘S" / "Ctrl+S". */
export const sc = (action: string) => kbd(getState().prefs?.shortcuts[action] || defaultShortcuts(PLATFORM)[action] || '');

/** Edits at "the playhead" mean the timeline playhead: switch the player to the program monitor first. */
export function toTimelineMonitor() {
    if (getState().monitor === 'timeline') return;
    setMonitor('timeline');
    setState({ time: player.timeline?.t ?? 0 });
}

/** Timeline navigation always works on the program monitor. */
function seekTimeline(t: number) {
    toTimelineMonitor();
    player.seek(t);
}

/** Adds a dissolve to the selected clip (on its LEFT boundary), trying shorter lengths until one fits. */
export function addDefaultTransition(clipId: string, kind: Parameters<typeof setTransition>[2] = 'dissolve'): boolean {
    const tl = getState().timeline;
    const clip = clipById(tl, clipId);
    if (!clip) return false;
    let last = '';
    for (const d of clip.transition ? [clip.transition.dur] : defaultTransitionDurations(tl.frameRate)) {
        const res = setTransition(getState().timeline, clipId, kind, d);
        if (res.ok) return editTimeline(() => res, '');
        last = res.error;
    }
    toast(last || 'Cannot add a transition here.', 'warn', 6000);
    return false;
}

/** Ripple-trims the selected clip's end to the playhead (later clips on the track follow). */
export function rippleTrimSelectedToPlayhead(): boolean {
    const st = getState();
    const clip = st.selectedClipId ? clipById(st.timeline, st.selectedClipId) : undefined;
    if (!clip) { toast('Select a clip first.', 'info', 2500); return false; }
    const t = snap(st.time, st.timeline.frameRate);
    if (t <= clip.start || t >= clipEnd(clip)) { toast('Ripple trim: the playhead must be inside the selected clip.', 'info', 3000); return false; }
    return editTimeline(tl => rippleTrimEnd(tl, clip.id, t - clip.start), '');
}

/**
 * NLE shortcut actions (defaults in shared/shortcuts.ts, editable in Settings). Returns false when the
 * action is not a timeline action so the caller can handle it.
 */
const AT_PLAYHEAD = new Set(['razor', 'rippleTrimEnd', 'marker', 'nextEdit', 'prevEdit', 'nextMarker', 'prevMarker', 'markIn', 'markOut', 'paste']);

/** Unlocked tracks: the default extent of a marking made from the keyboard. */
const unlockedTracks = () => getState().timeline.tracks.filter(t => !t.locked).map(t => t.id);

/** Moves the selected clip by whole frames (sync picture and sound); a blocked position is reported. */
function nudgeSelected(frames: number) {
    const st = getState();
    const clip = st.selectedClipId ? clipById(st.timeline, st.selectedClipId) : undefined;
    if (!clip) { toast('Select a clip first.', 'info', 2500); return; }
    const to = Math.max(0, clip.start + frames / st.timeline.frameRate);
    if (to === clip.start) return;
    editTimeline(tl => moveClip(tl, clip.id, { start: to }), '');
}

/** Zoom so the whole timeline fits the visible track area. */
export function zoomToFit() {
    const dur = timelineDuration(getState().timeline);
    const el = document.querySelector<HTMLElement>('.nle-scroll');
    if (!el || !dur) return;
    setTimelineZoom((el.clientWidth - 30) / dur);
    requestAnimationFrame(() => { el.scrollLeft = 0; });
}

export function runTimelineShortcut(action: string): boolean {
    if (AT_PLAYHEAD.has(action)) toTimelineMonitor();
    const st = getState();
    switch (action) {
        case 'razor': razorAtPlayhead(); return true;
        case 'rippleTrimEnd': rippleTrimSelectedToPlayhead(); return true;
        case 'lift': removeSelectedClip('lift'); return true;
        case 'ripple': removeSelectedClip('ripple'); return true;
        case 'duplicate': duplicateSelectedClip(); return true;
        case 'transition':
            if (st.selectedClipId) addDefaultTransition(st.selectedClipId);
            else toast('Select the clip right of the cut first.', 'info', 2500);
            return true;
        case 'addVideoTrack': insertTrack('video'); return true;
        case 'addAudioTrack': insertTrack('audio'); return true;
        case 'marker': editTimeline(tl => addMarker(tl, st.time), ''); return true;
        case 'nextEdit': case 'prevEdit': {
            const p = nextPoint(editPoints(st.timeline), st.time, action === 'nextEdit' ? 1 : -1);
            if (p !== null) seekTimeline(p);
            return true;
        }
        case 'nextMarker': case 'prevMarker': {
            const p = nextPoint(st.timeline.markers.map(m => m.t), st.time, action === 'nextMarker' ? 1 : -1);
            if (p !== null) seekTimeline(p);
            return true;
        }
        case 'snapping': setSnapping(!st.snapping); toast(`Snapping ${!st.snapping ? 'on' : 'off'}`, 'info', 1500); return true;
        case 'zoomIn': setTimelineZoom(st.timelineZoom * 1.5); return true;
        case 'zoomOut': setTimelineZoom(st.timelineZoom / 1.5); return true;
        case 'zoomFit': zoomToFit(); return true;
        case 'cut': copyClip(true); return true;
        case 'copy': copyClip(false); return true;
        case 'paste': pasteClip(); return true;
        case 'selectAll': {
            const dur = timelineDuration(st.timeline);
            if (dur > 0) setRangeSel({ t0: 0, t1: dur, trackIds: unlockedTracks() });
            return true;
        }
        case 'markIn': case 'markOut': {
            // I / O: set the start or the end of the marking at the playhead; the other edge is kept when it
            // still fits, else it falls back to the timeline start / end
            const r = st.rangeSel, end = timelineDuration(st.timeline), t = st.time;
            const trackIds = r?.trackIds ?? unlockedTracks();
            if (action === 'markIn') setRangeSel({ t0: t, t1: r && r.t1 > t ? r.t1 : Math.max(end, t), trackIds });
            else setRangeSel({ t0: r && r.t0 < t ? r.t0 : 0, t1: t, trackIds });
            return true;
        }
        case 'nudgeLeft': nudgeSelected(-1); return true;
        case 'nudgeRight': nudgeSelected(1); return true;
        case 'nudgeLeft10': nudgeSelected(-10); return true;
        case 'nudgeRight10': nudgeSelected(10); return true;
        default: return false;
    }
}
