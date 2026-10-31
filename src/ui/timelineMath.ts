// Pure timeline helpers for the NLE UI, the timeline preview compositor and the preview audio mixer.
// No DOM access here: everything is a function of the model (shared/timeline.ts) so it can be
// exercised headless and so preview and export read the same laws.
import type { Timeline, TimelineClip } from '../../shared/types';
import {
    clipById, clipEnd, clipsOn, dbToLin, mediaHeadroom, moveClip, patchClip, rippleTrimEnd, setTransition, trackById, trimClip, type OpResult, frameOf, handleSides, sampleCurve, secOf, snap, snapTime,
    srcTimeAt, type PlanClip, type PlanLayer, type PlanWindow, type TimelinePlan
} from '../../shared/timeline';
import { frameComposite as sharedFrameComposite } from '../../shared/uve/export-clip-graph';
import type { CompositeEntry } from '../../shared/uve/engine-facade';

/* ------------------------------------------------------------------ geometry & snapping */

/** Snap tolerance in frames for a pixel tolerance at the current zoom (px per second). */
export function snapTolFrames(tl: Timeline, zoom: number, px = 12): number {
    return Math.max(1, (px / Math.max(1e-6, zoom)) * tl.frameRate);
}

/** Timeline without one clip: its own edges must not attract it while it is dragged. */
export function withoutClip(tl: Timeline, clipId: string): Timeline {
    return { ...tl, clips: tl.clips.filter(c => c.id !== clipId) };
}

/** Snapped position of an edge; plain frame-grid rounding when snapping is off. */
export function snapEdge(tl: Timeline, t: number, on: boolean, tolFrames: number): number {
    return on ? snapTime(tl, Math.max(0, t), tolFrames).t : snap(Math.max(0, t), tl.frameRate);
}

/** New start of a moved clip: snaps whichever of its two edges is closer to a snap point. */
export function snapMoveStart(tl: Timeline, clip: TimelineClip, rawStart: number, on: boolean, tolFrames: number): number {
    const F = tl.frameRate;
    const start = Math.max(0, rawStart);
    if (!on) return snap(start, F);
    const others = withoutClip(tl, clip.id);
    const a = snapTime(others, start, tolFrames);
    const b = snapTime(others, start + clip.dur, tolFrames);
    if (a.snapped && (!b.snapped || Math.abs(a.t - start) <= Math.abs(b.t - clip.dur - start))) return a.t;
    if (b.snapped) return Math.max(0, snap(b.t - clip.dur, F));
    return snap(start, F);
}

/**
 * Where an edge drag ends up: snapped like a move, never before 00:00, never past the media the clip has
 * on that side, at least one frame long, and – except for a ripple trim of the right edge – never into the
 * neighbouring clip on the same track. The live drag preview and the commit both use this, so what is
 * shown while dragging is exactly what is applied.
 */
export function trimEdges(tl: Timeline, clip: TimelineClip, edge: 'left' | 'right', deltaSec: number, ripple: boolean, on: boolean, tolFrames: number): { start: number; end: number } {
    const F = tl.frameRate, frame = secOf(1, F);
    const others = withoutClip(tl, clip.id);
    const end = clipEnd(clip);
    const onTrack = clipsOn(tl, clip.trackId).filter(c => c.id !== clip.id);
    if (edge === 'left') {
        const prevEnd = Math.max(0, ...onTrack.filter(c => clipEnd(c) <= clip.start + 1e-9).map(clipEnd));
        const minStart = Math.max(prevEnd, Math.ceil((clip.start - mediaHeadroom(tl, clip, 'left') / clip.speed) * F - 1e-6) / F);
        const start = Math.max(minStart, Math.min(snapEdge(others, clip.start + deltaSec, on, tolFrames), end - frame));
        return { start, end };
    }
    const nextStart = Math.min(Infinity, ...onTrack.filter(c => c.start >= end - 1e-9).map(c => c.start));
    let maxEnd = Math.floor((end + mediaHeadroom(tl, clip, 'right') / clip.speed) * F + 1e-6) / F;
    if (!ripple) maxEnd = Math.min(maxEnd, nextStart);
    return { start: clip.start, end: Math.min(maxEnd, Math.max(clip.start + frame, snapEdge(others, end + deltaSec, on, tolFrames))) };
}

/* ------------------------------------------------------------------ navigation */

/** Every clip start/end on the timeline (frame-exact, sorted, unique). */
export function editPoints(tl: Timeline): number[] {
    const F = tl.frameRate;
    const set = new Set<number>([0]);
    for (const c of tl.clips) { set.add(frameOf(c.start, F)); set.add(frameOf(clipEnd(c), F)); }
    return [...set].sort((a, b) => a - b).map(f => secOf(f, F));
}

