// UVE effect graph: a faithful port of buildFilter()/ensureSvgFilter()/svgToneMatrix()
// (userscript source lines ~18794-19938) into a backend-neutral list of SVG filter
// primitives. The desktop GPU interpreter (src/engine/SvgGraphRenderer.ts) executes this
// list with SVG filter semantics for both the live preview and the offline export.
import type { ExpertSettings } from './expert';
import { GVF_BASE_SETTINGS, GVF_HDR_SETTINGS, GVF_MOODY_SETTINGS, GVF_TEAL_SETTINGS, GVF_VIBRANT_SETTINGS } from './expert';
import type { UveSettings } from './settings';
import { normBL, normDN, normEDGE, normHDR, normRGB, normSL, normU } from './settings';
import * as C from './curves';
import {
    type Mat4x5, colorBlindnessMatrix, matCssBrightness, matCssContrast, matHueRotateSpec, matIdentity4x5,
    matMul4x5, matRGBGain, matSaturateSpec, rgb3x3To4x5
} from './matrix';
import { parseCustomSvgPrimitives, type CustomFilterEntry } from './customFilters';

export type FuncType = 'identity' | 'table' | 'discrete' | 'linear' | 'gamma';
export interface TransferFunc {
    type: FuncType;
    table?: number[];
    slope?: number;
    intercept?: number;
    amplitude?: number;
    exponent?: number;
    offset?: number;
}

export type BlendMode = 'normal' | 'multiply' | 'screen' | 'overlay' | 'darken' | 'lighten' | 'color-dodge' | 'color-burn'
    | 'hard-light' | 'soft-light' | 'difference' | 'exclusion' | 'hue' | 'saturation' | 'color' | 'luminosity';

export type Prim =
    /**
     * `auto`: the engine substitutes the auto-levels matrix; `clip`: the engine substitutes the per-clip
     * look matrix (`ProcessOptions.clipMatrix`, timeline mode). `m` is used only when neither is set.
     */
    | { op: 'matrix'; in: string; out: string; m: Mat4x5; auto?: boolean; clip?: boolean }
    | { op: 'ct'; in: string; out: string; f: [TransferFunc, TransferFunc, TransferFunc, TransferFunc] }
    | { op: 'blur'; in: string; out: string; sx: number; sy: number }
    | { op: 'arith'; in: string; in2: string; out: string; k1: number; k2: number; k3: number; k4: number }
    | { op: 'composite'; in: string; in2: string; out: string; operator: 'over' | 'in' | 'out' | 'atop' | 'xor' | 'lighter' }
    | { op: 'blend'; in: string; in2: string; out: string; mode: BlendMode }
    | {
        op: 'convolve'; in: string; out: string; orderX: number; orderY: number; kernel: number[]; divisor: number;
        bias: number; targetX: number; targetY: number; edgeMode: 'duplicate' | 'wrap' | 'none'; preserveAlpha: boolean;
    }
    | { op: 'turbulence'; out: string; fx: number; fy: number; octaves: number; seed: number; fractal: boolean }
    | { op: 'flood'; out: string; color: [number, number, number]; opacity: number }
    | { op: 'offset'; in: string; out: string; dx: number; dy: number }
    | { op: 'morphology'; in: string; out: string; erode: boolean; rx: number; ry: number }
    | { op: 'merge'; out: string; ins: string[] }
    | { op: 'displacement'; in: string; in2: string; out: string; scale: number; xch: number; ych: number };

export interface UveGraph {
    prims: Prim[];
    /** Name of the result that is the filter output; 'SourceGraphic' when no filter is active. */
    output: string;
    /** True when the auto (scene match) matrix node is part of the graph. */
    hasAuto: boolean;
    warnings: string[];
}

const clamp = (n: number, a: number, b: number) => Math.min(b, Math.max(a, n));
const lin = (slope: number, intercept: number): TransferFunc => ({ type: 'linear', slope, intercept });
const ident = (): TransferFunc => ({ type: 'identity' });
const table = (vals: number[]): TransferFunc => ({ type: 'table', table: vals });
const rgbFuncs = (f: TransferFunc): [TransferFunc, TransferFunc, TransferFunc, TransferFunc] => [f, f, f, ident()];
const fx = (v: number, d: number) => Number(v.toFixed(d));

