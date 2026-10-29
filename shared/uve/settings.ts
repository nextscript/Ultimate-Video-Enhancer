// Canonical effect settings. Key names are identical to the userscript's user-profile
// "settings" object (DEFAULT_USER_PROFILE, source line ~4806) so profiles round-trip unchanged.

export type ToneProfile = 'off' | 'film' | 'anime' | 'gaming' | 'eyecare' | 'user';
export type CbFilter = 'none' | 'protanopia' | 'deuteranopia' | 'tritanomaly';

export interface UveSettings {
    enabled: boolean;      // Base Tone (Ctrl+Alt+B)
    darkMoody: boolean;    // Dark & Moody (Ctrl+Alt+D)
    tealOrange: boolean;   // Teal & Orange (Ctrl+Alt+O)
    vibrantSat: boolean;   // Vibrant (Ctrl+Alt+V)
    sl: number;            // Sharpen/Blur  -2..2
    sr: number;            // Radius        -2..2
    bl: number;            // Black level   -2..2
    wl: number;            // White level   -2..2
    dn: number;            // Denoise/Grain -1.5..1.5
    edge: number;          // Edge detect    0..1
    hdr: number;           // HDR           -1..2
    profile: ToneProfile;
    renderMode: 'svg' | 'gpu';
    lutProfile: string;    // "group||name" or "none"
    autoOn: boolean;
    autoStrength: number;  // 0..1
    autoLockWB: boolean;
    u_contrast: number;    // grading sliders -10..10
    u_black: number;
    u_white: number;
    u_highlights: number;
    u_shadows: number;
    u_sat: number;
    u_vib: number;
    u_sharp: number;
    u_gamma: number;
    u_grain: number;
    u_hue: number;
    u_r_gain: number;      // 0..255, 128 = neutral
    u_g_gain: number;
    u_b_gain: number;
    cbFilter: CbFilter;
}

// DEFAULT_USER_PROFILE.settings (Chromium variant) of UVE 1.17.5
export const DEFAULT_SETTINGS: UveSettings = {
    enabled: true, darkMoody: true, tealOrange: false, vibrantSat: false,
    sl: 1.0, sr: 0.5, bl: 1.2, wl: 0.2, dn: 0.0, edge: 0.1, hdr: 0.0,
    profile: 'user', renderMode: 'svg', lutProfile: 'none',
    autoOn: true, autoStrength: 0.65, autoLockWB: true,
    u_contrast: 0, u_black: 0, u_white: 0, u_highlights: 0, u_shadows: 0, u_sat: 0, u_vib: 0,
    u_sharp: 0, u_gamma: 0, u_grain: 0, u_hue: 0, u_r_gain: 128, u_g_gain: 128, u_b_gain: 128,
    cbFilter: 'none'
};

/** "Neutral" settings: every effect off. Used for the global bypass and "Reset all → neutral". */
export const NEUTRAL_SETTINGS: UveSettings = {
    ...DEFAULT_SETTINGS,
    enabled: false, darkMoody: false, sl: 0, sr: 0, bl: 0, wl: 0, dn: 0, edge: 0, hdr: 0,
    profile: 'off', autoOn: false
};

export interface NumSpec {
    key: keyof UveSettings;
    label: string;
    min: number;
    max: number;
    step: number;
    desc: string;
    sourceKey: string;
}

const clamp = (n: number, a: number, b: number) => Math.min(b, Math.max(a, n));
const roundTo = (n: number, step: number) => Math.round(n / step) * step;
const snap0 = (n: number, eps: number) => (Math.abs(n) <= eps ? 0 : n);

