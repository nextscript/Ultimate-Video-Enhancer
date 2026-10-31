import type {
    AutoPoint, AudioStreamInfo, EffectState, MediaInfo, Timeline, TimelineClip, TimelineMarker, TimelineSource,
    TimelineTrack, TrackKind, TransitionKind, UveProjectV1, UveProjectV2
} from './types';
import { TRANSITION_KINDS } from './types';

/* ------------------------------------------------------------------ ids & time */

let seq = 0;
export function newId(prefix: string): string {
    seq = (seq + 1) % 0xffff;
    return `${prefix}${Date.now().toString(36)}${seq.toString(36)}`;
}

/** Timeline positions are stored in seconds but always live on the project frame grid. */
export const DEFAULT_FRAME_RATE = 25;

export function frameOf(t: number, F: number): number { return Math.round(t * F); }
export function secOf(f: number, F: number): number { return f / F; }
export function snap(t: number, F: number): number { return secOf(Math.round(t * F), F); }

export function clipEnd(c: TimelineClip): number { return c.start + c.dur; }
/** Source seconds a clip consumes (the media span between its in and out point). */
export function srcSpan(c: TimelineClip): number { return c.dur * c.speed; }
export function srcOut(c: TimelineClip): number { return c.srcIn + srcSpan(c); }

export function isAligned(t: number, F: number): boolean { return Math.abs(t * F - Math.round(t * F)) < 1e-6; }

/** Guard against float dust in generated ffmpeg expressions / JSON. */
function num(x: number): string {
    const s = x.toFixed(6);
    return s.replace(/\.?0+$/, '') || '0';
}

export function dbToLin(db: number): number { return Math.pow(10, db / 20); }

/* ------------------------------------------------------------------ construction */

export function makeTrack(kind: TrackKind, index: number): TimelineTrack {
    return {
        id: newId('T'),
        name: kind === 'video' ? `V${index + 1}` : `A${index + 1}`,
        kind,
        locked: false, muted: false, hidden: false, solo: false,
        gainDb: 0, pan: 0
    };
}

export function makeClip(p: { trackId: string; sourceId: string; start: number; dur: number; srcIn: number }): TimelineClip {
    return {
        id: newId('C'),
        trackId: p.trackId, sourceId: p.sourceId,
        start: p.start, dur: p.dur, srcIn: p.srcIn,
        speed: 1, reverse: false,
        opacity: 1, level: 1, fadeIn: 0, fadeOut: 0, hidden: false,
        audioMuted: false, gainDb: 0, pan: 0, audioFadeIn: 0, audioFadeOut: 0,
        opacityCurve: [], levelCurve: [], gainCurve: [], panCurve: [],
        transition: null, effect: null
    };
}

export function emptyTimeline(frameRate = DEFAULT_FRAME_RATE, fps = String(frameRate)): Timeline {
    return { schema: 'uve-timeline', version: 1, frameRate, fps, sources: [], tracks: [], clips: [], markers: [], masterGainDb: 0 };
}

export function defaultTimeline(frameRate = DEFAULT_FRAME_RATE, fps = String(frameRate)): Timeline {
    const tl = emptyTimeline(frameRate, fps);
    tl.tracks = [makeTrack('video', 0), makeTrack('audio', 0)];
    return tl;
}

/* ------------------------------------------------------------------ lookups */

export function trackById(tl: Timeline, id: string): TimelineTrack | undefined { return tl.tracks.find(t => t.id === id); }
export function clipById(tl: Timeline, id: string): TimelineClip | undefined { return tl.clips.find(c => c.id === id); }
export function sourceById(tl: Timeline, id: string): TimelineSource | undefined { return tl.sources.find(s => s.id === id); }
export function mediaOf(tl: Timeline, clip: TimelineClip): MediaInfo | undefined { return sourceById(tl, clip.sourceId)?.media; }

/** Clips of one track, ordered by start. */
export function clipsOn(tl: Timeline, trackId: string): TimelineClip[] {
    return tl.clips.filter(c => c.trackId === trackId).sort((a, b) => a.start - b.start || a.id.localeCompare(b.id));
}

export function timelineDuration(tl: Timeline): number {
    let end = 0;
    for (const c of tl.clips) end = Math.max(end, clipEnd(c));
    for (const m of tl.markers) end = Math.max(end, m.t);
    return end;
}

export function clipsUsingSource(tl: Timeline, sourceId: string): TimelineClip[] {
    return tl.clips.filter(c => c.sourceId === sourceId);
}

/* ------------------------------------------------------------------ transitions */

export interface HandleSides { hL: number; hR: number }

/** A transition of D frames straddles the cut: hL frames left of it, hR right of it. */
export function handleSides(durSec: number, F: number): HandleSides {
    const D = Math.max(1, Math.round(durSec * F));
    const hL = Math.floor(D / 2);
    return { hL, hR: D - hL };
}

export function transitionFrames(durSec: number, F: number): number { return Math.max(1, Math.round(durSec * F)); }

export function isTransitionKind(k: unknown): k is TransitionKind {
    return typeof k === 'string' && (TRANSITION_KINDS as string[]).includes(k);
}

/** Source seconds a clip must still have available on one side for `frames` timeline frames. */
export function handleNeed(clip: TimelineClip, frames: number, F: number): number { return frames / F * clip.speed; }

/**
 * Source seconds available beyond the clip on one side.
 * Forward: left = media before srcIn, right = media after srcOut.
 * Reversed: the clip enters its media span at srcOut and leaves it at srcIn, so the
 * timeline-left handle reaches *after* srcOut and the timeline-right handle reaches *before* srcIn.
 */
export function mediaHeadroom(tl: Timeline, clip: TimelineClip, side: 'left' | 'right'): number {
    const media = mediaOf(tl, clip);
    if (!media) return 0;
    const total = Number.isFinite(media.duration) ? media.duration : 0;
    const left = clip.reverse ? total - srcOut(clip) : clip.srcIn;
    const right = clip.reverse ? clip.srcIn : total - srcOut(clip);
    return Math.max(0, side === 'left' ? left : right);
}

/* ------------------------------------------------------------------ validation */

export interface Issue { code: string; message: string; clipId?: string; trackId?: string }

