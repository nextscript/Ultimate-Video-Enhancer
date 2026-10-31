/**
 * Timeline render facade — the single picture API both GPU backends implement
 * (`GpuEngine` for WebGPU, `Webgl2Engine`/`EffectEngine` for WebGL2).
 *
 * Two kinds of "slot" are in play and they are not the same thing:
 * - a **layer slot** is a pair of GPU textures (uploaded source + processed layer) held by the
 *   engine; at most `MAX_SLOTS` layers are live in one frame, addressed by an integer 0..3;
 * - an `EngineSlot` says what `composite` draws into: the canvas (`'preview'`) or an offscreen
 *   target that `readOutput8`/`readOutput16` can read back (`'export'`).
 *
 * Both backends must produce the same numbers. The per-clip look law (`clipLookMatrix`) and the
 * composite law in `.verify/nle-contract.md` are the definitions their shaders mirror, so preview
 * and export stay identical frame for frame.
 */

import type { TransitionUniforms } from '../transitions';
import type { TimelineClip } from '../types';
import { dbToLin, sampleCurve, secOf, type PlanClip } from '../timeline';
import type { UveGraph } from './graph';
import { matIdentity4x5, type Mat4x5 } from './matrix';

/** Layer slots both backends provide: 0 .. MAX_SLOTS-1. Requests outside throw. */
export const MAX_SLOTS = 4;

/** What `composite` renders into: the canvas, or an offscreen target readable by readOutput8/16. */
export type EngineSlot = 'preview' | 'export';

/** One source frame for a layer slot: a live video element or an already decoded RGBA buffer. */
export type FrameSource =
    | { kind: 'video'; video: HTMLVideoElement }
    | { kind: 'rgba8'; data: Uint8Array; w: number; h: number }
    | { kind: 'rgba16'; data: Uint16Array; w: number; h: number };

/**
 * Everything a layer needs for one frame. `clipMatrix` is the authoritative per-clip look
 * (`clipLookMatrix`, fades included); when it is null the engine falls back to the same law in
 * scalar form: R/G/B gain `levelLin`, alpha scale `opacity`.
 */
export interface LayerRenderOptions {
    /** size of the frame handed to `setSource` */
    sourceW: number;
    sourceH: number;
    /** clip.opacity x opacityCurve(t) — scalar look, used only when `clipMatrix` is null */
    opacity: number;
    /** clip.level x levelCurve(t) x trackGainLin — scalar look, used only when `clipMatrix` is null */
    levelLin: number;
    /** veil fill 0..1 applied after the look (fadeBlack/fadeWhite at the window's edges) */
    veil: number;
    /** 0 = black, 1 = white */
    veilColor: 0 | 1;
    /** substituted for `auto: true` matrix prims; null = identity */
    autoMatrix: Mat4x5 | null;
    /** per-clip look matrix from `clipLookMatrix`; null = scalar look from opacity/levelLin */
    clipMatrix: Mat4x5 | null;
    /** the clip's effect graph, built once per clip with `graphFromEffect` */
    effectGraph: UveGraph;
    /** LUT handles from `uploadLut` / `uploadLut3D`; null when that LUT is not in use */
    lutHandle: number | null;
    hwLutHandle: number | null;
    /** edge length of the 3D LUT texture (`hwLutHandle`), null when there is none */
    lut3dSize: number | null;
    /** seconds for custom-filter animation (default 0) */
    time?: number;
    /** working-size scale of the source; 1 = exact export size (default 1) */
    pxScale?: number;
    /** 8-bit quantization pass, matching export parity mode (default false) */
    quantize8?: boolean;
    /**
     * Working size the layer is processed at (the size of the slot's layer texture). Default
     * `max(1, round(sourceW * pxScale))` x `max(1, round(sourceH * pxScale))`, which is what the
     * single-clip preview (`process(round(w * sc), round(h * sc))`) and the export (`pxScale` 1) use.
     * `composite` resamples every layer to its own width x height, so layers of different sizes mix.
     */
    outW?: number;
    outH?: number;
}

