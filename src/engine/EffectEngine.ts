// WebGL2 execution engine for the UVE effect graph.
// Executes SVG filter primitives with SVG semantics (premultiplied alpha where the spec requires
// it, 180° rotated convolution kernels, clamped primitive results) on RGBA16F textures, then runs
// the enabled custom GLSL passes. Shared by the live preview and the offline export worker so
// both produce identical pixels for identical inputs.
import type { Prim, TransferFunc, UveGraph } from '../../shared/uve/graph';
import type { CustomFilterEntry } from '../../shared/uve/customFilters';
import { buildFragSrc, lintGlsl, parseUniformDefs } from '../../shared/uve/customFilters';
import type { Mat4x5 } from '../../shared/uve/matrix';
import { buildTurbulenceLattice } from './turbulence';

export interface Tex { tex: WebGLTexture; fb: WebGLFramebuffer; w: number; h: number; fmt: 'f16' | 'u8' | 'u16' }

const VS = `#version 300 es
in vec2 aPos;
out vec2 v_uv;
void main() { v_uv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }`;

const COMMON = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
uniform ivec2 uSize;
uniform float uQuant;
in vec2 v_uv;
out vec4 o;
ivec2 P() { return ivec2(gl_FragCoord.xy); }
vec4 fetchC(sampler2D s, ivec2 p) { return texelFetch(s, clamp(p, ivec2(0), uSize - 1), 0); }
vec4 fetchT(sampler2D s, ivec2 p) {
    if (p.x < 0 || p.y < 0 || p.x >= uSize.x || p.y >= uSize.y) return vec4(0.0);
    return texelFetch(s, p, 0);
}
vec4 premul(vec4 c) { return vec4(c.rgb * c.a, c.a); }
vec4 unpremul(vec4 c) { return c.a > 0.0 ? vec4(clamp(c.rgb / c.a, 0.0, 1.0), c.a) : vec4(0.0); }
vec4 Q(vec4 c) { c = clamp(c, 0.0, 1.0); return uQuant > 0.0 ? floor(c * uQuant + 0.5) / uQuant : c; }
`;

const FS: Record<string, string> = {
    // Source import: resample the uploaded frame into the working size
    import: `${COMMON}
uniform sampler2D uIn;
void main() { o = Q(texture(uIn, v_uv)); }`,
    import16: `${COMMON}
uniform highp usampler2D uInU;
void main() {
    ivec2 sz = textureSize(uInU, 0);
    ivec2 p = clamp(ivec2(v_uv * vec2(sz)), ivec2(0), sz - 1);
    o = Q(vec4(texelFetch(uInU, p, 0)) / 65535.0);
}`,
    copy: `${COMMON}
uniform sampler2D uIn;
void main() { o = fetchC(uIn, P()); }`,
    sourceAlpha: `${COMMON}
uniform sampler2D uIn;
void main() { o = vec4(0.0, 0.0, 0.0, fetchC(uIn, P()).a); }`,
    matrix: `${COMMON}
uniform sampler2D uIn; uniform mat4 uM; uniform vec4 uOff;
void main() {
    vec4 c = fetchC(uIn, P());
    o = Q(uM * c + uOff);
}`,
    ct: `${COMMON}
uniform sampler2D uIn; uniform sampler2D uTable;
uniform ivec4 uType; uniform ivec4 uLen;
uniform vec4 uSlope; uniform vec4 uInter; uniform vec4 uAmp; uniform vec4 uExp; uniform vec4 uGOff;
float fn(int ch, float C) {
    int t = uType[ch];
    if (t == 0) return C;
    if (t == 1) {
        int n = uLen[ch];
        if (n < 1) return C;
        if (n == 1) return texelFetch(uTable, ivec2(0, ch), 0).r;
        float x = clamp(C, 0.0, 1.0) * float(n - 1);
        int k = int(floor(x));
        if (k >= n - 1) return texelFetch(uTable, ivec2(n - 1, ch), 0).r;
        float v0 = texelFetch(uTable, ivec2(k, ch), 0).r;
        float v1 = texelFetch(uTable, ivec2(k + 1, ch), 0).r;
        return v0 + (x - float(k)) * (v1 - v0);
    }
    if (t == 2) {
        int n = uLen[ch];
        if (n < 1) return C;
        int k = min(int(floor(clamp(C, 0.0, 1.0) * float(n))), n - 1);
        return texelFetch(uTable, ivec2(k, ch), 0).r;
    }
    if (t == 3) return uSlope[ch] * C + uInter[ch];
    return uAmp[ch] * pow(max(C, 0.0), uExp[ch]) + uGOff[ch];
}
void main() {
    vec4 c = fetchC(uIn, P());
    o = Q(vec4(fn(0, c.r), fn(1, c.g), fn(2, c.b), fn(3, c.a)));
}`,
    blur: `${COMMON}
uniform sampler2D uIn; uniform ivec2 uDir; uniform float uSigma; uniform int uRadius;
void main() {
    ivec2 p = P();
    if (uRadius <= 0) { o = fetchC(uIn, p); return; }
    vec4 acc = vec4(0.0); float wsum = 0.0;
    float k = -0.5 / (uSigma * uSigma);
    for (int i = -uRadius; i <= uRadius; i++) {
        float w = exp(float(i * i) * k);
        acc += premul(fetchC(uIn, p + uDir * i)) * w;
        wsum += w;
    }
    o = Q(unpremul(acc / wsum));
}`,
    arith: `${COMMON}
uniform sampler2D uIn; uniform sampler2D uIn2; uniform vec4 uK;
void main() {
    ivec2 p = P();
    vec4 i1 = premul(fetchC(uIn, p)), i2 = premul(fetchC(uIn2, p));
    vec4 r = clamp(uK.x * i1 * i2 + uK.y * i1 + uK.z * i2 + uK.w, 0.0, 1.0);
    r.rgb = min(r.rgb, vec3(r.a));
    o = Q(unpremul(r));
}`,
    composite: `${COMMON}