export function validateTimeline(tl: Timeline): Issue[] {
    const out: Issue[] = [];
    const F = tl.frameRate;
    if (!Number.isFinite(F) || F <= 0) out.push({ code: 'frameRate', message: 'Invalid timeline frame rate.' });
    const trackIds = new Set(tl.tracks.map(t => t.id));
    const sourceIds = new Set(tl.sources.map(s => s.id));

    for (const c of tl.clips) {
        if (!trackIds.has(c.trackId)) out.push({ code: 'missingTrack', message: `Clip "${c.id}" points at a missing track.`, clipId: c.id });
        if (!sourceIds.has(c.sourceId)) out.push({ code: 'missingSource', message: `Clip "${c.id}" points at a missing source.`, clipId: c.id });
        if (!(c.dur > 0)) out.push({ code: 'zeroDur', message: `Clip "${c.id}" has zero length.`, clipId: c.id });
        if (c.start < 0) out.push({ code: 'negStart', message: `Clip "${c.id}" starts before 0.`, clipId: c.id });
        if (!(c.speed > 0) || !Number.isFinite(c.speed)) out.push({ code: 'speed', message: `Clip "${c.id}" has an invalid speed.`, clipId: c.id });
        if (c.srcIn < 0) out.push({ code: 'negIn', message: `Clip "${c.id}" has a negative in point.`, clipId: c.id });
        if (!isAligned(c.start, F) || !isAligned(c.dur, F)) out.push({ code: 'unaligned', message: `Clip "${c.id}" is not on the ${F} fps frame grid.`, clipId: c.id });
        const media = mediaOf(tl, c);
        if (media) {
            const total = Number.isFinite(media.duration) ? media.duration : 0;
            if (srcOut(c) > total + 1e-4) {
                out.push({ code: 'mediaShort', message: `Clip "${c.id}" runs past the end of its media (${srcOut(c).toFixed(3)} s > ${total.toFixed(3)} s).`, clipId: c.id });
            }
            const track = trackById(tl, c.trackId);
            if (track?.kind === 'video' && !media.video) out.push({ code: 'noVideo', message: `Clip "${c.id}" sits on ${track.name} but its media has no video stream.`, clipId: c.id, trackId: track.id });
            if (track?.kind === 'audio' && media.audio.length === 0) out.push({ code: 'noAudio', message: `Clip "${c.id}" sits on ${track.name} but its media has no audio stream.`, clipId: c.id, trackId: track.id });
        }
        for (const curve of [c.opacityCurve, c.levelCurve, c.gainCurve, c.panCurve]) {
            for (const p of curve) if (!Number.isFinite(p.t) || !Number.isFinite(p.v)) out.push({ code: 'curve', message: `Clip "${c.id}" has an invalid automation point.`, clipId: c.id });
        }
    }

    for (const track of tl.tracks) {
        const list = clipsOn(tl, track.id);
        for (let i = 1; i < list.length; i++) {
            const prev = list[i - 1], cur = list[i];
            if (clipEnd(prev) > cur.start + 1e-6) {
                out.push({ code: 'overlap', message: `Overlap on ${track.name}: clip "${prev.id}" ends after "${cur.id}" starts.`, clipId: cur.id, trackId: track.id });
            }
        }
        for (let i = 0; i < list.length; i++) {
            const cur = list[i];
            if (!cur.transition) continue;
            const prev = i > 0 ? list[i - 1] : undefined;
            if (!prev) {
                out.push({ code: 'trNoLeft', message: `Transition "${cur.transition.kind}" needs a clip directly to its left (same track, no gap).`, clipId: cur.id, trackId: track.id });
                continue;
            }
            if (clipEnd(prev) < cur.start - 1e-6) {
                out.push({ code: 'trGap', message: `Transition "${cur.transition.kind}" cannot bridge a gap.`, clipId: cur.id, trackId: track.id });
                continue;
            }
            // the window straddles the cut: hL frames sit in the left neighbour, hR in this clip
            const D = transitionFrames(cur.transition.dur, F);
            const { hL, hR } = handleSides(cur.transition.dur, F);
            if (hL >= frameOf(prev.dur, F) || hR >= frameOf(cur.dur, F)) {
                out.push({ code: 'trTooLong', message: `Transition "${cur.transition.kind}" (${D} frames = ${hL}+${hR}) does not fit its neighbours (${frameOf(prev.dur, F)} / ${frameOf(cur.dur, F)} frames).`, clipId: cur.id, trackId: track.id });
                continue;
            }
            const needL = handleNeed(cur, hL, F), needR = handleNeed(prev, hR, F);
            const haveL = mediaHeadroom(tl, cur, 'left'), haveR = mediaHeadroom(tl, prev, 'right');
            if (haveL + 1e-6 < needL || haveR + 1e-6 < needR) {
                out.push({
                    code: 'trHandles',
                    message: `Transition "${cur.transition.kind}" needs media handles: ${needL.toFixed(3)} s before the in point of ${cur.id} (only ${haveL.toFixed(3)} s available) and ${needR.toFixed(3)} s after the out point of ${prev.id} (only ${haveR.toFixed(3)} s available).`,
                    clipId: cur.id, trackId: track.id
                });
                continue;
            }
            // two transitions sharing the middle clip must not overlap
            const next = list[i + 1];
            if (next?.transition) {
                const s2 = handleSides(next.transition.dur, F);
                if (hR + s2.hL > frameOf(cur.dur, F)) {
                    out.push({ code: 'trOverlap', message: `Two transitions on ${track.name} overlap inside clip "${cur.id}".`, clipId: cur.id, trackId: track.id });
                }
            }
        }
    }
    return out;
}

export function hasSoloTrack(tl: Timeline): boolean { return tl.tracks.some(t => t.solo); }
/** With at least one solo track everything else is muted. */
export function trackAudible(tl: Timeline, track: TimelineTrack): boolean {
    if (track.muted) return false;
    return hasSoloTrack(tl) ? track.solo : true;
}
/** Track mute only affects audio; hidden removes the picture. */
export function trackVisible(track: TimelineTrack): boolean { return track.kind === 'video' && !track.hidden; }

/* ------------------------------------------------------------------ editing operations */

export type OpResult = { ok: true; tl: Timeline } | { ok: false; error: string };

const clone = (tl: Timeline): Timeline => structuredClone(tl);
const bad = (error: string): OpResult => ({ ok: false, error });
const good = (tl: Timeline): OpResult => ({ ok: true, tl });

/** Move every clip of a track that starts at/after `fromT` by `deltaSec`. */
function rippleAfter(tl: Timeline, trackId: string, fromT: number, deltaSec: number): void {
    if (deltaSec === 0) return;
    for (const c of tl.clips) {
        if (c.trackId !== trackId) continue;
        if (c.start >= fromT - 1e-9) c.start = snap(c.start + deltaSec, tl.frameRate);
    }
}

/** Drop transitions that the current geometry no longer supports (keeps the timeline renderable). */
function clearInvalidTransitions(tl: Timeline, trackIds: Set<string> | null): void {
    for (const issue of validateTimeline(tl)) {
        if (!issue.clipId || !issue.code.startsWith('tr')) continue;
        if (trackIds && !trackIds.has(issue.trackId ?? '')) continue;
        const c = clipById(tl, issue.clipId);
        if (c) c.transition = null;
    }
}

function rejectFor(out: Timeline, clipId: string, trackId: string): OpResult | null {
    const issues = validateTimeline(out).filter(i => i.clipId === clipId || i.trackId === trackId);
    return issues.length ? bad(issues[0].message) : null;
}

export function addSource(tl: Timeline, path: string, media: MediaInfo): OpResult {
    const out = clone(tl);
    const existing = out.sources.find(s => s.path === path);
    if (existing) existing.media = media;
    else out.sources.push({ id: newId('S'), path, media });
    const issues = validateTimeline(out);
    return issues.length ? bad(issues[0].message) : good(out);
}

export function removeSource(tl: Timeline, sourceId: string): OpResult {
    const used = clipsUsingSource(tl, sourceId).length;
    if (used) return bad(`Source is still used by ${used} clip(s).`);
    const out = clone(tl);
    out.sources = out.sources.filter(s => s.id !== sourceId);
    return good(out);
}

/** Relink a source to another file path; the caller refreshes `media` with a fresh probe. */
export function setSourcePath(tl: Timeline, sourceId: string, path: string, media?: MediaInfo): OpResult {
    const out = clone(tl);
    const s = sourceById(out, sourceId);
    if (!s) return bad('Source not found.');
    s.path = path;
    if (media) s.media = media;
    const issues = validateTimeline(out);
    return issues.length ? bad(issues[0].message) : good(out);
}

export function addTrack(tl: Timeline, kind: TrackKind, atIndex?: number): OpResult {
    const out = clone(tl);
    const track = makeTrack(kind, out.tracks.filter(t => t.kind === kind).length);
    const at = atIndex === undefined
        ? (kind === 'video' ? out.tracks.filter(t => t.kind === 'video').length : out.tracks.length)
        : atIndex;
    if (at < 0 || out.tracks.length === 0) out.tracks.push(track);
    else out.tracks.splice(Math.min(at, out.tracks.length), 0, track);
    return good(out);
}

/**
 * Orders the tracks in pairs: 1st video, 1st audio, 2nd video, 2nd audio, … (V1, A1, V2, A2, …); the
 * tracks of one kind keep their order among themselves, extra tracks of one kind follow at the end.
 * Upper tracks are drawn on top, so V1 stays the top picture layer.
 */
export function pairTracks(tl: Timeline): OpResult {
    const vids = tl.tracks.filter(t => t.kind === 'video'), auds = tl.tracks.filter(t => t.kind === 'audio');
    const out = clone(tl);
    out.tracks = [];
    for (let i = 0; i < Math.max(vids.length, auds.length); i++) {
        if (vids[i]) out.tracks.push(structuredClone(vids[i]));
        if (auds[i]) out.tracks.push(structuredClone(auds[i]));
    }
    return good(out);
}

export function removeTrack(tl: Timeline, trackId: string): OpResult {
    const used = tl.clips.filter(c => c.trackId === trackId).length;
    if (used) return bad(`Track still contains ${used} clip(s).`);
    const out = clone(tl);
    out.tracks = out.tracks.filter(t => t.id !== trackId);
    return good(out);
}

export function moveTrack(tl: Timeline, trackId: string, toIndex: number): OpResult {
    const from = tl.tracks.findIndex(t => t.id === trackId);
    if (from < 0) return bad('Track not found.');
    const out = clone(tl);
    const [t] = out.tracks.splice(from, 1);
    const at = Math.max(0, Math.min(out.tracks.length - 1, toIndex));
    out.tracks.splice(at, 0, t);
    return good(out);
}