// Slider specs taken from the HUD ranges / norm*() functions of the userscript.
export const NUM_SPECS: Record<string, NumSpec> = {
    sl: { key: 'sl', label: 'Sharpen / Blur', min: -2, max: 2, step: 0.1, sourceKey: 'gvf_sl', desc: '> 0 unsharp-mask sharpening (amount), < 0 Gaussian blur.' },
    sr: { key: 'sr', label: 'Radius', min: -2, max: 2, step: 0.1, sourceKey: 'gvf_sr', desc: 'Radius (|value|, min 0.1 px) of the sharpen/blur pass.' },
    bl: { key: 'bl', label: 'Black Level', min: -2, max: 2, step: 0.1, sourceKey: 'gvf_bl', desc: 'Base black offset: higher = deeper blacks (offset -0.04 per step).' },
    wl: { key: 'wl', label: 'White Level', min: -2, max: 2, step: 0.1, sourceKey: 'gvf_wl', desc: 'Highlight table above 78% input: higher = brighter whites.' },
    dn: { key: 'dn', label: 'Denoise / Grain', min: -1.5, max: 1.5, step: 0.1, sourceKey: 'gvf_dn', desc: '> 0 blur-blend denoise, < 0 adds fractal film grain.' },
    edge: { key: 'edge', label: 'Edge Detection', min: 0, max: 1, step: 0.01, sourceKey: 'gvf_edge', desc: 'Sobel edge mask that darkens strong edges (outline look).' },
    hdr: { key: 'hdr', label: 'HDR Boost', min: -1, max: 2, step: 0.01, sourceKey: 'gvf_hdr', desc: 'HDR-like local contrast, S-curve tone map and saturation. < 0 = soft look. Not HDR10 output.' },
    autoStrength: { key: 'autoStrength', label: 'Auto Strength', min: 0, max: 1, step: 0.01, sourceKey: 'gvf_auto_strength', desc: 'How strongly Auto-Scene-Match corrects brightness, contrast, saturation and white balance.' },
    u_contrast: { key: 'u_contrast', label: 'Contrast', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_contrast', desc: 'S-curve around the pivot tone (Expert: Contrast).' },
    u_black: { key: 'u_black', label: 'Black Level', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_black', desc: 'Levels black point (> 0 crush, < 0 lift).' },
    u_white: { key: 'u_white', label: 'White Level', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_white', desc: 'Levels white point (> 0 brighten, < 0 lower).' },
    u_highlights: { key: 'u_highlights', label: 'Highlights', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_highlights', desc: 'Tone curve for bright tones only.' },
    u_shadows: { key: 'u_shadows', label: 'Shadows', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_shadows', desc: 'Tone curve for dark tones only (> 0 darker by default).' },
    u_sat: { key: 'u_sat', label: 'Saturation', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_saturation', desc: 'Saturation curve, vivid colors protected.' },
    u_vib: { key: 'u_vib', label: 'Vibrance', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_vibrance', desc: 'Boosts muted colors, protects skin tones.' },
    u_sharp: { key: 'u_sharp', label: 'Sharpen', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_sharpen', desc: 'Luma detail curve with noise gate; < 0 softens.' },
    u_gamma: { key: 'u_gamma', label: 'Gamma', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_gamma', desc: 'Midtone curve, black and white fixed.' },
    u_grain: { key: 'u_grain', label: 'Grain (Banding)', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_grain', desc: 'Luma-weighted debanding noise (only positive values add grain by default).' },
    u_hue: { key: 'u_hue', label: 'Hue Correction', min: -10, max: 10, step: 0.1, sourceKey: 'gvf_u_hue', desc: 'Luma-preserving hue rotation (BT.709 YCbCr plane).' },
    u_r_gain: { key: 'u_r_gain', label: 'Red Gain', min: 0, max: 255, step: 1, sourceKey: 'gvf_u_r_gain', desc: 'Red channel gain (128 = neutral).' },
    u_g_gain: { key: 'u_g_gain', label: 'Green Gain', min: 0, max: 255, step: 1, sourceKey: 'gvf_u_g_gain', desc: 'Green channel gain (128 = neutral).' },
    u_b_gain: { key: 'u_b_gain', label: 'Blue Gain', min: 0, max: 255, step: 1, sourceKey: 'gvf_u_b_gain', desc: 'Blue channel gain (128 = neutral).' }
};

export const PROFILE_NAMES: Record<ToneProfile, string> = {
    off: 'Off', film: 'Movie', anime: 'Anime', gaming: 'Gaming', eyecare: 'EyeCare', user: 'User'
};

export const CB_NAMES: Record<CbFilter, string> = {
    none: 'None', protanopia: 'Protanopia', deuteranopia: 'Deuteranopia', tritanomaly: 'Tritanomaly'
};

// norm*() functions (source line ~9320)
export const normSL = (v: number) => snap0(roundTo(clamp(Number(v) || 0, -2, 2), 0.01), 0.005);
export const normSR = normSL;
export const normBL = normSL;
export const normWL = normSL;
export const normDN = (v: number) => snap0(roundTo(clamp(Number(v) || 0, -1.5, 1.5), 0.01), 0.005);
export const normHDR = (v: number) => snap0(roundTo(clamp(Number(v) || 0, -1.0, 2.0), 0.01), 0.005);
export const normEDGE = (v: number) => snap0(roundTo(clamp(Number(v) || 0, 0, 1.0), 0.01), 0.005);
export const normU = (v: number) => roundTo(clamp(Number(v) || 0, -10, 10), 0.1);
export const normRGB = (v: number) => clamp(Math.round(Number(v) || 128), 0, 255);

const PROFILES: ToneProfile[] = ['off', 'film', 'anime', 'gaming', 'eyecare', 'user'];
const CBS: CbFilter[] = ['none', 'protanopia', 'deuteranopia', 'tritanomaly'];

/** Validates any object into complete settings (unknown keys dropped, ranges clamped). */
export function sanitizeSettings(input: unknown, base: UveSettings = DEFAULT_SETTINGS): UveSettings {
    const s = { ...base };
    if (!input || typeof input !== 'object') return s;
    const o = input as Record<string, unknown>;
    const bool = (k: keyof UveSettings) => { if (typeof o[k] === 'boolean') (s as Record<string, unknown>)[k] = o[k]; };
    const num = (k: keyof UveSettings, min: number, max: number) => {
        if (o[k] === undefined || o[k] === null || o[k] === '') return;
        const n = Number(o[k]);
        if (Number.isFinite(n)) (s as Record<string, unknown>)[k] = clamp(n, min, max);
    };
    (['enabled', 'darkMoody', 'tealOrange', 'vibrantSat', 'autoOn', 'autoLockWB'] as const).forEach(bool);
    num('sl', -2, 2); num('sr', -2, 2); num('bl', -2, 2); num('wl', -2, 2);
    num('dn', -1.5, 1.5); num('edge', 0, 1); num('hdr', -1, 2); num('autoStrength', 0, 1);
    (['u_contrast', 'u_black', 'u_white', 'u_highlights', 'u_shadows', 'u_sat', 'u_vib', 'u_sharp', 'u_gamma', 'u_grain', 'u_hue'] as const)
        .forEach(k => num(k, -10, 10));
    (['u_r_gain', 'u_g_gain', 'u_b_gain'] as const).forEach(k => num(k, 0, 255));
    const p = String(o.profile ?? '').toLowerCase() as ToneProfile;
    if (PROFILES.includes(p)) s.profile = p;
    const rm = String(o.renderMode ?? '').toLowerCase();
    if (rm === 'svg' || rm === 'gpu') s.renderMode = rm;
    if (typeof o.lutProfile === 'string') s.lutProfile = o.lutProfile || 'none';
    const cb = String(o.cbFilter ?? '').toLowerCase() as CbFilter;
    if (CBS.includes(cb)) s.cbFilter = cb;
    return s;
}

/**
 * Converts the "gvf-settings" JSON export of the userscript (e.g. My_Profile.json in the
 * repository) into canonical settings. Field mapping verified against the repository samples.
 */
export function settingsFromGvfSettings(obj: Record<string, unknown>): UveSettings {
    const u = (obj.user && typeof obj.user === 'object') ? obj.user as Record<string, unknown> : {};
    return sanitizeSettings({
        enabled: obj.baseOtp, darkMoody: obj.darkMoody, tealOrange: obj.tealOrange, vibrantSat: obj.vibrantSat,
        sl: obj.sl, sr: obj.sr, bl: obj.bl, wl: obj.wl, dn: obj.dn, edge: obj.edge, hdr: obj.hdr,
        profile: obj.profile, renderMode: obj.renderMode, lutProfile: obj.lutProfile,
        autoOn: obj.autoOn, autoStrength: obj.autoStrength, autoLockWB: obj.autoLockWB,
        u_contrast: u.contrast, u_black: u.black, u_white: u.white, u_highlights: u.highlights,
        u_shadows: u.shadows, u_sat: u.saturation, u_vib: u.vibrance, u_sharp: u.sharpen,
        u_gamma: u.gamma, u_grain: u.grain, u_hue: u.hue,
        u_r_gain: u.r_gain, u_g_gain: u.g_gain, u_b_gain: u.b_gain, cbFilter: obj.cbFilter
    });
}

/** Inverse of settingsFromGvfSettings(): writes the userscript "gvf-settings" format. */
export function settingsToGvfSettings(s: UveSettings): Record<string, unknown> {
    return {
        schema: 'gvf-settings', ver: '1.10',
        baseOtp: s.enabled, notify: true, darkMoody: s.darkMoody, tealOrange: s.tealOrange, vibrantSat: s.vibrantSat,
        sl: s.sl, sr: s.sr, bl: s.bl, wl: s.wl, dn: s.dn, edge: s.edge, hdr: s.hdr,
        profile: s.profile, lutProfile: s.lutProfile, renderMode: s.renderMode,
        autoOn: s.autoOn, autoStrength: s.autoStrength, autoLockWB: s.autoLockWB,
        adaptiveFps: { min: 2, max: 10, current: 2 },
        user: {
            contrast: s.u_contrast, black: s.u_black, white: s.u_white, highlights: s.u_highlights,
            shadows: s.u_shadows, saturation: s.u_sat, vibrance: s.u_vib, sharpen: s.u_sharp,
            gamma: s.u_gamma, grain: s.u_grain, hue: s.u_hue,
            r_gain: s.u_r_gain, g_gain: s.u_g_gain, b_gain: s.u_b_gain
        },
        debug: false, logs: true, cbFilter: s.cbFilter
    };
}
