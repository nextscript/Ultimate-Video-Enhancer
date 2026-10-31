/**
 * Timeline export: the pure (Node- and browser-safe) half of the renderer.
 *
 * Everything here is derived from `buildPlan` / `buildAudioGraph` (`shared/timeline.ts`) and the transition
 * law (`shared/transitions.ts`); nothing re-times media or invents fade/transition math. The main process
 * (`electron/render.ts`) uses it to build the FFmpeg command lines, the export worker (`src/export/worker.ts`)
 * uses it to schedule frames and composite entries, so both sides agree on every number.
 *
 * Laws implemented here (see `.verify/nle-contract.md`):
 * - one decoder per distinct clip stream: `-ss decodeStart -t (decodeEnd-decodeStart)` (input side, so the
 *   speed `setpts` never shortens the read), `-fps_mode cfr -r num/den -frames:v streamFrames`;
 * - CFR hold law: a stream that ends early repeats its last decoded frame (`HoldCursor`), never black;
 * - sampler: grid-exact seek + `setpts` + `fps=..:round=up:start_time=0`, so stream frame k is the media
 *   frame displayed at `srcTimeAt` (speed, off-grid starts and timeline/media rate differences included);
 * - reverse pre-pass: lossless reversed temp holding the clip's stream (first frame = media at `decodeEnd`);
 * - audio: `buildAudioGraph` rendered verbatim, then muxed with `-c:v copy`.
 */

import type { AudioGraph, PlanClip, PlanLayer, PlanWindow, TimelinePlan } from '../timeline';
import { transitionProgress, transitionUniforms } from '../transitions';
import type { AutoKey } from './auto';
import type { EffectState } from '../types';
import { type CompositeEntry, MAX_SLOTS, STACK_UNIFORMS, veilColorOf } from './engine-facade';

/** Fixed-precision number for command lines (no float dust, no exponent). */
export function argNum(x: number): string {
    const s = (Math.abs(x) < 5e-7 ? 0 : x).toFixed(6).replace(/0+$/, '').replace(/\.$/, '');
    return s === '-0' ? '0' : s;
}

/* ------------------------------------------------------------------ payload */

/** One decoder process of a timeline job (one per distinct clip stream). */
export interface TimelineStream {
    /** dedupe key: identical decode + identical timeline placement */
    key: string;
    clipIds: string[];
    /** file the decoder reads (the reversed temp for reversed clips) */
    input: string;
    viaTemp: boolean;
    /** absolute timeline frame of stream frame 0 */
    streamStartF: number;
    /** frames the stream must deliver (`PlanClip.streamFrames`); shorter streams are held */
    streamFrames: number;
    /** first / last absolute timeline frame this export reads from the stream */
    firstF: number;
    lastF: number;
    width: number;
    height: number;
    in16: boolean;
    args: string[];
}

/** `worker:job` payload of a timeline job (`WorkerPlan` stays the single-media payload). */
export interface TimelineWorkerPlan {
    mode: 'timeline';
    jobId: string;
    ffmpeg: string;
    encodeArgs: string[];
    /** composite / encoder input size */
    width: number;
    height: number;
    out16: boolean;
    fpsNum: number;
    fpsDen: number;
    /** JSON-safe plan of the export range (`buildPlan(tl, range.start, range.end)`) */
    plan: TimelinePlan;
    masterGainDb: number;
    streams: TimelineStream[];
    /** clip id -> stream key */
    streamOfClip: Record<string, string>;
    /** clip id -> effect state the clip renders with (`clip.effect ?? job effect`, LUT embedded) */
    effects: Record<string, EffectState>;
    autoKeys: AutoKey[] | null;
    /** source path the auto keys were analysed on; other sources get no auto matrix */
    autoSource: string | null;
    parityMode: boolean;
    enginePref: 'auto' | 'webgpu' | 'webgl2';
}

/* ------------------------------------------------------------------ plan queries */

/** Plan clips whose picture the range actually draws, with the first/last frame drawn. */
export function referencedVideoClips(plan: TimelinePlan): { clip: PlanClip; firstF: number; lastF: number }[] {
    const span = new Map<string, { firstF: number; lastF: number }>();
    for (const w of plan.windows) {
        for (const l of w.video) {
            const s = span.get(l.clipId);
            if (!s) span.set(l.clipId, { firstF: w.startF, lastF: w.endF - 1 });
            else { s.firstF = Math.min(s.firstF, w.startF); s.lastF = Math.max(s.lastF, w.endF - 1); }
        }
    }
    const out: { clip: PlanClip; firstF: number; lastF: number }[] = [];
    for (const p of plan.clips) {
        const s = span.get(p.clip.id);
        if (s && p.visible) out.push({ clip: p, ...s });
    }
    return out;
}

/**
 * Frame-exact read start for a stream whose frame 0 must show the media frame displayed at media time `x`:
 * seek to that frame's grid time (half a millisecond early, so 6-decimal rounding of a rational rate can
 * never drop it) and carry the remainder `off` into the sampler.
 */
