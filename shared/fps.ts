// Output frame rate of an export (v1.0.1). '' = keep the source frame rate.
// Common NTSC rates are mapped to their exact rationals (23.976 → 24000/1001 …).

const NTSC: Record<string, [number, number]> = {
    '23.976': [24000, 1001], '23.98': [24000, 1001], '29.97': [30000, 1001], '47.952': [48000, 1001],
    '59.94': [60000, 1001], '119.88': [120000, 1001]
};

export const FPS_PRESETS = ['23.976', '24', '25', '29.97', '30', '48', '50', '59.94', '60', '120'];

export const FPS_MIN = 1, FPS_MAX = 240;

/** Parses user input ("30", "29,97", "24000/1001"); null when empty or invalid. */
export function parseFps(input: string | undefined): { num: number; den: number } | null {
    const t = String(input ?? '').trim().replace(',', '.');
    if (!t) return null;
    if (NTSC[t]) return { num: NTSC[t][0], den: NTSC[t][1] };
    const frac = t.match(/^(\d+)\s*\/\s*(\d+)$/);
    let num: number, den: number;
    if (frac) { num = Number(frac[1]); den = Number(frac[2]); }
    else if (/^\d+(\.\d+)?$/.test(t)) {
        const x = Number(t);
        if (Number.isInteger(x)) { num = x; den = 1; } else { num = Math.round(x * 1000); den = 1000; }
    } else return null;
    if (!den || !(num / den >= FPS_MIN && num / den <= FPS_MAX)) return null;
    return { num, den };
}

/** The frame rate used by the export: the parsed setting, or the source rate. */
export function outputFps(setting: string | undefined, srcNum: number, srcDen: number): { num: number; den: number } {
    return parseFps(setting) ?? { num: srcNum || 25, den: srcDen || 1 };
}
