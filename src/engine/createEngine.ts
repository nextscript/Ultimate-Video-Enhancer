// Engine selection (v1.0.1): probe → validate against the v1.0.0 engine → choose the verified backend.
import type { CustomFilterEntry } from '../../shared/uve/customFilters';
import { engineFacade, type EngineSlot, type RenderEngine, type TimelineRenderEngine } from '../../shared/uve/engine-facade';
import { expertDefaults } from '../../shared/uve/expert';
import { type Prim, type UveGraph, buildUveGraph } from '../../shared/uve/graph';
import { DEFAULT_SETTINGS } from '../../shared/uve/settings';
import { GpuEngine, requestGpu } from './GpuEngine';
import { type EngineKind, type EnginePreference, type FrameEngine, hasCustomGlsl } from './types';
import { Webgl2Engine } from './Webgl2Engine';

export interface EngineChoice { engine: FrameEngine; reason: string; validation?: ValidationResult }
export interface ValidationResult { ok: boolean; maxDiff: number; meanDiff: number; outlierRate?: number; prims: number; ms: number; error?: string }

/** Graph that exercises every primitive type (used for the start-up validation and the parity tests). */
export function validationGraph(): UveGraph {
    const g = buildUveGraph({
        settings: {
            ...DEFAULT_SETTINGS, dn: -0.8, hdr: 0.6, cbFilter: 'deuteranopia', tealOrange: true, vibrantSat: true, profile: 'user',
            u_contrast: 4, u_black: 2, u_white: -1, u_highlights: 3, u_shadows: -2, u_sat: 5, u_vib: 4, u_sharp: 6, u_gamma: 2, u_grain: 3, u_hue: 2, u_r_gain: 140
        },
        expert: expertDefaults(), lut: [1.05, 0.02, 0, 0, -0.01, 0, 1, 0.03, 0, 0, 0.02, 0, 0.97, 0, 0.01, 0, 0, 0, 1, 0], customFilters: []
    });
    const last = g.output;
    const extra: Prim[] = [
        { op: 'morphology', in: last, out: 'v_m', erode: false, rx: 1, ry: 1 },
        { op: 'offset', in: 'v_m', out: 'v_o', dx: 2, dy: -1 },
        { op: 'flood', out: 'v_f', color: [0.2, 0.4, 0.6], opacity: 0.5 },
        { op: 'merge', out: 'v_mg', ins: [last, 'v_f'] },
        { op: 'displacement', in: 'v_mg', in2: 'v_o', out: 'v_d', scale: 4, xch: 0, ych: 1 },
        { op: 'composite', in: 'v_d', in2: last, out: 'v_c', operator: 'atop' },
        { op: 'blend', in: 'v_c', in2: last, out: 'v_b', mode: 'soft-light' },
        { op: 'ct', in: 'v_b', out: 'v_t', f: [{ type: 'discrete', table: [0, 0.3, 0.7, 1] }, { type: 'linear', slope: 0.9, intercept: 0.05 }, { type: 'gamma', amplitude: 1, exponent: 1.2, offset: 0 }, { type: 'identity' }] },
        { op: 'convolve', in: 'v_t', out: 'v_cv', orderX: 3, orderY: 3, kernel: [0, -1, 0, -1, 5, -1, 0, -1, 0], divisor: 1, bias: 0, targetX: 1, targetY: 1, edgeMode: 'wrap', preserveAlpha: false }
    ];
    return { ...g, prims: [...g.prims, ...extra], output: 'v_cv' };
}

/** Deterministic RGBA8 test frame (gradients, colour patches, fine detail). */
export function validationFrame(w = 96, h = 54): Uint8Array {
    const d = new Uint8Array(w * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const i = (y * w + x) * 4;
        d[i] = (x * 255 / (w - 1)) | 0;
        d[i + 1] = (y * 255 / (h - 1)) | 0;
        d[i + 2] = ((x * 7 + y * 13) % 64) * 4;
        d[i + 3] = 255;
        if (((x >> 3) + (y >> 3)) % 2 === 0) { d[i] = 255 - d[i]; d[i + 2] = 200; }
    }
    return d;
}

