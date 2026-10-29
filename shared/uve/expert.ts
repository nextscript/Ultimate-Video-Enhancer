// Port of the GVF_*_SETTINGS blocks and the Expert Settings logic of
// Ultimate Video Enhancer v1.17.5 (userscript lines ~4898-5242 and GVF_EXPERT_UI ~15878).
// Values, semantics and ranges are copied from the source; keep them in sync on upstream updates.

export const GVF_BASE_SETTINGS = { brightness: 1.02, contrast: 1.05, saturation: 1.15 };

export const GVF_MOODY_SETTINGS = {
    gpuBrightness: 0.96,
    gpuSaturation: 0.92,
    svgAmplitude: { r: 0.96, g: 0.96, b: 0.97 },
    svgExponent: { r: 1.14, g: 1.13, b: 1.11 },
    svgOffset: -0.015,
    svgSaturation: 0.90
};

export const GVF_TEAL_SETTINGS = {
    gpuHueShift: -5,
    gpuSepia: 0.15,
    gpuSaturation: 1.10,
    svgTealMatrix: [
        [0.96, 0.02, 0.00],
        [0.02, 1.02, 0.02],
        [0.00, 0.04, 1.06]
    ],
    svgOrangeMatrix: [
        [1.10, 0.02, 0.00],
        [0.02, 1.00, 0.00],
        [0.00, 0.00, 0.90]
    ],
    svgSaturation: 1.08
};

export const GVF_VIBRANT_SETTINGS = { saturation: 1.35 };

export const GVF_HDR_SETTINGS = {
    useLastValue: true,
    defaultValue: 0.3,
    gpuMaxSingleVideo: 0.65,
    gpuMaxMultiVideo: 0.42,
    gpuContrastPerStep: 0.15,
    gpuSaturationPerStep: 0.10,
    svgClarityBase: 0.55,
    svgClarityPerStep: 0.55,
    svgSlopeBase: 1.10,
    svgSlopePerStep: 0.18,
    svgSaturationBase: 1.10,
    svgSaturationPerStep: 0.30
};

export interface ExpertSettings {
    GVF_CONTRAST_SETTINGS: { higherIsMore: boolean; perStep: number; min: number; max: number; pivot: number };
    GVF_BLACK_SETTINGS: { higherIsDarker: boolean; perStep: number; maxOffset: number; softness: number };
    GVF_WHITE_SETTINGS: { higherIsBrighter: boolean; perStep: number; maxOffset: number; softness: number };
    GVF_HIGHLIGHTS_SETTINGS: { higherIsBrighter: boolean; perStep: number; power: number; threshold: number; maxDarken: number };
    GVF_SHADOWS_SETTINGS: { higherIsDarker: boolean; perStep: number; power: number; threshold: number; maxBrighten: number };
    GVF_SATURATION_SETTINGS: { higherIsMore: boolean; perStep: number; min: number; max: number; power: number; desaturateEvenly: boolean };
    GVF_VIBRANCE_SETTINGS: { higherIsMore: boolean; perStep: number; min: number; max: number; power: number; skinProtect: number };
    GVF_GAMMA_SETTINGS: { higherIsBrighter: boolean; perStep: number; min: number; max: number; maxShadowSlope: number };
    GVF_SHARPEN_SETTINGS: {
        higherIsSharper: boolean; maxAmount: number; lightHalo: number; darkHalo: number; skinProtect: number;
        noiseThreshold: number; maxChange: number; svgRadius: number; svgMaxSoften: number; webglPerStep: number;
    };
    GVF_GRAIN_SETTINGS: {
        higherIsMore: boolean; perStep: number; power: number; peak: number; colorAmount: number;
        svgStrength: number; svgFrequency: number;
    };
    GVF_HUE_SETTINGS: { reverse: boolean; perStep: number; maxDegrees: number; skinProtect: number };
}

export type ExpertBlockName = keyof ExpertSettings;