uniform sampler2D uIn; uniform sampler2D uIn2; uniform int uOp;
void main() {
    ivec2 p = P();
    vec4 a = premul(fetchC(uIn, p)), b = premul(fetchC(uIn2, p));
    vec4 r;
    if (uOp == 0) r = a + b * (1.0 - a.a);
    else if (uOp == 1) r = a * b.a;
    else if (uOp == 2) r = a * (1.0 - b.a);
    else if (uOp == 3) r = a * b.a + b * (1.0 - a.a);
    else if (uOp == 4) r = a * (1.0 - b.a) + b * (1.0 - a.a);
    else r = a + b;
    o = Q(unpremul(clamp(r, 0.0, 1.0)));
}`,
    blend: `${COMMON}
uniform sampler2D uIn; uniform sampler2D uIn2; uniform int uMode;
float lum(vec3 c) { return dot(c, vec3(0.3, 0.59, 0.11)); }
vec3 clipColor(vec3 c) {
    float l = lum(c), n = min(min(c.r, c.g), c.b), x = max(max(c.r, c.g), c.b);
    if (n < 0.0) c = l + (c - l) * l / max(l - n, 1e-6);
    if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 1e-6);
    return c;
}
vec3 setLum(vec3 c, float l) { return clipColor(c + (l - lum(c))); }
float sat(vec3 c) { return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b); }
vec3 setSat(vec3 c, float s) {
    float mx = max(max(c.r, c.g), c.b), mn = min(min(c.r, c.g), c.b);
    return mx > mn ? (c - mn) * s / (mx - mn) : vec3(0.0);
}
float softL(float b, float s) {
    if (s <= 0.5) return b - (1.0 - 2.0 * s) * b * (1.0 - b);
    float d = b <= 0.25 ? ((16.0 * b - 12.0) * b + 4.0) * b : sqrt(b);
    return b + (2.0 * s - 1.0) * (d - b);
}
float hardL(float b, float s) { return s <= 0.5 ? b * 2.0 * s : 1.0 - (1.0 - b) * (1.0 - (2.0 * s - 1.0)); }
vec3 B(vec3 cb, vec3 cs) {
    if (uMode == 1) return cb * cs;
    if (uMode == 2) return cb + cs - cb * cs;
    if (uMode == 3) return vec3(hardL(cs.r, cb.r), hardL(cs.g, cb.g), hardL(cs.b, cb.b));
    if (uMode == 4) return min(cb, cs);
    if (uMode == 5) return max(cb, cs);
    if (uMode == 6) return vec3(cb.r == 0.0 ? 0.0 : (cs.r >= 1.0 ? 1.0 : min(1.0, cb.r / (1.0 - cs.r))),
                                cb.g == 0.0 ? 0.0 : (cs.g >= 1.0 ? 1.0 : min(1.0, cb.g / (1.0 - cs.g))),
                                cb.b == 0.0 ? 0.0 : (cs.b >= 1.0 ? 1.0 : min(1.0, cb.b / (1.0 - cs.b))));
    if (uMode == 7) return vec3(cb.r >= 1.0 ? 1.0 : (cs.r <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - cb.r) / cs.r)),
                                cb.g >= 1.0 ? 1.0 : (cs.g <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - cb.g) / cs.g)),
                                cb.b >= 1.0 ? 1.0 : (cs.b <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - cb.b) / cs.b)));
    if (uMode == 8) return vec3(hardL(cb.r, cs.r), hardL(cb.g, cs.g), hardL(cb.b, cs.b));
    if (uMode == 9) return vec3(softL(cb.r, cs.r), softL(cb.g, cs.g), softL(cb.b, cs.b));
    if (uMode == 10) return abs(cb - cs);
    if (uMode == 11) return cb + cs - 2.0 * cb * cs;
    if (uMode == 12) return setLum(setSat(cs, sat(cb)), lum(cb));
    if (uMode == 13) return setLum(setSat(cb, sat(cs)), lum(cb));
    if (uMode == 14) return setLum(cs, lum(cb));
    if (uMode == 15) return setLum(cb, lum(cs));
    return cs;
}
void main() {
    ivec2 p = P();
    vec4 s = fetchC(uIn, p);   // in  = source (top)
    vec4 b = fetchC(uIn2, p);  // in2 = backdrop
    float ra = s.a + b.a - s.a * b.a;
    vec3 rc = (1.0 - b.a) * s.rgb * s.a + (1.0 - s.a) * b.rgb * b.a + s.a * b.a * clamp(B(b.rgb, s.rgb), 0.0, 1.0);
    o = Q(unpremul(vec4(clamp(rc, 0.0, 1.0), clamp(ra, 0.0, 1.0))));
}`,
    convolve: `${COMMON}
uniform sampler2D uIn; uniform ivec2 uOrder; uniform ivec2 uTarget; uniform float uKernel[81];
uniform float uDivisor; uniform float uBias; uniform int uEdge; uniform int uPreserve;
vec4 sampleE(ivec2 q) {
    if (uEdge == 0) return fetchC(uIn, q);
    if (uEdge == 1) return texelFetch(uIn, ivec2((q.x % uSize.x + uSize.x) % uSize.x, (q.y % uSize.y + uSize.y) % uSize.y), 0);
    return fetchT(uIn, q);
}
void main() {
    ivec2 p = P();
    vec4 acc = vec4(0.0);
    for (int i = 0; i < 9; i++) {
        if (i >= uOrder.y) break;
        for (int j = 0; j < 9; j++) {
            if (j >= uOrder.x) break;
            vec4 c = sampleE(p + ivec2(j - uTarget.x, i - uTarget.y));
            float k = uKernel[(uOrder.y - i - 1) * uOrder.x + (uOrder.x - j - 1)];
            acc += (uPreserve == 1 ? c : premul(c)) * k;
        }
    }
    vec4 src = fetchC(uIn, p);
    if (uPreserve == 1) {
        o = Q(vec4(clamp(acc.rgb / uDivisor + uBias, 0.0, 1.0), src.a));
    } else {
        vec4 r = clamp(acc / uDivisor + uBias, 0.0, 1.0);
        r.rgb = min(r.rgb, vec3(r.a));
        o = Q(unpremul(r));
    }
}`,
    turbulence: `${COMMON}