export function patchTrack(tl: Timeline, trackId: string, patch: Partial<TimelineTrack>): OpResult {
    const out = clone(tl);
    const t = trackById(out, trackId);
    if (!t) return bad('Track not found.');
    Object.assign(t, patch, { id: t.id });
    return good(out);
}

export function insertClip(tl: Timeline, spec: {
    trackId: string; sourceId: string; start: number; dur: number; srcIn: number; mode?: 'ripple' | 'replace';
}): OpResult {
    const F = tl.frameRate;
    const track = trackById(tl, spec.trackId);
    if (!track) return bad('Target track not found.');
    const source = sourceById(tl, spec.sourceId);
    if (!source) return bad('Source not found.');
    if (track.locked) return bad(`${track.name} is locked.`);
    const start = snap(spec.start, F), dur = snap(spec.dur, F), srcIn = snap(spec.srcIn, F);
    if (dur <= 0) return bad('Clip length must be at least one frame.');
    if (start < 0) return bad('Clip starts before 0.');
    const total = Number.isFinite(source.media.duration) ? source.media.duration : 0;
    if (srcIn < 0 || srcIn + dur > total + 1e-4) return bad(`In point and length do not fit into the source (${total.toFixed(3)} s).`);

    const out = clone(tl);
    if ((spec.mode ?? 'ripple') === 'replace') {
        out.clips = out.clips.filter(c => !(c.trackId === track.id && clipEnd(c) > start + 1e-9 && c.start < start + dur - 1e-9));
    } else {
        rippleAfter(out, track.id, start, dur);
    }
    out.clips.push(makeClip({ trackId: track.id, sourceId: spec.sourceId, start, dur, srcIn }));
    const issues = validateTimeline(out).filter(i => i.trackId === track.id);
    return issues.length ? bad(issues[0].message) : good(out);
}

/** Drop a clip onto a (possibly) different track at `start`. */
export function moveClip(tl: Timeline, clipId: string, to: { trackId?: string; start: number }): OpResult {
    const F = tl.frameRate;
    const src = clipById(tl, clipId);
    if (!src) return bad('Clip not found.');
    const targetId = to.trackId ?? src.trackId;
    const track = trackById(tl, targetId);
    if (!track) return bad('Target track not found.');
    const srcTrack = trackById(tl, src.trackId);
    if (track.locked || srcTrack?.locked) return bad('Track is locked.');
    const start = snap(to.start, F);
    if (start < 0) return bad('Clip starts before 0.');

    const out = clone(tl);
    const clip = clipById(out, clipId)!;
    const blocked = out.clips.filter(c => c.trackId === targetId && c.id !== clipId
        && clipEnd(c) > start + 1e-9 && c.start < start + clip.dur - 1e-9);
    if (blocked.length) return bad(`Position blocked: ${clip.id} would overlap ${blocked.length} clip(s).`);
    clip.trackId = targetId;
    clip.start = start;
    clearInvalidTransitions(out, new Set([targetId, src.trackId]));
    return rejectFor(out, clipId, targetId) ?? good(out);
}

/** Split a clip at `at`; the right half keeps speed/settings and re-bases its curves, and the outer fades follow the edge they belong to. */
export function splitClip(tl: Timeline, clipId: string, at: number): { ok: true; tl: Timeline; info: { leftId: string; rightId: string } } | { ok: false; error: string } {
    const F = tl.frameRate;
    const src = clipById(tl, clipId);
    if (!src) return { ok: false, error: 'Clip not found.' };
    if (tl.tracks.find(t => t.id === src.trackId)?.locked) return { ok: false, error: 'Track is locked.' };
    const f = frameOf(snap(at, F), F);
    const c0 = frameOf(src.start, F), c1 = c0 + frameOf(src.dur, F);
    if (f <= c0 || f >= c1) return { ok: false, error: 'Split point is outside the clip.' };

    const out = clone(tl);
    const clip = clipById(out, clipId)!;
    const leftDur = secOf(f - c0, F);
    const rightDur = secOf(c1 - f, F);
    const right = makeClip({
        trackId: clip.trackId, sourceId: clip.sourceId,
        start: secOf(f, F), dur: rightDur, srcIn: snap(clip.srcIn + leftDur * clip.speed, F)
    });
    right.speed = clip.speed;
    right.reverse = clip.reverse;
    right.opacity = clip.opacity;
    right.level = clip.level;
    right.gainDb = clip.gainDb;
    right.pan = clip.pan;
    right.audioMuted = clip.audioMuted;
    right.hidden = clip.hidden;
    right.effect = clip.effect ? structuredClone(clip.effect) : null;
    right.opacityCurve = rebasedCurve(clip.opacityCurve, leftDur);
    right.levelCurve = rebasedCurve(clip.levelCurve, leftDur);
    right.gainCurve = rebasedCurve(clip.gainCurve, leftDur);
    right.panCurve = rebasedCurve(clip.panCurve, leftDur);

    // the fade on the original right edge belongs to the piece that keeps that edge
    right.fadeOut = clip.fadeOut;
    right.audioFadeOut = clip.audioFadeOut;
    clip.dur = leftDur;
    clip.fadeOut = 0;
    clip.audioFadeOut = 0;
    clip.opacityCurve = clip.opacityCurve.filter(p => p.t <= leftDur + 1e-9);
    clip.levelCurve = clip.levelCurve.filter(p => p.t <= leftDur + 1e-9);
    clip.gainCurve = clip.gainCurve.filter(p => p.t <= leftDur + 1e-9);
    clip.panCurve = clip.panCurve.filter(p => p.t <= leftDur + 1e-9);
    out.clips.push(right);

    const issues = validateTimeline(out).filter(i => i.clipId === clip.id || i.clipId === right.id);
    if (issues.length) return { ok: false, error: issues[0].message };
    return { ok: true, tl: out, info: { leftId: clip.id, rightId: right.id } };
}

/** Points of the right half, re-based to its start; the boundary value is interpolated and kept. */
function rebasedCurve(points: AutoPoint[], atT: number): AutoPoint[] {
    const tail = points.filter(p => p.t >= atT - 1e-9).map(p => ({ t: p.t - atT, v: p.v }));
    if (tail.length && tail[0].t > 1e-9) {
        const v = sampleCurve(points, atT);
        tail.unshift({ t: 0, v: v ?? tail[0].v });
    }
    return tail;
}

/** Linear interpolation, holding the end values; null when the curve is empty. */
export function sampleCurve(points: AutoPoint[], t: number): number | null {
    if (!points.length) return null;
    if (points.length === 1) return points[0].v;
    const s = [...points].sort((a, b) => a.t - b.t);
    if (t <= s[0].t) return s[0].v;
    const last = s[s.length - 1];
    if (t >= last.t) return last.v;
    for (let i = 1; i < s.length; i++) {
        const a = s[i - 1], b = s[i];
        if (t <= b.t + 1e-9) {
            const span = b.t - a.t;
            return span <= 1e-9 ? b.v : a.v + (b.v - a.v) * ((t - a.t) / span);
        }
    }
    return last.v;
}

export type CurveLane = 'opacity' | 'level' | 'gain' | 'pan';

function laneCurve(clip: TimelineClip, lane: CurveLane): AutoPoint[] {
    switch (lane) {
        case 'gain': return clip.gainCurve;
        case 'pan': return clip.panCurve;
        case 'opacity': return clip.opacityCurve;
        default: return clip.levelCurve;
    }
}

export function setCurvePoint(tl: Timeline, clipId: string, lane: CurveLane, t: number, v: number): OpResult {
    const F = tl.frameRate;
    const out = clone(tl);
    const clip = clipById(out, clipId);
    if (!clip) return bad('Clip not found.');
    if (!Number.isFinite(v)) return bad('Invalid automation value.');
    const list = laneCurve(clip, lane);
    const at = snap(Math.max(0, Math.min(t, clip.dur)), F);
    const hit = list.find(p => Math.abs(p.t - at) < 1e-9);
    if (hit) hit.v = v;
    else list.push({ t: at, v });
    list.sort((a, b) => a.t - b.t);
    return good(out);
}

export function removeCurvePoint(tl: Timeline, clipId: string, lane: CurveLane, t: number): OpResult {
    const out = clone(tl);
    const clip = clipById(out, clipId);
    if (!clip) return bad('Clip not found.');
    const list = laneCurve(clip, lane);
    const keep = list.filter(p => Math.abs(p.t - t) > 1e-6);
    if (keep.length === list.length) return bad('Point not found.');
    if (lane === 'gain') clip.gainCurve = keep;
    else if (lane === 'pan') clip.panCurve = keep;
    else if (lane === 'opacity') clip.opacityCurve = keep;
    else clip.levelCurve = keep;
    return good(out);
}

