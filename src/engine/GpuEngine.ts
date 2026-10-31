// WebGPU (WGSL) backend of the UVE effect graph – v1.0.1.
// Vendor-neutral: Chromium's WebGPU implementation (Dawn) runs on D3D12 / Vulkan / Metal, so NVIDIA,
// AMD, Intel and Apple GPUs use the same shaders. Every SVG primitive is a 1:1 port of the v1.0.0
// WebGL2 shader in EffectEngine.ts (same premultiplication rules, rotated convolution kernels,
// per-primitive clamping, W3C turbulence lattice and optional 8-bit quantisation), so both backends
// produce the same image within float tolerance (verified by the parity tests).
import type { CustomFilterEntry } from '../../shared/uve/customFilters';
import {
    type CompositeOptions, type CompositeStep, type EngineSlot, type FrameSource, type LayerRenderOptions, type RenderEngine,
    MAX_SLOTS, compositeSteps, effectiveClipMatrix, graphHasClipPrim, layerSizeOf, STACK_UNIFORMS
} from '../../shared/uve/engine-facade';
import type { Prim, TransferFunc, UveGraph } from '../../shared/uve/graph';
import { matIdentity4x5, type Mat4x5 } from '../../shared/uve/matrix';
import { buildTurbulenceLattice } from './turbulence';
import type { CustomDiagnostic, FrameEngine, ProcessOptions, StageTimes } from './types';

interface GTex { tex: GPUTexture; view: GPUTextureView; w: number; h: number; fmt: GPUTextureFormat }

const VS = /* wgsl */`
struct VOut { @builtin(position) pos: vec4f };
@vertex fn vs(@builtin(vertex_index) i: u32) -> VOut {
    var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    var o: VOut; o.pos = vec4f(p[i], 0.0, 1.0); return o;
}`;

// Shared helpers; every shader has its uniform block U at @group(0) @binding(0).
const COMMON = /* wgsl */`
fn sz() -> vec2i { return vec2i(textureDimensions(tIn)); }
fn fetchC(t: texture_2d<f32>, p: vec2i) -> vec4f { let s = vec2i(textureDimensions(t)); return textureLoad(t, clamp(p, vec2i(0), s - 1), 0); }
fn fetchT(t: texture_2d<f32>, p: vec2i) -> vec4f {
    let s = vec2i(textureDimensions(t));
    if (p.x < 0 || p.y < 0 || p.x >= s.x || p.y >= s.y) { return vec4f(0.0); }
    return textureLoad(t, p, 0);
}
fn premul(c: vec4f) -> vec4f { return vec4f(c.rgb * c.a, c.a); }
fn unpremul(c: vec4f) -> vec4f { if (c.a > 0.0) { return vec4f(clamp(c.rgb / c.a, vec3f(0.0), vec3f(1.0)), c.a); } return vec4f(0.0); }
fn Q(c0: vec4f, quant: f32) -> vec4f { let c = clamp(c0, vec4f(0.0), vec4f(1.0)); if (quant > 0.0) { return floor(c * quant + 0.5) / quant; } return c; }
`;

const shader = (uniforms: string, bindings: string, body: string, out = 'vec4f') => `${VS}
struct U { ${uniforms} };
@group(0) @binding(0) var<uniform> u: U;
${bindings}
${COMMON.replace('fn sz() -> vec2i { return vec2i(textureDimensions(tIn)); }', bindings.includes('tIn') ? 'fn sz() -> vec2i { return vec2i(textureDimensions(tIn)); }' : '')}
@fragment fn fs(@builtin(position) fp: vec4f) -> @location(0) ${out} {
    _ = u; // keeps binding 0 in the auto layout for every pipeline
    let P = vec2i(fp.xy);
    ${body}
}`;

const IN1 = '@group(0) @binding(1) var tIn: texture_2d<f32>;';
const IN2 = '@group(0) @binding(1) var tIn: texture_2d<f32>;\n@group(0) @binding(2) var tIn2: texture_2d<f32>;';