export const EXPERT_DEFAULTS: ExpertSettings = {
    GVF_CONTRAST_SETTINGS: { higherIsMore: true, perStep: 0.04, min: 0.60, max: 1.60, pivot: 0.5 },
    GVF_BLACK_SETTINGS: { higherIsDarker: true, perStep: 0.012, maxOffset: 0.12, softness: 0.05 },
    GVF_WHITE_SETTINGS: { higherIsBrighter: true, perStep: 0.012, maxOffset: 0.12, softness: 0.05 },
    GVF_HIGHLIGHTS_SETTINGS: { higherIsBrighter: true, perStep: 0.010, power: 3, threshold: 0.0, maxDarken: 0.40 },
    GVF_SHADOWS_SETTINGS: { higherIsDarker: true, perStep: 0.010, power: 3, threshold: 1.0, maxBrighten: 0.40 },
    GVF_SATURATION_SETTINGS: { higherIsMore: true, perStep: 0.05, min: 0.40, max: 1.80, power: 1, desaturateEvenly: true },
    GVF_VIBRANCE_SETTINGS: { higherIsMore: true, perStep: 0.05, min: 0.50, max: 1.50, power: 3, skinProtect: 0.6 },
    GVF_GAMMA_SETTINGS: { higherIsBrighter: true, perStep: 0.025, min: 0.60, max: 1.60, maxShadowSlope: 2.0 },
    GVF_SHARPEN_SETTINGS: {
        higherIsSharper: true, maxAmount: 1.5, lightHalo: 0.6, darkHalo: 1.0, skinProtect: 0.5,
        noiseThreshold: 0.01, maxChange: 0.15, svgRadius: 1.0, svgMaxSoften: 1.2, webglPerStep: 0.1
    },
    GVF_GRAIN_SETTINGS: {
        higherIsMore: true, perStep: 0.01, power: 1, peak: 0.35, colorAmount: 0.3, svgStrength: 1.5, svgFrequency: 0.9
    },
    GVF_HUE_SETTINGS: { reverse: false, perStep: 3.0, maxDegrees: 30, skinProtect: 0.7 }
};

export const cloneExpert = (e: ExpertSettings): ExpertSettings => JSON.parse(JSON.stringify(e));
export const expertDefaults = (): ExpertSettings => cloneExpert(EXPERT_DEFAULTS);

export interface ExpertField {
    k: string;
    label: string;
    type: 'bool' | 'num';
    min?: number;
    max?: number;
    step?: number;
    desc: string;
}

export interface ExpertBlockUi {
    name: ExpertBlockName;
    title: string;
    desc: string;
    fields: ExpertField[];
}