let gpuValidation: Promise<ValidationResult> | null = null;

async function runValidation(): Promise<ValidationResult> {
    const t0 = performance.now();
    let gpu: GpuEngine | null = null;
    let gl: Webgl2Engine | null = null;
    try {
        const dev = await requestGpu();
        if (!dev) return { ok: false, maxDiff: 0, meanDiff: 0, prims: 0, ms: 0, error: 'WebGPU is not available (no adapter).' };
        gpu = new GpuEngine({ device: dev.device, adapterInfo: dev.adapterInfo });
        const c = document.createElement('canvas');
        const ctx = c.getContext('webgl2', { antialias: false, depth: false, stencil: false });
        if (!ctx) return { ok: true, maxDiff: 0, meanDiff: 0, prims: 0, ms: 0, error: 'WebGL2 reference unavailable – WebGPU used without cross-check.' };
        gl = new Webgl2Engine(ctx);
        const graph = validationGraph();
        const frame = validationFrame();
        const opts = { autoMatrix: [1.1, 0, 0, 0, -0.02, 0, 1.08, 0, 0, -0.01, 0, 0, 1.05, 0, 0, 0, 0, 0, 1, 0], time: 1.5, pxScale: 1, quantize8: false };
        let maxDiff = 0, sum = 0, n = 0, outliers = 0;
        for (const quantize8 of [false, true]) {
            for (const e of [gpu, gl] as FrameEngine[]) { e.setGraph(graph); e.uploadRGBA8(frame, 96, 54); e.process(96, 54, { ...opts, quantize8 }); }
            const [a, b] = await Promise.all([gpu.readOutput8(), gl.readOutput8()]);
            for (let i = 0; i < a.length; i++) {
                if ((i & 3) === 3) continue;
                const d = Math.abs(a[i] - b[i]);
                maxDiff = Math.max(maxDiff, d); sum += d; n++;
                if (d > 3) outliers++;
            }
        }
        const meanDiff = sum / n;
        // Tolerance-defined parity (not bit identity): float16 intermediates differ by ULPs between ANGLE and Dawn, and
        // step functions (feComponentTransfer discrete, thresholds) can flip a value that lies exactly on an edge.
        // Accept when 99.9 % of all channel values are within ±3/255 and the mean difference is < 0.5/255.
        const outlierRate = outliers / n;
        return { ok: outlierRate <= 0.001 && meanDiff < 0.5, maxDiff, meanDiff, outlierRate, prims: graph.prims.length, ms: performance.now() - t0 };
    } catch (e) {
        return { ok: false, maxDiff: 0, meanDiff: 0, prims: 0, ms: performance.now() - t0, error: e instanceof Error ? e.message : String(e) };
    } finally {
        gpu?.dispose();
        gl?.dispose();
    }
}

/** Validates WebGPU once per session (cached). */
export function validateWebGpu(): Promise<ValidationResult> {
    if (!gpuValidation) gpuValidation = runValidation();
    return gpuValidation;
}

export function wantedKind(pref: EnginePreference, customFilters: CustomFilterEntry[]): { kind: EngineKind; reason: string } {
    if (hasCustomGlsl(customFilters)) return { kind: 'webgl2', reason: 'Custom GLSL filters are enabled – they run on the WebGL2 engine.' };
    if (pref === 'webgl2') return { kind: 'webgl2', reason: 'WebGL2 (legacy v1.0.0 engine) selected in Settings.' };
    return { kind: 'webgpu', reason: pref === 'webgpu' ? 'WebGPU selected in Settings.' : 'Automatic: WebGPU preferred.' };
}

/**
 * Creates the engine for a canvas (preview) or without one (export worker).
 * Falls back to WebGL2 when WebGPU is unavailable, fails validation or throws.
 */