export function clearCurve(tl: Timeline, clipId: string, lane: CurveLane): OpResult {
    const out = clone(tl);
    const clip = clipById(out, clipId);
    if (!clip) return bad('Clip not found.');
    if (lane === 'gain') clip.gainCurve = [];
    else if (lane === 'pan') clip.panCurve = [];
    else if (lane === 'opacity') clip.opacityCurve = [];
    else clip.levelCurve = [];
    return good(out);
}

export function patchClip(tl: Timeline, clipId: string, patch: Partial<TimelineClip>): OpResult {
    const out = clone(tl);
    const clip = clipById(out, clipId);
    if (!clip) return bad('Clip not found.');
    Object.assign(clip, patch, { id: clip.id, trackId: clip.trackId });
    return rejectFor(out, clipId, clip.trackId) ?? good(out);
}

/**
 * Trim: `start`/`dur` are the NEW absolute timeline values. The in point follows the new start, and
 * extending past the media is rejected — that rejected amount is exactly the missing handle.
 */
export function trimClip(tl: Timeline, clipId: string, to: { start?: number; dur?: number }): OpResult {
    const F = tl.frameRate;
    const src = clipById(tl, clipId);
    if (!src) return bad('Clip not found.');
    if (tl.tracks.find(t => t.id === src.trackId)?.locked) return bad('Track is locked.');
    const out = clone(tl);
    const clip = clipById(out, clipId)!;
    const media = mediaOf(out, clip);
    const total = media && Number.isFinite(media.duration) ? media.duration : Infinity;

    const newStart = to.start === undefined ? clip.start : snap(to.start, F);
    const newDur = to.dur === undefined ? clip.dur : snap(to.dur, F);
    if (newDur <= 0) return bad('Clip length must be at least one frame.');
    if (newStart < 0) return bad('Clip starts before 0.');

    const dStart = newStart - clip.start;
    const dDur = newDur - clip.dur;
    // The media frame under the edge that did not move stays put. Forward the clip enters its
    // media at `srcIn`, so the in point follows the start; reversed plays the span backwards and
    // enters at `srcOut`, so trimming the left edge shortens the far end and the in point follows
    // the *right* edge instead.
    const dSrcIn = (clip.reverse ? -(dStart + dDur) : dStart) * clip.speed;
    const mediaIn = clip.srcIn;
    const mediaOut = srcOut(clip);
    const newSrcIn = snap(mediaIn + dSrcIn, F);
    if (newSrcIn < -1e-9) return bad(`Not enough media before the in point (${mediaIn.toFixed(3)} s available).`);
    if (newSrcIn + newDur * clip.speed > total + 1e-4) {
        const avail = Number.isFinite(total) ? Math.max(0, total - mediaOut) : 0;
        return bad(`Not enough media after the out point (${avail.toFixed(3)} s available).`);
    }

    clip.start = newStart;
    clip.dur = newDur;
    clip.srcIn = Math.max(0, newSrcIn);
    return rejectFor(out, clipId, clip.trackId) ?? good(out);
}

/** Trim the end of a clip and ripple everything after it on the same track. */
export function rippleTrimEnd(tl: Timeline, clipId: string, newDur: number): OpResult {
    const F = tl.frameRate;
    const src = clipById(tl, clipId);
    if (!src) return bad('Clip not found.');
    const dur = snap(newDur, F);
    const trimmed = trimClip(tl, clipId, { dur });
    if (!trimmed.ok) return trimmed;
    const clip = clipById(trimmed.tl, clipId)!;
    rippleAfter(trimmed.tl, clip.trackId, clipEnd(clip), dur - src.dur);
    clearInvalidTransitions(trimmed.tl, new Set([clip.trackId]));
    return rejectFor(trimmed.tl, clipId, clip.trackId) ?? good(trimmed.tl);
}

/** Change speed, keeping the consumed media span; ripples the rest of the track. */
export function setSpeed(tl: Timeline, clipId: string, speed: number): OpResult {
    const F = tl.frameRate;
    const src = clipById(tl, clipId);
    if (!src) return bad('Clip not found.');
    if (!(speed > 0) || speed > 100) return bad('Speed must be between 0 and 100.');
    const media = mediaOf(tl, src);
    const total = media && Number.isFinite(media.duration) ? media.duration : Infinity;
    const newDur = snap(srcSpan(src) / speed, F);
    if (newDur <= 0) return bad('Too short for this speed.');
    if (src.srcIn + srcSpan(src) > total + 1e-4) return bad('Clip runs past the end of its media.');
    const out = clone(tl);
    const clip = clipById(out, clipId)!;
    clip.speed = speed;
    clip.dur = newDur;
    rippleAfter(out, src.trackId, clipEnd(src), newDur - src.dur);
    clearInvalidTransitions(out, new Set([clip.trackId]));
    return rejectFor(out, clipId, clip.trackId) ?? good(out);
}

/** Set (kind=null clears) the transition centred on this clip's LEFT boundary. */
export function setTransition(tl: Timeline, clipId: string, kind: TransitionKind | null, durSec?: number): OpResult {
    const F = tl.frameRate;
    const src = clipById(tl, clipId);
    if (!src) return bad('Clip not found.');
    const out = clone(tl);
    const clip = clipById(out, clipId)!;
    if (kind === null) { clip.transition = null; return good(out); }
    if (!isTransitionKind(kind)) return bad('Unknown transition.');
    const track = trackById(out, clip.trackId);
    if (!track) return bad('Track not found.');
    const list = clipsOn(out, clip.trackId);
    const idx = list.findIndex(c => c.id === clip.id);
    if (idx <= 0) return bad('Transition needs a clip directly to its left (same track, no gap).');
    const prev = list[idx - 1];
    if (clipEnd(prev) < clip.start - 1e-6) return bad('Transition cannot bridge a gap.');
    const D = transitionFrames(durSec ?? clip.transition?.dur ?? 1, F);
    const { hL, hR } = handleSides(secOf(D, F), F);
    if (hL >= frameOf(prev.dur, F) || hR >= frameOf(clip.dur, F)) {
        return bad(`Transition (${D} frames = ${hL}+${hR}) does not fit its neighbours (${frameOf(prev.dur, F)} / ${frameOf(clip.dur, F)} frames).`);
    }
    clip.transition = { kind, dur: secOf(D, F) };
    const issue = validateTimeline(out).find(i => i.clipId === clip.id);
    if (issue) { clip.transition = null; return bad(issue.message); }
    return good(out);
}

/** 'ripple' closes the gap left behind, 'lift' (extract) keeps it. */
export function deleteClip(tl: Timeline, clipId: string, mode: 'ripple' | 'lift'): OpResult {
    const src = clipById(tl, clipId);
    if (!src) return bad('Clip not found.');
    if (tl.tracks.find(t => t.id === src.trackId)?.locked) return bad('Track is locked.');
    const out = clone(tl);
    out.clips = out.clips.filter(c => c.id !== clipId);
    if (mode === 'ripple') rippleAfter(out, src.trackId, clipEnd(src), -src.dur);
    clearInvalidTransitions(out, new Set([src.trackId]));
    const issues = validateTimeline(out).filter(i => i.trackId === src.trackId);
    return issues.length ? bad(issues[0].message) : good(out);
}

export function duplicateClip(tl: Timeline, clipId: string, at: number): OpResult {
    const src = clipById(tl, clipId);
    if (!src) return bad('Clip not found.');
    const res = insertClip(tl, { trackId: src.trackId, sourceId: src.sourceId, start: at, dur: src.dur, srcIn: src.srcIn });
    if (!res.ok) return res;
    const start = snap(at, tl.frameRate);
    const copy = res.tl.clips.find(c => c.trackId === src.trackId && c.id !== clipId && c.start === start && c.dur === snap(src.dur, tl.frameRate));
    if (!copy) return bad('Copy not found.');
    copy.speed = src.speed;
    copy.reverse = src.reverse;
    copy.opacity = src.opacity;
    copy.level = src.level;
    copy.fadeIn = src.fadeIn;
    copy.fadeOut = src.fadeOut;
    copy.gainDb = src.gainDb;
    copy.pan = src.pan;
    copy.audioFadeIn = src.audioFadeIn;
    copy.audioFadeOut = src.audioFadeOut;
    copy.audioMuted = src.audioMuted;
    copy.hidden = src.hidden;
    copy.opacityCurve = structuredClone(src.opacityCurve);
    copy.levelCurve = structuredClone(src.levelCurve);
    copy.gainCurve = structuredClone(src.gainCurve);
    copy.panCurve = structuredClone(src.panCurve);
    copy.effect = src.effect ? structuredClone(src.effect) : null;
    return good(res.tl);
}