/** One layer inside a composite, with the weights its transition produces for it. */
export interface CompositeEntry {
    slot: number;
    weightA: number;
    weightB: number;
    /** `transitionUniforms(kind, progress, W, H)` for the window this layer belongs to, W x H = composite size */
    uniforms: TransitionUniforms;
    /**
     * `PlanLayer.role`. Optional, but pass it whenever more than two layers can be live: with roles
     * the stack is composited exactly (see `compositeSteps`); without roles the legacy fold applies.
     */
    role?: 'single' | 'a' | 'b';
}

/**
 * `entries === null` draws the last rendered layer with the single-layer present path — pixel
 * identical to `upload*` + `process` + `present(w, h, 0, 0)` (preview) or `process` + `readOutput*`
 * (export, when width x height equals the layer size). Otherwise the entries are composited
 * bottom-first by the passes `compositeSteps(entries)` returns; every pass evaluates the contract
 * expression (`cA * A(offset A) + cB * B(offset B)`, premultiplied, the shading-language built-in edge ramp).
 *
 * The per-pair veil is NOT applied inside the passes: `TransitionUniforms.veil` reaches the picture
 * once, through `veil`/`veilColor` here, after the whole stack is composited. The output is the
 * premultiplied stack over black (alpha dropped).
 */
export interface CompositeOptions {
    width: number;
    height: number;
    entries: CompositeEntry[] | null;
    /** veil 0..1 from `TransitionUniforms.veil`, applied once to the composited picture */
    veil: number;
    /** 0 = black, 1 = white */
    veilColor: 0 | 1;
}

/** What a GPU backend must provide for timeline rendering; both engines implement it. */
export interface RenderEngine {
    /** allocate the layer-slot textures and select the composite target */
    beginTimeline(mode: EngineSlot): void;
    /** free the layer-slot textures */
    endTimeline(): void;
    readonly timelineReady: boolean;
    /** upload one frame into the slot's source texture */
    setSource(slot: number, src: FrameSource): void;
    /** slot source -> LUTs -> effect graph -> clip look -> veil fill, written into the slot target */
    renderLayer(slot: number, opts: LayerRenderOptions): void;
    composite(opts: CompositeOptions): void;
    /** `size` RGBA float entries as a per-channel 1D LUT; null when the engine cannot hold it */
    uploadLut(rgba: Float32Array, size: number): number | null;
    /** `size^3` RGB triples as a trilinear 3D LUT; null when the engine cannot hold it */
    uploadLut3D(rgb: Float32Array, size: number): number | null;
    /** read back the frame `composite` produced (WebGL2 answers synchronously, WebGPU asynchronously) */
    readOutput8(into?: Uint8Array): Uint8Array | Promise<Uint8Array>;
    readOutput16(into?: Uint16Array): Uint16Array | Promise<Uint16Array>;
}

/** Async facade both preview and export drive; validation happens here so both callers behave alike. */
export interface TimelineRenderEngine {
    init(): Promise<void>;
    ready(): boolean;
    setSource(slot: number, src: FrameSource): Promise<void>;
    renderLayer(slot: number, opts: LayerRenderOptions): Promise<void>;
    composite(opts: CompositeOptions): Promise<void>;
    uploadLut(rgba: Float32Array, size: number): Promise<number | null>;
    uploadLut3D(rgb: Float32Array, size: number): Promise<number | null>;
    /** read back the composited frame written by `composite` in 'export' mode */
    readOutput8(into?: Uint8Array): Promise<Uint8Array>;
    readOutput16(into?: Uint16Array): Promise<Uint16Array>;
    /** releases the layer-slot textures; the engine itself is disposed by its creator */
    destroy(): void;
}

const isFiniteNum = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);

function checkSlot(slot: number, what: string): number {
    if (!Number.isInteger(slot) || slot < 0 || slot >= MAX_SLOTS) {
        throw new RangeError(`Layer slot must be an integer from 0 to ${MAX_SLOTS - 1} (${what}).`);
    }
    return slot;
}