/** Next (dir 1) / previous (dir -1) point after/before `t`; null when there is none. */
export function nextPoint(points: number[], t: number, dir: 1 | -1, eps = 1e-6): number | null {
    if (dir > 0) return points.find(p => p > t + eps) ?? null;
    for (let i = points.length - 1; i >= 0; i--) if (points[i] < t - eps) return points[i];
    return null;
}

/** The clip directly left of `clip` on its track (null when there is none). */
export function prevClipOf(tl: Timeline, clip: TimelineClip): TimelineClip | null {
    const list = clipsOn(tl, clip.trackId);
    const i = list.findIndex(c => c.id === clip.id);
    return i > 0 ? list[i - 1] : null;
}

/** Transition window of a clip in seconds (it straddles the clip's LEFT boundary). */
export function transitionSpan(tl: Timeline, clip: TimelineClip): { t0: number; t1: number } | null {
    if (!clip.transition) return null;
    const F = tl.frameRate;
    const { hL, hR } = handleSides(clip.transition.dur, F);
    const cut = frameOf(clip.start, F);
    return { t0: secOf(cut - hL, F), t1: secOf(cut + hR, F) };
}

/** Transition durations tried (in order) when a transition is added without an explicit length. */
export function defaultTransitionDurations(F: number): number[] {
    return [1, 0.5, 0.25, secOf(2, F)];
}

/* ------------------------------------------------------------------ preview frame schedule */

export function windowAt(plan: TimelinePlan, frame: number): PlanWindow | null {
    const ws = plan.windows;
    let lo = 0, hi = ws.length - 1;
    while (lo <= hi) {
        const mid = (lo + hi) >> 1;
        const w = ws[mid];
        if (frame < w.startF) hi = mid - 1;
        else if (frame >= w.endF) lo = mid + 1;
        else return w;
    }
    return null;
}

export function planClipOf(plan: TimelinePlan, clipId: string): PlanClip | undefined {
    return plan.clips.find(p => p.clip.id === clipId);
}

/**
 * Media time a layer shows at a timeline frame, with the CFR hold law: a stream that ends early holds
 * its last decoded frame (never black, never re-timed). `lastFrameSec` is the source's frame duration.
 */
export function heldSourceTime(pc: PlanClip, frame: number, F: number, lastFrameSec: number): number {
    const t = srcTimeAt(pc, frame, F);
    const media = pc.source.media;
    const total = Number.isFinite(media.duration) && media.duration > 0 ? media.duration : Infinity;
    // hold inside the decoded span and inside the real media (last frame stays on screen)
    const hi = Math.min(pc.decodeEnd, total) - Math.max(1e-4, lastFrameSec * 0.5);
    const lo = pc.decodeStart;
    return Math.max(lo, Math.min(Math.max(lo, hi), t));
}

/** One picture layer of a frame: which slot, which clip, and its composite entry. */
export interface FrameLayer {
    slot: number;
    layer: PlanLayer;
    plan: PlanClip;
    entry: CompositeEntry;
}

export interface FrameComposite {
    layers: FrameLayer[];
    veil: number;
    veilColor: 0 | 1;
}

const clipMaps = new WeakMap<TimelinePlan, Map<string, PlanClip>>();

/**
 * Layers of one frame, bottom-first. Delegates to the export worker's `frameComposite`
 * (`shared/uve/export-clip-graph.ts`) so slot order, roles, transition uniforms and the veil are
 * the same numbers in preview and export.
 */
export function frameComposite(plan: TimelinePlan, frame: number, W: number, H: number): FrameComposite {
    const win = windowAt(plan, frame);
    if (!win) return { layers: [], veil: 0, veilColor: 0 };
    let byId = clipMaps.get(plan);
    if (!byId) { byId = new Map(plan.clips.map(p => [p.clip.id, p])); clipMaps.set(plan, byId); }
    const comp = sharedFrameComposite(byId, win, frame, W, H);
    return {
        layers: comp.layers.map((l, i) => ({ slot: l.slot, layer: l.layer, plan: l.clip, entry: comp.entries[i] })),
        veil: comp.veil, veilColor: comp.veilColor
    };
}

/* ------------------------------------------------------------------ preview audio law (mirror of buildAudioGraph) */

const ramp = (x: number) => Math.min(1, Math.max(0, x));

/**
 * Linear gain of one clip's audio at timeline frame `frame` — the same terms `buildAudioGraph` puts into
 * `volume=eval=frame`: dB gains (clip + track + master), clip audio fades, transition crossfade ramps on
 * both boundaries and the gain automation curve, all on the clip's stream time.
 */