/* ------------------------------------------------------------------ markers */

export function addMarker(tl: Timeline, t: number, name = ''): OpResult {
    const out = clone(tl);
    const marker: TimelineMarker = { id: out.markers.reduce((m, x) => Math.max(m, x.id), 0) + 1, t: snap(t, tl.frameRate), name };
    out.markers.push(marker);
    out.markers.sort((a, b) => a.t - b.t);
    return good(out);
}

export function removeMarker(tl: Timeline, id: number): OpResult {
    const out = clone(tl);
    const before = out.markers.length;
    out.markers = out.markers.filter(m => m.id !== id);
    return out.markers.length === before ? bad('Marker not found.') : good(out);
}

export function renameMarker(tl: Timeline, id: number, name: string): OpResult {
    const out = clone(tl);
    const m = out.markers.find(x => x.id === id);
    if (!m) return bad('Marker not found.');
    m.name = name;
    return good(out);
}

/* ------------------------------------------------------------------ snapping */

export function snapPoints(tl: Timeline): number[] {
    const F = tl.frameRate;
    const frames = new Set<number>([0]);
    for (const c of tl.clips) { frames.add(frameOf(c.start, F)); frames.add(frameOf(clipEnd(c), F)); }
    for (const m of tl.markers) frames.add(frameOf(m.t, F));
    return [...frames].sort((a, b) => a - b).map(f => secOf(f, F));
}

/** Nearest candidate within `tol` frames; snaps to the grid when nothing is close. */
export function snapTime(tl: Timeline, t: number, tol = 6): { t: number; snapped: boolean } {
    const F = tl.frameRate;
    let best = t * F, bestDist = tol + 1e-9;
    for (const p of snapPoints(tl)) {
        const pf = p * F;
        const d = Math.abs(pf - t * F);
        if (d < bestDist) { bestDist = d; best = pf; }
    }
    return bestDist <= tol ? { t: secOf(Math.round(best), F), snapped: true } : { t: snap(t, F), snapped: false };
}

/* ------------------------------------------------------------------ render plan */

export interface PlanClip {
    clip: TimelineClip;
    source: TimelineSource;
    track: TimelineTrack;
    startF: number; endF: number;
    /** handle frames this clip must supply left/right because of a neighbouring transition */
    headF: number; tailF: number;
    /** frames this clip's media stream must contain */
    streamFrames: number;
    /** source span to decode, seconds */
    decodeStart: number; decodeEnd: number;
    /** export must pre-reverse this clip's span (video + audio) before compositing */
    reversePrepass: boolean;
    audible: boolean;
    visible: boolean;
}

export interface PlanLayer {
    clipId: string; sourceId: string; trackId: string; kind: TrackKind;
    /** 0 = drawn first (bottom); higher = on top */
    z: number;
    role: 'single' | 'a' | 'b';
    /** first timeline frame at which this clip's media stream is read */
    streamStartF: number;
    transitionKind: TransitionKind | null;
    /** transition window in timeline frames (0 when role === 'single') */
    transStartF: number; transFrames: number;
}

export interface PlanWindow { startF: number; endF: number; frames: number; video: PlanLayer[]; audio: PlanLayer[] }

export interface TimelinePlan {
    frameRate: number;
    /** frames the pipeline must produce for this range */
    totalFrames: number;
    /** absolute timeline frame where the range begins (0 unless the plan covers only a range) */
    rangeStartF: number;
    clips: PlanClip[];
    windows: PlanWindow[];
    issues: Issue[];
}

interface TrackPiece {
    startF: number; endF: number; kind: TransitionKind | null;
    /** Full transition window; a piece that is only part of it still reports the whole one so `p` stays continuous. */
    transStartF: number; transFrames: number;
    layers: { clip: TimelineClip; role: 'single' | 'a' | 'b' }[]
}

/** One track -> non-overlapping pieces: single-clip spans plus transition pieces straddling a cut. */
function piecesOfTrack(tl: Timeline, track: TimelineTrack): TrackPiece[] {
    const F = tl.frameRate;
    const list = clipsOn(tl, track.id);
    interface Link { w0: number; w1: number; left: TimelineClip; cur: TimelineClip; kind: TransitionKind }
    const links: Link[] = [];
    for (let i = 1; i < list.length; i++) {
        const cur = list[i], prev = list[i - 1], tr = cur.transition;
        if (!tr) continue;
        const { hL, hR } = handleSides(tr.dur, F);
        const cutF = frameOf(clipEnd(prev), F);
        links.push({ w0: cutF - hL, w1: cutF - hL + hL + hR, left: prev, cur, kind: tr.kind });
    }
    const marks = new Set<number>();
    for (const c of list) {
        const s = frameOf(c.start, F);
        marks.add(s); marks.add(s + frameOf(c.dur, F));
    }
    for (const l of links) { marks.add(l.w0); marks.add(l.w1); }
    const edges = [...marks].sort((a, b) => a - b);
    const pieces: TrackPiece[] = [];
    for (let i = 0; i + 1 < edges.length; i++) {
        const a = edges[i], b = edges[i + 1];
        if (b <= a) continue;
        const mid = (a + b) / 2;
        const link = links.find(l => l.w0 <= mid && mid < l.w1);
        if (link) {
            pieces.push({
                startF: a, endF: b, kind: link.kind,
                transStartF: link.w0, transFrames: link.w1 - link.w0,
                layers: [{ clip: link.left, role: 'a' }, { clip: link.cur, role: 'b' }]
            });
            continue;
        }
        for (const c of list) {
            const s = frameOf(c.start, F);
            if (s <= mid && mid < s + frameOf(c.dur, F)) {
                pieces.push({ startF: a, endF: b, kind: null, transStartF: 0, transFrames: 0, layers: [{ clip: c, role: 'single' }] });
            }
        }
    }
    return pieces.sort((a, b) => a.startF - b.startF);
}

/** Handle frames a clip must deliver on one side because of a neighbouring transition. */
function handleOf(F: number, list: TimelineClip[], index: number, side: 'left' | 'right'): number {
    const clip = list[index];
    if (side === 'left') return clip.transition ? handleSides(clip.transition.dur, F).hL : 0;
    const next = list[index + 1];
    return next?.transition ? handleSides(next.transition.dur, F).hR : 0;
}