function checkLayer(opts: LayerRenderOptions, what: string): void {
    if (!Number.isInteger(opts.sourceW) || opts.sourceW < 1 || !Number.isInteger(opts.sourceH) || opts.sourceH < 1) {
        throw new RangeError(`Layer source size must be at least 1x1 (${what}).`);
    }
    if (!isFiniteNum(opts.opacity) || !isFiniteNum(opts.levelLin)) throw new RangeError(`Layer opacity and level must be finite (${what}).`);
    if (!isFiniteNum(opts.veil) || opts.veil < 0 || opts.veil > 1) throw new RangeError(`Layer veil must be 0..1 (${what}).`);
    if (opts.clipMatrix && opts.clipMatrix.length !== 20) throw new RangeError(`Clip matrix must have 20 entries (${what}).`);
    if (opts.autoMatrix && opts.autoMatrix.length !== 20) throw new RangeError(`Auto matrix must have 20 entries (${what}).`);
    if (opts.veilColor !== 0 && opts.veilColor !== 1) throw new RangeError(`Veil color must be 0 (black) or 1 (white) (${what}).`);
    if (!opts.effectGraph || !Array.isArray(opts.effectGraph.prims) || typeof opts.effectGraph.output !== 'string') {
        throw new Error(`Layer needs an effect graph (${what}).`);
    }
    const pxScale = opts.pxScale ?? 1;
    if (!isFiniteNum(pxScale) || pxScale <= 0) throw new RangeError(`pxScale must be greater than 0 (${what}).`);
    for (const v of [opts.outW, opts.outH]) {
        if (v !== undefined && (!Number.isInteger(v) || v < 1)) throw new RangeError(`Layer working size must be at least 1x1 (${what}).`);
    }
}

/** Working size of a layer: `outW`/`outH`, else the source size times `pxScale` (at least 1x1). */
export function layerSizeOf(opts: LayerRenderOptions): { w: number; h: number } {
    const sc = opts.pxScale ?? 1;
    return {
        w: opts.outW ?? Math.max(1, Math.round(opts.sourceW * sc)),
        h: opts.outH ?? Math.max(1, Math.round(opts.sourceH * sc))
    };
}

/** The look a layer carries: `clipMatrix`, else the scalar law (RGB x levelLin, alpha x opacity). */
export function effectiveClipMatrix(opts: LayerRenderOptions, out?: Mat4x5): Mat4x5 {
    if (opts.clipMatrix) return opts.clipMatrix;
    const m = out ?? matIdentity4x5();
    const g = opts.levelLin;
    for (let i = 0; i < 20; i++) m[i] = 0;
    m[0] = g; m[6] = g; m[12] = g; m[18] = opts.opacity;
    return m;
}

/** True when the graph places the clip look itself (a `clip: true` matrix prim); the engine then skips its own look pass matrix. */
export function graphHasClipPrim(graph: UveGraph): boolean {
    return graph.prims.some(p => p.op === 'matrix' && p.clip === true);
}

function checkComposite(opts: CompositeOptions): void {
    if (!Number.isInteger(opts.width) || opts.width < 1 || !Number.isInteger(opts.height) || opts.height < 1) {
        throw new RangeError(`Composite size must be at least 1x1.`);
    }
    if (!isFiniteNum(opts.veil) || opts.veil < 0 || opts.veil > 1) throw new RangeError('Composite veil must be 0..1.');
    if (opts.veilColor !== 0 && opts.veilColor !== 1) throw new RangeError('Composite veil color must be 0 (black) or 1 (white).');
    if (opts.entries === null) return;
    if (opts.entries.length === 0) throw new Error('Composite needs at least one layer or entries: null.');
    if (opts.entries.length > MAX_SLOTS) throw new RangeError(`Composite takes at most ${MAX_SLOTS} layers.`);
    for (const entry of opts.entries) {
        checkSlot(entry.slot, 'composite');
        if (!isFiniteNum(entry.weightA) || !isFiniteNum(entry.weightB)) throw new RangeError('Composite weights must be finite.');
        if (!entry.uniforms) throw new Error('Composite entry needs its transition uniforms.');
        if (entry.role !== undefined && entry.role !== 'single' && entry.role !== 'a' && entry.role !== 'b') {
            throw new Error("Composite entry role must be 'single', 'a' or 'b'.");
        }
    }
}

/** What one composite pass reads: a layer slot, the accumulated stack so far, or nothing. */
export type StepInput = number | 'acc' | null;

