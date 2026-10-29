// Grading-HUD curve math, ported 1:1 from the userscript (source line ~9327-9537).
// Every function takes the active ExpertSettings instead of reading globals.
import type { ExpertSettings } from './expert';
import { normU, normSL, normSR, normRGB } from './settings';

const clamp = (n: number, a: number, b: number) => Math.min(b, Math.max(a, n));

export function userFactor(v: number, cfg: { perStep: number; min: number; max: number }) {
    return clamp(1.0 + (normU(v) * cfg.perStep), cfg.min, cfg.max);
}
export function userOffset(v: number, cfg: { perStep: number; maxOffset: number }) {
    return clamp(normU(v) * cfg.perStep, -cfg.maxOffset, cfg.maxOffset);
}

// Gamma
export function userGammaValue(v: number, e: ExpertSettings) {
    const gs = e.GVF_GAMMA_SETTINGS;
    const d = gs.higherIsBrighter === false ? -normU(v) : normU(v);
    return clamp(1.0 + (d * gs.perStep), gs.min, gs.max);
}
export function gammaShadowSlope(e: ExpertSettings) {
    const sl = Number(e.GVF_GAMMA_SETTINGS.maxShadowSlope) || 0;
    return sl > 0 ? Math.max(1, sl) : 0;
}
export function gammaCurve(x: number, g: number, e: ExpertSettings) {
    if (Math.abs(g - 1.0) < 0.0005) return x;
    let y = Math.pow(Math.max(0, x), 1.0 / g);
    const sl = gammaShadowSlope(e);
    if (sl > 0) y = Math.min(y, x * sl);
    return clamp(y, 0, 1);
}

// Contrast
export function userContrastAmount(v: number, e: ExpertSettings) {
    const cs = e.GVF_CONTRAST_SETTINGS;
    const d = cs.higherIsMore === false ? -normU(v) : normU(v);
    return clamp(userFactor(d, cs) - 1.0, -0.95, 0.95);
}
export function contrastPivot(e: ExpertSettings) { return clamp(Number(e.GVF_CONTRAST_SETTINGS.pivot) || 0.5, 0.2, 0.8); }
export function contrastPivotExp(e: ExpertSettings) { return Math.log(0.5) / Math.log(contrastPivot(e)); }
export function contrastCurve(x: number, amount: number, e: ExpertSettings) {
    const k = contrastPivotExp(e);
    const u = Math.pow(clamp(x, 0, 1), k);
    const v = clamp(u - amount * Math.sin(2 * Math.PI * u) / (2 * Math.PI), 0, 1);
    return Math.pow(v, 1 / k);
}

// Saturation / Vibrance
export function userSaturationAmount(v: number, e: ExpertSettings) {
    const ss = e.GVF_SATURATION_SETTINGS;
    const d = ss.higherIsMore === false ? -normU(v) : normU(v);
    return clamp(userFactor(d, ss) - 1.0, -1.0, 1.0);
}
export function saturationPower(e: ExpertSettings) { return Math.max(0, Number(e.GVF_SATURATION_SETTINGS.power) || 0); }
export function saturationIsUniform(a: number, e: ExpertSettings) {
    return saturationPower(e) === 0 || (a < 0 && e.GVF_SATURATION_SETTINGS.desaturateEvenly !== false);
}
export function userVibranceAmount(v: number, e: ExpertSettings) {
    const vs = e.GVF_VIBRANCE_SETTINGS;
    const d = vs.higherIsMore === false ? -normU(v) : normU(v);
    return clamp(userFactor(d, vs) - 1.0, -1.0, 1.0);
}
export function vibranceSkinProtect(e: ExpertSettings) { return clamp(Number(e.GVF_VIBRANCE_SETTINGS.skinProtect) || 0, 0, 1); }
export function vibrancePower(e: ExpertSettings) { return Math.max(0.5, Number(e.GVF_VIBRANCE_SETTINGS.power) || 3); }