uniform sampler2D uLattice; uniform vec2 uFreq; uniform int uOctaves; uniform int uFractal; uniform float uPxScale;
float lat(int i) { return texelFetch(uLattice, ivec2(i, 0), 0).r; }
vec2 grd(int ch, int i) { return texelFetch(uLattice, ivec2(i, ch + 1), 0).rg; }
float scurve(float t) { return t * t * (3.0 - 2.0 * t); }
float noise2(int ch, vec2 v) {
    float t = v.x + 4096.0;
    int bx0 = int(t) & 255; int bx1 = (bx0 + 1) & 255;
    float rx0 = t - floor(t); float rx1 = rx0 - 1.0;
    t = v.y + 4096.0;
    int by0 = int(t) & 255; int by1 = (by0 + 1) & 255;
    float ry0 = t - floor(t); float ry1 = ry0 - 1.0;
    int i = int(lat(bx0)); int j = int(lat(bx1));
    int b00 = int(lat(i + by0)); int b10 = int(lat(j + by0));
    int b01 = int(lat(i + by1)); int b11 = int(lat(j + by1));
    float sx = scurve(rx0), sy = scurve(ry0);
    vec2 q = grd(ch, b00); float u = rx0 * q.x + ry0 * q.y;
    q = grd(ch, b10); float w = rx1 * q.x + ry0 * q.y;
    float a = mix(u, w, sx);
    q = grd(ch, b01); u = rx0 * q.x + ry1 * q.y;
    q = grd(ch, b11); w = rx1 * q.x + ry1 * q.y;
    float b = mix(u, w, sx);
    return mix(a, b, sy);
}
float turb(int ch, vec2 pt) {
    float sum = 0.0, ratio = 1.0;
    vec2 v = pt * uFreq;
    for (int k = 0; k < 8; k++) {
        if (k >= uOctaves) break;
        float n = noise2(ch, v);
        sum += (uFractal == 1 ? n : abs(n)) / ratio;
        v *= 2.0; ratio *= 2.0;
    }
    return uFractal == 1 ? (sum + 1.0) * 0.5 : sum;
}
void main() {
    vec2 pt = vec2(P()) / uPxScale;
    o = Q(vec4(turb(0, pt), turb(1, pt), turb(2, pt), turb(3, pt)));
}`,
    flood: `${COMMON}
uniform vec4 uColor;
void main() { o = Q(uColor); }`,
    offset: `${COMMON}
uniform sampler2D uIn; uniform vec2 uD;
void main() { o = fetchT(uIn, P() - ivec2(floor(uD + 0.5))); }`,
    morphology: `${COMMON}
uniform sampler2D uIn; uniform ivec2 uR; uniform int uErode;
void main() {
    ivec2 p = P();
    vec4 r = uErode == 1 ? vec4(1.0) : vec4(0.0);
    for (int y = -16; y <= 16; y++) {
        if (y < -uR.y || y > uR.y) continue;
        for (int x = -16; x <= 16; x++) {
            if (x < -uR.x || x > uR.x) continue;
            vec4 c = premul(fetchT(uIn, p + ivec2(x, y)));
            r = uErode == 1 ? min(r, c) : max(r, c);
        }
    }
    o = Q(unpremul(r));
}`,
    displacement: `${COMMON}
uniform sampler2D uIn; uniform sampler2D uIn2; uniform float uScale; uniform ivec2 uCh;
void main() {
    ivec2 p = P();
    vec4 m = fetchC(uIn2, p);
    vec2 d = uScale * (vec2(m[uCh.x], m[uCh.y]) - 0.5);
    o = fetchT(uIn, p + ivec2(floor(d + 0.5)));
}`,
    // Custom GLSL blend of a pass result (top) over the previous image (backdrop)
    present: `#version 300 es
precision highp float;
uniform sampler2D uOrig; uniform sampler2D uProc; uniform float uSplit; uniform int uMode; uniform vec2 uView;
in vec2 v_uv; out vec4 o;
void main() {
    vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
    vec4 c;
    if (uMode == 0) c = texture(uProc, uv);
    else if (uMode == 1) c = texture(uOrig, uv);
    else c = uv.x < uSplit ? texture(uOrig, uv) : texture(uProc, uv);
    if (uMode == 2 && abs(uv.x - uSplit) * uView.x < 1.0) c = vec4(1.0);
    o = vec4(c.rgb, 1.0);
}`,
    out8: `#version 300 es
precision highp float;
uniform sampler2D uIn; uniform int uDither;
out vec4 o;
void main() { o = vec4(clamp(texelFetch(uIn, ivec2(gl_FragCoord.xy), 0).rgb, 0.0, 1.0), 1.0); }`,
    out16: `#version 300 es