const LUMA_ROWS: Mat4x5 = [0.2126, 0.7152, 0.0722, 0, 0, 0.2126, 0.7152, 0.0722, 0, 0, 0.2126, 0.7152, 0.0722, 0, 0, 0, 0, 0, 1, 0];
const SKIN_K1: Mat4x5 = [8, -8, 0, 0, 0, 8, -8, 0, 0, 0, 8, -8, 0, 0, 0, 0, 0, 0, 1, 0];
const SKIN_K2: Mat4x5 = [0, 8, -8, 0, 0, 0, 8, -8, 0, 0, 0, 8, -8, 0, 0, 0, 0, 0, 1, 0];

// PROFILE_TONE (source line ~19906)
const PROFILE_TONE: Record<string, { brightness: number; contrast: number; saturate: number; hue?: number }> = {
    film: { brightness: 1.01, contrast: 1.08, saturate: 1.08 },
    anime: { brightness: 1.03, contrast: 1.10, saturate: 1.16 },
    gaming: { brightness: 1.01, contrast: 1.12, saturate: 1.06 },
    eyecare: { brightness: 1.05, contrast: 0.96, saturate: 0.88, hue: -12 }
};

// mkProfileMatrixCT() (source line ~18993)
const PROFILE_MATRIX: Record<string, Mat4x5> = {
    film: [1.06, 0.02, 0.00, 0, -0.03, 0.01, 1.03, 0.01, 0, -0.02, 0.00, 0.03, 1.05, 0, -0.03, 0, 0, 0, 1, 0],
    anime: [1.06, 0.01, 0.00, 0, -0.012, 0.00, 1.07, 0.01, 0, -0.012, 0.01, 0.03, 1.10, 0, -0.016, 0, 0, 0, 1, 0],
    gaming: [1.04, 0.00, 0.00, 0, -0.010, 0.00, 1.04, 0.00, 0, -0.010, 0.00, 0.00, 1.04, 0, -0.010, 0, 0, 0, 1, 0],
    eyecare: [1.08, 0.00, 0.00, 0, 0.00, 0.15, 1.05, 0.00, 0, 0.00, 0.25, 0.00, 0.50, 0, 0.00, 0, 0, 0, 1, 0]
};
const PROFILE_SAT: Record<string, number> = { film: 1.08, anime: 1.18, gaming: 1.06, eyecare: 0.90 };

/** svgToneMatrix(): base tone + profile tone as one matrix (Chromium path of the userscript). */
export function toneMatrix(s: UveSettings): Mat4x5 | null {
    const ops: Mat4x5[] = [];
    if (s.enabled) {
        const b = GVF_BASE_SETTINGS;
        ops.push(matCssBrightness(b.brightness), matCssContrast(b.contrast), matSaturateSpec(b.saturation));
    }
    const t = PROFILE_TONE[s.profile];
    if (t) {
        ops.push(matCssBrightness(t.brightness), matCssContrast(t.contrast), matSaturateSpec(t.saturate));
        if (t.hue) ops.push(matHueRotateSpec(t.hue));
    }
    if (!ops.length) return null;
    return ops.reduce((m, op) => matMul4x5(op, m), matIdentity4x5());
}

/** applyFilter() "nothingOn" test: when true the userscript removes the filter completely. */
export function isNothingOn(s: UveSettings): boolean {
    return !s.enabled && !s.darkMoody && !s.tealOrange && !s.vibrantSat && normEDGE(s.edge) === 0 && normHDR(s.hdr) === 0
        && s.profile === 'off' && !s.autoOn && s.cbFilter === 'none';
}

class Builder {
    prims: Prim[] = [];
    push(p: Prim) { this.prims.push(p); }
    matrix(inp: string, out: string, m: Mat4x5, auto = false) { this.push({ op: 'matrix', in: inp, out, m, auto }); }
    ct(inp: string, out: string, f: TransferFunc) { this.push({ op: 'ct', in: inp, out, f: rgbFuncs(f) }); }
    blur(inp: string, out: string, std: number) { this.push({ op: 'blur', in: inp, out, sx: std, sy: std }); }
    arith(inp: string, in2: string, out: string, k1: number, k2: number, k3: number, k4: number) {
        this.push({ op: 'arith', in: inp, in2, out, k1, k2, k3, k4 });
    }
}

// mkGrain(): turbulence + mix matrix + arithmetic add
function mkGrain(b: Builder, inId: string, outId: string, alpha: number, freq = 0.9, noiseMat?: Mat4x5, addStep = true) {
    b.push({ op: 'turbulence', out: outId + '_n', fx: freq, fy: freq, octaves: 2, seed: 2, fractal: true });
    b.matrix(outId + '_n', outId + '_nm', noiseMat ?? [0.33, 0.33, 0.33, 0, 0, 0.33, 0.33, 0.33, 0, 0, 0.33, 0.33, 0.33, 0, 0, 0, 0, 0, 1, 0]);
    if (addStep) b.arith(inId, outId + '_nm', outId, 0, 1, alpha, 0);
}