// Levels
export function userBlackOffset(v: number, e: ExpertSettings) {
    const o = userOffset(v, e.GVF_BLACK_SETTINGS);
    return e.GVF_BLACK_SETTINGS.higherIsDarker === false ? -o : o;
}
export function userWhiteOffset(v: number, e: ExpertSettings) {
    const o = userOffset(v, e.GVF_WHITE_SETTINGS);
    return e.GVF_WHITE_SETTINGS.higherIsBrighter === false ? -o : o;
}
export interface Levels { inBlack: number; inWhite: number; outBlack: number; outWhite: number }
export function userLevels(uBlack: number, uWhite: number, e: ExpertSettings): Levels {
    const b = userBlackOffset(uBlack, e);
    const w = userWhiteOffset(uWhite, e);
    return { inBlack: Math.max(0, b), inWhite: 1 - Math.max(0, w), outBlack: Math.max(0, -b), outWhite: 1 + Math.min(0, w) };
}
export function userLevelsActive(uBlack: number, uWhite: number) { return normU(uBlack) !== 0 || normU(uWhite) !== 0; }
function levelsSoftness(cfg: { softness: number }) { return clamp(Number(cfg.softness) || 0, 0, 0.5); }
function softToe(t: number, k: number) {
    if (k <= 0) return Math.max(0, t);
    if (t <= -k) return 0;
    if (t >= k) return t;
    return (t + k) * (t + k) / (4 * k);
}
export function levelsCurve(x: number, L: Levels, e: ExpertSettings) {
    let t = (x - L.inBlack) / Math.max(L.inWhite - L.inBlack, 0.0001);
    if (L.inBlack > 0.0001) t = softToe(t, levelsSoftness(e.GVF_BLACK_SETTINGS));
    if (L.inWhite < 0.9999) t = 1 - softToe(1 - t, levelsSoftness(e.GVF_WHITE_SETTINGS));
    t = clamp(t, 0, 1);
    return L.outBlack + (L.outWhite - L.outBlack) * t;
}

// Highlights
function highlightsPower(e: ExpertSettings) { return Math.max(1, Number(e.GVF_HIGHLIGHTS_SETTINGS.power) || 3); }
function highlightsThreshold(e: ExpertSettings) { return clamp(Number(e.GVF_HIGHLIGHTS_SETTINGS.threshold) || 0, 0, 0.9); }
function highlightsCurvePeak(e: ExpertSettings) {
    const p = highlightsPower(e);
    return Math.pow(p / (p + 1), p) / (p + 1);
}
export function userHighlightsAmount(v: number, e: ExpertSettings) {
    const hs = e.GVF_HIGHLIGHTS_SETTINGS;
    const d = hs.higherIsBrighter ? normU(v) : -normU(v);
    const p = highlightsPower(e), range = 1 - highlightsThreshold(e), peak = highlightsCurvePeak(e);
    let maxRise = 0;
    for (let i = 1; i < 200; i++) {
        const t = i / 200;
        maxRise = Math.max(maxRise, p * Math.pow(t, p - 1) - (p + 1) * Math.pow(t, p));
    }
    const darkCap = Math.min(Math.max(0, Number(hs.maxDarken) || 0), 0.95 * peak * range / Math.max(maxRise, 0.0001));
    return clamp(d * hs.perStep, -darkCap, peak * range * 0.95);
}
export function highlightsCurve(x: number, amount: number, e: ExpertSettings) {
    const th = highlightsThreshold(e);
    if (x <= th) return clamp(x, 0, 1);
    const t = (x - th) / (1 - th);
    return clamp(x + (amount / highlightsCurvePeak(e)) * Math.pow(t, highlightsPower(e)) * (1 - t), 0, 1);
}

// Shadows
function shadowsCurvePower(e: ExpertSettings) { return Math.max(1, Number(e.GVF_SHADOWS_SETTINGS.power) || 3); }
function shadowsCurvePeak(e: ExpertSettings) {
    const p = shadowsCurvePower(e);
    return Math.pow(p / (p + 1), p) / (p + 1);
}
function shadowsThreshold(e: ExpertSettings) { return clamp(Number(e.GVF_SHADOWS_SETTINGS.threshold) || 1, 0.1, 1); }
export function userShadowsAmount(v: number, e: ExpertSettings) {
    const ss = e.GVF_SHADOWS_SETTINGS;
    const d = ss.higherIsDarker ? -normU(v) : normU(v);
    const p = shadowsCurvePower(e), range = shadowsThreshold(e), peak = shadowsCurvePeak(e);
    let maxFall = 0;
    for (let i = 1; i < 200; i++) {
        const t = i / 200;
        maxFall = Math.max(maxFall, -(Math.pow(1 - t, p) - p * t * Math.pow(1 - t, p - 1)));
    }
    const brightCap = Math.min(Math.max(0, Number(ss.maxBrighten) || 0), 0.95 * peak * range / Math.max(maxFall, 0.0001));
    return clamp(d * ss.perStep, -peak * range * 0.95, brightCap);
}
export function shadowsCurve(x: number, amount: number, e: ExpertSettings) {
    const th = shadowsThreshold(e);
    if (x >= th) return clamp(x, 0, 1);
    const t = Math.max(0, x) / th;
    return clamp(x + (amount / shadowsCurvePeak(e)) * t * Math.pow(1 - t, shadowsCurvePower(e)), 0, 1);
}