export function samplerStart(x: number, srcFrameSec: number): { ss: number; off: number } {
    const g0 = Math.max(0, Math.floor(x / srcFrameSec + 1e-6) * srcFrameSec);
    return { ss: g0 > 0.0005 ? g0 - 0.0005 : 0, off: Math.max(0, x - g0) };
}

/**
 * Stream sampler: source time -> timeline time (`/speed`, minus the read offset), then exactly one frame
 * per timeline tick. With `round=up:start_time=0` stream frame k carries the media frame DISPLAYED at
 * `srcTimeAt` (floor(t * srcFps)), which is what the preview's seeking `<video>` shows. Measured against
 * real ffmpeg for speeds 0.5, 0.75, 1, 1.5, 2, 3, off-grid starts and a 30 fps timeline over 25 fps media.
 */
export function samplerFilter(speed: number, fps: string, off: number): string {
    const shift = off > 1e-9 ? `-${argNum(off)}/TB` : '';
    return `setpts=(PTS-STARTPTS${shift})/${argNum(speed)},fps=${fps}:round=up:start_time=0`;
}

/* ------------------------------------------------------------------ ffmpeg argument builders */

export function decodeStreamArgs(o: {
    input: string; ss: number; t: number; map: string; vf: string; fps: string; frames: number;
    pixFmt: 'rgba' | 'rgba64le'; hwaccel: string | null;
}): string[] {
    const a = ['-hide_banner', '-loglevel', 'error', '-nostdin'];
    if (o.hwaccel) a.push('-hwaccel', o.hwaccel);
    if (o.ss > 0) a.push('-ss', argNum(o.ss));
    // input-side -t: the read window is in SOURCE seconds; the speed setpts runs after it
    a.push('-t', argNum(o.t), '-i', o.input, '-map', o.map, '-vf', o.vf, '-an', '-sn',
        '-fps_mode', 'cfr', '-r', o.fps, '-frames:v', String(o.frames), '-f', 'rawvideo', '-pix_fmt', o.pixFmt, 'pipe:1');
    return a;
}

/**
 * Reverse pre-pass of one clip into a lossless temp (`ffv1` `gbrap16le`, audio `pcm_s16le`).
 *
 * Picture: already the clip's stream at the timeline rate. Stream frame k must show the media frame at
 * `srcTimeAt = decodeEnd - k * speed / F`, so the span is sampled FORWARD with the speed sampler from
 * `decodeEnd - (frames - 1) * speed / F` (sample m = media at that time + m * speed / F), held at the end
 * (`tpad` clone: a span clamped at the media end repeats the last frame, as a seek past the end does in the
 * preview), cut to exactly `frames` frames and then reversed. Temp frame 0 is the media frame AT `decodeEnd`.
 * `reverse` buffers the frames before the RGB conversion, in the (small) source format.
 *
 * Audio: exactly `[decodeStart, decodeEnd]` reversed, so temp time tau holds media time `decodeEnd - tau`,
 * which is what `buildAudioGraph({ pathOverride })` reads (`ss = decodeEnd - hi`).
 */
export function reversePrepassArgs(o: {
    input: string; decodeStart: number; decodeEnd: number; speed: number; frames: number; fps: string; F: number;
    srcFrameSec: number; videoMap: string; vf: string; audioMap: string | null; out: string;
}): string[] {
    const first = Math.max(0, o.decodeEnd - (o.frames - 1) * o.speed / o.F);
    const st = samplerStart(first, o.srcFrameSec);
    // up to and including the frame at decodeEnd, not the one after it
    const readSpan = Math.max(0, o.decodeEnd - st.ss) + 0.5 * o.srcFrameSec;
    const sampler = `${samplerFilter(o.speed, o.fps, st.off)},tpad=stop_mode=clone:stop_duration=${argNum(o.frames / o.F)},trim=end_frame=${o.frames}`;
    const a = ['-hide_banner', '-loglevel', 'error', '-nostdin', '-y'];
    if (st.ss > 0) a.push('-ss', argNum(st.ss));
    a.push('-t', argNum(readSpan), '-i', o.input);
    const span = Math.max(0, o.decodeEnd - o.decodeStart);
    if (o.audioMap) a.push('-ss', argNum(o.decodeStart), '-t', argNum(span), '-i', o.input);
    a.push('-map', o.videoMap, '-vf', `${sampler},reverse,setpts=PTS-STARTPTS,${o.vf}`, '-c:v', 'ffv1', '-level', '3', '-pix_fmt', 'gbrap16le');
    if (o.audioMap) a.push('-map', o.audioMap.replace(/^0:/, '1:'), '-af', 'areverse,asetpts=PTS-STARTPTS', '-c:a', 'pcm_s16le');
    else a.push('-an');
    a.push('-sn', '-f', 'matroska', o.out);
    return a;
}