const FS: Record<string, { code: string; target: 'f16' | 'u8' | 'u16' | 'canvas' }> = {
    import: {
        target: 'f16',
        code: shader('quant: f32, w: f32, h: f32, pad: f32', '@group(0) @binding(1) var tSrc: texture_2d<f32>;\n@group(0) @binding(2) var smp: sampler;',
            'let uv = fp.xy / vec2f(u.w, u.h); return Q(textureSampleLevel(tSrc, smp, uv, 0.0), u.quant);')
    },
    import16: {
        target: 'f16',
        code: shader('quant: f32, w: f32, h: f32, pad: f32', '@group(0) @binding(1) var tSrcU: texture_2d<u32>;',
            `let s = vec2i(textureDimensions(tSrcU));
            let uv = fp.xy / vec2f(u.w, u.h);
            let p = clamp(vec2i(uv * vec2f(s)), vec2i(0), s - 1);
            return Q(vec4f(textureLoad(tSrcU, p, 0)) / 65535.0, u.quant);`)
    },
    copy: { target: 'f16', code: shader('quant: f32, a: f32, b: f32, c: f32', IN1, 'return fetchC(tIn, P);') },
    sourceAlpha: { target: 'f16', code: shader('quant: f32, a: f32, b: f32, c: f32', IN1, 'return vec4f(0.0, 0.0, 0.0, fetchC(tIn, P).a);') },
    matrix: {
        target: 'f16',
        code: shader('m: mat4x4f, off: vec4f, quant: f32, a: f32, b: f32, c: f32', IN1, 'let c = fetchC(tIn, P); return Q(u.m * c + u.off, u.quant);')
    },
    ct: {
        target: 'f16',
        code: shader('types: vec4i, lens: vec4i, slope: vec4f, inter: vec4f, amp: vec4f, ex: vec4f, goff: vec4f, quant: f32, width: f32, a: f32, b: f32',
            IN1 + '\n@group(0) @binding(2) var<storage, read> tbl: array<f32>;',
            `let c = fetchC(tIn, P);
            return Q(vec4f(fn_ct(0, c.r), fn_ct(1, c.g), fn_ct(2, c.b), fn_ct(3, c.a)), u.quant);`).replace('@fragment', `
fn tv(ch: i32, k: i32) -> f32 { return tbl[ch * i32(u.width) + k]; }
fn fn_ct(ch: i32, C: f32) -> f32 {
    let t = u.types[ch];
    if (t == 0) { return C; }
    if (t == 1) {
        let n = u.lens[ch];
        if (n < 1) { return C; }
        if (n == 1) { return tv(ch, 0); }
        let x = clamp(C, 0.0, 1.0) * f32(n - 1);
        let k = i32(floor(x));
        if (k >= n - 1) { return tv(ch, n - 1); }
        let v0 = tv(ch, k); let v1 = tv(ch, k + 1);
        return v0 + (x - f32(k)) * (v1 - v0);
    }
    if (t == 2) {
        let n = u.lens[ch];
        if (n < 1) { return C; }
        let k = min(i32(floor(clamp(C, 0.0, 1.0) * f32(n))), n - 1);
        return tv(ch, k);
    }
    if (t == 3) { return u.slope[ch] * C + u.inter[ch]; }
    return u.amp[ch] * pow(max(C, 0.0), u.ex[ch]) + u.goff[ch];
}
@fragment`)
    },
    blur: {
        target: 'f16',
        code: shader('dir: vec2i, radius: i32, pad: i32, sigma: f32, quant: f32, a: f32, b: f32', IN1,
            `if (u.radius <= 0) { return fetchC(tIn, P); }
            var acc = vec4f(0.0); var wsum = 0.0;
            let k = -0.5 / (u.sigma * u.sigma);
            for (var i = -u.radius; i <= u.radius; i++) {
                let w = exp(f32(i * i) * k);
                acc += premul(fetchC(tIn, P + u.dir * i)) * w;
                wsum += w;
            }
            return Q(unpremul(acc / wsum), u.quant);`)
    },
    arith: {
        target: 'f16',
        code: shader('k: vec4f, quant: f32, a: f32, b: f32, c: f32', IN2,
            `let i1 = premul(fetchC(tIn, P)); let i2 = premul(fetchC(tIn2, P));
            var r = clamp(u.k.x * i1 * i2 + u.k.y * i1 + u.k.z * i2 + vec4f(u.k.w), vec4f(0.0), vec4f(1.0));
            r = vec4f(min(r.rgb, vec3f(r.a)), r.a);
            return Q(unpremul(r), u.quant);`)
    },
    composite: {
        target: 'f16',
        code: shader('op: i32, a: i32, b: i32, c: i32, quant: f32, d: f32, e: f32, f: f32', IN2,
            `let a = premul(fetchC(tIn, P)); let b = premul(fetchC(tIn2, P));
            var r: vec4f;
            if (u.op == 0) { r = a + b * (1.0 - a.a); }
            else if (u.op == 1) { r = a * b.a; }
            else if (u.op == 2) { r = a * (1.0 - b.a); }
            else if (u.op == 3) { r = a * b.a + b * (1.0 - a.a); }
            else if (u.op == 4) { r = a * (1.0 - b.a) + b * (1.0 - a.a); }
            else { r = a + b; }
            return Q(unpremul(clamp(r, vec4f(0.0), vec4f(1.0))), u.quant);`)
    },
    blend: {
        target: 'f16',
        code: shader('mode: i32, a: i32, b: i32, c: i32, quant: f32, d: f32, e: f32, f: f32', IN2,
            `let s = fetchC(tIn, P); let b = fetchC(tIn2, P);
            let ra = s.a + b.a - s.a * b.a;
            let rc = (1.0 - b.a) * s.rgb * s.a + (1.0 - s.a) * b.rgb * b.a + s.a * b.a * clamp(B(b.rgb, s.rgb), vec3f(0.0), vec3f(1.0));
            return Q(unpremul(vec4f(clamp(rc, vec3f(0.0), vec3f(1.0)), clamp(ra, 0.0, 1.0))), u.quant);`).replace('@fragment', `
fn lum(c: vec3f) -> f32 { return dot(c, vec3f(0.3, 0.59, 0.11)); }
fn clipColor(c0: vec3f) -> vec3f {
    var c = c0; let l = lum(c); let n = min(min(c.r, c.g), c.b); let x = max(max(c.r, c.g), c.b);
    if (n < 0.0) { c = l + (c - l) * l / max(l - n, 1e-6); }
    if (x > 1.0) { c = l + (c - l) * (1.0 - l) / max(x - l, 1e-6); }
    return c;
}
fn setLum(c: vec3f, l: f32) -> vec3f { return clipColor(c + (l - lum(c))); }
fn satf(c: vec3f) -> f32 { return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b); }
fn setSat(c: vec3f, s: f32) -> vec3f {
    let mx = max(max(c.r, c.g), c.b); let mn = min(min(c.r, c.g), c.b);
    if (mx > mn) { return (c - mn) * s / (mx - mn); } return vec3f(0.0);
}
fn softL(b: f32, s: f32) -> f32 {
    if (s <= 0.5) { return b - (1.0 - 2.0 * s) * b * (1.0 - b); }
    var d: f32; if (b <= 0.25) { d = ((16.0 * b - 12.0) * b + 4.0) * b; } else { d = sqrt(b); }
    return b + (2.0 * s - 1.0) * (d - b);
}
fn hardL(b: f32, s: f32) -> f32 { if (s <= 0.5) { return b * 2.0 * s; } return 1.0 - (1.0 - b) * (1.0 - (2.0 * s - 1.0)); }
fn dodge(cb: f32, cs: f32) -> f32 { if (cb == 0.0) { return 0.0; } if (cs >= 1.0) { return 1.0; } return min(1.0, cb / (1.0 - cs)); }
fn burn(cb: f32, cs: f32) -> f32 { if (cb >= 1.0) { return 1.0; } if (cs <= 0.0) { return 0.0; } return 1.0 - min(1.0, (1.0 - cb) / cs); }
fn B(cb: vec3f, cs: vec3f) -> vec3f {
    let m = u.mode;
    if (m == 1) { return cb * cs; }
    if (m == 2) { return cb + cs - cb * cs; }
    if (m == 3) { return vec3f(hardL(cs.r, cb.r), hardL(cs.g, cb.g), hardL(cs.b, cb.b)); }
    if (m == 4) { return min(cb, cs); }
    if (m == 5) { return max(cb, cs); }
    if (m == 6) { return vec3f(dodge(cb.r, cs.r), dodge(cb.g, cs.g), dodge(cb.b, cs.b)); }
    if (m == 7) { return vec3f(burn(cb.r, cs.r), burn(cb.g, cs.g), burn(cb.b, cs.b)); }
    if (m == 8) { return vec3f(hardL(cb.r, cs.r), hardL(cb.g, cs.g), hardL(cb.b, cs.b)); }
    if (m == 9) { return vec3f(softL(cb.r, cs.r), softL(cb.g, cs.g), softL(cb.b, cs.b)); }
    if (m == 10) { return abs(cb - cs); }
    if (m == 11) { return cb + cs - 2.0 * cb * cs; }
    if (m == 12) { return setLum(setSat(cs, satf(cb)), lum(cb)); }
    if (m == 13) { return setLum(setSat(cb, satf(cs)), lum(cb)); }
    if (m == 14) { return setLum(cs, lum(cb)); }
    if (m == 15) { return setLum(cb, lum(cs)); }
    return cs;
}
@fragment`)
    },
    convolve: {
        target: 'f16',
        code: shader('order: vec2i, tgt: vec2i, edge: i32, preserve: i32, pad0: i32, pad1: i32, divisor: f32, bias: f32, quant: f32, pad2: f32, kernel: array<vec4f, 21>', IN1,
            `var acc = vec4f(0.0);
            for (var i = 0; i < 9; i++) {
                if (i >= u.order.y) { break; }
                for (var j = 0; j < 9; j++) {
                    if (j >= u.order.x) { break; }
                    let c = sampleE(P + vec2i(j - u.tgt.x, i - u.tgt.y));
                    let ki = (u.order.y - i - 1) * u.order.x + (u.order.x - j - 1);
                    let k = u.kernel[ki / 4][ki % 4];
                    if (u.preserve == 1) { acc += c * k; } else { acc += premul(c) * k; }
                }
            }
            let src = fetchC(tIn, P);
            if (u.preserve == 1) { return Q(vec4f(clamp(acc.rgb / u.divisor + u.bias, vec3f(0.0), vec3f(1.0)), src.a), u.quant); }
            var r = clamp(acc / u.divisor + vec4f(u.bias), vec4f(0.0), vec4f(1.0));
            r = vec4f(min(r.rgb, vec3f(r.a)), r.a);
            return Q(unpremul(r), u.quant);`).replace('@fragment', `
fn sampleE(q: vec2i) -> vec4f {
    let s = vec2i(textureDimensions(tIn));
    if (u.edge == 0) { return fetchC(tIn, q); }
    if (u.edge == 1) { return textureLoad(tIn, vec2i(((q.x % s.x) + s.x) % s.x, ((q.y % s.y) + s.y) % s.y), 0); }
    return fetchT(tIn, q);
}
@fragment`)
    },
    turbulence: {
        target: 'f16',
        code: shader('freq: vec2f, octaves: i32, fractal: i32, pxScale: f32, quant: f32, a: f32, b: f32',
            '@group(0) @binding(1) var<storage, read> lat: array<vec4f>;',
            `let pt = vec2f(P) / u.pxScale;
            return Q(vec4f(turb(0, pt), turb(1, pt), turb(2, pt), turb(3, pt)), u.quant);`).replace('@fragment', `
const NL: i32 = 514;
fn latv(i: i32) -> f32 { return lat[i].x; }
fn grd(ch: i32, i: i32) -> vec2f { return lat[(ch + 1) * NL + i].xy; }
fn scurve(t: f32) -> f32 { return t * t * (3.0 - 2.0 * t); }
fn noise2(ch: i32, v: vec2f) -> f32 {
    var t = v.x + 4096.0;
    let bx0 = i32(t) & 255; let bx1 = (bx0 + 1) & 255;
    let rx0 = t - floor(t); let rx1 = rx0 - 1.0;
    t = v.y + 4096.0;
    let by0 = i32(t) & 255; let by1 = (by0 + 1) & 255;
    let ry0 = t - floor(t); let ry1 = ry0 - 1.0;
    let i = i32(latv(bx0)); let j = i32(latv(bx1));
    let b00 = i32(latv(i + by0)); let b10 = i32(latv(j + by0));
    let b01 = i32(latv(i + by1)); let b11 = i32(latv(j + by1));
    let sx = scurve(rx0); let sy = scurve(ry0);
    var q = grd(ch, b00); var uu = rx0 * q.x + ry0 * q.y;
    q = grd(ch, b10); var w = rx1 * q.x + ry0 * q.y;
    let a = mix(uu, w, sx);
    q = grd(ch, b01); uu = rx0 * q.x + ry1 * q.y;
    q = grd(ch, b11); w = rx1 * q.x + ry1 * q.y;
    let b = mix(uu, w, sx);
    return mix(a, b, sy);
}
fn turb(ch: i32, pt: vec2f) -> f32 {
    var sum = 0.0; var ratio = 1.0; var v = pt * u.freq;
    for (var k = 0; k < 8; k++) {
        if (k >= u.octaves) { break; }
        let n = noise2(ch, v);
        if (u.fractal == 1) { sum += n / ratio; } else { sum += abs(n) / ratio; }
        v *= 2.0; ratio *= 2.0;
    }
    if (u.fractal == 1) { return (sum + 1.0) * 0.5; }
    return sum;
}
@fragment`)
    },
    flood: { target: 'f16', code: shader('color: vec4f, quant: f32, a: f32, b: f32, c: f32', '', 'return Q(u.color, u.quant);') },
    offset: { target: 'f16', code: shader('d: vec2f, quant: f32, a: f32', IN1, 'return fetchT(tIn, P - vec2i(floor(u.d + 0.5)));') },
    morphology: {
        target: 'f16',
        code: shader('r: vec2i, erode: i32, pad: i32, quant: f32, a: f32, b: f32, c: f32', IN1,
            `var r = vec4f(0.0); if (u.erode == 1) { r = vec4f(1.0); }
            for (var y = -16; y <= 16; y++) {
                if (y < -u.r.y || y > u.r.y) { continue; }
                for (var x = -16; x <= 16; x++) {
                    if (x < -u.r.x || x > u.r.x) { continue; }
                    let c = premul(fetchT(tIn, P + vec2i(x, y)));
                    if (u.erode == 1) { r = min(r, c); } else { r = max(r, c); }
                }
            }
            return Q(unpremul(r), u.quant);`)
    },
    displacement: {
        target: 'f16',
        code: shader('ch: vec2i, scale: f32, quant: f32', IN2,
            `let m = fetchC(tIn2, P);
            let d = u.scale * (vec2f(m[u.ch.x], m[u.ch.y]) - 0.5);
            return fetchT(tIn, P + vec2i(floor(d + 0.5)));`)
    },
    out8: { target: 'u8', code: shader('a: f32, b: f32, c: f32, d: f32', IN1, 'return vec4f(clamp(fetchC(tIn, P).rgb, vec3f(0.0), vec3f(1.0)), 1.0);') },
    out16: {
        target: 'u16',
        code: shader('a: f32, b: f32, c: f32, d: f32', IN1, 'return vec4u(floor(clamp(fetchC(tIn, P), vec4f(0.0), vec4f(1.0)) * 65535.0 + 0.5));', 'vec4u')
    },
    scope: {
        target: 'u8',
        code: shader('w: f32, h: f32, a: f32, b: f32', '@group(0) @binding(1) var tSrc: texture_2d<f32>;\n@group(0) @binding(2) var smp: sampler;',
            'return vec4f(textureSampleLevel(tSrc, smp, fp.xy / vec2f(u.w, u.h), 0.0).rgb, 1.0);')
    },
    present: {
        target: 'canvas',
        code: shader('split: f32, mode: i32, vw: f32, vh: f32',
            '@group(0) @binding(1) var tOrig: texture_2d<f32>;\n@group(0) @binding(2) var tProc: texture_2d<f32>;\n@group(0) @binding(3) var smp: sampler;',
            `let uv = fp.xy / vec2f(u.vw, u.vh);
            var c: vec4f;
            let o = textureSampleLevel(tOrig, smp, uv, 0.0);
            let p = textureSampleLevel(tProc, smp, uv, 0.0);
            if (u.mode == 0) { c = p; } else if (u.mode == 1) { c = o; } else { c = select(p, o, uv.x < u.split); }
            if (u.mode == 2 && abs(uv.x - u.split) * u.vw < 1.0) { c = vec4f(1.0); }
            return vec4f(c.rgb, 1.0);`)
    }
};