/**
 * One fullscreen composite pass: `P = cA * A(px + dA) + cB * B(px + dB)` with cA/cB from the contract
 * expression on `uniforms`/`weightA`/`weightB`; then `out = over ? acc * (1 - P.a) + P : P`.
 */
export interface CompositeStep {
    a: StepInput;
    b: StepInput;
    weightA: number;
    weightB: number;
    uniforms: TransitionUniforms;
    /** composite P over the accumulated result (track stacking) instead of replacing it */
    over: boolean;
}

/** Frame-wide uniforms of a layer that is not part of a transition (only the geometry fields are read). */
export const STACK_UNIFORMS: Readonly<TransitionUniforms> = Object.freeze({
    weightA: 0, weightB: 1, mode: 0, axis: 0, edge: 0, highSideIsB: true, cx: 0, cy: 0, feather: 0,
    dxA: 0, dyA: 0, dxB: 0, dyB: 0, veil: 0, veilColor: null
} as TransitionUniforms);

/**
 * The pass list both engines execute for `CompositeOptions.entries`, so WebGPU and WebGL2 composite
 * identically. Entries are bottom-first.
 *
 * With roles (any entry has `role`; a missing role then counts as 'single'):
 * - 'single': `P = layer`, drawn over the stack (`acc * (1 - P.a) + P`);
 * - 'a' directly followed by 'b': one pair `P = cA * a + cB * b` with the 'b' entry's weights and
 *   uniforms, drawn over the stack; a lone 'a' or 'b' is that pair with the partner missing.
 * Without roles (legacy fold): entries[0] is the base; every following entry is the B of a pair whose
 * A is the accumulated result, with that entry's own weights and uniforms.
 */
export function compositeSteps(entries: CompositeEntry[]): CompositeStep[] {
    const steps: CompositeStep[] = [];
    if (!entries.length) return steps;
    const single = (slot: number, over: boolean): CompositeStep =>
        ({ a: null, b: slot, weightA: 0, weightB: 1, uniforms: STACK_UNIFORMS, over });
    if (!entries.some(e => e.role !== undefined)) {
        steps.push(single(entries[0].slot, false));
        for (let i = 1; i < entries.length; i++) {
            const e = entries[i];
            steps.push({ a: 'acc', b: e.slot, weightA: e.weightA, weightB: e.weightB, uniforms: e.uniforms, over: false });
        }
        return steps;
    }
    for (let i = 0; i < entries.length; i++) {
        const e = entries[i];
        const over = steps.length > 0;
        const role = e.role ?? 'single';
        if (role === 'single') { steps.push(single(e.slot, over)); continue; }
        if (role === 'a') {
            const next = entries[i + 1];
            if (next && next.role === 'b') {
                steps.push({ a: e.slot, b: next.slot, weightA: next.weightA, weightB: next.weightB, uniforms: next.uniforms, over });
                i++;
            } else {
                steps.push({ a: e.slot, b: null, weightA: e.weightA, weightB: e.weightB, uniforms: e.uniforms, over });
            }
            continue;
        }
        steps.push({ a: null, b: e.slot, weightA: e.weightA, weightB: e.weightB, uniforms: e.uniforms, over });
    }
    return steps;
}

/** Wrap an engine so preview and export use the same validated entry points. */
export function engineFacade(engine: RenderEngine, mode: EngineSlot): TimelineRenderEngine {
    return {
        init: async () => { engine.beginTimeline(mode); },
        ready: () => engine.timelineReady,
        setSource: async (slot, src) => {
            if (!src || (src.kind !== 'video' && src.kind !== 'rgba8' && src.kind !== 'rgba16')) {
                throw new Error('Frame source must be video, rgba8 or rgba16.');
            }
            engine.setSource(checkSlot(slot, 'setSource'), src);
        },
        renderLayer: async (slot, opts) => {
            checkLayer(opts, 'renderLayer');
            engine.renderLayer(checkSlot(slot, 'renderLayer'), opts);
        },
        composite: async (opts) => {
            checkComposite(opts);
            engine.composite(opts);
        },
        uploadLut: async (rgba, size) => {
            if (!Number.isInteger(size) || size < 1) throw new RangeError('LUT size must be a positive integer.');
            if (rgba.length < size * 4) throw new RangeError(`1D LUT needs ${size * 4} float entries.`);
            return engine.uploadLut(rgba, size);
        },
        uploadLut3D: async (rgb, size) => {
            if (!Number.isInteger(size) || size < 1) throw new RangeError('3D LUT size must be a positive integer.');
            if (rgb.length < size * size * size * 3) throw new RangeError(`3D LUT needs ${size * size * size * 3} float entries.`);
            return engine.uploadLut3D(rgb, size);
        },
        readOutput8: async (into) => await engine.readOutput8(into),
        readOutput16: async (into) => await engine.readOutput16(into),
        destroy: () => { engine.endTimeline(); }
    };
}

