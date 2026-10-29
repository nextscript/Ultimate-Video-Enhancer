// 4x5 color matrix utilities, ported 1:1 from the userscript (source line ~9598-9715).
export type Mat4x5 = number[];

export const LUMA = { r: 0.2126, g: 0.7152, b: 0.0722 };

export function matIdentity4x5(): Mat4x5 {
    return [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0];
}

/** out = a * b (apply b first, then a) */
export function matMul4x5(a: Mat4x5, b: Mat4x5): Mat4x5 {
    const out = new Array<number>(20);
    for (let row = 0; row < 4; row++) {
        for (let col = 0; col < 4; col++) {
            let s = 0;
            for (let k = 0; k < 4; k++) s += a[row * 5 + k] * b[k * 5 + col];
            out[row * 5 + col] = s;
        }
        let o = a[row * 5 + 4];
        for (let k = 0; k < 4; k++) o += a[row * 5 + k] * b[k * 5 + 4];
        out[row * 5 + 4] = o;
    }
    return out;
}

export function matBrightnessContrast(br: number, ct: number): Mat4x5 {
    const g = br * ct;
    const off = br * 0.5 * (1 - ct);
    return [g, 0, 0, 0, off, 0, g, 0, 0, off, 0, 0, g, 0, off, 0, 0, 0, 1, 0];
}

export function matSaturation(s: number): Mat4x5 {
    const ir = (1 - s) * LUMA.r, ig = (1 - s) * LUMA.g, ib = (1 - s) * LUMA.b;
    return [ir + s, ig, ib, 0, 0, ir, ig + s, ib, 0, 0, ir, ig, ib + s, 0, 0, 0, 0, 0, 1, 0];
}

export function matHueRotate(deg: number): Mat4x5 {
    const rad = (deg * Math.PI) / 180;
    const cosA = Math.cos(rad), sinA = Math.sin(rad);
    const lr = LUMA.r, lg = LUMA.g, lb = LUMA.b;
    return [
        lr + cosA * (1 - lr) + sinA * (-lr), lg + cosA * (-lg) + sinA * (-lg), lb + cosA * (-lb) + sinA * (1 - lb), 0, 0,
        lr + cosA * (-lr) + sinA * (0.143), lg + cosA * (1 - lg) + sinA * (0.140), lb + cosA * (-lb) + sinA * (-0.283), 0, 0,
        lr + cosA * (-lr) + sinA * (-(1 - lr)), lg + cosA * (-lg) + sinA * (lg), lb + cosA * (1 - lb) + sinA * (lb), 0, 0,
        0, 0, 0, 1, 0
    ];
}

export function matRGBGain(r: number, g: number, b: number): Mat4x5 {
    return [r, 0, 0, 0, 0, 0, g, 0, 0, 0, 0, 0, b, 0, 0, 0, 0, 0, 1, 0];
}

// Exact feColorMatrix type="saturate" / "hueRotate" matrices (Filter Effects spec weights)
export function matSaturateSpec(s: number): Mat4x5 {
    return [
        0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s, 0, 0,
        0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s, 0, 0,
        0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s, 0, 0,
        0, 0, 0, 1, 0
    ];
}

export function matHueRotateSpec(deg: number): Mat4x5 {
    const rad = (deg * Math.PI) / 180;
    const c = Math.cos(rad), s = Math.sin(rad);
    return [
        0.213 + c * 0.787 - s * 0.213, 0.715 - c * 0.715 - s * 0.715, 0.072 - c * 0.072 + s * 0.928, 0, 0,
        0.213 - c * 0.213 + s * 0.143, 0.715 + c * 0.285 + s * 0.140, 0.072 - c * 0.072 - s * 0.283, 0, 0,
        0.213 - c * 0.213 - s * 0.787, 0.715 - c * 0.715 + s * 0.715, 0.072 + c * 0.928 + s * 0.072, 0, 0,
        0, 0, 0, 1, 0
    ];
}

/** feColorMatrix type="luminanceToAlpha" */
export function matLuminanceToAlpha(): Mat4x5 {
    return [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.2125, 0.7154, 0.0721, 0, 0];
}

export const matCssBrightness = (b: number): Mat4x5 => [b, 0, 0, 0, 0, 0, b, 0, 0, 0, 0, 0, b, 0, 0, 0, 0, 0, 1, 0];
export function matCssContrast(c: number): Mat4x5 {
    const o = 0.5 - 0.5 * c;
    return [c, 0, 0, 0, o, 0, c, 0, 0, o, 0, 0, c, 0, o, 0, 0, 0, 1, 0];
}

export function rgb3x3To4x5(m: number[][]): Mat4x5 {
    return [m[0][0], m[0][1], m[0][2], 0, 0, m[1][0], m[1][1], m[1][2], 0, 0, m[2][0], m[2][1], m[2][2], 0, 0, 0, 0, 0, 1, 0];
}

// getColorBlindnessMatrix() (source line ~9553)
export function colorBlindnessMatrix(type: string, strength = 1.0): Mat4x5 {
    const k = Math.max(0, Math.min(1, strength));
    const I = matIdentity4x5();
    const mix = (M: number[]) => M.map((v, i) => I[i] + (v - I[i]) * k);
    if (type === 'protanopia') {
        return mix([
            0.9715365562, 0.0000451198, 0.0000221655, 0, 0.0054466531,
            0.4906507166, 0.4721896523, 0.0000465477, 0, 0.0045839181,
            0.4515750655, -0.3950199862, 0.7845041177, 0, 0.0772238079,
            0, 0, 0, 1, 0
        ]);
    }
    if (type === 'deuteranopia') {
        return mix([
            1.1313510788, -0.2442083804, 0.0000487006, 0, 0.0570148323,
            0.0001729681, 0.9711718674, 0.0000761152, 0, 0.0057285327,
            -0.1474623881, 0.1563820548, 0.9401548784, 0, 0.0166599033,
            0, 0, 0, 1, 0
        ]);
    }
    if (type === 'tritanomaly' || type === 'tritanopia') {
        return mix([0.92, -0.06, 0.00, 0, 0, 0.04, 1.05, 0.00, 0, 0, 0.00, -0.02, 1.02, 0, 0, 0, 0, 0, 1, 0]);
    }
    return I;
}

/** Applies a 4x5 matrix to an RGB triple (alpha assumed 1). */
export function applyMat(m: Mat4x5, r: number, g: number, b: number): [number, number, number] {
    return [
        m[0] * r + m[1] * g + m[2] * b + m[3] + m[4],
        m[5] * r + m[6] * g + m[7] * b + m[8] + m[9],
        m[10] * r + m[11] * g + m[12] * b + m[13] + m[14]
    ];
}

export function isValidMat4x5(m: unknown): m is Mat4x5 {
    return Array.isArray(m) && m.length === 20 && m.every(v => typeof v === 'number' && Number.isFinite(v) && Math.abs(v) < 1e6);
}