function mkDenoiseBlend(b: Builder, inId: string, outId: string, sigma: number, mix: number) {
    b.blur(inId, outId + '_b', sigma);
    b.arith(inId, outId + '_b', outId, 0, 1 - mix, mix, 0);
}

function mkHighlightsTable(hiAdj: number): number[] {
    const knee = 0.78, steps = 17, vals: number[] = [];
    for (let i = 0; i < steps; i++) {
        const x = i / (steps - 1);
        let y = x;
        if (x > knee) y = x + hiAdj * ((x - knee) / (1 - knee));
        vals.push(fx(clamp(y, 0, 1), 4));
    }
    return vals;
}

function mkSCurveTable(strength: number): number[] {
    const s = clamp(strength, 0, 2);
    const steps = 33, vals: number[] = [];
    const toe = 0.20 + s * 0.06, shoulder = 0.78 - s * 0.05, shoulderGain = 0.16 + s * 0.10;
    for (let i = 0; i < steps; i++) {
        const x = i / (steps - 1);
        let y = x;
        if (x < toe) {
            const t = x / toe, ss = t * t * (3 - 2 * t);
            y = x + (toe - x) * (0.10 + s * 0.10) * (1 - ss);
        }
        if (x > shoulder) {
            const t = (x - shoulder) / (1 - shoulder), ss = t * t * (3 - 2 * t);
            y = x - shoulderGain * ss * t;
        }
        vals.push(fx(clamp(y, 0, 1), 4));
    }
    return vals;
}

function appendUserSharpen(b: Builder, s: UveSettings, e: ExpertSettings, inName: string, prefix: string): string {
    const v = C.userSharpenValue(s.u_sharp, e);
    if (Math.abs(v) < 0.05) return inName;
    if (v <= 0) {
        b.blur(inName, prefix, (-v / 10) * e.GVF_SHARPEN_SETTINGS.svgMaxSoften);
        return prefix;
    }
    const a = (v / 10) * e.GVF_SHARPEN_SETTINGS.maxAmount;
    b.blur(inName, prefix + '_blur', e.GVF_SHARPEN_SETTINGS.svgRadius);
    b.ct(prefix + '_blur', prefix + '_inv', lin(-1, 1));
    b.arith(inName, prefix + '_inv', prefix + '_d', 0, 0.5, 0.5, 0);
    b.matrix(prefix + '_d', prefix + '_dl', LUMA_ROWS);
    const N = 512, lh = C.sharpenLightHalo(e), dh = C.sharpenDarkHalo(e), sk = C.sharpenSkinProtect(e);
    const tPos: number[] = [], tNeg: number[] = [], tNegInv: number[] = [];
    for (let i = 0; i <= N; i++) {
        const c = C.sharpenCurve((i / N - 0.5) * 2, a, e);
        tPos.push(fx(Math.max(0, c) * lh, 4));
        tNeg.push(fx(Math.max(0, -c) * dh, 4));
        tNegInv.push(fx(1 - Math.max(0, -c) * dh, 4));
    }
    if (sk <= 0) {
        b.ct(prefix + '_dl', prefix + '_cp', table(tPos));
        b.ct(prefix + '_dl', prefix + '_cn', table(tNegInv));
    } else {
        b.matrix(inName, prefix + '_k1', SKIN_K1);
        b.matrix(inName, prefix + '_k2', SKIN_K2);
        b.arith(prefix + '_k1', prefix + '_k2', prefix + '_sk', 1, 0, 0, 0);
        b.ct(prefix + '_sk', prefix + '_sp', lin(-sk, 1));
        b.ct(prefix + '_dl', prefix + '_cp0', table(tPos));
        b.arith(prefix + '_cp0', prefix + '_sp', prefix + '_cp', 1, 0, 0, 0);
        b.ct(prefix + '_dl', prefix + '_cn0', table(tNeg));
        b.arith(prefix + '_cn0', prefix + '_sp', prefix + '_cn1', 1, 0, 0, 0);
        b.ct(prefix + '_cn1', prefix + '_cn', lin(-1, 1));
    }
    b.arith(inName, prefix + '_cp', prefix + '_up', 0, 1, 1, 0);
    b.arith(prefix + '_up', prefix + '_cn', prefix, 0, 1, 1, -1);
    return prefix;
}