/** `dbToLin(track.gainDb + timeline.masterGainDb)`: the brightness law every clip's RGB gain carries. */
export function trackGainLinOf(gainDb: number, masterGainDb: number): number {
    return dbToLin(gainDb + masterGainDb);
}

/** Maps `TransitionUniforms.veilColor` to the `veilColor` both shader backends take. */
export function veilColorOf(color: 'black' | 'white' | null): 0 | 1 {
    return color === 'white' ? 1 : 0;
}

/** `min(1, max(0, value / dur))`, the same ramp `fadeTerm` emits for audio; 1 when `dur` is 0. */
function fadeRamp(value: number, dur: number): number {
    if (dur <= 1e-9) return 1;
    return Math.min(1, Math.max(0, value / dur));
}

/**
 * Per-clip picture law, mirroring the audio law (`dbToLin(clip.gainDb + track.gainDb + masterGainDb)`
 * times the fade ramps) and `buildAudioGraph`'s clamps (`Math.min(fadeIn, clip.dur)`).
 *
 * `t = (frame - planClip.startF) / F` is seconds from the clip's own timeline start. R/G/B carry
 * `clip.level x levelCurve(t) x trackGainLin`; the alpha row carries the coverage
 * `clip.opacity x opacityCurve(t)` times the fade ramps — `clip.fadeIn` ramps up from the clip's start,
 * `clip.fadeOut` ramps down to its end. The engine premultiplies after the look, so over black the
 * picture is the same either way, but only coverage in alpha lets a lower track show through an upper
 * track at opacity < 1 in the over-composite. `trackGainLin` is `trackGainLinOf(track.gainDb, timeline.masterGainDb)`, so master gain
 * reaches the picture exactly once.
 *
 * Row-major 4x5 (`shared/uve/matrix.ts`): diagonal `m[0] m[6] m[12]` (RGB) and `m[18]` (alpha, row `m[15..19]`),
 * offsets `m[4] m[9] m[14] m[19]`. Pass `out` to reuse a scratch matrix: every one of the 20 entries
 * is written, so nothing stale survives.
 */
export function clipLookMatrix(
    clip: TimelineClip,
    planClip: PlanClip,
    frame: number,
    F: number,
    trackGainLin: number,
    out?: Mat4x5
): Mat4x5 {
    const m = out ?? matIdentity4x5();
    const t = secOf(frame - planClip.startF, F);
    const dur = Math.max(0, clip.dur);
    const opacityCurve = sampleCurve(clip.opacityCurve, t) ?? 1;
    const levelCurve = sampleCurve(clip.levelCurve, t) ?? 1;
    const rgb = clip.level * levelCurve * trackGainLin;
    const alpha = clip.opacity * opacityCurve * fadeRamp(t, Math.min(clip.fadeIn, dur)) * fadeRamp(dur - t, Math.min(clip.fadeOut, dur));

    m[0] = rgb; m[1] = 0; m[2] = 0; m[3] = 0; m[4] = 0;
    m[5] = 0; m[6] = rgb; m[7] = 0; m[8] = 0; m[9] = 0;
    m[10] = 0; m[11] = 0; m[12] = rgb; m[13] = 0; m[14] = 0;
    m[15] = 0; m[16] = 0; m[17] = 0; m[18] = alpha; m[19] = 0;
    return m;
}
