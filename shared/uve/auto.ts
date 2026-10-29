// Auto-Scene-Match (Ctrl+Alt+A) – deterministic offline port of the userscript auto loop
// (source lines ~11076-11724). The browser samples the playing video on a 96x54 canvas with
// an adaptive 2-10 fps scheduler driven by wall-clock time. UVE Manager pre-analyses the clip
// once (96x54 frames on a fixed 10 fps media-time grid) and replays the identical state machine
// with media time instead of wall-clock time, so seeking to any timestamp and the final export
// both see exactly the same correction matrix.
import { type Mat4x5, LUMA, matBrightnessContrast, matHueRotate, matIdentity4x5, matMul4x5, matSaturation } from './matrix';

export const ANALYSIS_W = 96;
export const ANALYSIS_H = 54;
export const ANALYSIS_FPS = 10;

export interface FrameStats { mR: number; mG: number; mB: number; mY: number; sdY: number; mCh: number; mSat: number; skin: number }
export interface AnalysisSample { t: number; stats: FrameStats; luma: Uint8Array }
export interface AutoKey { t: number; m: Mat4x5 }

const clamp = (n: number, a: number, b: number) => Math.min(b, Math.max(a, n));

/** computeFrameStats() on an RGB24 (3 bytes per pixel) 96x54 frame. */
export function computeFrameStats(rgb: Uint8Array, w = ANALYSIS_W, h = ANALYSIS_H): FrameStats {
    let sumR = 0, sumG = 0, sumB = 0, sumY = 0, sumY2 = 0, sumCh = 0, sumSatRel = 0, satRelCount = 0, skinCount = 0, count = 0;
    const stepPx = 2;
    for (let y = 0; y < h; y += stepPx) {
        for (let x = 0; x < w; x += stepPx) {
            const i = (y * w + x) * 3;
            const r = rgb[i] / 255, g = rgb[i + 1] / 255, b = rgb[i + 2] / 255;
            const Y = LUMA.r * r + LUMA.g * g + LUMA.b * b;
            sumR += r; sumG += g; sumB += b; sumY += Y; sumY2 += Y * Y;
            const mx = Math.max(r, g, b), mn = Math.min(r, g, b);
            sumCh += (mx - mn);
            if (mx > 0.06) { sumSatRel += (mx - mn) / mx; satRelCount++; }
            if (r > g && g >= b && r > 0.12) {
                const span = r - b;
                if (span > 0.04) {
                    const hRatio = (g - b) / span, sRel = span / r;
                    if (hRatio > 0.15 && hRatio < 0.80 && sRel > 0.12 && sRel < 0.70) skinCount++;
                }
            }
            count++;
        }
    }
    const inv = 1 / Math.max(1, count);
    const mY = sumY * inv;
    return {
        mR: sumR * inv, mG: sumG * inv, mB: sumB * inv, mY,
        sdY: Math.sqrt(Math.max(0, (sumY2 * inv) - (mY * mY))),
        mCh: sumCh * inv,
        mSat: satRelCount > 0 ? (sumSatRel / satRelCount) : 0.25,
        skin: skinCount * inv
    };
}

/** Subsampled 8-bit luma used by computeMotionFromImage(). */
export function motionLuma(rgb: Uint8Array, w = ANALYSIS_W, h = ANALYSIS_H): Uint8Array {
    const out = new Uint8Array(Math.ceil(w / 2) * Math.ceil(h / 2));
    let k = 0;
    for (let y = 0; y < h; y += 2) {
        for (let x = 0; x < w; x += 2) {
            const i = (y * w + x) * 3;
            out[k++] = (rgb[i] * 54 + rgb[i + 1] * 183 + rgb[i + 2] * 19) >> 8;
        }
    }
    return out;
}

function motionBetween(cur: Uint8Array, prev: Uint8Array | null): number {
    if (!prev || prev.length !== cur.length) return 1.0;
    let diff = 0;
    for (let i = 0; i < cur.length; i++) diff += Math.abs(cur[i] - prev[i]);
    return diff / cur.length;
}

function detectCut(sig: FrameStats, last: FrameStats | null): { cut: boolean; score: number } {
    if (!last) return { cut: false, score: 0 };
    const dY = Math.abs(sig.mY - last.mY);
    const dCh = Math.abs(sig.mCh - last.mCh);
    const dRB = Math.abs((sig.mR - sig.mB) - (last.mR - last.mB));
    const dGB = Math.abs((sig.mG - sig.mB) - (last.mG - last.mB));
    const score = (dY * 1.1) + (dCh * 0.9) + (dRB * 0.7) + (dGB * 0.7);
    return { cut: score > 0.14, score };
}