function appendUserGrain(b: Builder, s: UveSettings, e: ExpertSettings, inName: string, prefix: string): string {
    const amt = C.userGrainAmount(s.u_grain, e);
    if (amt <= 0) return inName;
    const alpha = amt * e.GVF_GRAIN_SETTINGS.svgStrength;
    const cA = C.grainColorAmount(e);
    const dg = fx(1 / 3 + 2 * cA / 3, 4), od = fx(1 / 3 - cA / 3, 4);
    mkGrain(b, inName, prefix, alpha, e.GVF_GRAIN_SETTINGS.svgFrequency,
        [dg, od, od, 0, 0, od, dg, od, 0, 0, od, od, dg, 0, 0, 0, 0, 0, 0, 1], false);
    b.matrix(inName, prefix + '_l', LUMA_ROWS);
    const N = 32, t: number[] = [];
    for (let i = 0; i <= N; i++) t.push(fx(C.grainWeight(i / N, e), 4));
    b.ct(prefix + '_l', prefix + '_w', table(t));
    b.arith(prefix + '_nm', prefix + '_w', prefix + '_r', 1, 0, -0.5, 0.5);
    b.arith(inName, prefix + '_r', prefix, 0, 1, alpha, -alpha * 0.5);
    return prefix;
}

function appendUserHue(b: Builder, s: UveSettings, e: ExpertSettings, inName: string, prefix: string): string {
    const deg = C.userHueDegrees(s.u_hue, e);
    if (Math.abs(deg) < 0.05) return inName;
    const k = C.hueSkinProtect(e);
    const rotName = k > 0 ? prefix + '_rot' : prefix;
    b.matrix(inName, rotName, rgb3x3To4x5(C.hueRotationMatrix(deg).map(r => r.map(v => fx(v, 5)))));
    if (k <= 0) return prefix;
    b.matrix(inName, prefix + '_k1', SKIN_K1);
    b.matrix(inName, prefix + '_k2', SKIN_K2);
    b.arith(prefix + '_k1', prefix + '_k2', prefix + '_sk', 1, 0, 0, 0);
    b.ct(prefix + '_sk', prefix + '_m', lin(k, 0));
    b.ct(prefix + '_sk', prefix + '_im', lin(-k, 1));
    b.arith(inName, prefix + '_m', prefix + '_a', 1, 0, 0, 0);
    b.arith(rotName, prefix + '_im', prefix + '_b', 1, 0, 0, 0);
    b.arith(prefix + '_a', prefix + '_b', prefix, 0, 1, 1, 0);
    return prefix;
}

function appendSatCurve(b: Builder, inName: string, prefix: string, gainOf: (sv: number) => number,
    skin: { amount: number; weightOf: (sv: number) => number; strength: number } | null): string {
    const lr = 0.2126, lg = 0.7152, lb = 0.0722;
    b.matrix(inName, prefix + '_l', [lr, lg, lb, 0, 0, lr, lg, lb, 0, 0, lr, lg, lb, 0, 0, 0, 0, 0, 1, 0]);
    b.matrix(inName, prefix + '_d', [
        (1 - lr) / 2, -lg / 2, -lb / 2, 0, 0.5,
        -lr / 2, (1 - lg) / 2, -lb / 2, 0, 0.5,
        -lr / 2, -lg / 2, (1 - lb) / 2, 0, 0.5, 0, 0, 0, 1, 0]);
    b.matrix(inName, prefix + '_m', [0.3333, -0.1667, -0.1667, 0, 0.5, -0.1667, 0.3333, -0.1667, 0, 0.5, -0.1667, -0.1667, 0.3333, 0, 0.5, 0, 0, 0, 1, 0]);
    b.ct(prefix + '_m', prefix + '_a', table([1, 0, 1]));
    b.matrix(prefix + '_a', prefix + '_s', [0.75, 0.75, 0.75, 0, 0, 0.75, 0.75, 0.75, 0, 0, 0.75, 0.75, 0.75, 0, 0, 0, 0, 0, 1, 0]);
    const N = 32;
    if (!skin) {
        const g: number[] = [];
        for (let i = 0; i <= N; i++) g.push(fx(clamp(gainOf(i / N) / 2, 0, 1), 4));
        b.ct(prefix + '_s', prefix + '_g', table(g));
    } else {
        const w: number[] = [];
        for (let i = 0; i <= N; i++) w.push(fx(clamp(skin.weightOf(i / N), 0, 1), 4));
        b.ct(prefix + '_s', prefix + '_wt', table(w));
        b.matrix(inName, prefix + '_k1', SKIN_K1);
        b.matrix(inName, prefix + '_k2', SKIN_K2);
        b.arith(prefix + '_k1', prefix + '_k2', prefix + '_sk', 1, 0, 0, 0);
        b.ct(prefix + '_sk', prefix + '_sp', lin(-skin.strength, 1));
        b.arith(prefix + '_wt', prefix + '_sp', prefix + '_w2', 1, 0, 0, 0);
        b.ct(prefix + '_w2', prefix + '_g', lin(skin.amount / 2, 0.5));
    }
    b.arith(prefix + '_d', prefix + '_g', prefix + '_r', 2, 0, -1, 0.5);
    b.arith(prefix + '_l', prefix + '_r', prefix, 0, 1, 2, -1);
    return prefix;
}