/** Full render plan: per-clip decode spans plus the per-window layer schedule. */
export function buildPlan(tl: Timeline, fromT?: number, toT?: number): TimelinePlan {
    const F = tl.frameRate;
    const issues = validateTimeline(tl);
    const byTrack = tl.tracks.map(track => ({ track, clips: clipsOn(tl, track.id), pieces: piecesOfTrack(tl, track) }));

    const handles = new Map<string, { headF: number; tailF: number }>();
    for (const { clips: list } of byTrack) {
        for (let i = 0; i < list.length; i++) {
            handles.set(list[i].id, { headF: handleOf(F, list, i, 'left'), tailF: handleOf(F, list, i, 'right') });
        }
    }

    const boundaries = new Set<number>();
    for (const { pieces } of byTrack) for (const p of pieces) { boundaries.add(p.startF); boundaries.add(p.endF); }
    const naturalEnd = Math.max(0, ...boundaries);
    const fromF = fromT === undefined ? 0 : Math.max(0, frameOf(fromT, F));
    const toF = toT === undefined ? naturalEnd : Math.min(naturalEnd, frameOf(toT, F));

    const windows: PlanWindow[] = [];
    const marks = [...new Set([...boundaries, fromF, toF])].filter(f => f >= fromF && f <= toF).sort((a, b) => a - b);
    for (let i = 0; i + 1 < marks.length; i++) {
        const a = marks[i], b = marks[i + 1];
        if (b <= a) continue;
        const mid = (a + b) / 2;
        const video: PlanLayer[] = [];
        const audio: PlanLayer[] = [];
        let z = 0;
        for (let ti = tl.tracks.length - 1; ti >= 0; ti--) {
            const { track, pieces } = byTrack[ti];
            const piece = pieces.find(p => p.startF <= mid && mid < p.endF);
            if (!piece) continue;
            const visible = trackVisible(track);
            const audible = trackAudible(tl, track);
            if (!visible && !audible) continue;
            for (const l of piece.layers) {
                const clipF = frameOf(l.clip.start, F);
                const src = sourceById(tl, l.clip.sourceId);
                // a picture layer needs a real video stream, and `hidden` clips are dropped here too so
                // that the window schedule and PlanClip.visible describe the same picture
                const draws = visible && src !== undefined && src.media.video !== null && !l.clip.hidden;
                const layer: PlanLayer = {
                    clipId: l.clip.id, sourceId: l.clip.sourceId, trackId: track.id, kind: track.kind,
                    z: draws ? z++ : 0,
                    role: l.role,
                    streamStartF: clipF - (handles.get(l.clip.id)?.headF ?? 0),
                    transitionKind: piece.kind,
                    transStartF: piece.transStartF,
                    transFrames: piece.transFrames
                };
                if (draws) video.push(layer);
                if (audible) audio.push(layer);
            }
        }
        windows.push({ startF: a, endF: b, frames: b - a, video, audio });
    }

    const clips: PlanClip[] = [];
    for (const { track, clips: list } of byTrack) {
        for (let i = 0; i < list.length; i++) {
            const clip = list[i];
            const source = sourceById(tl, clip.sourceId);
            if (!source) continue;
            const headF = handles.get(clip.id)?.headF ?? 0;
            const tailF = handles.get(clip.id)?.tailF ?? 0;
            const startF = frameOf(clip.start, F);
            const headSec = handleNeed(clip, headF, F);
            const tailSec = handleNeed(clip, tailF, F);
            const total = Number.isFinite(source.media.duration) ? source.media.duration : Infinity;
            const decodeStart = Math.max(0, snap(clip.srcIn - (clip.reverse ? tailSec : headSec), F));
            const rawEnd = snap(srcOut(clip) + (clip.reverse ? headSec : tailSec), F);
            clips.push({
                clip, source, track,
                startF, endF: startF + frameOf(clip.dur, F),
                headF, tailF,
                streamFrames: frameOf(clip.dur, F) + headF + tailF,
                decodeStart,
                decodeEnd: Math.max(decodeStart, rawEnd > total ? total : rawEnd),
                reversePrepass: clip.reverse,
                audible: trackAudible(tl, track) && !clip.audioMuted,
                visible: trackVisible(track) && !clip.hidden && source.media.video !== null
            });
        }
    }

    return {
        frameRate: F,
        totalFrames: Math.max(0, toF - fromF),
        rangeStartF: fromF,
        clips,
        windows,
        issues
    };
}

/**
 * Source time (seconds) of a clip's media at a timeline frame, handles included. `frame` is an
 * absolute timeline frame number, `F` the plan's frame rate.
 */
export function srcTimeAt(p: { clip: TimelineClip; startF: number }, frame: number, F: number): number {
    const c = p.clip;
    const rel = (frame - p.startF) / F;
    return c.reverse ? srcOut(c) - rel * c.speed : c.srcIn + rel * c.speed;
}

/**
 * Where a clip's media stream begins on the timeline, for a window that reads it early
 * (transition head) or normally.
 */
export function streamStartFrameOf(tl: Timeline, clip: TimelineClip): number {
    const list = clipsOn(tl, clip.trackId);
    const i = list.findIndex(c => c.id === clip.id);
    return frameOf(clip.start, tl.frameRate) - handleOf(tl.frameRate, list, i, 'left');
}

/* ------------------------------------------------------------------ audio graph (ffmpeg) */

/** Equal-power-free, linear crossfade law — identical to the preview mixer. */
export function audioWindowWeight(role: 'a' | 'b', p: number): number {
    return role === 'a' ? 1 - p : p;
}

/** Pan law: linear, `-1..1` -> left/right gains (matches the preview ChannelMerger gains). */
export function panLaw(pan: number): { left: number; right: number } {
    const x = Math.max(-1, Math.min(1, pan));
    return { left: Math.min(1, 1 - x), right: Math.min(1, 1 + x) };
}


/**
 * Stereo fold of a source layout: coefficient per ffmpeg channel index (FL FR FC LFE BL BR SLs SRs).
 * Surrounds fold at 1/sqrt(2), the LFE is dropped. Export runs it through ffmpeg's `pan`, the preview
 * mixer through a ChannelSplitter with the same numbers, so both land on the same stereo field.
 */
const SURROUND = 0.7071067811865476;
const CHANNEL_FOLD: Record<string, { l: number[]; r: number[] }> = {
    mono: { l: [1], r: [1] },
    stereo: { l: [1, 0], r: [0, 1] },
    '2.1': { l: [1, 0, 0], r: [0, 1, 0] },
    quad: { l: [1, 0, SURROUND, 0], r: [0, 1, 0, SURROUND] },
    '5.0(side)': { l: [1, 0, SURROUND, SURROUND, 0], r: [0, 1, SURROUND, 0, SURROUND] },
    '5.1': { l: [1, 0, SURROUND, 0, SURROUND, SURROUND], r: [0, 1, SURROUND, 0, SURROUND, SURROUND] },
    '5.1(side)': { l: [1, 0, SURROUND, 0, SURROUND, SURROUND], r: [0, 1, SURROUND, 0, SURROUND, SURROUND] },
    '7.1': { l: [1, 0, SURROUND, 0, SURROUND, SURROUND, SURROUND, SURROUND], r: [0, 1, SURROUND, 0, SURROUND, SURROUND, SURROUND, SURROUND] }
};

/** `null` for layouts we do not fold ourselves; ffmpeg's own rematrix takes those. */
export function channelFold(stream: AudioStreamInfo): { l: number[]; r: number[] } | null {
    const named = CHANNEL_FOLD[(stream.layout ?? '').trim()];
    if (named) return named;
    if (stream.channels === 1) return CHANNEL_FOLD.mono;
    if (stream.channels === 2) return CHANNEL_FOLD.stereo;
    return null;
}

/** ffmpeg's `pan` only accepts flat `coefficient*cN` sums: no parentheses, no channel name before `*`. */
function panFilter(fold: { l: number[]; r: number[] }, left: number, right: number): string {
    const side = (coeffs: number[], gain: number) => {
        const terms = coeffs.map((c, i) => (c === 0 ? null : `${num(c * gain)}*c${i}`)).filter((s): s is string => s !== null);
        return terms.length ? terms.join('+') : `0*c0`;
    };
    return `pan=stereo|c0=${side(fold.l, left)}|c1=${side(fold.r, right)}`;
}

/** Filters that bring one clip's audio stream to stereo with the pan law applied. */
function channelFilters(stream: AudioStreamInfo, left: number, right: number): string[] {
    const fold = channelFold(stream);
    if (fold) return [panFilter(fold, left, right)];
    return [
        `aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo`,
        `pan=stereo|c0=${num(left)}*c0|c1=${num(right)}*c1`
    ];
}

/** ffmpeg's `atempo` scales only 0.5..2 per instance, so wider speeds become a chain of factors. */
function atempoFilters(speed: number): string[] {
    if (Math.abs(speed - 1) < 1e-9) return [];
    const factors: number[] = [];
    let rest = speed;
    while (rest > 2 + 1e-9) { factors.push(2); rest /= 2; }
    while (rest < 0.5 - 1e-9) { factors.push(0.5); rest *= 2; }
    if (Math.abs(rest - 1) > 1e-9) factors.push(rest);
    return factors.map(f => `atempo=${num(f)}`);
}

export interface AudioClipFilter {
    plan: PlanClip;
    /** ffmpeg input index this clip's audio was added at */
    inputIndex: number;
    /** stream specifier, e.g. '0:a:1' */
    stream: string;
    /** source seconds to read */
    readStart: number;
    readDur: number;
    /** timeline frame at which this stream begins (for adelay) */
    streamStartF: number;
    filter: string;
}

export interface AudioGraph {
    inputs: { path: string; ss: number; t: number }[];
    chains: AudioClipFilter[];
    filterComplex: string;
    /** label of the final mixed stream, e.g. 'amix_out' */
    outLabel: string;
}

/**
 * The time variable an expression is evaluated against. A chain whose read window was clipped at the
 * range boundary (or at the media start) no longer begins at stream time 0, so its expressions run on
 * `(t+skip)` and stay anchored to the clip's own stream.
 */
function timeToken(skipSec: number): string {
    return skipSec > 1e-9 ? `(t+${num(skipSec)})` : 't';
}

function fadeTerm(start: number, dur: number, kind: 'in' | 'out', T = 't'): string | null {
    if (dur <= 1e-9) return null;
    return kind === 'in'
        ? `min(1\\,max(0\\,(${T}-${num(start)})/${num(dur)}))`
        : `min(1\\,max(0\\,(${num(start + dur)}-${T})/${num(dur)}))`;
}