const approach = (cur: number, tgt: number, a: number, dead = 0.002) => {
    const d = tgt - cur;
    return Math.abs(d) < dead ? tgt : cur + d * a;
};
const wrapHueDeg = (d: number) => { while (d > 180) d -= 360; while (d < -180) d += 360; return d; };

export interface AutoParams { strength: number; lockWB: boolean }

/** buildAutoMatrixValues() for given smoothed values. */
export function autoMatrix(cur: { br: number; ct: number; sat: number; hue: number }): Mat4x5 {
    let m = matIdentity4x5();
    m = matMul4x5(matHueRotate(clamp(cur.hue, -12, 12)), m);
    m = matMul4x5(matSaturation(clamp(cur.sat, 1.0, 1.15)), m);
    m = matMul4x5(matBrightnessContrast(clamp(cur.br, 0.78, 1.22), clamp(cur.ct, 1.0, 1.30)), m);
    return m;
}

/**
 * Replays the userscript auto loop over pre-analysed samples (sorted by t, seconds).
 * Returns keyframes: the matrix that is active from key.t until the next key.
 */
export function runAutoSceneMatch(samples: AnalysisSample[], p: AutoParams): AutoKey[] {
    const keys: AutoKey[] = [{ t: 0, m: matIdentity4x5() }];
    if (!samples.length) return keys;
    const s = clamp(p.strength, 0, 1);
    const AUTO = {
        boostMs: 800, minBoostEarlyMs: 700, motionAlpha: 0.20, motionThresh: 0.015, motionMinFrames: 8,
        statsAlpha: 0.06, scoreAlpha: 0.16,
        cur: { br: 1.0, ct: 1.0, sat: 1.0, hue: 0.0 }, tgt: { br: 1.0, ct: 1.0, sat: 1.0, hue: 0.0 },
        lastSig: null as FrameStats | null, lastLuma: null as Uint8Array | null, motionEma: 0, motionFrames: 0,
        statsEma: null as FrameStats | null, scoreEma: 0, tBoostStart: 0, tBoostUntil: 800
    };
    const ADAPT = { MIN: 2, MAX: 10, current: 2, history: [] as number[], historySize: 5 };

    const adaptiveFps = (changeScore: number) => {
        ADAPT.history.push(changeScore);
        if (ADAPT.history.length > ADAPT.historySize) ADAPT.history.shift();
        const avg = ADAPT.history.reduce((a, b) => a + b, 0) / ADAPT.history.length;
        const t1 = Math.min(avg, 0.1) / 0.1;
        const t2 = Math.max(0, Math.min(avg - 0.1, 0.2)) / 0.2;
        const t3 = Math.max(0, Math.min(avg - 0.3, 0.7)) / 0.7;
        const target = (avg < 0.1 ? 2 + t1 * 2 : 0) + (avg >= 0.1 && avg < 0.3 ? 4 + t2 * 3 : 0) + (avg >= 0.3 ? 7 + t3 * 3 : 0);
        const rounded = Math.round(Math.max(ADAPT.MIN, Math.min(target, ADAPT.MAX)) * 2) / 2;
        ADAPT.current += Math.max(-1, Math.min(rounded - ADAPT.current, 1));
        return ADAPT.current;
    };
    const pickFps = (nowMs: number, cutScore: number) => {
        const a = clamp(AUTO.scoreAlpha, 0.05, 0.95);
        AUTO.scoreEma = AUTO.scoreEma * (1 - a) + cutScore * a;
        const fps = adaptiveFps(cutScore);
        if (nowMs < AUTO.tBoostUntil) {
            const age = nowMs - AUTO.tBoostStart;
            return Math.max(fps, (age >= 0 && age < AUTO.minBoostEarlyMs) ? 10 : 8);
        }
        return fps;
    };
    const statsAvg = (sig: FrameStats): FrameStats => {
        const a = clamp(AUTO.statsAlpha, 0.05, 0.95);
        if (!AUTO.statsEma) { AUTO.statsEma = { ...sig }; return AUTO.statsEma; }
        const e = AUTO.statsEma;
        (Object.keys(e) as (keyof FrameStats)[]).forEach(k => { e[k] = e[k] * (1 - a) + sig[k] * a; });
        return e;
    };
    const updateTargets = (sig: FrameStats) => {
        const errY = clamp(0.50 - sig.mY, -0.22, 0.22);
        const br = clamp(1.0 + errY * 0.85, 0.78, 1.22);
        const errSd = clamp(0.23 - sig.sdY, -0.18, 0.18);
        const ct = clamp(1.0 + errSd * 0.85, 1.0, 1.30);
        const errSat = clamp(0.30 - (sig.mSat ?? 0.25), 0, 0.20);
        const skinDamp = 1.0 - clamp((sig.skin || 0) / 0.08, 0, 1) * 0.70;
        const sat = clamp(1.0 + errSat * 0.60 * skinDamp, 1.0, 1.15);
        let hue = 0.0;
        if (p.lockWB) hue = clamp(-clamp(sig.mR - sig.mB, -0.18, 0.18) * 28.0, -10.0, 10.0);
        AUTO.tgt.br = clamp(1.0 + (br - 1.0) * s, 0.78, 1.22);
        AUTO.tgt.ct = clamp(1.0 + (ct - 1.0) * s, 1.0, 1.30);
        AUTO.tgt.sat = clamp(1.0 + (sat - 1.0) * s, 1.0, 1.15);
        AUTO.tgt.hue = clamp(hue * s, -12.0, 12.0);
    };
    const smooth = (isCut: boolean) => {
        const a = isCut ? 0.10 : 0.025;
        AUTO.cur.br = approach(AUTO.cur.br, AUTO.tgt.br, a, 0.003);
        AUTO.cur.ct = approach(AUTO.cur.ct, AUTO.tgt.ct, a, 0.003);
        AUTO.cur.sat = approach(AUTO.cur.sat, AUTO.tgt.sat, a, 0.004);
        AUTO.cur.hue = wrapHueDeg(approach(AUTO.cur.hue, AUTO.tgt.hue, a, 0.06));
    };

    const step = 1 / ANALYSIS_FPS;
    let i = 0;
    while (i < samples.length) {
        const smp = samples[i];
        const nowMs = smp.t * 1000;
        const motion = motionBetween(smp.luma, AUTO.lastLuma);
        AUTO.lastLuma = smp.luma;
        AUTO.motionEma = AUTO.motionEma * (1 - AUTO.motionAlpha) + motion * AUTO.motionAlpha;
        AUTO.motionFrames = AUTO.motionEma >= AUTO.motionThresh ? AUTO.motionFrames + 1 : 0;
        const cut = detectCut(smp.stats, AUTO.lastSig);
        AUTO.lastSig = smp.stats;
        const sig = statsAvg(smp.stats);
        if (cut.cut) { AUTO.tBoostStart = nowMs; AUTO.tBoostUntil = nowMs + AUTO.boostMs; }
        const allowUpdate = cut.cut || AUTO.motionFrames >= AUTO.motionMinFrames;
        let fps = ADAPT.current;
        if (allowUpdate) {
            fps = pickFps(nowMs, clamp(cut.score, 0, 1));
            updateTargets(sig);
            smooth(cut.cut);
            const m = autoMatrix(AUTO.cur);
            const prev = keys[keys.length - 1].m;
            if (m.some((v, k) => Math.abs(v - prev[k]) > 1e-6)) keys.push({ t: smp.t, m });
        }
        const delaySec = Math.max(0.08, 1 / Math.max(1, fps));
        // next tick on the analysis grid (never stays on the same sample)
        i = Math.max(i + 1, Math.round((smp.t + delaySec) / step) - Math.round(samples[0].t / step));
    }
    return keys;
}