function appendUserSaturation(b: Builder, s: UveSettings, e: ExpertSettings, inName: string, prefix: string): string {
    const a = C.userSaturationAmount(s.u_sat, e);
    if (Math.abs(a) < 0.0005) return inName;
    if (C.saturationIsUniform(a, e)) {
        b.matrix(inName, prefix, matSaturateSpec(1 + a));
        return prefix;
    }
    const p = C.saturationPower(e);
    return appendSatCurve(b, inName, prefix, sv => 1 + a * Math.pow(1 - sv, p), null);
}

function appendUserVibrance(b: Builder, s: UveSettings, e: ExpertSettings, inName: string, prefix: string): string {
    const a = C.userVibranceAmount(s.u_vib, e);
    if (Math.abs(a) < 0.0005) return inName;
    const p = C.vibrancePower(e), k = C.vibranceSkinProtect(e);
    return appendSatCurve(b, inName, prefix, sv => 1 + a * Math.pow(1 - sv, p),
        k > 0 ? { amount: a, weightOf: sv => Math.pow(1 - sv, p), strength: k } : null);
}

function appendUserToneCurve(b: Builder, s: UveSettings, e: ExpertSettings, inName: string, prefix: string): string {
    const shAmt = C.userShadowsAmount(s.u_shadows, e);
    const hiAmt = C.userHighlightsAmount(s.u_highlights, e);
    const ctAmt = C.userContrastAmount(s.u_contrast, e);
    const gam = C.userGammaValue(s.u_gamma, e);
    const lvOn = C.userLevelsActive(s.u_black, s.u_white);
    if (!lvOn && Math.abs(ctAmt) < 0.0005 && Math.abs(shAmt) < 0.0005 && Math.abs(hiAmt) < 0.0005 && Math.abs(gam - 1.0) < 0.0005) return inName;
    const L = C.userLevels(s.u_black, s.u_white, e);
    const N = 256, t: number[] = [];
    for (let i = 0; i <= N; i++) {
        const x = lvOn ? C.levelsCurve(i / N, L, e) : i / N;
        t.push(fx(C.gammaCurve(C.highlightsCurve(C.shadowsCurve(C.contrastCurve(x, ctAmt, e), shAmt, e), hiAmt, e), gam, e), 4));
    }
    b.ct(inName, prefix, table(t));
    return prefix;
}

/**
 * mergeChainedColorMatrices(): merges directly chained matrices when the intermediate result has a single consumer.
 * `auto` and `clip` prims are substituted per frame by the engine, so they break a chain (never merged).
 */
export function mergeChainedMatrices(prims: Prim[], output: string): Prim[] {
    const refs: Record<string, number> = {};
    const addRef = (n?: string) => { if (n) refs[n] = (refs[n] || 0) + 1; };
    prims.forEach(p => {
        if ('in' in p) addRef(p.in);
        if ('in2' in p) addRef(p.in2);
        if (p.op === 'merge') p.ins.forEach(addRef);
    });
    addRef(output);
    const out: Prim[] = [];
    for (const p of prims) {
        const prev = out[out.length - 1];
        if (prev && prev.op === 'matrix' && p.op === 'matrix' && !prev.auto && !p.auto && !prev.clip && !p.clip && p.in === prev.out && refs[prev.out] === 1) {
            out[out.length - 1] = { op: 'matrix', in: prev.in, out: p.out, m: matMul4x5(p.m, prev.m) };
            continue;
        }
        out.push(p);
    }
    return out;
}