// Hue
export function hueRotationMatrix(deg: number): number[][] {
    const t = deg * Math.PI / 180, cs = Math.cos(t), sn = Math.sin(t);
    const lr = 0.2126, lg = 0.7152, lb = 0.0722, kb = 1.8556, kr = 1.5748;
    const rot = (c: number[]) => {
        const y = lr * c[0] + lg * c[1] + lb * c[2];
        const cb = (c[2] - y) / kb, cr = (c[0] - y) / kr;
        const cb2 = cs * cb - sn * cr, cr2 = sn * cb + cs * cr;
        const r = y + kr * cr2, b = y + kb * cb2;
        return [r, (y - lr * r - lb * b) / lg, b];
    };
    const cr = rot([1, 0, 0]), cg = rot([0, 1, 0]), cbl = rot([0, 0, 1]);
    return [[cr[0], cg[0], cbl[0]], [cr[1], cg[1], cbl[1]], [cr[2], cg[2], cbl[2]]];
}
export function userHueDegrees(v: number, e: ExpertSettings) {
    const hs = e.GVF_HUE_SETTINGS;
    const d = hs.reverse === true ? -normU(v) : normU(v);
    return clamp(d * hs.perStep, -hs.maxDegrees, hs.maxDegrees);
}
export function hueSkinProtect(e: ExpertSettings) { return clamp(Number(e.GVF_HUE_SETTINGS.skinProtect) || 0, 0, 1); }

// Grain
export function userGrainAmount(v: number, e: ExpertSettings) {
    const gs = e.GVF_GRAIN_SETTINGS;
    const d = gs.higherIsMore ? normU(v) : -normU(v);
    return Math.max(0, d * gs.perStep);
}
function grainPower(e: ExpertSettings) { return Math.max(0, Number(e.GVF_GRAIN_SETTINGS.power) || 0); }
function grainPeak(e: ExpertSettings) { return clamp(Number(e.GVF_GRAIN_SETTINGS.peak) || 0.5, 0.2, 0.8); }
function grainPeakExp(e: ExpertSettings) { return Math.log(0.5) / Math.log(grainPeak(e)); }
export function grainColorAmount(e: ExpertSettings) { return clamp(Number(e.GVF_GRAIN_SETTINGS.colorAmount) || 0, 0, 1); }
export function grainWeight(l: number, e: ExpertSettings) {
    const p = grainPower(e);
    if (p === 0) return 1;
    const lk = Math.pow(clamp(l, 0, 1), grainPeakExp(e));
    return Math.pow(clamp(4 * lk * (1 - lk), 0, 1), p);
}

// Sharpen
export function userSharpenValue(v: number, e: ExpertSettings) {
    return e.GVF_SHARPEN_SETTINGS.higherIsSharper === false ? -normU(v) : normU(v);
}
export function sharpenLightHalo(e: ExpertSettings) { return clamp(Number(e.GVF_SHARPEN_SETTINGS.lightHalo ?? 1), 0, 1); }
export function sharpenDarkHalo(e: ExpertSettings) { return clamp(Number(e.GVF_SHARPEN_SETTINGS.darkHalo ?? 1), 0, 1); }
export function sharpenSkinProtect(e: ExpertSettings) { return clamp(Number(e.GVF_SHARPEN_SETTINGS.skinProtect) || 0, 0, 1); }
export function sharpenCurve(d: number, amount: number, e: ExpertSettings) {
    const ss = e.GVF_SHARPEN_SETTINGS;
    const m = Math.max(0.001, Number(ss.maxChange) || 0.15);
    const t = Math.max(0, Number(ss.noiseThreshold) || 0);
    const ad = Math.abs(d);
    let gate = 1;
    if (t > 0) { const k = clamp(ad / (2 * t), 0, 1); gate = k * k * (3 - 2 * k); }
    const y = amount * d * gate / m;
    return m * y / Math.sqrt(1 + y * y);
}

export const rgbGainToFactor = (v: number) => normRGB(v) / 128;

// Base SL/SR/BL/WL/DN mapping
export const getSharpenA = (sl: number) => Math.max(0, normSL(sl));
export const getBlurSigma = (sl: number) => Math.max(0, -normSL(sl));
export const getRadius = (sr: number) => Math.max(0.1, Math.abs(normSR(sr)));
export const blackToOffset = (v: number) => -clamp(v, -2, 2) * 0.04;
export const whiteToHiAdj = (v: number) => clamp(v, -2, 2) * 0.06;
export const dnToDenoiseMix = (v: number) => clamp(v, 0, 1.5) * 0.5;
export const dnToDenoiseSigma = (v: number) => clamp(v, 0, 1.5) * 0.8;
export const dnToGrainAlpha = (v: number) => clamp(-v, 0, 1.5) * (0.20 / 1.5);