function curveTerm(points: AutoPoint[], shift: number, T = 't'): string | null {
    if (!points.length) return null;
    const s = [...points].sort((a, b) => a.t - b.t).map(p => ({ t: p.t + shift, v: p.v }));
    if (s.length === 1) return num(s[0].v);
    let expr = num(s[s.length - 1].v);
    for (let i = s.length - 1; i >= 1; i--) {
        const a = s[i - 1], b = s[i];
        const span = Math.max(1e-9, b.t - a.t);
        expr = `if(lt(${T}\\,${num(b.t)})\\,${num(a.v)}+(${num(b.v)}-${num(a.v)})*(${T}-${num(a.t)})/${num(span)}\\,${expr})`;
    }
    return `if(lt(${T}\\,${num(s[0].t)})\\,${num(s[0].v)}\\,${expr})`;
}

/**
 * Build the ffmpeg audio graph for a timeline range: one input per contributing clip (the same file can
 * appear several times with different -ss/-t), one chain per clip that reads exactly the source span the
 * clip contributes to this range, and one `amix` trimmed and padded to the exact range length.
 * Chain time 0 is the first sample the chain carries; expressions re-anchor it to the clip's stream.
 */
export function buildAudioGraph(tl: Timeline, plan: TimelinePlan, opts: {
    fromF?: number; toF?: number;
    /** clips whose audio must be skipped (already mixed elsewhere / no audio stream) */
    skipClipIds?: Set<string>;
    /** substitute media path for clips that were pre-processed (e.g. reversed) */
    pathOverride?: Map<string, string>;
} = {}): AudioGraph | { error: string } {
    const F = plan.frameRate;
    const fromF = opts.fromF ?? plan.rangeStartF;
    const toF = opts.toF ?? plan.rangeStartF + plan.totalFrames;
    const inputs: AudioGraph['inputs'] = [];
    const chains: AudioClipFilter[] = [];

    for (const p of plan.clips) {
        if (p.endF + p.tailF <= fromF || p.startF - p.headF >= toF) continue;
        if (!p.audible) continue;
        if (opts.skipClipIds?.has(p.clip.id)) continue;
        const media = p.source.media;
        const streams = media.audio ?? [];
        if (!streams.length) continue;
        const stream = streams[0];
        const S = p.clip.speed;                        // source seconds per timeline second
        const headT = secOf(p.headF, F);               // timeline seconds the stream carries before the in point
        const tailT = secOf(p.tailF, F);
        const clipT = p.clip.dur;
        const srcOutSec = p.clip.srcIn + clipT * S;    // media time at the clip's out point
        const rev = p.clip.reverse;
        const carrier = opts.pathOverride?.get(p.clip.id);
        const viaTemp = rev && carrier !== undefined;
        const streamStartF = p.startF - p.headF;
        const fLo = Math.max(fromF, streamStartF);
        const fHi = Math.min(toF, p.endF + p.tailF);
        if (fHi <= fLo) continue;
        // clip timeline frame <-> original media second (a reversed clip maps backwards)
        const mediaAtFrame = (f: number): number =>
            rev ? srcOutSec - ((f - p.startF) / F) * S : p.clip.srcIn + ((f - p.startF) / F) * S;
        const frameAtMedia = (m: number): number =>
            rev ? p.startF + ((srcOutSec - m) / S) * F : p.startF + ((m - p.clip.srcIn) / S) * F;
        const mediaDur = Number.isFinite(media.duration) ? media.duration : Infinity;
        // the source span this clip's audio stream wants, cut by the range and by what the carrier holds
        const idealLo = rev ? p.clip.srcIn - tailT * S : p.clip.srcIn - headT * S;
        const idealHi = rev ? srcOutSec + headT * S : srcOutSec + tailT * S;
        const spanLo = Math.min(mediaAtFrame(fLo), mediaAtFrame(fHi));
        const spanHi = Math.max(mediaAtFrame(fLo), mediaAtFrame(fHi));
        const lo = Math.max(idealLo, spanLo, viaTemp ? p.decodeStart : 0);
        const hi = Math.min(idealHi, spanHi, viaTemp ? p.decodeEnd : mediaDur);
        if (hi <= lo) continue;
        // a reversed carrier (the pre-reversed temp) holds [decodeStart, decodeEnd] flipped: original media
        // m sits at temp time decodeEnd - m, and the stream starts at its HIGHEST media time
        const readStart = viaTemp ? p.decodeEnd - hi : lo;
        const readDur = hi - lo;
        const firstFrame = rev ? frameAtMedia(hi) : frameAtMedia(lo);
        const skipSec = Math.max(0, (rev ? idealHi - hi : lo - idealLo) / S);
        const path = carrier ?? p.source.path;
        const inputIndex = inputs.length;
        inputs.push({ path, ss: readStart, t: readDur });

        // `t` in every expression is chain time; `(t+skip)` puts it back on the clip's own stream, so
        // tempo scales inside `atempo` and every fade window stays in timeline units.
        const T = timeToken(skipSec);
        const gain = dbToLin(p.clip.gainDb + p.track.gainDb + tl.masterGainDb);
        const terms: string[] = [num(gain)];

        const fadeIn = fadeTerm(headT, Math.min(p.clip.audioFadeIn, p.clip.dur), 'in', T);
        const fadeOut = fadeTerm(headT + clipT - Math.min(p.clip.audioFadeOut, p.clip.dur), Math.min(p.clip.audioFadeOut, p.clip.dur), 'out', T);
        if (fadeIn) terms.push(fadeIn);
        if (fadeOut) terms.push(fadeOut);

        // crossfade ramps: transition on this clip's LEFT boundary (clip is the incoming side)
        // and on its RIGHT boundary (next clip carries it; clip is the outgoing side)
        const nextClip = nextClipOf(tl, p.clip);
        for (const side of ['left', 'right'] as const) {
            const tr = side === 'left' ? p.clip.transition : nextClip?.transition;
            if (!tr) continue;
            const { hL, hR } = handleSides(tr.dur, F);
            const cutF = side === 'left' ? p.startF : p.endF;
            const start = secOf(cutF - hL - streamStartF, F);
            const rampDur = secOf(hL + hR, F);
            const ramp = side === 'left'
                ? `min(1\\,max(0\\,(${T}-${num(start)})/${num(rampDur)}))`
                : `(1-min(1\\,max(0\\,(${T}-${num(start)})/${num(rampDur)})))`;
            terms.push(ramp);
        }

        const automation = curveTerm(p.clip.gainCurve, headT, T);
        if (automation) terms.push(automation);

        const pan = panLaw(p.clip.pan + p.track.pan);
        // placement in the range: the samples that survived the cut begin at `firstFrame`, so the chain
        // waits exactly that long and `adelay` materialises the silence in front of it
        const delayMs = Math.max(0, Math.round((firstFrame - fromF) * 1000 / F));
        const position = delayMs > 0 ? `adelay=delays=${delayMs}:all=1` : null;
        // channel-wise law: the fold map carries the pan gains, so panning never collapses the field
        const filter = [
            `asetpts=PTS-STARTPTS`,
            ...(rev && !viaTemp ? ['areverse'] : []),
            ...atempoFilters(S),
            `aformat=sample_fmts=fltp:sample_rates=48000`,
            `volume=eval=frame:volume='${terms.join('*')}'`,
            ...channelFilters(stream, pan.left, pan.right),
            position
        ].filter(Boolean).join(',');

        chains.push({
            plan: p, inputIndex, stream: `${inputIndex}:a:${media.audio.indexOf(stream)}`,
            readStart, readDur, streamStartF, filter
        });
    }

    if (!chains.length) return { error: 'No audio tracks in this range.' };
    const labels = chains.map((_, i) => `ua${i}`);
    const filterComplex = chains.map((c, i) => `[${c.stream}]${c.filter}[${labels[i]}]`).join(';');
    const mix = chains.length === 1
        ? `[${labels[0]}]anull[aout]`
        : `${labels.map(l => `[${l}]`).join('')}amix=inputs=${chains.length}:duration=longest:normalize=0:dropout_transition=0:weights=${chains.map(() => '1').join(' ')}[aout]`;
    const totalSec = secOf(toF - fromF, F);
    return {
        inputs,
        chains,
        filterComplex: `${filterComplex};${mix};[aout]apad=whole_dur=${num(totalSec)},atrim=0:${num(totalSec)},aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo[aout]`,
        outLabel: 'aout'
    };
}