// ---------------------------------------------------------------- timeline (NLE) shaders
// Field names of these uniform blocks are the shared parameter names of src/engine/params.ts; the
// WebGL2 twins in EffectEngine.ts (GLSL_TIMELINE_SOURCES) declare the same names as `u<Name>` uniforms.
const TL: Record<string, { code: string; target: 'f16' }> = {
    // slot source -> per-channel 1D LUT -> trilinear 3D LUT (texel x = r, y = g, z = b, like .cube data)
    tlLut: {
        target: 'f16',
        code: shader('quant: f32, hasLut: i32, lutSize: i32, hasLut3d: i32, lut3dSize: i32, pad0: f32, pad1: f32, pad2: f32',
            IN1 + '\n@group(0) @binding(2) var tLut: texture_2d<f32>;\n@group(0) @binding(3) var tLut3: texture_3d<f32>;',
            `let c = fetchC(tIn, P);
            var rgb = c.rgb;
            if (u.hasLut != 0) { rgb = vec3f(lut1(0, rgb.r), lut1(1, rgb.g), lut1(2, rgb.b)); }
            if (u.hasLut3d != 0) { rgb = lut3(rgb); }
            return Q(vec4f(rgb, c.a), u.quant);`).replace('@fragment', `
fn lut1(ch: i32, x0: f32) -> f32 {
    let n = u.lutSize;
    let x = clamp(x0, 0.0, 1.0) * f32(n - 1);
    let i0 = min(i32(floor(x)), n - 1);
    let i1 = min(i0 + 1, n - 1);
    let a = textureLoad(tLut, vec2i(i0, 0), 0)[ch];
    let b = textureLoad(tLut, vec2i(i1, 0), 0)[ch];
    return a + (b - a) * (x - f32(i0));
}
fn l3(x: i32, y: i32, z: i32) -> vec3f { return textureLoad(tLut3, vec3i(x, y, z), 0).rgb; }
fn lut3(c: vec3f) -> vec3f {
    let n = u.lut3dSize;
    let f = clamp(c, vec3f(0.0), vec3f(1.0)) * f32(n - 1);
    let i0 = min(vec3i(floor(f)), vec3i(n - 1));
    let i1 = min(i0 + vec3i(1), vec3i(n - 1));
    let t = f - vec3f(i0);
    let x00 = mix(l3(i0.x, i0.y, i0.z), l3(i1.x, i0.y, i0.z), t.x);
    let x10 = mix(l3(i0.x, i1.y, i0.z), l3(i1.x, i1.y, i0.z), t.x);
    let x01 = mix(l3(i0.x, i0.y, i1.z), l3(i1.x, i0.y, i1.z), t.x);
    let x11 = mix(l3(i0.x, i1.y, i1.z), l3(i1.x, i1.y, i1.z), t.x);
    return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}
@fragment`)
    },
    // clip look matrix (clipLookMatrix: RGB gain, alpha = fade ramps), premultiply, layer veil fill
    tlLook: {
        target: 'f16',
        code: shader('clipMatrix: mat4x4f, clipMatrixOff: vec4f, quant: f32, layerVeil: f32, layerVeilColor: f32, pad0: f32', IN1,
            `var c = u.clipMatrix * fetchC(tIn, P) + u.clipMatrixOff;
            if (u.quant > 0.0) { c = floor(clamp(c, vec4f(0.0), vec4f(1.0)) * u.quant + 0.5) / u.quant; }
            let a = clamp(c.a, 0.0, 1.0);
            return mix(vec4f(c.rgb * a, a), vec4f(vec3f(u.layerVeilColor), 1.0), u.layerVeil);`)
    },
    // one composite step: the contract expression of .verify/nle-contract.md, additive premultiplied
    tlStep: {
        target: 'f16',
        code: shader('viewSize: vec2f, cx: f32, cy: f32, mode: i32, axis: i32, highSideIsB: i32, useAcc: i32, edge: f32, feather: f32, weightA: f32, weightB: f32, dxA: f32, dyA: f32, dxB: f32, dyB: f32',
            '@group(0) @binding(1) var tAcc: texture_2d<f32>;\n@group(0) @binding(2) var tA: texture_2d<f32>;\n@group(0) @binding(3) var tB: texture_2d<f32>;\n@group(0) @binding(4) var smp: sampler;',
            `let L = max(u.viewSize.x, u.viewSize.y);
            let uv = fp.xy / L;
            var cA = u.weightA;
            var cB = u.weightB;
            if (u.mode != 0) {
                var s = distance(uv, vec2f(u.cx, u.cy));
                if (u.mode == 1) { s = select(uv.y, uv.x, u.axis == 0); }
                let g = smoothstep(u.edge - u.feather, u.edge + u.feather, s);
                var geoB = 1.0 - g;
                if (u.highSideIsB != 0) { geoB = g; }
                cA = u.weightA * (1.0 - geoB);
                cB = u.weightB * geoB;
            }
            let pc = cA * layerAt(tA, fp.xy + vec2f(u.dxA, u.dyA)) + cB * layerAt(tB, fp.xy + vec2f(u.dxB, u.dyB));
            if (u.useAcc != 0) { return textureLoad(tAcc, P, 0) * (1.0 - pc.a) + pc; }
            return pc;`).replace('@fragment', `
fn layerAt(t: texture_2d<f32>, q: vec2f) -> vec4f {
    let tc = q / u.viewSize;
    if (tc.x < 0.0 || tc.y < 0.0 || tc.x > 1.0 || tc.y > 1.0) { return vec4f(0.0); }
    return textureSampleLevel(t, smp, tc, 0.0);
}
@fragment`)
    },
    // composite veil (applied once to the whole stack), alpha dropped: premultiplied stack over black
    tlOut: {
        target: 'f16',
        code: shader('veil: f32, veilColor: f32, pad0: f32, pad1: f32', IN1,
            'let c = fetchC(tIn, P); return vec4f(mix(c.rgb, vec3f(u.veilColor), u.veil), 1.0);')
    }
};
for (const [name, def] of Object.entries(TL)) FS[name] = def;

