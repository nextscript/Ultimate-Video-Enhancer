// LUT profiles. UVE represents every LUT as a 4x5 color matrix (affine RGB fit of the source
// LUT), stored as {schema:'gvf-lut-profile', name, group, matrix4x5}. Ported from the userscript
// (source lines ~4773-4800, ~5959-6333, ~7704-7806).
import { type Mat4x5, isValidMat4x5 } from './matrix';

export interface LutProfile {
    name: string;
    group: string;
    matrix4x5: Mat4x5;
    createdAt: number;
    updatedAt: number;
    /** 'repo' = from the auto-downloaded LUTsProfiles package, 'user' = imported by the user */
    source?: 'repo' | 'user';
    origin?: string;
}

export const lutNormGroup = (g: unknown) => (g === undefined || g === null) ? '' : String(g).trim();
export const lutNormName = (n: unknown) => String(n || '').trim();
export const lutMakeKey = (name: string, group: string) => `${lutNormGroup(group)}||${lutNormName(name)}`;
export function lutParseKey(key: string): { group: string; name: string } {
    const k = String(key || '');
    const i = k.indexOf('||');
    if (i < 0) return { group: '', name: k };
    return { group: k.slice(0, i), name: k.slice(i + 2) };
}
export const lutKeyOf = (p: { name: string; group: string }) => lutMakeKey(p.name, p.group);

/** Validates a gvf-lut-profile object. */
export function parseLutProfileObject(obj: unknown, source: 'repo' | 'user' = 'user'): LutProfile | null {
    if (!obj || typeof obj !== 'object') return null;
    const o = obj as Record<string, unknown>;
    if (o.schema !== undefined && o.schema !== 'gvf-lut-profile') return null;
    const name = lutNormName(o.name);
    if (!name || name.length > 200 || !isValidMat4x5(o.matrix4x5)) return null;
    const now = Date.now();
    return {
        name, group: lutNormGroup(o.group).slice(0, 120), matrix4x5: (o.matrix4x5 as number[]).slice(),
        createdAt: Number(o.createdAt) || now, updatedAt: Number(o.updatedAt) || now, source
    };
}

export function lutProfileToJson(p: LutProfile) {
    return { schema: 'gvf-lut-profile', ver: 1, name: p.name, group: p.group, createdAt: p.createdAt, updatedAt: p.updatedAt, matrix4x5: p.matrix4x5 };
}

// ---- affine fit (fitAffineRGBWeighted / invert4x4) ----
function invert4x4(A: number[][]): number[][] {
    const M = A.map(r => r.slice());
    const I = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]];
    for (let col = 0; col < 4; col++) {
        let piv = col, pivVal = Math.abs(M[piv][col]);
        for (let r = col + 1; r < 4; r++) { const v = Math.abs(M[r][col]); if (v > pivVal) { pivVal = v; piv = r; } }
        if (pivVal < 1e-12) throw new Error('Matrix inversion failed (singular).');
        if (piv !== col) { [M[col], M[piv]] = [M[piv], M[col]]; [I[col], I[piv]] = [I[piv], I[col]]; }
        const pivot = M[col][col];
        for (let j = 0; j < 4; j++) { M[col][j] /= pivot; I[col][j] /= pivot; }
        for (let r = 0; r < 4; r++) {
            if (r === col) continue;
            const f = M[r][col];
            for (let j = 0; j < 4; j++) { M[r][j] -= f * M[col][j]; I[r][j] -= f * I[col][j]; }
        }
    }
    return I;
}

export function fitAffineRGB(X: number[][], Y: number[][]): number[][] {
    const XtX = [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]];
    const XtY = [[0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0]];
    for (let i = 0; i < X.length; i++) {
        const x = X[i], y = Y[i];
        for (let a = 0; a < 4; a++) {
            for (let b = 0; b < 4; b++) XtX[a][b] += x[a] * x[b];
            XtY[a][0] += x[a] * y[0]; XtY[a][1] += x[a] * y[1]; XtY[a][2] += x[a] * y[2];
        }
    }
    const inv = invert4x4(XtX);
    const M = [[0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0]];
    for (let i = 0; i < 4; i++) for (let k = 0; k < 3; k++) {
        let sum = 0;
        for (let j = 0; j < 4; j++) sum += inv[i][j] * XtY[j][k];
        M[i][k] = sum;
    }
    return M;
}