// GVF_EXPERT_UI (source line ~15878): labels, ranges and step sizes exactly as in UVE.
export const EXPERT_UI: ExpertBlockUi[] = [
    {
        name: 'GVF_CONTRAST_SETTINGS', title: 'Contrast',
        desc: 'S-curve around a pivot tone. Black, white and the pivot stay fixed, nothing is clipped.',
        fields: [
            { k: 'higherIsMore', label: 'Higher = more contrast', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'perStep', label: 'Strength per step', type: 'num', min: 0.005, max: 0.09, step: 0.005, desc: 'How much each slider step changes the contrast.' },
            { k: 'min', label: 'Lowest contrast', type: 'num', min: 0.1, max: 1, step: 0.01, desc: 'Limit for the flattest curve (slider at the low end).' },
            { k: 'max', label: 'Highest contrast', type: 'num', min: 1, max: 1.95, step: 0.01, desc: 'Limit for the steepest curve (slider at the high end).' },
            { k: 'pivot', label: 'Pivot tone', type: 'num', min: 0.2, max: 0.8, step: 0.01, desc: 'Tone that stays fixed. Lower = contrast turns around darker tones (good for dark videos).' }
        ]
    },
    {
        name: 'GVF_BLACK_SETTINGS', title: 'Black Level',
        desc: 'Black point like in a levels tool: deepen or lift the blacks.',
        fields: [
            { k: 'higherIsDarker', label: 'Higher = deeper blacks', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'perStep', label: 'Shift per step', type: 'num', min: 0.002, max: 0.03, step: 0.001, desc: 'How far each slider step moves the black point.' },
            { k: 'maxOffset', label: 'Max shift', type: 'num', min: 0.02, max: 0.3, step: 0.01, desc: 'Limit of the black point shift in both directions.' },
            { k: 'softness', label: 'Softness', type: 'num', min: 0, max: 0.2, step: 0.01, desc: 'Smooth transition into black instead of a hard cut. 0 = hard.' }
        ]
    },
    {
        name: 'GVF_WHITE_SETTINGS', title: 'White Level',
        desc: 'White point like in a levels tool: brighten or lower the whites.',
        fields: [
            { k: 'higherIsBrighter', label: 'Higher = brighter whites', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'perStep', label: 'Shift per step', type: 'num', min: 0.002, max: 0.03, step: 0.001, desc: 'How far each slider step moves the white point.' },
            { k: 'maxOffset', label: 'Max shift', type: 'num', min: 0.02, max: 0.3, step: 0.01, desc: 'Limit of the white point shift in both directions.' },
            { k: 'softness', label: 'Softness', type: 'num', min: 0, max: 0.2, step: 0.01, desc: 'Smooth transition into white instead of a hard cut. 0 = hard.' }
        ]
    },
    {
        name: 'GVF_HIGHLIGHTS_SETTINGS', title: 'Highlights',
        desc: 'Tone curve for bright tones only. Black and white stay fixed.',
        fields: [
            { k: 'higherIsBrighter', label: 'Higher = brighter highlights', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'perStep', label: 'Strength per step', type: 'num', min: 0.002, max: 0.03, step: 0.001, desc: 'How much each slider step brightens or darkens the highlights.' },
            { k: 'power', label: 'Focus', type: 'num', min: 1, max: 8, step: 0.5, desc: 'Higher = only the very brightest tones are affected.' },
            { k: 'threshold', label: 'Start tone', type: 'num', min: 0, max: 0.9, step: 0.01, desc: 'Tones below this stay completely untouched. 0 = whole range.' },
            { k: 'maxDarken', label: 'Max darkening', type: 'num', min: 0, max: 0.6, step: 0.01, desc: 'Strongest allowed darkening of the highlights.' }
        ]
    },
    {
        name: 'GVF_SHADOWS_SETTINGS', title: 'Shadows',
        desc: 'Tone curve for dark tones only. Black and white stay fixed.',
        fields: [
            { k: 'higherIsDarker', label: 'Higher = darker shadows', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'perStep', label: 'Strength per step', type: 'num', min: 0.002, max: 0.03, step: 0.001, desc: 'How much each slider step darkens or lifts the shadows.' },
            { k: 'power', label: 'Focus', type: 'num', min: 1, max: 8, step: 0.5, desc: 'Higher = only the very darkest tones are affected.' },
            { k: 'threshold', label: 'End tone', type: 'num', min: 0.1, max: 1, step: 0.01, desc: 'Tones above this stay completely untouched. 1 = whole range.' },
            { k: 'maxBrighten', label: 'Max lifting', type: 'num', min: 0, max: 0.6, step: 0.01, desc: 'Strongest allowed lifting of the shadows.' }
        ]
    },
    {
        name: 'GVF_SATURATION_SETTINGS', title: 'Saturation',
        desc: 'Saturation curve: muted colors get the full boost, vivid colors less, so nothing clips.',
        fields: [
            { k: 'higherIsMore', label: 'Higher = more color', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'perStep', label: 'Strength per step', type: 'num', min: 0.01, max: 0.1, step: 0.005, desc: 'How much each slider step changes the color strength.' },
            { k: 'min', label: 'Lowest saturation', type: 'num', min: 0, max: 1, step: 0.01, desc: 'Limit when reducing color. 0 = can go fully grey.' },
            { k: 'max', label: 'Highest saturation', type: 'num', min: 1, max: 2, step: 0.01, desc: 'Limit of the boost for muted colors.' },
            { k: 'power', label: 'Vivid color protection', type: 'num', min: 0, max: 4, step: 0.1, desc: '0 = all colors boosted equally (may clip). Higher = vivid colors are protected more.' },
            { k: 'desaturateEvenly', label: 'Desaturate evenly', type: 'bool', desc: 'On: lowering removes color from all colors equally. Off: vivid colors keep more color.' }
        ]
    },
    {
        name: 'GVF_VIBRANCE_SETTINGS', title: 'Vibrance',
        desc: 'Boosts mainly the muted colors, vivid colors and skin tones stay natural.',
        fields: [
            { k: 'higherIsMore', label: 'Higher = more vibrance', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'perStep', label: 'Strength per step', type: 'num', min: 0.01, max: 0.1, step: 0.005, desc: 'How much each slider step changes the muted colors.' },
            { k: 'min', label: 'Lowest vibrance', type: 'num', min: 0, max: 1, step: 0.01, desc: 'Limit when reducing vibrance.' },
            { k: 'max', label: 'Highest vibrance', type: 'num', min: 1, max: 2, step: 0.01, desc: 'Limit of the boost for muted colors.' },
            { k: 'power', label: 'Focus on muted colors', type: 'num', min: 0.5, max: 6, step: 0.1, desc: 'Higher = only the most muted colors are boosted.' },
            { k: 'skinProtect', label: 'Skin protection', type: 'num', min: 0, max: 1, step: 0.05, desc: '0 = off, 1 = skin tones are not changed at all.' }
        ]
    },
    {
        name: 'GVF_GAMMA_SETTINGS', title: 'Gamma',
        desc: 'Brightens or darkens the midtones. Black and white stay fixed.',
        fields: [
            { k: 'higherIsBrighter', label: 'Higher = brighter midtones', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'perStep', label: 'Strength per step', type: 'num', min: 0.005, max: 0.08, step: 0.005, desc: 'How much each slider step changes the midtones.' },
            { k: 'min', label: 'Darkest midtones', type: 'num', min: 0.3, max: 1, step: 0.01, desc: 'Lower limit of the gamma value.' },
            { k: 'max', label: 'Brightest midtones', type: 'num', min: 1, max: 3, step: 0.01, desc: 'Upper limit of the gamma value.' },
            { k: 'maxShadowSlope', label: 'Shadow noise protection', type: 'num', min: 0, max: 6, step: 0.1, desc: 'Limits how hard near-black tones are lifted (less visible noise). 0 = off.' }
        ]
    },
    {
        name: 'GVF_SHARPEN_SETTINGS', title: 'Sharpen',
        desc: 'Detail curve on brightness: real details get sharper, noise and halos are kept low.',
        fields: [
            { k: 'higherIsSharper', label: 'Higher = sharper', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'maxAmount', label: 'Strength at +10', type: 'num', min: 0.2, max: 4, step: 0.1, desc: 'Edge boost at the highest slider value.' },
            { k: 'lightHalo', label: 'Bright halos', type: 'num', min: 0, max: 1, step: 0.05, desc: 'Strength of the brightening side of edges. Lower = fewer bright halos.' },
            { k: 'darkHalo', label: 'Dark halos', type: 'num', min: 0, max: 1, step: 0.05, desc: 'Strength of the darkening side of edges. Lower = fewer dark outlines.' },
            { k: 'skinProtect', label: 'Skin protection', type: 'num', min: 0, max: 1, step: 0.05, desc: 'Less sharpening on skin (pores, blemishes). 0 = off.' },
            { k: 'noiseThreshold', label: 'Noise threshold', type: 'num', min: 0, max: 0.05, step: 0.001, desc: 'Details smaller than this are not sharpened (noise, compression). 0 = off.' },
            { k: 'maxChange', label: 'Max change', type: 'num', min: 0.02, max: 0.4, step: 0.01, desc: 'Soft limit of the brightness change per pixel (prevents halos).' },
            { k: 'svgRadius', label: 'Detail radius (SVG)', type: 'num', min: 0.3, max: 3, step: 0.1, desc: 'SVG mode: size of the details that are sharpened, in pixels.' },
            { k: 'svgMaxSoften', label: 'Soften radius (SVG)', type: 'num', min: 0.2, max: 4, step: 0.1, desc: 'SVG mode: blur radius at -10, in pixels.' },
            { k: 'webglPerStep', label: 'Strength per step (WebGL)', type: 'num', min: 0.02, max: 0.3, step: 0.01, desc: 'WebGL mode of the browser script only. Stored for profile compatibility; the desktop engine uses the SVG chain.' }
        ]
    },
    {
        name: 'GVF_GRAIN_SETTINGS', title: 'Grain (Banding)',
        desc: 'Fine noise that hides color banding. Black and white stay clean.',
        fields: [
            { k: 'higherIsMore', label: 'Higher = more grain', type: 'bool', desc: 'Slider direction. Off reverses the slider.' },
            { k: 'perStep', label: 'Amount per step', type: 'num', min: 0.002, max: 0.04, step: 0.001, desc: 'How much grain each slider step adds.' },
            { k: 'power', label: 'Focus', type: 'num', min: 0, max: 4, step: 0.1, desc: 'Higher = grain only around the peak tone. 0 = even on all tones.' },
            { k: 'peak', label: 'Peak tone', type: 'num', min: 0.2, max: 0.8, step: 0.01, desc: 'Brightness with the strongest grain. Lower = more grain in the shadows.' },
            { k: 'colorAmount', label: 'Color grain', type: 'num', min: 0, max: 1, step: 0.05, desc: '0 = grey grain, 1 = full color grain like film.' },
            { k: 'svgStrength', label: 'Strength (SVG)', type: 'num', min: 0.2, max: 4, step: 0.1, desc: 'SVG mode: grain strength, to match the WebGL look.' },
            { k: 'svgFrequency', label: 'Fineness (SVG)', type: 'num', min: 0.2, max: 2, step: 0.05, desc: 'SVG mode: grain size. Higher = finer grain.' }
        ]
    },
    {
        name: 'GVF_HUE_SETTINGS', title: 'Hue Correction',
        desc: 'Rotates all hues while brightness stays exactly the same. Useful against color casts.',
        fields: [
            { k: 'reverse', label: 'Reverse direction', type: 'bool', desc: 'On: the slider rotates the hue the other way.' },
            { k: 'perStep', label: 'Degrees per step', type: 'num', min: 0.5, max: 10, step: 0.5, desc: 'Hue rotation per slider step.' },
            { k: 'maxDegrees', label: 'Max rotation', type: 'num', min: 5, max: 180, step: 1, desc: 'Limit of the rotation in both directions, in degrees.' },
            { k: 'skinProtect', label: 'Skin protection', type: 'num', min: 0, max: 1, step: 0.05, desc: 'Skin tones keep their hue. 0 = off, 1 = full protection.' }
        ]
    }
];