/** Binary search: matrix active at media time t. */
export function autoMatrixAt(keys: AutoKey[] | null, t: number): Mat4x5 {
    if (!keys || !keys.length) return matIdentity4x5();
    let lo = 0, hi = keys.length - 1;
    if (t < keys[0].t) return keys[0].m;
    while (lo < hi) {
        const mid = (lo + hi + 1) >> 1;
        if (keys[mid].t <= t) lo = mid; else hi = mid - 1;
    }
    return keys[lo].m;
}

/** Unpacks the analysis cache format (8 floats of stats + 48x27 luma per sample). */
export function samplesFromAnalysis(r: { t0: number; count: number; stats: Float32Array; luma: Uint8Array }): AnalysisSample[] {
    const lumaLen = Math.ceil(ANALYSIS_W / 2) * Math.ceil(ANALYSIS_H / 2);
    const out: AnalysisSample[] = [];
    for (let i = 0; i < r.count; i++) {
        const s = r.stats.subarray(i * 8, i * 8 + 8);
        out.push({
            t: r.t0 + i / ANALYSIS_FPS,
            stats: { mR: s[0], mG: s[1], mB: s[2], mY: s[3], sdY: s[4], mCh: s[5], mSat: s[6], skin: s[7] },
            luma: r.luma.subarray(i * lumaLen, (i + 1) * lumaLen)
        });
    }
    return out;
}