export function buildMatrix4x5FromAffine(M: number[][]): Mat4x5 {
    return [M[0][0], M[1][0], M[2][0], 0, M[3][0], M[0][1], M[1][1], M[2][1], 0, M[3][1], M[0][2], M[1][2], M[2][2], 0, M[3][2], 0, 0, 0, 1, 0];
}

const srgbToLinear = (v: number) => (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4));
const linearToSrgb = (v: number) => (v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055);

export function buildSamplesGrid(n: number, linearizeIn: boolean): number[][] {
    const X: number[][] = [];
    for (let bi = 0; bi < n; bi++) for (let gi = 0; gi < n; gi++) for (let ri = 0; ri < n; ri++) {
        let r = ri / (n - 1), g = gi / (n - 1), b = bi / (n - 1);
        if (linearizeIn) { r = srgbToLinear(r); g = srgbToLinear(g); b = srgbToLinear(b); }
        X.push([r, g, b, 1.0]);
    }
    return X;
}

export interface FitOptions { samplesPerAxis?: number; linearizeIn?: boolean; delinearizeOut?: boolean }
const clamp = (n: number, a: number, b: number) => Math.min(b, Math.max(a, n));

// ---- .cube ----
export interface CubeLut { size: number; domainMin: number[]; domainMax: number[]; data: number[][]; title?: string }

export function parseCubeText(text: string): CubeLut {
    const lines = String(text || '').split(/\r?\n/);
    let size = 0, title: string | undefined;
    let domainMin = [0, 0, 0], domainMax = [1, 1, 1];
    const data: number[][] = [];
    for (const raw of lines) {
        const line = raw.trim();
        if (!line || line.startsWith('#')) continue;
        const parts = line.split(/\s+/);
        if (parts[0] === 'TITLE') { title = line.slice(5).trim().replace(/^"|"$/g, ''); continue; }
        if (parts[0] === 'LUT_3D_SIZE') { size = parseInt(parts[1], 10) || 0; continue; }
        if (parts[0] === 'DOMAIN_MIN' && parts.length >= 4) { domainMin = [Number(parts[1]), Number(parts[2]), Number(parts[3])]; continue; }
        if (parts[0] === 'DOMAIN_MAX' && parts.length >= 4) { domainMax = [Number(parts[1]), Number(parts[2]), Number(parts[3])]; continue; }
        if (parts.length >= 3) {
            const r = Number(parts[0]), g = Number(parts[1]), b = Number(parts[2]);
            if (Number.isFinite(r) && Number.isFinite(g) && Number.isFinite(b)) data.push([r, g, b]);
        }
    }
    if (!size || size > 256 || data.length !== size * size * size) throw new Error('Invalid .cube file: missing LUT_3D_SIZE or wrong data length (1D LUTs are not supported).');
    return { size, domainMin, domainMax, data, title };
}

export function cubeSampleTrilinear(lut: CubeLut, r: number, g: number, b: number): number[] {
    const { size, data, domainMin: dmin, domainMax: dmax } = lut;
    const sc = (v: number, mn: number, mx: number) => clamp((v - mn) / (mx - mn), 0, 1);
    const rf = sc(r, dmin[0], dmax[0]) * (size - 1), gf = sc(g, dmin[1], dmax[1]) * (size - 1), bf = sc(b, dmin[2], dmax[2]) * (size - 1);
    const r0 = Math.floor(rf), r1 = Math.min(r0 + 1, size - 1), g0 = Math.floor(gf), g1 = Math.min(g0 + 1, size - 1);
    const b0 = Math.floor(bf), b1 = Math.min(b0 + 1, size - 1);
    const tr = rf - r0, tg = gf - g0, tb = bf - b0;
    const idx = (ri: number, gi: number, bi: number) => ri + size * gi + size * size * bi;
    const lerp = (a: number, c: number, t: number) => a + (c - a) * t;
    const out = [0, 0, 0];
    for (let i = 0; i < 3; i++) {
        const x00 = lerp(data[idx(r0, g0, b0)][i], data[idx(r1, g0, b0)][i], tr);
        const x10 = lerp(data[idx(r0, g1, b0)][i], data[idx(r1, g1, b0)][i], tr);
        const x01 = lerp(data[idx(r0, g0, b1)][i], data[idx(r1, g0, b1)][i], tr);
        const x11 = lerp(data[idx(r0, g1, b1)][i], data[idx(r1, g1, b1)][i], tr);
        out[i] = lerp(lerp(x00, x10, tg), lerp(x01, x11, tg), tb);
    }
    return out;
}