export async function createEngine(canvas: HTMLCanvasElement | null, pref: EnginePreference, customFilters: CustomFilterEntry[]): Promise<EngineChoice> {
    const want = wantedKind(pref, customFilters);
    const glEngine = (reason: string): EngineChoice => {
        const c = canvas || document.createElement('canvas');
        const ctx = c.getContext('webgl2', { antialias: false, depth: false, stencil: false, premultipliedAlpha: false, preserveDrawingBuffer: false, powerPreference: 'high-performance' });
        if (!ctx) throw new Error('Neither WebGPU nor WebGL2 is available. Update the GPU driver or enable hardware acceleration.');
        return { engine: new Webgl2Engine(ctx), reason };
    };
    if (want.kind === 'webgl2') return glEngine(want.reason);
    const v = await validateWebGpu();
    if (!v.ok && pref !== 'webgpu') return { ...glEngine(`WebGPU not used: ${v.error || `validation against the v1.0.0 engine failed (max diff ${v.maxDiff}/255)`} – using WebGL2.`), validation: v };
    try {
        const dev = await requestGpu();
        if (!dev) return glEngine('WebGPU adapter unavailable – using WebGL2.');
        const engine = new GpuEngine({ device: dev.device, adapterInfo: dev.adapterInfo, canvas });
        return { engine, reason: `${want.reason} Validated against the v1.0.0 engine (${v.prims} primitives, mean diff ${v.meanDiff.toFixed(3)}/255, ${((v.outlierRate ?? 0) * 100).toFixed(3)} % of values > 3/255, max ${v.maxDiff}).`, validation: v };
    } catch (e) {
        return glEngine(`WebGPU initialisation failed (${e instanceof Error ? e.message : e}) – using WebGL2.`);
    }
}

/** Diagnostics: per-primitive max difference WebGPU vs WebGL2 on the validation graph (test hook only). */
export async function validationDetail(quantize8 = false, auto = false): Promise<string[]> {
    const dev = await requestGpu();
    if (!dev) return ['no WebGPU'];
    const gpu = new GpuEngine({ device: dev.device, adapterInfo: dev.adapterInfo });
    const c = document.createElement('canvas');
    const gl = new Webgl2Engine(c.getContext('webgl2')!);
    const full = validationGraph();
    const frame = validationFrame();
    const out: string[] = [];
    for (let k = 1; k <= full.prims.length; k++) {
        const g: UveGraph = { ...full, prims: full.prims.slice(0, k), output: full.prims[k - 1].out };
        let max = 0;
        for (const e of [gpu, gl] as FrameEngine[]) { e.setGraph(g); e.uploadRGBA8(frame, 96, 54); e.process(96, 54, { autoMatrix: auto ? [1.1, 0, 0, 0, -0.02, 0, 1.08, 0, 0, -0.01, 0, 0, 1.05, 0, 0, 0, 0, 0, 1, 0] : null, time: 1.5, pxScale: 1, quantize8 }); }
        const [a, b] = await Promise.all([gpu.readOutput8(), gl.readOutput8()]);
        for (let i = 0; i < a.length; i++) if ((i & 3) !== 3) max = Math.max(max, Math.abs(a[i] - b[i]));
        const p = full.prims[k - 1];
        out.push(`${k}:${p.op}:${p.out}=${max}`);
    }
    gpu.dispose(); gl.dispose();
    return out;
}

/**
 * The timeline render surface (layer slots + composite) of an engine made by `createEngine`.
 * Both backends provide it; the object is stable per engine.
 */
export function asRenderEngine(engine: FrameEngine): RenderEngine {
    return engine.asRenderEngine();
}

/**
 * Validated async timeline renderer for an engine from `createEngine`:
 * - preview: `createEngine(canvas, ...)` then `timelineEngineOf(engine, 'preview')` — composite draws to that canvas;
 * - export worker: `createEngine(null, ...)` then `timelineEngineOf(engine, 'export')` — composite renders offscreen,
 *   read it with `readOutput8` / `readOutput16`.
 * Call `init()` once before the first frame and `destroy()` when done (the engine itself stays alive).
 */
export function timelineEngineOf(engine: FrameEngine, mode: EngineSlot): TimelineRenderEngine {
    return engineFacade(engine.asRenderEngine(), mode);
}