export interface GraphInput {
    settings: UveSettings;
    expert: ExpertSettings;
    /** Active LUT matrix (4x5 row-major) or null. */
    lut: Mat4x5 | null;
    customFilters: CustomFilterEntry[];
}

/**
 * buildFilter() port. The resulting graph corresponds to the active combo filter
 * (pickComboId) plus the in-filter tone matrix used by Chromium.
 */
export function buildUveGraph(input: GraphInput): UveGraph {
    const s = input.settings;
    const e = input.expert;
    const warnings: string[] = [];
    if (isNothingOn(s)) return { prims: [], output: 'SourceGraphic', hasAuto: false, warnings };

    // ensureSvgFilter() rounding of the base values
    const R = fx(C.getRadius(s.sr), 1);
    const A = fx(C.getSharpenA(s.sl), 3);
    const BS = fx(C.getBlurSigma(s.sl), 3);
    const BL = fx(normBL(s.bl), 1);
    const WL = fx(normSL(s.wl), 1);
    const DN = fx(normDN(s.dn), 1);
    const HDR = fx(normHDR(s.hdr), 2);
    const EDGE = fx(normEDGE(s.edge), 2);
    const blackOffset = C.blackToOffset(BL);
    const whiteAdj = C.whiteToHiAdj(WL);
    const prof = s.profile;

    const b = new Builder();
    let last = 'SourceGraphic';

    if (BS > 0) {
        b.blur(last, 'r_blur', R);
        last = 'r_blur';
    } else {
        b.blur('SourceGraphic', 'blur', R);
        b.arith('SourceGraphic', 'blur', 'r0', 0, 1 + A, -A, 0);
        last = 'r0';
    }

    if (blackOffset !== 0) {
        b.push({ op: 'ct', in: last, out: 'r_bl', f: rgbFuncs({ type: 'gamma', amplitude: 1, exponent: 1, offset: blackOffset }) });
        last = 'r_bl';
    }
    if (whiteAdj !== 0) {
        b.ct(last, 'r_wl', table(mkHighlightsTable(whiteAdj)));
        last = 'r_wl';
    }
    if (DN > 0) {
        mkDenoiseBlend(b, last, 'r_dn', C.dnToDenoiseSigma(DN), C.dnToDenoiseMix(DN));
        last = 'r_dn';
    } else if (DN < 0) {
        mkGrain(b, last, 'r_gr', C.dnToGrainAlpha(DN));
        last = 'r_gr';
    }

    if (HDR !== 0) {
        if (HDR > 0) {
            const sv = clamp(HDR, 0, 2);
            const clarityAmt = GVF_HDR_SETTINGS.svgClarityBase + sv * GVF_HDR_SETTINGS.svgClarityPerStep;
            const claritySigma = clamp(1.3 + R * 0.75, 1.3, 3.6);
            b.blur(last, 'r_hdr_cl_b', claritySigma);
            b.arith(last, 'r_hdr_cl_b', 'r_hdr_cl', 0, 1 + clarityAmt, -clarityAmt, 0);
            const mixB = clamp(0.65 + sv * 0.12, 0.65, 0.89);
            b.arith(last, 'r_hdr_cl', 'r_hdr_clb', 0, 1 - mixB, mixB, 0);
            last = 'r_hdr_clb';
            b.ct(last, 'r_hdr_tm', table(mkSCurveTable(sv)));
            last = 'r_hdr_tm';
            const slope = GVF_HDR_SETTINGS.svgSlopeBase + sv * GVF_HDR_SETTINGS.svgSlopePerStep;
            b.ct(last, 'r_hdr_lin', lin(slope, -0.015 + sv * 0.006));
            last = 'r_hdr_lin';
            b.matrix(last, 'r_hdr_sat', matSaturateSpec(GVF_HDR_SETTINGS.svgSaturationBase + sv * GVF_HDR_SETTINGS.svgSaturationPerStep));
            last = 'r_hdr_sat';
        } else {
            const sv = clamp(-HDR, 0, 1);
            mkDenoiseBlend(b, last, 'r_hdr_soft', clamp(0.9 + sv * 1.8, 0.9, 2.7), clamp(sv * 0.55, 0, 0.55));
            last = 'r_hdr_soft';
            b.matrix(last, 'r_hdr_soft2', matSaturateSpec(1.0 - sv * 0.18));
            last = 'r_hdr_soft2';
        }
    }

    if (s.cbFilter !== 'none') {
        b.matrix(last, 'r_cb', colorBlindnessMatrix(s.cbFilter));
        last = 'r_cb';
    }

    if (input.lut && s.lutProfile !== 'none') {
        b.matrix(last, 'r_lut', input.lut);
        last = 'r_lut';
    } else if (s.lutProfile !== 'none' && !input.lut) {
        warnings.push(`LUT profile "${s.lutProfile.replace('||', ' / ')}" is missing – LUT is not applied.`);
    }

    if (prof === 'user') {
        const rG = C.rgbGainToFactor(s.u_r_gain), gG = C.rgbGainToFactor(s.u_g_gain), bG = C.rgbGainToFactor(s.u_b_gain);
        if (Math.abs(rG - 1) > 0.01 || Math.abs(gG - 1) > 0.01 || Math.abs(bG - 1) > 0.01) {
            b.matrix(last, 'r_rgb', matRGBGain(rG, gG, bG));
            last = 'r_rgb';
        }
        last = appendUserSharpen(b, s, e, last, 'r_usharp');
        last = appendUserGrain(b, s, e, last, 'r_ugrain');
        last = appendUserToneCurve(b, s, e, last, 'r_utone');
        last = appendUserVibrance(b, s, e, last, 'r_uvib');
        last = appendUserSaturation(b, s, e, last, 'r_usat');
        last = appendUserHue(b, s, e, last, 'r_uhue');
    }

    if (s.darkMoody) {
        const ms = GVF_MOODY_SETTINGS;
        b.push({
            op: 'ct', in: last, out: 'r1', f: [
                { type: 'gamma', amplitude: ms.svgAmplitude.r, exponent: ms.svgExponent.r, offset: ms.svgOffset },
                { type: 'gamma', amplitude: ms.svgAmplitude.g, exponent: ms.svgExponent.g, offset: ms.svgOffset },
                { type: 'gamma', amplitude: ms.svgAmplitude.b, exponent: ms.svgExponent.b, offset: ms.svgOffset },
                ident()]
        });
        b.matrix('r1', 'r2', matSaturateSpec(ms.svgSaturation));
        last = 'r2';
    }
    if (s.tealOrange) {
        b.matrix(last, 'r3', rgb3x3To4x5(GVF_TEAL_SETTINGS.svgTealMatrix));
        b.matrix('r3', 'r4', rgb3x3To4x5(GVF_TEAL_SETTINGS.svgOrangeMatrix));
        b.matrix('r4', 'r4b', matSaturateSpec(GVF_TEAL_SETTINGS.svgSaturation));
        last = 'r4b';
    }
    if (s.vibrantSat) {
        b.matrix(last, 'r5', matSaturateSpec(GVF_VIBRANT_SETTINGS.saturation));
        last = 'r5';
    }
    if (PROFILE_MATRIX[prof]) {
        b.matrix(last, 'r_prof', PROFILE_MATRIX[prof]);
        b.matrix('r_prof', 'r_prof_sat', matSaturateSpec(PROFILE_SAT[prof]));
        last = 'r_prof_sat';
    }
    if (prof === 'anime') {
        b.blur(last, 'anime_denoised', 0.8);
        b.push({
            op: 'convolve', in: 'anime_denoised', out: 'anime_edges', orderX: 3, orderY: 3,
            kernel: [-1, -2, -1, 0, 0, 0, 1, 2, 1], divisor: 1, bias: 0, targetX: 1, targetY: 1, edgeMode: 'duplicate', preserveAlpha: false
        });
        b.ct('anime_edges', 'anime_darkEdges', lin(2.2, -0.3));
        b.ct('anime_darkEdges', 'anime_threshold', lin(3, -0.4));
        b.arith(last, 'anime_threshold', 'r_anime_lines', 0, 1, 0.3, 0);
        last = 'r_anime_lines';
    }
    if (EDGE > 0.0001) {
        const edgeStrength = Math.pow(clamp(EDGE, 0, 1), 2.2);
        b.blur(last, 'r_edge_pre', 0.30 + edgeStrength * 0.70);
        b.push({
            op: 'convolve', in: 'r_edge_pre', out: 'r_edge_sx', orderX: 3, orderY: 3, kernel: [-1, 0, 1, -2, 0, 2, -1, 0, 1],
            divisor: 1, bias: 0, targetX: 1, targetY: 1, edgeMode: 'duplicate', preserveAlpha: true
        });
        b.push({
            op: 'convolve', in: 'r_edge_pre', out: 'r_edge_sy', orderX: 3, orderY: 3, kernel: [-1, -2, -1, 0, 0, 0, 1, 2, 1],
            divisor: 1, bias: 0, targetX: 1, targetY: 1, edgeMode: 'duplicate', preserveAlpha: true
        });
        b.push({ op: 'blend', in: 'r_edge_sx', in2: 'r_edge_sy', out: 'r_edge_mix', mode: 'lighten' });
        b.ct('r_edge_mix', 'r_edge_mask', lin(-(0.35 + edgeStrength * 3.65), 1.0));
        b.push({ op: 'blend', in: last, in2: 'r_edge_mask', out: 'r_edge_final', mode: 'multiply' });
        last = 'r_edge_final';
    }

    b.matrix(last, 'r_auto', matIdentity4x5(), true);
    last = 'r_auto';

    // Enabled custom SVG filter codes (WebGL entries run as separate GPU passes afterwards)
    input.customFilters.filter(f => f.enabled && f.type === 'svg').forEach((entry, idx) => {
        const parsed = parseCustomSvgPrimitives(entry.code, last, `r_cust_${idx}_`);
        if (parsed.errors.length) warnings.push(`Custom filter "${entry.label}": ${parsed.errors.join('; ')}`);
        if (!parsed.prims.length) return;
        const before = last;
        parsed.prims.forEach(p => b.push(p));
        last = parsed.output;
        const bm = (entry.blendMode || 'normal') as BlendMode;
        const svgModes = new Set(['multiply', 'screen', 'overlay', 'darken', 'lighten', 'color-dodge', 'color-burn', 'hard-light',
            'soft-light', 'difference', 'exclusion', 'hue', 'saturation', 'color', 'luminosity']);
        if (svgModes.has(bm)) {
            b.push({ op: 'blend', in: before, in2: last, out: `r_cust_blend_${idx}`, mode: bm });
            last = `r_cust_blend_${idx}`;
        }
    });

    const tm = toneMatrix(s);
    if (tm) {
        b.matrix(last, 'r_tone', tm);
        last = 'r_tone';
    }

    return { prims: mergeChainedMatrices(b.prims, last), output: last, hasAuto: true, warnings };
}