export function cubeTextToMatrix4x5(text: string, opts: FitOptions = {}): { matrix4x5: Mat4x5; size: number; title?: string } {
    const lut = parseCubeText(text);
    const X = buildSamplesGrid(clamp(Number(opts.samplesPerAxis || 11), 5, 25), !!opts.linearizeIn);
    const Y = X.map(x => {
        let out = cubeSampleTrilinear(lut, x[0], x[1], x[2]);
        if (opts.delinearizeOut) out = out.map(linearToSrgb);
        return out;
    });
    return { matrix4x5: buildMatrix4x5FromAffine(fitAffineRGB(X, Y)), size: lut.size, title: lut.title };
}

// ---- tiled PNG LUT (HALD-like tiles, e.g. 512x512 = 64^3 in 8x8 tiles) ----
export function detectTiledLutLayout(w: number, h: number) {
    for (const lutSize of [16, 32, 64, 128]) {
        if (w % lutSize || h % lutSize) continue;
        const tilesX = w / lutSize, tilesY = h / lutSize;
        if (tilesX * tilesY !== lutSize) continue;
        return { lutSize, tilesX, tilesY };
    }
    return null;
}

/** pngFileToMatrix4x5() on already decoded RGBA pixels. */
export function tiledRgbaToMatrix4x5(rgba: Uint8ClampedArray | Uint8Array, w: number, h: number, opts: FitOptions & { flipY?: boolean } = {}): Mat4x5 {
    const layout = detectTiledLutLayout(w, h);
    if (!layout) throw new Error(`Unsupported LUT layout for ${w}x${h}. Expected tiled layout (e.g. 512x512 => LUT_SIZE=64 tiles 8x8).`);
    const { lutSize, tilesX } = layout;
    const px = (x: number, y: number) => {
        if (opts.flipY) y = (h - 1) - y;
        const i = (y * w + x) * 4;
        return [rgba[i] / 255, rgba[i + 1] / 255, rgba[i + 2] / 255];
    };
    const lerp = (a: number, c: number, t: number) => a + (c - a) * t;
    const sample = (r: number, g: number, b: number) => {
        r = clamp(r, 0, 1); g = clamp(g, 0, 1); b = clamp(b, 0, 1);
        const bf = b * (lutSize - 1), b0 = Math.floor(bf), b1 = Math.min(b0 + 1, lutSize - 1), bt = bf - b0;
        const slice = (bs: number) => {
            const tx0 = (bs % tilesX) * lutSize, ty0 = Math.floor(bs / tilesX) * lutSize;
            const xf = r * (lutSize - 1), yf = g * (lutSize - 1);
            const x0 = Math.floor(xf), x1 = Math.min(x0 + 1, lutSize - 1), y0 = Math.floor(yf), y1 = Math.min(y0 + 1, lutSize - 1);
            const tx = xf - x0, ty = yf - y0;
            const c00 = px(tx0 + x0, ty0 + y0), c10 = px(tx0 + x1, ty0 + y0), c01 = px(tx0 + x0, ty0 + y1), c11 = px(tx0 + x1, ty0 + y1);
            return [0, 1, 2].map(i => lerp(lerp(c00[i], c10[i], tx), lerp(c01[i], c11[i], tx), ty));
        };
        const c0 = slice(b0), c1 = slice(b1);
        return [lerp(c0[0], c1[0], bt), lerp(c0[1], c1[1], bt), lerp(c0[2], c1[2], bt)];
    };
    const X = buildSamplesGrid(clamp(Number(opts.samplesPerAxis || 11), 5, 25), !!opts.linearizeIn);
    const Y = X.map(x => {
        let out = sample(x[0], x[1], x[2]);
        if (opts.delinearizeOut) out = out.map(linearToSrgb);
        return out;
    });
    return buildMatrix4x5FromAffine(fitAffineRGB(X, Y));
}