/** The audio graph rendered verbatim into a PCM wav. */
export function audioPassArgs(g: AudioGraph, outWav: string): string[] {
    const a = ['-hide_banner', '-loglevel', 'error', '-nostdin', '-y'];
    for (const inp of g.inputs) a.push('-ss', argNum(inp.ss), '-t', argNum(inp.t), '-i', inp.path);
    a.push('-filter_complex', g.filterComplex, '-map', `[${g.outLabel}]`, '-c:a', 'pcm_s16le', '-f', 'wav', outWav);
    return a;
}

/** Final mux: encoded video stream-copied, timeline audio encoded (never copied, never -shortest). */
export function timelineMuxArgs(o: { video: string; wav: string; out: string; muxer: string; audioCodec: string[]; extra: string[] }): string[] {
    return ['-hide_banner', '-loglevel', 'warning', '-nostdin', '-y', '-i', o.video, '-i', o.wav,
        '-map', '0:v:0', '-map', '1:a:0', '-c:v', 'copy', ...o.audioCodec, ...o.extra, '-f', o.muxer, o.out];
}

/* ------------------------------------------------------------------ CFR hold law */

/**
 * Sequential reader of one clip stream. `at(k)` returns stream frame k; when the decoder ended early
 * (`-frames:v` never invents frames past the read window) the last decoded frame is repeated until
 * `streamFrames` frames have been consumed. Never re-times, never substitutes black.
 */
export class HoldCursor<T> {
    private index = 0;
    private last: T | null = null;
    private ended = false;
    /** frames that were repeated because the stream ended early */
    held = 0;
    decoded = 0;

    constructor(readonly streamFrames: number, private readonly read: () => Promise<T | null>) {}

    async at(k: number): Promise<T> {
        if (!Number.isInteger(k) || k < 0) throw new RangeError(`Stream frame ${k} is out of range.`);
        if (k < this.index - 1) throw new Error(`Stream cursor cannot go backwards (frame ${k} after ${this.index - 1}).`);
        const target = Math.min(k, Math.max(0, this.streamFrames - 1));
        while (this.index <= target) {
            if (!this.ended) {
                const d = await this.read();
                if (d === null) this.ended = true;
                else { this.last = d; this.decoded++; }
            }
            if (this.ended) this.held++;
            this.index++;
        }
        if (this.last === null) throw new Error('The decoder delivered no frame for this clip.');
        return this.last;
    }
}

/* ------------------------------------------------------------------ composite schedule */

export interface FrameLayer { layer: PlanLayer; clip: PlanClip; slot: number }
export interface FrameComposite {
    layers: FrameLayer[];
    entries: CompositeEntry[];
    veil: number;
    veilColor: 0 | 1;
    /** layers above MAX_SLOTS that could not be drawn (bottom-most are dropped) */
    dropped: number;
}

/** Window containing an absolute frame (windows tile the range contiguously). */
export function windowAt(plan: TimelinePlan, frame: number, hint = 0): { window: PlanWindow | null; index: number } {
    const ws = plan.windows;
    for (let i = Math.max(0, hint); i < ws.length; i++) if (ws[i].startF <= frame && frame < ws[i].endF) return { window: ws[i], index: i };
    for (let i = 0; i < Math.min(hint, ws.length); i++) if (ws[i].startF <= frame && frame < ws[i].endF) return { window: ws[i], index: i };
    return { window: null, index: hint };
}

/**
 * Composite entries for one frame, bottom first (slot = index), with `role` so the engine stacks them
 * exactly (`compositeSteps`): 'single' layers are drawn over the stack, an 'a'/'b' transition pair is one
 * pass with the 'b' entry's `transitionUniforms(kind, transitionProgress(frame, transStartF, transFrames), W, H)`.
 * The transition veil is applied once on the composite. More than MAX_SLOTS layers: the bottom-most go.
 */
export function frameComposite(clipsById: Map<string, PlanClip>, win: PlanWindow, frame: number, W: number, H: number): FrameComposite {
    const sorted = [...win.video].sort((a, b) => a.z - b.z);
    const dropped = Math.max(0, sorted.length - MAX_SLOTS);
    const layers: FrameLayer[] = [];
    const entries: CompositeEntry[] = [];
    let veil = 0;
    let veilColor: 0 | 1 = 0;
    for (const layer of sorted.slice(dropped)) {
        const clip = clipsById.get(layer.clipId);
        if (!clip) continue;
        const slot = layers.length;
        layers.push({ layer, clip, slot });
        if (layer.role !== 'single' && layer.transitionKind && layer.transFrames > 0) {
            const u = transitionUniforms(layer.transitionKind, transitionProgress(frame, layer.transStartF, layer.transFrames), W, H);
            if (layer.role === 'b' && u.veil > veil) { veil = u.veil; veilColor = veilColorOf(u.veilColor); }
            entries.push({ slot, weightA: u.weightA, weightB: u.weightB, uniforms: u, role: layer.role });
        } else {
            entries.push({ slot, weightA: STACK_UNIFORMS.weightA, weightB: STACK_UNIFORMS.weightB, uniforms: STACK_UNIFORMS, role: 'single' });
        }
    }
    return { layers, entries, veil, veilColor, dropped };
}