export function clipAudioGain(tl: Timeline, pc: PlanClip, frame: number): number {
    const F = tl.frameRate;
    const c = pc.clip;
    const streamStartF = pc.startF - pc.headF;
    const T = (frame - streamStartF) / F;            // chain time
    const headT = secOf(pc.headF, F);
    let g = dbToLin(c.gainDb + pc.track.gainDb + tl.masterGainDb);
    const fi = Math.min(c.audioFadeIn, c.dur), fo = Math.min(c.audioFadeOut, c.dur);
    if (fi > 1e-9) g *= ramp((T - headT) / fi);
    if (fo > 1e-9) g *= ramp((headT + c.dur - T) / fo);
    const list = clipsOn(tl, c.trackId);
    const next = list[list.findIndex(x => x.id === c.id) + 1];
    for (const side of ['left', 'right'] as const) {
        const tr = side === 'left' ? c.transition : next?.transition;
        if (!tr) continue;
        const { hL, hR } = handleSides(tr.dur, F);
        const cutF = side === 'left' ? pc.startF : pc.endF;
        const start = secOf(cutF - hL - streamStartF, F);
        const p = ramp((T - start) / secOf(hL + hR, F));
        g *= side === 'left' ? p : 1 - p;
    }
    const auto = sampleCurve(c.gainCurve, T - headT);
    if (auto !== null) g *= auto;
    return g;
}

/** Clips whose audio is heard at `frame` (window audio layers of audible clips with an audio stream). */
export function audibleClipsAt(plan: TimelinePlan, frame: number): PlanClip[] {
    const out: PlanClip[] = [];
    for (const pc of plan.clips) {
        if (!pc.audible || !pc.source.media.audio.length) continue;
        if (frame < pc.startF - pc.headF || frame >= pc.endF + pc.tailF) continue;
        out.push(pc);
    }
    return out;
}

/* ------------------------------------------------------------------ drag commits (pure: Timeline -> OpResult) */

/** Body drag: move within / across tracks; the clip's closer edge snaps. */
export function commitMove(tl: Timeline, clipId: string, rawStart: number, trackId: string | undefined, snapOn: boolean, tolFrames: number): OpResult {
    const clip = clipById(tl, clipId);
    if (!clip) return { ok: false, error: 'Clip not found.' };
    return moveClip(tl, clipId, { trackId, start: snapMoveStart(tl, clip, rawStart, snapOn, tolFrames) });
}

/**
 * Edge drag. Left edge: plain trim (the in point follows). Right edge: plain trim, or with `ripple`
 * `rippleTrimEnd` (later clips on the track follow). Collisions are rejected by the model.
 */
export function commitTrim(tl: Timeline, clipId: string, edge: 'left' | 'right', deltaSec: number, ripple: boolean, snapOn: boolean, tolFrames: number): OpResult {
    const clip = clipById(tl, clipId);
    if (!clip) return { ok: false, error: 'Clip not found.' };
    const { start, end } = trimEdges(tl, clip, edge, deltaSec, ripple && edge === 'right', snapOn, tolFrames);
    if (edge === 'left') return trimClip(tl, clipId, { start, dur: end - start });
    return ripple ? rippleTrimEnd(tl, clipId, end - clip.start) : trimClip(tl, clipId, { dur: end - clip.start });
}

/** Fade handle drag: video tracks set the picture and the audio fade together, audio tracks the audio fade. */
export function commitFade(tl: Timeline, clipId: string, edge: 'in' | 'out', seconds: number): OpResult {
    const clip = clipById(tl, clipId);
    if (!clip) return { ok: false, error: 'Clip not found.' };
    const v = snap(Math.max(0, Math.min(clip.dur, seconds)), tl.frameRate);
    const video = trackById(tl, clip.trackId)?.kind === 'video';
    const patch: Partial<TimelineClip> = edge === 'in'
        ? (video ? { fadeIn: v, audioFadeIn: v } : { audioFadeIn: v })
        : (video ? { fadeOut: v, audioFadeOut: v } : { audioFadeOut: v });
    return patchClip(tl, clipId, patch);
}

/** Transition handle drag: the window grows symmetrically around the cut, so D = 2 x (handle - cut). */
export function commitTransitionDur(tl: Timeline, clipId: string, handleT: number): OpResult {
    const clip = clipById(tl, clipId);
    if (!clip?.transition) return { ok: false, error: 'This clip has no transition.' };
    const dur = Math.max(secOf(1, tl.frameRate), 2 * (handleT - clip.start));
    return setTransition(tl, clipId, clip.transition.kind, dur);
}