/** Human readable summary of the active chain (for the inspector "Filter chain" list). */
export function describeChain(s: UveSettings, lutName: string | null): { id: string; label: string; on: boolean }[] {
    return [
        { id: 'sharp', label: normSL(s.sl) >= 0 ? `Sharpen ${normSL(s.sl).toFixed(1)} (r ${C.getRadius(s.sr).toFixed(1)})` : `Blur ${(-normSL(s.sl)).toFixed(1)}`, on: true },
        { id: 'bl', label: 'Black level', on: normBL(s.bl) !== 0 },
        { id: 'wl', label: 'White level', on: normSL(s.wl) !== 0 },
        { id: 'dn', label: normDN(s.dn) >= 0 ? 'Denoise' : 'Film grain', on: normDN(s.dn) !== 0 },
        { id: 'hdr', label: normHDR(s.hdr) >= 0 ? 'HDR boost' : 'HDR soft', on: normHDR(s.hdr) !== 0 },
        { id: 'cb', label: 'Color blindness filter', on: s.cbFilter !== 'none' },
        { id: 'lut', label: `LUT${lutName ? ': ' + lutName : ''}`, on: s.lutProfile !== 'none' },
        { id: 'user', label: 'Grading (User profile)', on: s.profile === 'user' && hasUserGrading(s) },
        { id: 'moody', label: 'Dark & Moody', on: s.darkMoody },
        { id: 'teal', label: 'Teal & Orange', on: s.tealOrange },
        { id: 'vib', label: 'Vibrant', on: s.vibrantSat },
        { id: 'prof', label: 'Profile matrix', on: !!PROFILE_MATRIX[s.profile] },
        { id: 'edge', label: 'Edge detection', on: normEDGE(s.edge) > 0 },
        { id: 'auto', label: 'Auto-Scene-Match', on: s.autoOn },
        { id: 'tone', label: 'Base / profile tone', on: s.enabled || !!PROFILE_TONE[s.profile] }
    ];
}

export function hasUserGrading(s: UveSettings): boolean {
    return [s.u_contrast, s.u_black, s.u_white, s.u_highlights, s.u_shadows, s.u_sat, s.u_vib, s.u_sharp, s.u_gamma, s.u_grain, s.u_hue]
        .some(v => normU(v) !== 0) || [s.u_r_gain, s.u_g_gain, s.u_b_gain].some(v => normRGB(v) !== 128);
}