precision highp float;
uniform sampler2D uIn;
out uvec4 o;
void main() { o = uvec4(floor(clamp(texelFetch(uIn, ivec2(gl_FragCoord.xy), 0), 0.0, 1.0) * 65535.0 + 0.5)); }`
};

const BLEND_IDS: Record<string, number> = {
    normal: 0, multiply: 1, screen: 2, overlay: 3, darken: 4, lighten: 5, 'color-dodge': 6, 'color-burn': 7, 'hard-light': 8,
    'soft-light': 9, difference: 10, exclusion: 11, hue: 12, saturation: 13, color: 14, luminosity: 15
};
/** Texture unit used only for creating/uploading textures, never sampled by a program. */
const SCRATCH_UNIT = 7;
const COMPOSITE_IDS: Record<string, number> = { over: 0, in: 1, out: 2, atop: 3, xor: 4, lighter: 5 };
const FUNC_IDS: Record<TransferFunc['type'], number> = { identity: 0, table: 1, discrete: 2, linear: 3, gamma: 4 };

interface Program { prog: WebGLProgram; loc: Record<string, WebGLUniformLocation | null> }

export interface CompiledCustom {
    entry: CustomFilterEntry;
    prog: Program | null;
    error: string | null;
    usesStats: boolean;
}

export interface ProcessOptions {
    autoMatrix: Mat4x5 | null;
    time: number;
    /** processing pixels per source pixel (preview quality); 1 = exact */
    pxScale: number;
    quantize8: boolean;
}

export interface FrameStatsU { avg_lum: number; avg_r: number; avg_g: number; avg_b: number; contrast: number }

export class EffectEngine {
    readonly gl: WebGL2RenderingContext;
    private vao: WebGLVertexArrayObject;
    private programs = new Map<string, Program>();
    private pool: Tex[] = [];
    private srcTex8: WebGLTexture | null = null;
    private srcTex16: WebGLTexture | null = null;
    private srcIs16 = false;
    private srcW = 0;
    private srcH = 0;
    private ctTables = new Map<Prim, WebGLTexture>();
    private lattices = new Map<number, WebGLTexture>();
    private graph: UveGraph = { prims: [], output: 'SourceGraphic', hasAuto: false, warnings: [] };
    private customs: CompiledCustom[] = [];
    private statsTex: Tex | null = null;
    lastSource: Tex | null = null;
    lastOutput: Tex | null = null;

    constructor(gl: WebGL2RenderingContext) {
        this.gl = gl;
        if (!gl.getExtension('EXT_color_buffer_float')) throw new Error('GPU does not support float render targets (EXT_color_buffer_float).');
        gl.getExtension('OES_texture_float_linear');
        const vao = gl.createVertexArray();
        if (!vao) throw new Error('WebGL2: cannot create vertex array');
        this.vao = vao;
        gl.bindVertexArray(vao);
        const buf = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buf);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
        gl.disable(gl.BLEND);
        gl.disable(gl.DEPTH_TEST);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        gl.pixelStorei(gl.PACK_ALIGNMENT, 1);
    }

    // ------------------------------------------------------------------ programs
    private compile(type: number, src: string): { shader: WebGLShader | null; log: string } {
        const gl = this.gl;
        const sh = gl.createShader(type)!;
        gl.shaderSource(sh, src);
        gl.compileShader(sh);
        if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
            const log = gl.getShaderInfoLog(sh) || 'compile error';
            gl.deleteShader(sh);
            return { shader: null, log };
        }
        return { shader: sh, log: '' };
    }

    private link(fsSrc: string): { program: Program | null; log: string } {
        const gl = this.gl;
        const vs = this.compile(gl.VERTEX_SHADER, VS);
        const fs = this.compile(gl.FRAGMENT_SHADER, fsSrc);
        if (!vs.shader || !fs.shader) return { program: null, log: fs.log || vs.log };
        const prog = gl.createProgram()!;
        gl.attachShader(prog, vs.shader);
        gl.attachShader(prog, fs.shader);
        gl.bindAttribLocation(prog, 0, 'aPos');
        gl.linkProgram(prog);
        gl.deleteShader(vs.shader);
        gl.deleteShader(fs.shader);
        if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
            const log = gl.getProgramInfoLog(prog) || 'link error';
            gl.deleteProgram(prog);
            return { program: null, log };
        }
        const loc: Record<string, WebGLUniformLocation | null> = {};
        const n = gl.getProgramParameter(prog, gl.ACTIVE_UNIFORMS) as number;
        for (let i = 0; i < n; i++) {
            const info = gl.getActiveUniform(prog, i);
            if (!info) continue;
            const name = info.name.replace(/\[0\]$/, '');
            loc[name] = gl.getUniformLocation(prog, info.name);
        }
        return { program: { prog, loc }, log: '' };
    }

    private program(name: string): Program {
        let p = this.programs.get(name);
        if (!p) {
            const r = this.link(FS[name]);
            if (!r.program) throw new Error(`Internal shader "${name}" failed: ${r.log}`);
            p = r.program;
            this.programs.set(name, p);
        }
        return p;
    }

    // ------------------------------------------------------------------ textures
    private createTex(w: number, h: number, fmt: Tex['fmt']): Tex {
        const gl = this.gl;
        const tex = gl.createTexture()!;
        gl.activeTexture(gl.TEXTURE0 + SCRATCH_UNIT);
        gl.bindTexture(gl.TEXTURE_2D, tex);
        if (fmt === 'f16') gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA16F, w, h);
        else if (fmt === 'u8') gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, w, h);
        else gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA16UI, w, h);
        const filter = fmt === 'u16' ? gl.NEAREST : gl.LINEAR;
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        const fb = gl.createFramebuffer()!;
        gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
        const st = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
        if (st !== gl.FRAMEBUFFER_COMPLETE) throw new Error(`Framebuffer incomplete (${fmt} ${w}x${h}): 0x${st.toString(16)}`);
        return { tex, fb, w, h, fmt };
    }

    private acquire(w: number, h: number, fmt: Tex['fmt'] = 'f16'): Tex {
        const i = this.pool.findIndex(t => t.w === w && t.h === h && t.fmt === fmt);
        if (i >= 0) return this.pool.splice(i, 1)[0];
        return this.createTex(w, h, fmt);
    }

    private release(t: Tex | null | undefined) {
        if (!t) return;
        this.pool.push(t);
        if (this.pool.length > 48) this.destroyTex(this.pool.shift()!);
    }

    private destroyTex(t: Tex) {
        this.gl.deleteTexture(t.tex);
        this.gl.deleteFramebuffer(t.fb);
    }

    // ------------------------------------------------------------------ sources
    private ensureSrc8() {
        const gl = this.gl;
        if (!this.srcTex8) {
            this.srcTex8 = gl.createTexture();
            gl.bindTexture(gl.TEXTURE_2D, this.srcTex8);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        }
        gl.bindTexture(gl.TEXTURE_2D, this.srcTex8);
    }

    /** Uploads a decoded <video> frame (preview path). */
    uploadVideo(video: HTMLVideoElement) {
        const gl = this.gl;
        this.ensureSrc8();
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, video);
        this.srcW = video.videoWidth;
        this.srcH = video.videoHeight;
        this.srcIs16 = false;
    }

    /** Uploads a raw RGBA8 frame (export path / still images). */
    uploadRGBA8(data: Uint8Array, w: number, h: number) {
        const gl = this.gl;
        this.ensureSrc8();
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, data);
        this.srcW = w; this.srcH = h; this.srcIs16 = false;
    }

    /** Uploads a raw RGBA 16-bit frame (rgba64le from FFmpeg, high bit depth sources). */
    uploadRGBA16(data: Uint16Array, w: number, h: number) {
        const gl = this.gl;
        if (!this.srcTex16) {
            this.srcTex16 = gl.createTexture();
            gl.bindTexture(gl.TEXTURE_2D, this.srcTex16);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        }
        gl.bindTexture(gl.TEXTURE_2D, this.srcTex16);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16UI, w, h, 0, gl.RGBA_INTEGER, gl.UNSIGNED_SHORT, data);
        this.srcW = w; this.srcH = h; this.srcIs16 = true;
    }

    get sourceSize() { return { w: this.srcW, h: this.srcH }; }

    // ------------------------------------------------------------------ graph
    setGraph(graph: UveGraph) {
        for (const t of this.ctTables.values()) this.gl.deleteTexture(t);
        this.ctTables.clear();
        this.graph = graph;
    }

    /** Compiles custom GLSL entries; returns diagnostics with user-relative line numbers. */
    setCustomFilters(entries: CustomFilterEntry[]): CompiledCustom[] {
        for (const c of this.customs) if (c.prog) this.gl.deleteProgram(c.prog.prog);
        this.customs = entries.filter(e => e.type === 'webgl').map(entry => this.compileCustom(entry));
        return this.customs;
    }

    compileCustom(entry: CustomFilterEntry): CompiledCustom {
        const lint = lintGlsl(entry.code);
        if (lint.length) return { entry, prog: null, error: lint.join('\n'), usesStats: false };
        const { src, headerLines } = buildFragSrc(entry.code);
        const r = this.link(src);
        if (!r.program) {
            const log = r.log.replace(/ERROR:\s*0:(\d+)/g, (_m, l) => `ERROR: line ${Math.max(1, Number(l) - headerLines)}`);
            return { entry, prog: null, error: log, usesStats: false };
        }
        return { entry, prog: r.program, error: null, usesStats: /u_avg_(lum|r|g|b)|u_contrast/.test(entry.code) };
    }

    /** Validates GLSL without changing the active stack (Filter Studio). */
    validateGlsl(code: string): string | null {
        const c = this.compileCustom({ id: '__validate__', label: 'validate', code, type: 'webgl', enabled: true, blendMode: 'normal', tags: [], category: '', uniforms: {} });
        if (c.prog) this.gl.deleteProgram(c.prog.prog);
        return c.error;
    }

    get customDiagnostics() { return this.customs; }

    private ctTableTex(p: Prim & { op: 'ct' }): WebGLTexture {
        let t = this.ctTables.get(p);
        if (t) return t;
        const gl = this.gl;
        const width = Math.max(1, ...p.f.map(f => f.table?.length ?? 0));
        const data = new Float32Array(width * 4);
        p.f.forEach((f, ch) => (f.table || []).forEach((v, i) => { data[ch * width + i] = v; }));
        t = gl.createTexture()!;
        gl.activeTexture(gl.TEXTURE0 + SCRATCH_UNIT);
        gl.bindTexture(gl.TEXTURE_2D, t);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32F, width, 4, 0, gl.RED, gl.FLOAT, data);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        this.ctTables.set(p, t);
        return t;
    }

    private latticeTex(seed: number): WebGLTexture {
        let t = this.lattices.get(seed);
        if (t) return t;
        const gl = this.gl;
        const l = buildTurbulenceLattice(seed);
        t = gl.createTexture()!;
        gl.activeTexture(gl.TEXTURE0 + SCRATCH_UNIT);
        gl.bindTexture(gl.TEXTURE_2D, t);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, l.width, l.height, 0, gl.RGBA, gl.FLOAT, l.data);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        this.lattices.set(seed, t);
        return t;
    }

    // ------------------------------------------------------------------ drawing
    private bindTex(unit: number, tex: WebGLTexture, loc: WebGLUniformLocation | null | undefined) {
        const gl = this.gl;
        gl.activeTexture(gl.TEXTURE0 + unit);
        gl.bindTexture(gl.TEXTURE_2D, tex);
        if (loc) gl.uniform1i(loc, unit);
    }

    private draw(p: Program, target: Tex, quant: number, set: (l: Program['loc']) => void) {
        const gl = this.gl;
        gl.useProgram(p.prog);
        gl.bindFramebuffer(gl.FRAMEBUFFER, target.fb);
        gl.viewport(0, 0, target.w, target.h);
        if (p.loc.uSize) gl.uniform2i(p.loc.uSize, target.w, target.h);
        if (p.loc.uQuant) gl.uniform1f(p.loc.uQuant, quant);
        set(p.loc);
        gl.bindVertexArray(this.vao);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    /**
     * Runs the full chain on the uploaded source frame.
     * Returns the processed texture (owned by the engine until the next call).
     */
    process(outW: number, outH: number, opts: ProcessOptions): Tex {
        const gl = this.gl;
        const quant = opts.quantize8 ? 255 : 0;
        const sc = opts.pxScale > 0 ? opts.pxScale : 1;
        this.release(this.lastSource);
        this.release(this.lastOutput);
        this.lastOutput = null;

        // 1. import
        const src = this.acquire(outW, outH);
        if (this.srcIs16) this.draw(this.program('import16'), src, quant, l => this.bindTex(0, this.srcTex16!, l.uInU));
        else this.draw(this.program('import'), src, quant, l => this.bindTex(0, this.srcTex8!, l.uIn));
        this.lastSource = src;

        // 2. graph
        const prims = this.graph.prims;
        const results = new Map<string, Tex>();
        const lastUse = new Map<string, number>();
        prims.forEach((p, i) => {
            const ins: string[] = [];
            if ('in' in p) ins.push(p.in);
            if ('in2' in p) ins.push(p.in2);
            if (p.op === 'merge') ins.push(...p.ins);
            ins.forEach(n => lastUse.set(n, i));
        });
        lastUse.set(this.graph.output, Number.MAX_SAFE_INTEGER);
        let prev = 'SourceGraphic';
        let sourceAlpha: Tex | null = null;
        const get = (name: string): Tex => {
            if (name === 'SourceGraphic') return src;
            if (name === 'SourceAlpha') {
                if (!sourceAlpha) {
                    sourceAlpha = this.acquire(outW, outH);
                    const pa = this.program('sourceAlpha');
                    this.draw(pa, sourceAlpha, quant, l => this.bindTex(0, src.tex, l.uIn));
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
            // release inputs that are no longer needed
            for (const [name, t] of results) {
                if (name !== p.out && (lastUse.get(name) ?? -1) <= i && name !== this.graph.output) {
                    this.release(t);
                    results.delete(name);
                }
            }
        });
        if (sourceAlpha) this.release(sourceAlpha);

        let cur: Tex = this.graph.output === 'SourceGraphic' ? src : (results.get(this.graph.output) ?? src);
        for (const [name, t] of results) if (t !== cur) { this.release(t); results.delete(name); }

        // 3. custom GLSL passes
        const active = this.customs.filter(c => c.entry.enabled && c.prog);
        let stats: FrameStatsU | null = null;
        if (active.some(c => c.usesStats)) stats = this.frameStats(src);
        for (const c of active) {
            const out = this.acquire(outW, outH);
            const prog = c.prog!;
            this.draw(prog, out, 0, l => {
                this.bindTex(0, cur.tex, l.u_video);
                this.bindTex(1, src.tex, l.u_video_raw);
                if (l.u_res) gl.uniform2f(l.u_res, outW, outH);
                if (l.u_time) gl.uniform1f(l.u_time, opts.time);
                if (l.u_mouse) gl.uniform2f(l.u_mouse, 0.5, 0.5);
                if (l.u_strength) gl.uniform1f(l.u_strength, 1.0);
                if (l.u_layers) gl.uniform1f(l.u_layers, 1.0);
                if (l.u_zoom) gl.uniform1f(l.u_zoom, 1.0);
                if (l.u_avg_lum) gl.uniform1f(l.u_avg_lum, stats?.avg_lum ?? 0.5);
                if (l.u_avg_r) gl.uniform1f(l.u_avg_r, stats?.avg_r ?? 0.5);
                if (l.u_avg_g) gl.uniform1f(l.u_avg_g, stats?.avg_g ?? 0.5);
                if (l.u_avg_b) gl.uniform1f(l.u_avg_b, stats?.avg_b ?? 0.5);
                if (l.u_contrast) gl.uniform1f(l.u_contrast, stats?.contrast ?? 0.5);
                if (l.u_gvf_mix) gl.uniform1f(l.u_gvf_mix, 1.0);
                if (l.u_gvf_offset_scale) gl.uniform1f(l.u_gvf_offset_scale, 1.0);
                for (const d of parseUniformDefs(c.entry.code)) {
                    const loc = l[d.name];
                    if (loc) gl.uniform1f(loc, Number(c.entry.uniforms[d.name] ?? d.def));
                }
            });
            const bm = c.entry.blendMode || 'normal';
            let next = out;
            if (bm !== 'normal' && BLEND_IDS[bm] !== undefined) {
                next = this.acquire(outW, outH);
                const pb = this.program('blend');
                this.draw(pb, next, quant, l => {
                    this.bindTex(0, out.tex, l.uIn);
                    this.bindTex(1, cur.tex, l.uIn2);
                    gl.uniform1i(l.uMode!, BLEND_IDS[bm]);
                });
                this.release(out);
            }
            if (cur !== src) this.release(cur);
            cur = next;
        }
        if (cur === src) {
            // output must be a separate texture because lastSource is released independently
            const cp = this.acquire(outW, outH);
            this.draw(this.program('copy'), cp, 0, l => this.bindTex(0, src.tex, l.uIn));
            cur = cp;
        }
        this.lastOutput = cur;
        return cur;
    }

    private runPrim(p: Prim, out: Tex, get: (n: string) => Tex, quant: number, sc: number, opts: ProcessOptions) {
        const gl = this.gl;
        switch (p.op) {
            case 'matrix': {
                const m = p.auto ? (opts.autoMatrix ?? [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0]) : p.m;
                // column-major mat4 of the 4x4 part, offsets from column 5
                const M = new Float32Array([m[0], m[5], m[10], m[15], m[1], m[6], m[11], m[16], m[2], m[7], m[12], m[17], m[3], m[8], m[13], m[18]]);
                this.draw(this.program('matrix'), out, quant, l => {
                    this.bindTex(0, get(p.in).tex, l.uIn);
                    gl.uniformMatrix4fv(l.uM!, false, M);
                    gl.uniform4f(l.uOff!, m[4], m[9], m[14], m[19]);
                });
                break;
            }
            case 'ct': {
                const f = p.f;
                this.draw(this.program('ct'), out, quant, l => {
                    this.bindTex(0, get(p.in).tex, l.uIn);
                    this.bindTex(1, this.ctTableTex(p), l.uTable);
                    gl.uniform4i(l.uType!, FUNC_IDS[f[0].type], FUNC_IDS[f[1].type], FUNC_IDS[f[2].type], FUNC_IDS[f[3].type]);
                    gl.uniform4i(l.uLen!, f[0].table?.length ?? 0, f[1].table?.length ?? 0, f[2].table?.length ?? 0, f[3].table?.length ?? 0);
                    gl.uniform4f(l.uSlope!, f[0].slope ?? 1, f[1].slope ?? 1, f[2].slope ?? 1, f[3].slope ?? 1);
                    gl.uniform4f(l.uInter!, f[0].intercept ?? 0, f[1].intercept ?? 0, f[2].intercept ?? 0, f[3].intercept ?? 0);
                    gl.uniform4f(l.uAmp!, f[0].amplitude ?? 1, f[1].amplitude ?? 1, f[2].amplitude ?? 1, f[3].amplitude ?? 1);
                    gl.uniform4f(l.uExp!, f[0].exponent ?? 1, f[1].exponent ?? 1, f[2].exponent ?? 1, f[3].exponent ?? 1);
                    gl.uniform4f(l.uGOff!, f[0].offset ?? 0, f[1].offset ?? 0, f[2].offset ?? 0, f[3].offset ?? 0);
                });
                break;
            }
            case 'blur': {
                const input = get(p.in);
                const sx = p.sx * sc, sy = p.sy * sc;
                const tmp = this.acquire(out.w, out.h);
                const pass = (from: Tex, to: Tex, dir: [number, number], sigma: number) => {
                    const radius = sigma > 0 ? Math.min(Math.ceil(sigma * 3), 192) : 0;
                    this.draw(this.program('blur'), to, quant, l => {
                        this.bindTex(0, from.tex, l.uIn);
                        gl.uniform2i(l.uDir!, dir[0], dir[1]);
                        gl.uniform1f(l.uSigma!, Math.max(sigma, 1e-4));
                        gl.uniform1i(l.uRadius!, radius);
                    });
                };
                pass(input, tmp, [1, 0], sx);
                pass(tmp, out, [0, 1], sy);
                this.release(tmp);
                break;
            }
            case 'arith':
                this.draw(this.program('arith'), out, quant, l => {
                    this.bindTex(0, get(p.in).tex, l.uIn);
                    this.bindTex(1, get(p.in2).tex, l.uIn2);
                    gl.uniform4f(l.uK!, p.k1, p.k2, p.k3, p.k4);
                });
                break;
            case 'composite':
                this.draw(this.program('composite'), out, quant, l => {
                    this.bindTex(0, get(p.in).tex, l.uIn);
                    this.bindTex(1, get(p.in2).tex, l.uIn2);
                    gl.uniform1i(l.uOp!, COMPOSITE_IDS[p.operator] ?? 0);
                });
                break;
            case 'blend':
                this.draw(this.program('blend'), out, quant, l => {
                    this.bindTex(0, get(p.in).tex, l.uIn);
                    this.bindTex(1, get(p.in2).tex, l.uIn2);
                    gl.uniform1i(l.uMode!, BLEND_IDS[p.mode] ?? 0);
                });
                break;
            case 'convolve': {
                const k = new Float32Array(81);
                p.kernel.forEach((v, i) => { if (i < 81) k[i] = v; });
                this.draw(this.program('convolve'), out, quant, l => {
                    this.bindTex(0, get(p.in).tex, l.uIn);
                    gl.uniform2i(l.uOrder!, p.orderX, p.orderY);
                    gl.uniform2i(l.uTarget!, p.targetX, p.targetY);
                    gl.uniform1fv(l.uKernel!, k);
                    gl.uniform1f(l.uDivisor!, p.divisor || 1);
                    gl.uniform1f(l.uBias!, p.bias);
                    gl.uniform1i(l.uEdge!, p.edgeMode === 'duplicate' ? 0 : p.edgeMode === 'wrap' ? 1 : 2);
                    gl.uniform1i(l.uPreserve!, p.preserveAlpha ? 1 : 0);
                });
                break;
            }
            case 'turbulence':
                this.draw(this.program('turbulence'), out, quant, l => {
                    this.bindTex(0, this.latticeTex(p.seed), l.uLattice);
                    gl.uniform2f(l.uFreq!, p.fx, p.fy);
                    gl.uniform1i(l.uOctaves!, p.octaves);
                    gl.uniform1i(l.uFractal!, p.fractal ? 1 : 0);
                    gl.uniform1f(l.uPxScale!, sc);
                });
                break;
            case 'flood':
                this.draw(this.program('flood'), out, quant, l => gl.uniform4f(l.uColor!, p.color[0], p.color[1], p.color[2], p.opacity));
                break;
            case 'offset':
                this.draw(this.program('offset'), out, quant, l => {
                    this.bindTex(0, get(p.in).tex, l.uIn);
                    gl.uniform2f(l.uD!, p.dx * sc, p.dy * sc);
                });
                break;
            case 'morphology':
                this.draw(this.program('morphology'), out, quant, l => {
                    this.bindTex(0, get(p.in).tex, l.uIn);
                    gl.uniform2i(l.uR!, Math.min(16, Math.round(p.rx * sc)), Math.min(16, Math.round(p.ry * sc)));
                    gl.uniform1i(l.uErode!, p.erode ? 1 : 0);
                });
                break;
            case 'merge': {
                // successive "over" composites, first input at the bottom
                const ins = p.ins.map(get);
                if (!ins.length) { this.draw(this.program('flood'), out, quant, l => gl.uniform4f(l.uColor!, 0, 0, 0, 0)); break; }
                let acc = this.acquire(out.w, out.h);
                this.draw(this.program('copy'), acc, quant, l => this.bindTex(0, ins[0].tex, l.uIn));
                for (let i = 1; i < ins.length; i++) {
                    const nx = i === ins.length - 1 ? out : this.acquire(out.w, out.h);
                    this.draw(this.program('composite'), nx, quant, l => {
                        this.bindTex(0, ins[i].tex, l.uIn);
                        this.bindTex(1, acc.tex, l.uIn2);
                        gl.uniform1i(l.uOp!, 0);
                    });
                    this.release(acc);
                    acc = nx;
                }
                if (acc !== out) { this.draw(this.program('copy'), out, quant, l => this.bindTex(0, acc.tex, l.uIn)); this.release(acc); }
                break;
            }
            case 'displacement':
                this.draw(this.program('displacement'), out, quant, l => {
                    this.bindTex(0, get(p.in).tex, l.uIn);
                    this.bindTex(1, get(p.in2).tex, l.uIn2);
                    gl.uniform1f(l.uScale!, p.scale * sc);
                    gl.uniform2i(l.uCh!, p.xch, p.ych);
                });
                break;
        }
    }

    /** GvfFrameAnalyzer equivalent: 64x36 statistics of the source frame. */
    private frameStats(src: Tex): FrameStatsU {
        const gl = this.gl;
        if (!this.statsTex) this.statsTex = this.createTex(64, 36, 'u8');
        const p = this.program('import');
        gl.useProgram(p.prog);
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.statsTex.fb);
        gl.viewport(0, 0, 64, 36);
        gl.uniform1f(p.loc.uQuant!, 0);
        this.bindTex(0, src.tex, p.loc.uIn);
        gl.bindVertexArray(this.vao);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        const d = new Uint8Array(64 * 36 * 4);
        gl.readPixels(0, 0, 64, 36, gl.RGBA, gl.UNSIGNED_BYTE, d);
        let sr = 0, sg = 0, sb = 0, sl = 0, mn = 1, mx = 0;
        const n = 64 * 36;
        for (let i = 0; i < d.length; i += 4) {
            const r = d[i] / 255, g = d[i + 1] / 255, b = d[i + 2] / 255;
            const l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            sr += r; sg += g; sb += b; sl += l;
            if (l < mn) mn = l;
            if (l > mx) mx = l;
        }
        return { avg_lum: sl / n, avg_r: sr / n, avg_g: sg / n, avg_b: sb / n, contrast: mx - mn };
    }

    // ------------------------------------------------------------------ output
    /** Reads the processed frame as RGBA8 (row 0 = top). */
    readRGBA8(t: Tex, into?: Uint8Array): Uint8Array {
        const gl = this.gl;
        const o = this.acquire(t.w, t.h, 'u8');
        this.draw(this.program('out8'), o, 0, l => this.bindTex(0, t.tex, l.uIn));
        const buf = into && into.length === t.w * t.h * 4 ? into : new Uint8Array(t.w * t.h * 4);
        gl.bindFramebuffer(gl.FRAMEBUFFER, o.fb);
        gl.readPixels(0, 0, t.w, t.h, gl.RGBA, gl.UNSIGNED_BYTE, buf);
        this.release(o);
        return buf;
    }

    /** Reads the processed frame as RGBA 16-bit (for 10-bit encodes). */
    readRGBA16(t: Tex, into?: Uint16Array): Uint16Array {
        const gl = this.gl;
        const o = this.acquire(t.w, t.h, 'u16');
        this.draw(this.program('out16'), o, 0, l => this.bindTex(0, t.tex, l.uIn));
        const buf = into && into.length === t.w * t.h * 4 ? into : new Uint16Array(t.w * t.h * 4);
        gl.bindFramebuffer(gl.FRAMEBUFFER, o.fb);
        gl.readPixels(0, 0, t.w, t.h, gl.RGBA_INTEGER, gl.UNSIGNED_SHORT, buf);
        this.release(o);
        return buf;
    }

    /** Small readback of the processed or original frame for the scopes (not a user-facing capture). */
    readScopeSample(t: Tex, w: number, h: number): Uint8Array {
        const gl = this.gl;
        const o = this.acquire(w, h, 'u8');
        const p = this.program('import');
        gl.useProgram(p.prog);
        gl.bindFramebuffer(gl.FRAMEBUFFER, o.fb);
        gl.viewport(0, 0, w, h);
        gl.uniform1f(p.loc.uQuant!, 0);
        this.bindTex(0, t.tex, p.loc.uIn);
        gl.bindVertexArray(this.vao);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        const buf = new Uint8Array(w * h * 4);
        gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, buf);
        this.release(o);
        return buf;
    }

    /** Draws to the default framebuffer: mode 0 = enhanced, 1 = original, 2 = split wipe. */
    present(canvasW: number, canvasH: number, mode: 0 | 1 | 2, split: number) {
        const gl = this.gl;
        if (!this.lastOutput || !this.lastSource) return;
        const p = this.program('present');
        gl.useProgram(p.prog);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, canvasW, canvasH);
        this.bindTex(0, this.lastSource.tex, p.loc.uOrig);
        this.bindTex(1, this.lastOutput.tex, p.loc.uProc);
        gl.uniform1f(p.loc.uSplit!, split);
        gl.uniform1i(p.loc.uMode!, mode);
        gl.uniform2f(p.loc.uView!, canvasW, canvasH);
        gl.bindVertexArray(this.vao);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    dispose() {
        const gl = this.gl;
        this.pool.forEach(t => this.destroyTex(t));
        this.pool = [];
        [this.lastSource, this.lastOutput, this.statsTex].forEach(t => t && this.destroyTex(t));
        for (const t of this.ctTables.values()) gl.deleteTexture(t);
        for (const t of this.lattices.values()) gl.deleteTexture(t);
        for (const p of this.programs.values()) gl.deleteProgram(p.prog);
        for (const c of this.customs) if (c.prog) gl.deleteProgram(c.prog.prog);
        if (this.srcTex8) gl.deleteTexture(this.srcTex8);
        if (this.srcTex16) gl.deleteTexture(this.srcTex16);
    }
}