// expertMerge(): copies values from src into target, only for keys that exist in def and have the same type.
function expertMerge(target: Record<string, unknown>, src: unknown, def: Record<string, unknown>): void {
    if (!src || typeof src !== 'object' || !target || !def) return;
    const s = src as Record<string, unknown>;
    Object.keys(def).forEach(k => {
        if (!(k in s)) return;
        const d = def[k], v = s[k];
        if (typeof d === 'number') {
            const n = Number(v);
            if (Number.isFinite(n)) target[k] = n;
        } else if (typeof d === 'boolean') {
            if (typeof v === 'boolean') target[k] = v;
        }
    });
}

/** Builds a full expert object from defaults + a (partial) override object. Unknown keys are ignored. */
export function expertFromDiff(diff: unknown): ExpertSettings {
    const out = expertDefaults();
    if (!diff || typeof diff !== 'object') return out;
    (Object.keys(EXPERT_DEFAULTS) as ExpertBlockName[]).forEach(name => {
        expertMerge(out[name] as unknown as Record<string, unknown>, (diff as Record<string, unknown>)[name],
            EXPERT_DEFAULTS[name] as unknown as Record<string, unknown>);
    });
    return out;
}

/** expertSettingsDiff(): all values that differ from the defaults. */
export function expertDiff(e: ExpertSettings): Partial<Record<ExpertBlockName, Record<string, unknown>>> {
    const out: Partial<Record<ExpertBlockName, Record<string, unknown>>> = {};
    (Object.keys(EXPERT_DEFAULTS) as ExpertBlockName[]).forEach(name => {
        const cur = e[name] as unknown as Record<string, unknown>;
        const def = EXPERT_DEFAULTS[name] as unknown as Record<string, unknown>;
        const diff: Record<string, unknown> = {};
        Object.keys(def).forEach(k => { if (JSON.stringify(cur[k]) !== JSON.stringify(def[k])) diff[k] = cur[k]; });
        if (Object.keys(diff).length) out[name] = diff;
    });
    return out;
}

/** exportExpertSettings() format (type gvf-expert-settings), compatible with the userscript. */
export function exportExpert(e: ExpertSettings) {
    return { type: 'gvf-expert-settings', version: 1, script: '1.17.5', exported: new Date().toISOString(), settings: expertDiff(e) };
}

/** importExpertSettings(): returns null when the object is not an expert settings export. */
export function importExpert(obj: unknown): ExpertSettings | null {
    if (!obj || typeof obj !== 'object') return null;
    const o = obj as Record<string, unknown>;
    if (o.type && o.type !== 'gvf-expert-settings') return null;
    const data = (o.settings && typeof o.settings === 'object') ? o.settings as Record<string, unknown> : o;
    const names = Object.keys(data).filter(n => n in EXPERT_DEFAULTS);
    if (!names.length && o.type !== 'gvf-expert-settings') return null;
    return expertFromDiff(data);
}