function nextClipOf(tl: Timeline, clip: TimelineClip): TimelineClip | undefined {
    const list = clipsOn(tl, clip.trackId);
    return list[list.findIndex(c => c.id === clip.id) + 1];
}

/* ------------------------------------------------------------------ schema v1 -> v2 */

/**
 * A v1 project (single media + in/out points) becomes one clip on the first video track using the
 * project look, so the rendered result is unchanged. v1 is never written again.
 */
export function migrateProjectV1toV2(v1: UveProjectV1, media: MediaInfo, frameRate = DEFAULT_FRAME_RATE): UveProjectV2 {
    const F = Number.isFinite(frameRate) && frameRate > 0 ? frameRate : DEFAULT_FRAME_RATE;
    const tl = defaultTimeline(F, String(F));
    const srcId = newId('S');
    tl.sources = [{ id: srcId, path: v1.media?.path ?? '', media }];
    const inPoint = snap(Math.max(0, v1.inPoint ?? 0), F);
    const total = Number.isFinite(media.duration) ? media.duration : 0;
    const outPoint = snap(Math.max(inPoint, v1.outPoint ?? total), F);
    if (outPoint > inPoint && v1.media?.path) {
        tl.clips = [makeClip({ trackId: tl.tracks[0].id, sourceId: srcId, start: 0, dur: outPoint - inPoint, srcIn: inPoint })];
    }
    return {
        schema: 'uve-project', version: 2, app: v1.app, uveSource: v1.uveSource, savedAt: v1.savedAt,
        effect: v1.effect, timeline: tl, export: v1.export,
        ui: { wipe: v1.ui?.wipe ?? 0.5, compare: v1.ui?.compare ?? true, timelineZoom: 90 }
    };
}

/* ------------------------------------------------------------------ media helpers */

/** Project frame rate of a probe: the exact stream rate when known (29.97 stays 29.97). */
export function frameRateFromMedia(media: MediaInfo, fallback = DEFAULT_FRAME_RATE): number {
    const v = media.video ?? media.videoStreams[0];
    if (v && Number.isFinite(v.fpsNum) && Number.isFinite(v.fpsDen) && v.fpsDen > 0 && v.fpsNum > 0) return v.fpsNum / v.fpsDen;
    if (v && Number.isFinite(v.fps) && v.fps > 0) return v.fps;
    return fallback;
}

/** ffmpeg `-r` argument for a project: rational when the source rate is rational. */
export function fpsArgument(media: MediaInfo, F: number): string {
    const v = media.video ?? media.videoStreams[0];
    if (v && v.fpsNum > 0 && v.fpsDen > 0) return v.fpsDen === 1 ? String(v.fpsNum) : `${v.fpsNum}/${v.fpsDen}`;
    return Number.isInteger(F) ? String(F) : F.toFixed(6);
}

export function emptyMediaInfo(path: string): MediaInfo {
    return {
        path, name: path.replace(/^.*[\\/]/, ''), size: 0, mtimeMs: 0, container: '', containerLong: '',
        duration: 0, startTime: 0, bitRate: 0, video: null, videoStreams: [], audio: [], subtitles: [],
        encrypted: false, warnings: ['Media not probed yet.']
    };
}

/* ------------------------------------------------------------------ defensive parsing of saved timelines */

const numOr = (v: unknown, d: number): number => (typeof v === 'number' && Number.isFinite(v) ? v : d);
const strOr = (v: unknown, d: string): string => (typeof v === 'string' ? v : d);
const boolOr = (v: unknown, d: boolean): boolean => (typeof v === 'boolean' ? v : d);
const recOf = (v: unknown): Record<string, unknown> => (v && typeof v === 'object' ? v as Record<string, unknown> : {});
const listOf = (v: unknown): unknown[] => Array.isArray(v) ? v : [];

function curveOr(v: unknown): AutoPoint[] {
    return listOf(v)
        .map(p => recOf(p))
        .filter(p => Number.isFinite(p.t) && Number.isFinite(p.v))
        .map(p => ({ t: p.t as number, v: p.v as number }))
        .sort((a, b) => a.t - b.t);
}

/**
 * Reads a saved timeline defensively: every field falls back to a sane default, unusable entries are
 * dropped, ids are preserved. Never trusts a project file (it is plain JSON on disk).
 */
export function sanitizeTimeline(raw: unknown, opts: { frameRate?: number; parseEffect?: (e: unknown) => EffectState } = {}): Timeline {
    const o = recOf(raw);
    const F = numOr(o.frameRate, opts.frameRate ?? DEFAULT_FRAME_RATE);
    const rate = F > 0 ? F : DEFAULT_FRAME_RATE;
    const tl: Timeline = {
        schema: 'uve-timeline', version: 1, frameRate: rate,
        fps: strOr(o.fps, '') || (Number.isInteger(rate) ? String(rate) : rate.toFixed(6)),
        sources: [], tracks: [], clips: [], markers: [],
        masterGainDb: numOr(o.masterGainDb, 0)
    };
    for (const s of listOf(o.sources)) {
        const rec = recOf(s);
        const path = strOr(rec.path, '');
        if (!path) continue;
        tl.sources.push({
            id: strOr(rec.id, newId('S')), path,
            media: rec.media && typeof rec.media === 'object' ? rec.media as MediaInfo : emptyMediaInfo(path)
        });
    }
    for (const t of listOf(o.tracks)) {
        const rec = recOf(t);
        const kind: TrackKind = rec.kind === 'audio' ? 'audio' : 'video';
        tl.tracks.push({
            id: strOr(rec.id, newId('T')),
            name: strOr(rec.name, kind === 'video' ? `V${tl.tracks.length + 1}` : `A${tl.tracks.length + 1}`),
            kind,
            locked: boolOr(rec.locked, false), muted: boolOr(rec.muted, false),
            hidden: boolOr(rec.hidden, false), solo: boolOr(rec.solo, false),
            gainDb: numOr(rec.gainDb, 0), pan: numOr(rec.pan, 0)
        });
    }
    if (!tl.tracks.length) tl.tracks = [makeTrack('video', 0), makeTrack('audio', 0)];
    for (const c of listOf(o.clips)) {
        const rec = recOf(c);
        const clip = makeClip({
            trackId: strOr(rec.trackId, ''), sourceId: strOr(rec.sourceId, ''),
            start: Math.max(0, numOr(rec.start, 0)), dur: numOr(rec.dur, 0), srcIn: Math.max(0, numOr(rec.srcIn, 0))
        });
        clip.id = strOr(rec.id, clip.id);
        clip.speed = numOr(rec.speed, 1) > 0 ? numOr(rec.speed, 1) : 1;
        clip.reverse = boolOr(rec.reverse, false);
        clip.opacity = numOr(rec.opacity, 1);
        clip.level = numOr(rec.level, 1);
        clip.fadeIn = numOr(rec.fadeIn, 0);
        clip.fadeOut = numOr(rec.fadeOut, 0);
        clip.hidden = boolOr(rec.hidden, false);
        clip.audioMuted = boolOr(rec.audioMuted, false);
        clip.gainDb = numOr(rec.gainDb, 0);
        clip.pan = numOr(rec.pan, 0);
        clip.audioFadeIn = numOr(rec.audioFadeIn, 0);
        clip.audioFadeOut = numOr(rec.audioFadeOut, 0);
        clip.opacityCurve = curveOr(rec.opacityCurve);
        clip.levelCurve = curveOr(rec.levelCurve);
        clip.gainCurve = curveOr(rec.gainCurve);
        clip.panCurve = curveOr(rec.panCurve);
        const tr = rec.transition && typeof rec.transition === 'object' ? rec.transition as Record<string, unknown> : null;
        clip.transition = tr && isTransitionKind(tr.kind) ? { kind: tr.kind, dur: numOr(tr.dur, 1) } : null;
        clip.effect = rec.effect && typeof rec.effect === 'object' && opts.parseEffect ? opts.parseEffect(rec.effect) : null;
        tl.clips.push(clip);
    }
    let nextMarkerId = 0;
    for (const m of listOf(o.markers)) nextMarkerId = Math.max(nextMarkerId, numOr(recOf(m).id, 0));
    for (const m of listOf(o.markers)) {
        const rec = recOf(m);
        const given = numOr(rec.id, NaN);
        tl.markers.push({
            id: Number.isFinite(given) ? given : ++nextMarkerId,
            t: Math.max(0, numOr(rec.t, 0)), name: strOr(rec.name, '')
        });
    }
    tl.markers.sort((a, b) => a.t - b.t);
    return tl;
}