/** WGSL sources of the timeline passes (consistency tests read their uniform blocks). */
export const WGSL_TIMELINE_SOURCES: Readonly<Record<string, string>> = Object.freeze(
    Object.fromEntries(Object.entries(TL).map(([name, def]) => [name, def.code])));

const IDENTITY: Mat4x5 = matIdentity4x5();

/** Row-major 4x5 -> column-major mat4x4f at f[at..at+15] and the offset column at f[at+16..at+19]. */
function fillMat4x5(f: Float32Array, m: Mat4x5, at: number) {
    f[at] = m[0]; f[at + 1] = m[5]; f[at + 2] = m[10]; f[at + 3] = m[15];
    f[at + 4] = m[1]; f[at + 5] = m[6]; f[at + 6] = m[11]; f[at + 7] = m[16];
    f[at + 8] = m[2]; f[at + 9] = m[7]; f[at + 10] = m[12]; f[at + 11] = m[17];
    f[at + 12] = m[3]; f[at + 13] = m[8]; f[at + 14] = m[13]; f[at + 15] = m[18];
    f[at + 16] = m[4]; f[at + 17] = m[9]; f[at + 18] = m[14]; f[at + 19] = m[19];
}

interface GSlot { src8: GTex | null; src16: GTex | null; is16: boolean; layer: GTex | null }
interface GLut { tex: GPUTexture; view: GPUTextureView; size: number; dim: '1d' | '3d' }

const BLEND_IDS: Record<string, number> = {
    normal: 0, multiply: 1, screen: 2, overlay: 3, darken: 4, lighten: 5, 'color-dodge': 6, 'color-burn': 7, 'hard-light': 8,
    'soft-light': 9, difference: 10, exclusion: 11, hue: 12, saturation: 13, color: 14, luminosity: 15
};
const COMPOSITE_IDS: Record<string, number> = { over: 0, in: 1, out: 2, atop: 3, xor: 4, lighter: 5 };
const FUNC_IDS: Record<TransferFunc['type'], number> = { identity: 0, table: 1, discrete: 2, linear: 3, gamma: 4 };
const UNIFORM_SLOT = 512; // bytes per draw in the uniform ring (largest block: convolve = 16*4 + 21*16 = 400)
const RING_SLOTS = 1024;

/** Writes typed uniform values into a 512-byte slot. */
class UBuilder {
    readonly buf = new ArrayBuffer(UNIFORM_SLOT);
    readonly f = new Float32Array(this.buf);
    readonly i = new Int32Array(this.buf);
}

export interface GpuEngineInit { device: GPUDevice; adapterInfo: string; canvas?: HTMLCanvasElement | OffscreenCanvas | null }

export async function requestGpu(): Promise<{ device: GPUDevice; adapterInfo: string } | null> {
    const gpu = (navigator as Navigator & { gpu?: GPU }).gpu;
    if (!gpu) return null;
    const adapter = await gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) return null;
    const info = (adapter as GPUAdapter & { info?: GPUAdapterInfo }).info;
    const adapterInfo = info ? [info.vendor, info.architecture, info.device, info.description].filter(Boolean).join(' · ') : 'WebGPU adapter';
    const device = await adapter.requestDevice({ requiredLimits: { maxStorageBufferBindingSize: Math.min(adapter.limits.maxStorageBufferBindingSize, 256 * 1024 * 1024) } });
    return { device, adapterInfo: `WebGPU · ${adapterInfo || 'adapter'}` };
}

export class GpuEngine implements FrameEngine {
    readonly kind = 'webgpu' as const;
    readonly adapter: string;
    readonly times: StageTimes = { upload: 0, process: 0, readback: 0, frames: 0 };
    readonly device: GPUDevice;
    lost = false;
    private ctx: GPUCanvasContext | null = null;
    private canvasFormat: GPUTextureFormat = 'bgra8unorm';
    private pipelines = new Map<string, GPURenderPipeline>();
    private linear: GPUSampler;
    private ring: GPUBuffer;
    private ringPos = 0;
    private pool: GTex[] = [];
    private src8: GTex | null = null;
    private src16: GTex | null = null;
    private srcIs16 = false;
    private ctBuffers = new Map<Prim, { buf: GPUBuffer; width: number }>();
    private lattices = new Map<number, GPUBuffer>();
    private graph: UveGraph = { prims: [], output: 'SourceGraphic', hasAuto: false, warnings: [] };
    private encoder: GPUCommandEncoder | null = null;
    private ub = new UBuilder();
    lastSource: GTex | null = null;
    lastOutput: GTex | null = null;
    // timeline (NLE) state: layer slots, LUTs, composite output
    private tlOn = false;
    private tlMode: EngineSlot = 'export';
    private slots: GSlot[] = [];
    private tlLast = -1;
    private tlOutput: GTex | null = null;
    private luts = new Map<number, GLut>();
    private lutSeq = 1;
    private dummy: GTex | null = null;
    private dummyLut: GLut | null = null;
    private dummyLut3: GLut | null = null;
    private tlClip: Mat4x5 = matIdentity4x5();
    private renderSurface: RenderEngine | null = null;

    constructor(init: GpuEngineInit) {
        this.device = init.device;
        this.adapter = init.adapterInfo;
        void this.device.lost.then(() => { this.lost = true; });
        this.linear = this.device.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge' });
        this.ring = this.device.createBuffer({ size: UNIFORM_SLOT * RING_SLOTS, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        if (init.canvas) {
            this.ctx = init.canvas.getContext('webgpu') as GPUCanvasContext | null;
            if (!this.ctx) throw new Error('WebGPU canvas context unavailable');
            this.canvasFormat = navigator.gpu.getPreferredCanvasFormat();
            this.ctx.configure({ device: this.device, format: this.canvasFormat, alphaMode: 'opaque' });
        }
        // compile every pipeline up front: shader errors surface immediately, no per-frame compilation
        for (const name of Object.keys(FS)) this.pipeline(name);
    }

    // ------------------------------------------------------------------ pipelines / resources
    private pipeline(name: string): GPURenderPipeline {
        let p = this.pipelines.get(name);
        if (p) return p;
        const def = FS[name];
        const module = this.device.createShaderModule({ code: def.code, label: name });
        const format: GPUTextureFormat = def.target === 'f16' ? 'rgba16float' : def.target === 'u8' ? 'rgba8unorm' : def.target === 'u16' ? 'rgba16uint' : this.canvasFormat;
        p = this.device.createRenderPipeline({
            label: name, layout: 'auto',
            vertex: { module, entryPoint: 'vs' },
            fragment: { module, entryPoint: 'fs', targets: [{ format }] },
            primitive: { topology: 'triangle-list' }
        });
        this.pipelines.set(name, p);
        return p;
    }

    private createTex(w: number, h: number, fmt: GPUTextureFormat, extraUsage = 0): GTex {
        const tex = this.device.createTexture({
            size: { width: w, height: h }, format: fmt,
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST | extraUsage
        });
        return { tex, view: tex.createView(), w, h, fmt };
    }

    private acquire(w: number, h: number, fmt: GPUTextureFormat = 'rgba16float'): GTex {
        const i = this.pool.findIndex(t => t.w === w && t.h === h && t.fmt === fmt);
        if (i >= 0) return this.pool.splice(i, 1)[0];
        return this.createTex(w, h, fmt);
    }

    private release(t: GTex | null | undefined) {
        if (!t) return;
        this.pool.push(t);
        if (this.pool.length > 48) this.pool.shift()!.tex.destroy();
    }

    private enc(): GPUCommandEncoder {
        if (!this.encoder) this.encoder = this.device.createCommandEncoder();
        return this.encoder;
    }

    private submit() {
        if (this.encoder) { this.device.queue.submit([this.encoder.finish()]); this.encoder = null; }
        this.ringPos = 0;
    }

    /** One fullscreen draw. `fill` writes the uniform slot; `textures` are bound from binding 1 on. */
    private draw(name: string, target: GTex | GPUTextureView, fill: (u: UBuilder) => void, resources: (GTex | GPUBuffer | GPUSampler | GPUTextureView)[]) {
        if (this.ringPos >= RING_SLOTS) this.submit();
        const ub = this.ub;
        ub.f.fill(0);
        fill(ub);
        const offset = this.ringPos * UNIFORM_SLOT;
        this.device.queue.writeBuffer(this.ring, offset, ub.buf);
        this.ringPos++;
        const pipe = this.pipeline(name);
        const entries: GPUBindGroupEntry[] = [{ binding: 0, resource: { buffer: this.ring, offset, size: UNIFORM_SLOT } }];
        resources.forEach((r, i) => {
            let resource: GPUBindingResource;
            if (r instanceof GPUBuffer) resource = { buffer: r };
            else if (r instanceof GPUSampler || r instanceof GPUTextureView) resource = r;
            else resource = (r as GTex).view;
            entries.push({ binding: i + 1, resource });
        });
        const bind = this.device.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries });
        const view = target instanceof GPUTextureView ? target : (target as GTex).view;
        const pass = this.enc().beginRenderPass({ colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }] });
        pass.setPipeline(pipe);
        pass.setBindGroup(0, bind);
        pass.draw(3);
        pass.end();
    }

    // ------------------------------------------------------------------ sources
    uploadVideo(video: HTMLVideoElement) {
        const t = performance.now();
        const w = video.videoWidth, h = video.videoHeight;
        if (!this.src8 || this.src8.w !== w || this.src8.h !== h) { this.src8?.tex.destroy(); this.src8 = this.createTex(w, h, 'rgba8unorm'); }
        this.device.queue.copyExternalImageToTexture({ source: video }, { texture: this.src8.tex }, { width: w, height: h });
        this.srcIs16 = false;
        this.times.upload += performance.now() - t;
    }

    uploadRGBA8(data: Uint8Array, w: number, h: number) {
        const t = performance.now();
        if (!this.src8 || this.src8.w !== w || this.src8.h !== h) { this.src8?.tex.destroy(); this.src8 = this.createTex(w, h, 'rgba8unorm'); }
        this.device.queue.writeTexture({ texture: this.src8.tex }, data as Uint8Array<ArrayBuffer>, { bytesPerRow: w * 4, rowsPerImage: h }, { width: w, height: h });
        this.srcIs16 = false;
        this.times.upload += performance.now() - t;
    }

    uploadRGBA16(data: Uint16Array, w: number, h: number) {
        const t = performance.now();
        if (!this.src16 || this.src16.w !== w || this.src16.h !== h) { this.src16?.tex.destroy(); this.src16 = this.createTex(w, h, 'rgba16uint'); }
        this.device.queue.writeTexture({ texture: this.src16.tex }, data as Uint16Array<ArrayBuffer>, { bytesPerRow: w * 8, rowsPerImage: h }, { width: w, height: h });
        this.srcIs16 = true;
        this.times.upload += performance.now() - t;
    }

    setGraph(graph: UveGraph) {
        for (const b of this.ctBuffers.values()) b.buf.destroy();
        this.ctBuffers.clear();
        this.graph = graph;
    }

    setCustomFilters(entries: CustomFilterEntry[]): CustomDiagnostic[] {
        // Custom GLSL runs on the WebGL2 engine; the engine selector switches backends when such filters are enabled.
        return entries.filter(e => e.type === 'webgl').map(entry => ({ entry, error: entry.enabled ? 'Custom GLSL filters run on the WebGL2 engine (selected automatically while they are enabled).' : null }));
    }

    updateCustomUniforms() { /* no GLSL passes on this backend */ }

    private ctBuffer(p: Prim & { op: 'ct' }) {
        let b = this.ctBuffers.get(p);
        if (b) return b;
        const width = Math.max(1, ...p.f.map(f => f.table?.length ?? 0));
        const data = new Float32Array(width * 4);
        p.f.forEach((f, ch) => (f.table || []).forEach((v, i) => { data[ch * width + i] = v; }));
        const buf = this.device.createBuffer({ size: data.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        this.device.queue.writeBuffer(buf, 0, data);
        b = { buf, width };
        this.ctBuffers.set(p, b);
        return b;
    }

    private lattice(seed: number) {
        let b = this.lattices.get(seed);
        if (b) return b;
        const l = buildTurbulenceLattice(seed);
        b = this.device.createBuffer({ size: l.data.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        this.device.queue.writeBuffer(b, 0, l.data);
        this.lattices.set(seed, b);
        return b;
    }

    // ------------------------------------------------------------------ processing
    process(outW: number, outH: number, opts: ProcessOptions) {
        const t0 = performance.now();
        const quant = opts.quantize8 ? 255 : 0;
        const sc = opts.pxScale > 0 ? opts.pxScale : 1;
        this.release(this.lastSource);
        this.release(this.lastOutput);
        this.lastSource = null;
        this.lastOutput = null;

        const src = this.acquire(outW, outH);
        this.importInto(src, this.srcIs16, this.src8, this.src16, outW, outH, quant);
        this.lastSource = src;
        this.lastOutput = this.runGraph(src, this.graph, outW, outH, quant, sc, opts);
        this.submit();
        this.times.process += performance.now() - t0;
        this.times.frames++;
    }

    private importInto(dst: GTex, is16: boolean, src8: GTex | null, src16: GTex | null, outW: number, outH: number, quant: number) {
        if (is16) this.draw('import16', dst, u => { u.f[0] = quant; u.f[1] = outW; u.f[2] = outH; }, [src16!]);
        else this.draw('import', dst, u => { u.f[0] = quant; u.f[1] = outW; u.f[2] = outH; }, [src8!, this.linear]);
    }

    /** Runs `graph` on `src`; returns a new texture (never `src` itself), `src` stays owned by the caller. */
    private runGraph(src: GTex, graph: UveGraph, outW: number, outH: number, quant: number, sc: number, opts: ProcessOptions): GTex {
        const prims = graph.prims;
        const results = new Map<string, GTex>();
        const lastUse = new Map<string, number>();
        prims.forEach((p, i) => {
            const ins: string[] = [];
            if ('in' in p) ins.push(p.in);
            if ('in2' in p) ins.push(p.in2);
            if (p.op === 'merge') ins.push(...p.ins);
            ins.forEach(n => lastUse.set(n, i));
        });
        lastUse.set(graph.output, Number.MAX_SAFE_INTEGER);
        let prev = 'SourceGraphic';
        let sourceAlpha: GTex | null = null;
        const get = (name: string): GTex => {
            if (name === 'SourceGraphic') return src;
            if (name === 'SourceAlpha') {
                if (!sourceAlpha) {
                    sourceAlpha = this.acquire(outW, outH);
                    this.draw('sourceAlpha', sourceAlpha, u => { u.f[0] = quant; }, [src]);
                }
                return sourceAlpha;
            }
            return results.get(name) ?? (prev === 'SourceGraphic' ? src : results.get(prev) ?? src);
        };
        prims.forEach((p, i) => {
            const out = this.acquire(outW, outH);
            this.runPrim(p, out, get, quant, sc, opts);
            const old = results.get(p.out);
            results.set(p.out, out);
            if (old) this.release(old);
            prev = p.out;
            for (const [name, t] of results) {
                if (name !== p.out && (lastUse.get(name) ?? -1) <= i && name !== graph.output) { this.release(t); results.delete(name); }
            }
        });
        if (sourceAlpha) this.release(sourceAlpha);
        let cur: GTex = graph.output === 'SourceGraphic' ? src : (results.get(graph.output) ?? src);
        for (const [name, t] of results) if (t !== cur) { this.release(t); results.delete(name); }
        if (cur === src) {
            const cp = this.acquire(outW, outH);
            this.draw('copy', cp, () => undefined, [src]);
            cur = cp;
        }
        return cur;
    }

    private runPrim(p: Prim, out: GTex, get: (n: string) => GTex, quant: number, sc: number, opts: ProcessOptions) {
        switch (p.op) {
            case 'matrix': {
                const m = p.auto ? (opts.autoMatrix ?? [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0]) : p.clip ? (opts.clipMatrix ?? IDENTITY) : p.m;
                this.draw('matrix', out, u => {
                    // column-major mat4x4f of the 4x4 part; offsets from column 5
                    u.f.set([m[0], m[5], m[10], m[15], m[1], m[6], m[11], m[16], m[2], m[7], m[12], m[17], m[3], m[8], m[13], m[18]], 0);
                    u.f.set([m[4], m[9], m[14], m[19]], 16);
                    u.f[20] = quant;
                }, [get(p.in)]);
                break;
            }
            case 'ct': {
                const f = p.f, tb = this.ctBuffer(p);
                this.draw('ct', out, u => {
                    u.i.set(f.map(x => FUNC_IDS[x.type]), 0);
                    u.i.set(f.map(x => x.table?.length ?? 0), 4);
                    u.f.set(f.map(x => x.slope ?? 1), 8);
                    u.f.set(f.map(x => x.intercept ?? 0), 12);
                    u.f.set(f.map(x => x.amplitude ?? 1), 16);
                    u.f.set(f.map(x => x.exponent ?? 1), 20);
                    u.f.set(f.map(x => x.offset ?? 0), 24);
                    u.f[28] = quant; u.f[29] = tb.width;
                }, [get(p.in), tb.buf]);
                break;
            }
            case 'blur': {
                const input = get(p.in);
                const tmp = this.acquire(out.w, out.h);
                const pass = (from: GTex, to: GTex, dx: number, dy: number, sigma: number) => {
                    const radius = sigma > 0 ? Math.min(Math.ceil(sigma * 3), 192) : 0;
                    this.draw('blur', to, u => { u.i[0] = dx; u.i[1] = dy; u.i[2] = radius; u.f[4] = Math.max(sigma, 1e-4); u.f[5] = quant; }, [from]);
                };
                pass(input, tmp, 1, 0, p.sx * sc);
                pass(tmp, out, 0, 1, p.sy * sc);
                this.release(tmp);
                break;
            }
            case 'arith':
                this.draw('arith', out, u => { u.f.set([p.k1, p.k2, p.k3, p.k4], 0); u.f[4] = quant; }, [get(p.in), get(p.in2)]);
                break;
            case 'composite':
                this.draw('composite', out, u => { u.i[0] = COMPOSITE_IDS[p.operator] ?? 0; u.f[4] = quant; }, [get(p.in), get(p.in2)]);
                break;
            case 'blend':
                this.draw('blend', out, u => { u.i[0] = BLEND_IDS[p.mode] ?? 0; u.f[4] = quant; }, [get(p.in), get(p.in2)]);
                break;
            case 'convolve':
                this.draw('convolve', out, u => {
                    u.i[0] = p.orderX; u.i[1] = p.orderY; u.i[2] = p.targetX; u.i[3] = p.targetY;
                    u.i[4] = p.edgeMode === 'duplicate' ? 0 : p.edgeMode === 'wrap' ? 1 : 2; u.i[5] = p.preserveAlpha ? 1 : 0;
                    u.f[8] = p.divisor || 1; u.f[9] = p.bias; u.f[10] = quant;
                    p.kernel.forEach((v, i) => { if (i < 84) u.f[12 + i] = v; });
                }, [get(p.in)]);
                break;
            case 'turbulence':
                this.draw('turbulence', out, u => { u.f[0] = p.fx; u.f[1] = p.fy; u.i[2] = p.octaves; u.i[3] = p.fractal ? 1 : 0; u.f[4] = sc; u.f[5] = quant; }, [this.lattice(p.seed)]);
                break;
            case 'flood':
                this.draw('flood', out, u => { u.f.set([p.color[0], p.color[1], p.color[2], p.opacity], 0); u.f[4] = quant; }, []);
                break;
            case 'offset':
                this.draw('offset', out, u => { u.f[0] = p.dx * sc; u.f[1] = p.dy * sc; u.f[2] = quant; }, [get(p.in)]);
                break;
            case 'morphology':
                this.draw('morphology', out, u => {
                    u.i[0] = Math.min(16, Math.round(p.rx * sc)); u.i[1] = Math.min(16, Math.round(p.ry * sc)); u.i[2] = p.erode ? 1 : 0; u.f[4] = quant;
                }, [get(p.in)]);
                break;
            case 'merge': {
                const ins = p.ins.map(get);
                if (!ins.length) { this.draw('flood', out, u => { u.f[4] = quant; }, []); break; }
                let acc = this.acquire(out.w, out.h);
                this.draw('copy', acc, u => { u.f[0] = quant; }, [ins[0]]);
                for (let i = 1; i < ins.length; i++) {
                    const nx = i === ins.length - 1 ? out : this.acquire(out.w, out.h);
                    const under = acc;
                    this.draw('composite', nx, u => { u.i[0] = 0; u.f[4] = quant; }, [ins[i], under]);
                    this.release(under);
                    acc = nx;
                }
                if (acc !== out) { this.draw('copy', out, () => undefined, [acc]); this.release(acc); }
                break;
            }
            case 'displacement':
                this.draw('displacement', out, u => { u.i[0] = p.xch; u.i[1] = p.ych; u.f[2] = p.scale * sc; u.f[3] = quant; }, [get(p.in), get(p.in2)]);
                break;
        }
    }

    // ------------------------------------------------------------------ output
    get hasFrame() { return !!this.lastOutput && !!this.lastSource; }
    get outputSize() { return { w: this.lastOutput?.w ?? 0, h: this.lastOutput?.h ?? 0 }; }

    private readBuffers: GPUBuffer[] = [];

    /**
     * Copies a texture into a pooled MAP_READ buffer and submits immediately. The copy is ordered on the
     * GPU queue before any later pass, so the caller may already upload/process the next frame while this
     * readback is in flight (pipelining). `into` receives the tightly packed rows.
     */
    private readback(tex: GTex, bpp: number, into?: Uint8Array): Promise<Uint8Array> {
        const rowBytes = tex.w * bpp;
        const padded = Math.ceil(rowBytes / 256) * 256;
        const size = padded * tex.h;
        const bi = this.readBuffers.findIndex(b => b.size === size);
        const buf = bi >= 0 ? this.readBuffers.splice(bi, 1)[0] : this.device.createBuffer({ size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        this.enc().copyTextureToBuffer({ texture: tex.tex }, { buffer: buf, bytesPerRow: padded, rowsPerImage: tex.h }, { width: tex.w, height: tex.h });
        this.submit();
        return buf.mapAsync(GPUMapMode.READ).then(() => {
            const src = new Uint8Array(buf.getMappedRange());
            const out = into && into.length === rowBytes * tex.h ? into : new Uint8Array(rowBytes * tex.h);
            if (padded === rowBytes) out.set(src);
            else for (let y = 0; y < tex.h; y++) out.set(src.subarray(y * padded, y * padded + rowBytes), y * rowBytes);
            buf.unmap();
            if (this.readBuffers.length < 6) this.readBuffers.push(buf); else buf.destroy();
            return out;
        });
    }

    async readOutput8(into?: Uint8Array): Promise<Uint8Array> {
        return this.read8(this.lastOutput!, into);
    }

    async readOutput16(into?: Uint16Array): Promise<Uint16Array> {
        return this.read16(this.lastOutput!, into);
    }

    private async read8(o: GTex, into?: Uint8Array): Promise<Uint8Array> {
        const t = performance.now();
        const t8 = this.acquire(o.w, o.h, 'rgba8unorm');
        this.draw('out8', t8, () => undefined, [o]);
        const pending = this.readback(t8, 4, into);
        this.release(t8); // safe: the copy is already queued before any later pass that could reuse t8
        const r = await pending;
        this.times.readback += performance.now() - t;
        return r;
    }

    private async read16(o: GTex, into?: Uint16Array): Promise<Uint16Array> {
        const t = performance.now();
        const t16 = this.acquire(o.w, o.h, 'rgba16uint');
        this.draw('out16', t16, () => undefined, [o]);
        const bytes = into ? new Uint8Array(into.buffer, into.byteOffset, into.byteLength) : undefined;
        const pending = this.readback(t16, 8, bytes);
        this.release(t16);
        const r = await pending;
        this.times.readback += performance.now() - t;
        return into && r.buffer === into.buffer ? into : new Uint16Array(r.buffer, r.byteOffset, r.byteLength / 2);
    }

    async readScope(which: 'output' | 'source' | 'timeline', w: number, h: number): Promise<Uint8Array> {
        const tex = which === 'output' ? this.lastOutput : which === 'timeline' ? this.tlOutput : this.lastSource;
        if (!tex) return new Uint8Array(w * h * 4);
        const t8 = this.acquire(w, h, 'rgba8unorm');
        this.draw('scope', t8, u => { u.f[0] = w; u.f[1] = h; }, [tex, this.linear]);
        const pending = this.readback(t8, 4);
        this.release(t8);
        return pending;
    }

    present(cw: number, ch: number, mode: 0 | 1 | 2, split: number) {
        if (!this.ctx || !this.lastOutput || !this.lastSource) return;
        const canvas = this.ctx.canvas as HTMLCanvasElement;
        if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch; }
        const view = this.ctx.getCurrentTexture().createView();
        this.draw('present', view, u => { u.f[0] = split; u.i[1] = mode; u.f[2] = cw; u.f[3] = ch; }, [this.lastSource, this.lastOutput, this.linear]);
        this.submit();
    }

    // ------------------------------------------------------------------ timeline (NLE) rendering
    get timelineReady() { return this.tlOn && !this.lost; }

    beginTimeline(mode: EngineSlot) {
        if (mode === 'preview' && !this.ctx) throw new Error('Timeline preview needs an engine created with a canvas.');
        if (!this.tlOn) {
            this.slots = Array.from({ length: MAX_SLOTS }, () => ({ src8: null, src16: null, is16: false, layer: null }));
            this.tlLast = -1;
        }
        this.tlMode = mode;
        this.tlOn = true;
        if (!this.dummy) this.dummy = this.createTex(1, 1, 'rgba16float');
        if (!this.dummyLut) this.dummyLut = this.makeLut(new Float32Array(4), 1, '1d');
        if (!this.dummyLut3) this.dummyLut3 = this.makeLut(new Float32Array(4), 1, '3d');
    }

    endTimeline() {
        if (!this.tlOn) return;
        this.submit();
        for (const s of this.slots) {
            this.release(s.layer);
            s.src8?.tex.destroy();
            s.src16?.tex.destroy();
        }
        this.slots = [];
        this.release(this.tlOutput);
        this.tlOutput = null;
        for (const l of this.luts.values()) l.tex.destroy();
        this.luts.clear();
        this.tlLast = -1;
        this.tlOn = false;
    }

    private slot(i: number): GSlot {
        if (!this.tlOn) throw new Error('Timeline rendering is not started (call beginTimeline / init first).');
        if (!Number.isInteger(i) || i < 0 || i >= MAX_SLOTS) throw new RangeError(`Layer slot must be an integer from 0 to ${MAX_SLOTS - 1}.`);
        return this.slots[i];
    }

    setSource(i: number, src: FrameSource) {
        const s = this.slot(i);
        const t = performance.now();
        if (src.kind === 'video') {
            const w = src.video.videoWidth, h = src.video.videoHeight;
            if (!w || !h) throw new Error('Video frame is not decoded yet (videoWidth is 0).');
            if (!s.src8 || s.src8.w !== w || s.src8.h !== h) { s.src8?.tex.destroy(); s.src8 = this.createTex(w, h, 'rgba8unorm'); }
            this.device.queue.copyExternalImageToTexture({ source: src.video }, { texture: s.src8.tex }, { width: w, height: h });
            s.is16 = false;
        } else if (src.kind === 'rgba8') {
            if (src.data.length < src.w * src.h * 4) throw new RangeError('RGBA8 frame is smaller than w x h x 4.');
            if (!s.src8 || s.src8.w !== src.w || s.src8.h !== src.h) { s.src8?.tex.destroy(); s.src8 = this.createTex(src.w, src.h, 'rgba8unorm'); }
            this.device.queue.writeTexture({ texture: s.src8.tex }, src.data as Uint8Array<ArrayBuffer>, { bytesPerRow: src.w * 4, rowsPerImage: src.h }, { width: src.w, height: src.h });
            s.is16 = false;
        } else {
            if (src.data.length < src.w * src.h * 4) throw new RangeError('RGBA16 frame is smaller than w x h x 4.');
            if (!s.src16 || s.src16.w !== src.w || s.src16.h !== src.h) { s.src16?.tex.destroy(); s.src16 = this.createTex(src.w, src.h, 'rgba16uint'); }
            this.device.queue.writeTexture({ texture: s.src16.tex }, src.data as Uint16Array<ArrayBuffer>, { bytesPerRow: src.w * 8, rowsPerImage: src.h }, { width: src.w, height: src.h });
            s.is16 = true;
        }
        this.times.upload += performance.now() - t;
    }

    private lutOf(handle: number | null, dim: '1d' | '3d'): GLut | null {
        if (handle === null || handle === undefined) return null;
        const l = this.luts.get(handle);
        if (!l || l.dim !== dim) throw new Error(`Unknown ${dim} LUT handle ${handle}.`);
        return l;
    }

    renderLayer(i: number, opts: LayerRenderOptions) {
        const s = this.slot(i);
        if (s.is16 ? !s.src16 : !s.src8) throw new Error(`Layer slot ${i} has no source frame (call setSource first).`);
        const t0 = performance.now();
        // per-clip graphs each bring their own table prims: bound the cache (the queue is idle after submit)
        if (this.ctBuffers.size > 512) { this.submit(); for (const b of this.ctBuffers.values()) b.buf.destroy(); this.ctBuffers.clear(); }
        const { w, h } = layerSizeOf(opts);
        const quant = opts.quantize8 ? 255 : 0;
        const sc = opts.pxScale ?? 1;
        const lut1 = this.lutOf(opts.lutHandle, '1d');
        const lut3 = this.lutOf(opts.hwLutHandle, '3d');
        let src = this.acquire(w, h);
        this.importInto(src, s.is16, s.src8, s.src16, w, h, quant);
        if (lut1 || lut3) {
            const lt = this.acquire(w, h);
            this.draw('tlLut', lt, u => {
                u.f[0] = quant; u.i[1] = lut1 ? 1 : 0; u.i[2] = lut1?.size ?? 1; u.i[3] = lut3 ? 1 : 0; u.i[4] = lut3?.size ?? 1;
            }, [src, (lut1 ?? this.dummyLut!).view, (lut3 ?? this.dummyLut3!).view]);
            this.release(src);
            src = lt;
        }
        const clipM = effectiveClipMatrix(opts, this.tlClip);
        const out = this.runGraph(src, opts.effectGraph, w, h, quant, sc,
            { autoMatrix: opts.autoMatrix, time: opts.time ?? 0, pxScale: sc, quantize8: !!opts.quantize8, clipMatrix: clipM });
        const look = graphHasClipPrim(opts.effectGraph) ? IDENTITY : clipM;
        const layer = this.acquire(w, h);
        this.draw('tlLook', layer, u => {
            fillMat4x5(u.f, look, 0);
            u.f[20] = quant; u.f[21] = opts.veil; u.f[22] = opts.veilColor;
        }, [out]);
        this.release(out);
        this.release(src);
        this.release(s.layer);
        s.layer = layer;
        this.tlLast = i;
        this.submit();
        this.times.process += performance.now() - t0;
    }

    private stepInput(x: CompositeStep['a'], acc: GTex | null): GTex {
        if (x === null) return this.dummy!;
        if (x === 'acc') {
            if (!acc) throw new Error('Composite step reads the accumulated stack before anything was drawn.');
            return acc;
        }
        const l = this.slot(x).layer;
        if (!l) throw new Error(`Layer slot ${x} was not rendered (call renderLayer first).`);
        return l;
    }

    composite(opts: CompositeOptions) {
        if (!this.tlOn) throw new Error('Timeline rendering is not started (call beginTimeline / init first).');
        const t0 = performance.now();
        const W = opts.width, H = opts.height;
        let out: GTex | null = null;
        let steps: CompositeStep[];
        if (opts.entries) steps = compositeSteps(opts.entries);
        else {
            if (this.tlLast < 0) throw new Error('Nothing to composite: no layer was rendered.');
            const layer = this.stepInput(this.tlLast, null);
            steps = [{ a: null, b: this.tlLast, weightA: 0, weightB: 1, uniforms: STACK_UNIFORMS, over: false }];
            if (this.tlMode === 'export' && opts.veil === 0 && layer.w === W && layer.h === H) {
                // exactly what process() + readOutput*() delivers for the same frame
                out = this.acquire(W, H);
                this.draw('copy', out, () => undefined, [layer]);
                steps = [];
            }
        }
        if (!out) {
            let acc: GTex | null = null;
            for (const st of steps) {
                const a = this.stepInput(st.a, acc), b = this.stepInput(st.b, acc);
                const under = st.over && acc ? acc : this.dummy!;
                const next = this.acquire(W, H);
                const un = st.uniforms;
                this.draw('tlStep', next, u => {
                    u.f[0] = W; u.f[1] = H; u.f[2] = un.cx; u.f[3] = un.cy;
                    u.i[4] = un.mode; u.i[5] = un.axis; u.i[6] = un.highSideIsB ? 1 : 0; u.i[7] = st.over && acc ? 1 : 0;
                    u.f[8] = un.edge; u.f[9] = un.feather; u.f[10] = st.weightA; u.f[11] = st.weightB;
                    u.f[12] = un.dxA; u.f[13] = un.dyA; u.f[14] = un.dxB; u.f[15] = un.dyB;
                }, [under, a, b, this.linear]);
                this.release(acc);
                acc = next;
            }
            if (!acc) throw new Error('Composite needs at least one layer.');
            out = this.acquire(W, H);
            this.draw('tlOut', out, u => { u.f[0] = opts.veil; u.f[1] = opts.veilColor; }, [acc]);
            this.release(acc);
        }
        this.release(this.tlOutput);
        this.tlOutput = out;
        if (this.tlMode === 'preview' && this.ctx) {
            const canvas = this.ctx.canvas as HTMLCanvasElement;
            if (canvas.width !== W || canvas.height !== H) { canvas.width = W; canvas.height = H; }
            const view = this.ctx.getCurrentTexture().createView();
            this.draw('present', view, u => { u.f[0] = 0; u.i[1] = 0; u.f[2] = W; u.f[3] = H; }, [out, out, this.linear]);
        }
        this.submit();
        this.times.process += performance.now() - t0;
        this.times.frames++;
    }

    private makeLut(data: Float32Array, size: number, dim: '1d' | '3d'): GLut {
        const tex = this.device.createTexture({
            size: dim === '1d' ? { width: size, height: 1 } : { width: size, height: size, depthOrArrayLayers: size },
            dimension: dim === '1d' ? '2d' : '3d', format: 'rgba32float',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
        });
        this.device.queue.writeTexture({ texture: tex }, data as Float32Array<ArrayBuffer>, { bytesPerRow: size * 16, rowsPerImage: dim === '1d' ? 1 : size },
            dim === '1d' ? { width: size, height: 1 } : { width: size, height: size, depthOrArrayLayers: size });
        return { tex, view: tex.createView(), size, dim };
    }

    uploadLut(rgba: Float32Array, size: number): number | null {
        if (!Number.isInteger(size) || size < 1 || rgba.length < size * 4) return null;
        if (size > this.device.limits.maxTextureDimension2D) return null;
        const id = this.lutSeq++;
        this.luts.set(id, this.makeLut(rgba.subarray(0, size * 4), size, '1d'));
        return id;
    }

    uploadLut3D(rgb: Float32Array, size: number): number | null {
        const n = size * size * size;
        if (!Number.isInteger(size) || size < 1 || rgb.length < n * 3) return null;
        if (size > this.device.limits.maxTextureDimension3D) return null;
        const data = new Float32Array(n * 4);
        for (let k = 0; k < n; k++) { data[k * 4] = rgb[k * 3]; data[k * 4 + 1] = rgb[k * 3 + 1]; data[k * 4 + 2] = rgb[k * 3 + 2]; data[k * 4 + 3] = 1; }
        const id = this.lutSeq++;
        this.luts.set(id, this.makeLut(data, size, '3d'));
        return id;
    }

    readTimeline8(into?: Uint8Array): Promise<Uint8Array> {
        if (!this.tlOutput) return Promise.reject(new Error('No composited frame to read (call composite first).'));
        return this.read8(this.tlOutput, into);
    }

    readTimeline16(into?: Uint16Array): Promise<Uint16Array> {
        if (!this.tlOutput) return Promise.reject(new Error('No composited frame to read (call composite first).'));
        return this.read16(this.tlOutput, into);
    }

    asRenderEngine(): RenderEngine {
        if (!this.renderSurface) {
            const self = this;
            this.renderSurface = {
                beginTimeline: mode => self.beginTimeline(mode),
                endTimeline: () => self.endTimeline(),
                get timelineReady() { return self.timelineReady; },
                setSource: (slot, src) => self.setSource(slot, src),
                renderLayer: (slot, o) => self.renderLayer(slot, o),
                composite: o => self.composite(o),
                uploadLut: (rgba, size) => self.uploadLut(rgba, size),
                uploadLut3D: (rgb, size) => self.uploadLut3D(rgb, size),
                readOutput8: into => self.readTimeline8(into),
                readOutput16: into => self.readTimeline16(into)
            };
        }
        return this.renderSurface;
    }

    dispose() {
        this.endTimeline();
        [this.dummy].forEach(t => t?.tex.destroy());
        [this.dummyLut, this.dummyLut3].forEach(l => l?.tex.destroy());
        this.dummy = null; this.dummyLut = null; this.dummyLut3 = null;
        this.submit();
        for (const t of this.pool) t.tex.destroy();
        this.pool = [];
        [this.lastSource, this.lastOutput, this.src8, this.src16].forEach(t => t?.tex.destroy());
        for (const b of this.ctBuffers.values()) b.buf.destroy();
        for (const b of this.lattices.values()) b.destroy();
        for (const b of this.readBuffers) b.destroy();
        this.ring.destroy();
        try { this.ctx?.unconfigure(); } catch { /* ignore */ }
    }
}
