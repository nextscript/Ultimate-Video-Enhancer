// Custom filter system: schema, import/export (gvf-custom-filter-codes), GLSL annotation
// parsing and source normalization ported from the userscript (source lines ~775, ~1029-1150,
// ~1623-1913, ~3726), plus a safe SVG primitive parser (no DOM, no script execution).
import type { BlendMode, Prim, TransferFunc } from './graph';
import { matHueRotateSpec, matLuminanceToAlpha, matSaturateSpec } from './matrix';

export type CustomFilterType = 'svg' | 'webgl' | 'canvas2d' | 'audio';

export interface CustomFilterEntry {
    id: string;
    label: string;
    code: string;
    type: CustomFilterType;
    enabled: boolean;
    blendMode: string;
    tags: string[];
    category: string;
    description?: string;
    uniforms: Record<string, number>;
    params?: Record<string, unknown>;
    hotkey?: string;
    /** Unknown non-dangerous fields from imported files are preserved on round-trip. */
    extra?: Record<string, unknown>;
}

export const SUPPORTED_TYPES: CustomFilterType[] = ['svg', 'webgl'];

export function isTypeSupported(t: CustomFilterType) { return SUPPORTED_TYPES.includes(t); }

export const BLEND_MODES: string[] = ['normal', 'multiply', 'screen', 'overlay', 'darken', 'lighten', 'color-dodge', 'color-burn',
    'hard-light', 'soft-light', 'difference', 'exclusion', 'hue', 'saturation', 'color', 'luminosity'];

const KNOWN_KEYS = new Set(['id', 'label', 'code', 'type', 'enabled', 'blendMode', 'tags', 'category', 'description', 'uniforms', 'params', 'hotkey']);
const MAX_CODE = 512 * 1024;

export function newFilterId() { return 'csvg_' + Date.now() + '_' + Math.random().toString(36).slice(2, 7); }

export function detectType(code: string, declared?: unknown): CustomFilterType {
    if (declared === 'svg' || declared === 'webgl' || declared === 'canvas2d' || declared === 'audio') return declared;
    return (/^\s*#version\s+300\s+es/m.test(code) || /\bvoid\s+main\s*\(/m.test(code) || /\buniform\s+sampler2D\b/m.test(code)) ? 'webgl' : 'svg';
}

/** Normalizes one imported record (userscript _importFilterCodesFromText rules). Returns null when invalid. */
export function normalizeEntry(e: unknown): CustomFilterEntry | null {
    if (!e || typeof e !== 'object') return null;
    const o = e as Record<string, unknown>;
    if (typeof o.label !== 'string' || !o.label.trim() || typeof o.code !== 'string' || !o.code.trim()) return null;
    if (o.code.length > MAX_CODE) return null;
    const uniforms: Record<string, number> = {};
    if (o.uniforms && typeof o.uniforms === 'object') {
        for (const [k, v] of Object.entries(o.uniforms as Record<string, unknown>)) {
            const n = Number(v);
            if (/^\w{1,64}$/.test(k) && Number.isFinite(n)) uniforms[k] = n;
        }
    }
    const extra: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(o)) {
        if (!KNOWN_KEYS.has(k) && /^\w{1,40}$/.test(k) && (typeof v === 'string' || typeof v === 'number' || typeof v === 'boolean')) extra[k] = v;
    }
    return {
        id: typeof o.id === 'string' && /^[\w.-]{1,80}$/.test(o.id) ? o.id : newFilterId(),
        label: o.label.trim().slice(0, 120),
        code: o.code,
        type: detectType(o.code, o.type),
        enabled: o.enabled !== false,
        blendMode: BLEND_MODES.includes(String(o.blendMode)) ? String(o.blendMode) : 'normal',
        tags: Array.isArray(o.tags) ? o.tags.map(String).slice(0, 32) : (typeof o.tags === 'string' ? o.tags.split(',').map(t => t.trim()).filter(Boolean) : []),
        category: o.category ? String(o.category).trim().slice(0, 60) : '',
        description: typeof o.description === 'string' ? o.description.slice(0, 2000) : undefined,
        uniforms,
        params: o.params && typeof o.params === 'object' ? JSON.parse(JSON.stringify(o.params)) : undefined,
        hotkey: typeof o.hotkey === 'string' ? o.hotkey.slice(0, 20) : undefined,
        extra: Object.keys(extra).length ? extra : undefined
    };
}

/** Accepts an array, a { codes: [] } export or a single record. */
export function parseFilterCodesJson(text: string): CustomFilterEntry[] {
    const parsed = JSON.parse(text);
    const incoming = Array.isArray(parsed) ? parsed : (parsed && Array.isArray(parsed.codes)) ? parsed.codes : (parsed && parsed.code) ? [parsed] : [];
    return (incoming as unknown[]).map(normalizeEntry).filter((x): x is CustomFilterEntry => !!x);
}

export function exportFilterCodesJson(entries: CustomFilterEntry[]) {
    return {
        schema: 'gvf-custom-filter-codes', ver: 1, exportedAt: Date.now(),
        codes: entries.map(e => {
            const { extra, ...rest } = e;
            return { ...(extra || {}), ...rest };
        })
    };
}

// ---------------------------------------------------------------------------
// GLSL annotations
// ---------------------------------------------------------------------------
export interface UniformDef {
    name: string;
    def: number;
    min?: number;
    max?: number;
    label: string;
    kind: 'slider' | 'select';
    options?: { value: number; label: string }[];
}

/** parseUniformDefs(): @uniform sliders, numeric #define sliders, @select dropdowns. */
export function parseUniformDefs(src: string): UniformDef[] {
    if (!src) return [];
    const defs: (UniformDef & { _pos: number })[] = [];
    const hasDef = (name: string) => defs.some(d => d.name === name);
    const addSlider = (name: string, def: number, min: number, max: number, label: string, pos: number) => {
        if (!name || hasDef(name) || !Number.isFinite(def)) return;
        defs.push({ name, def, min, max, label: label || name, kind: 'slider', _pos: pos });
    };
    const re = /\/\/\s*@uniform\s+float\s+(\w+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)(?:\s+"([^"]*)")?/g;
    let m: RegExpExecArray | null;
    while ((m = re.exec(src)) !== null) addSlider(m[1], parseFloat(m[2]), parseFloat(m[3]), parseFloat(m[4]), m[5] || m[1], m.index);

    const defRe = /^\s*#define\s+([A-Za-z_]\w*)\s+(-?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)(?:f)?(?:\s*(?:\/\/\s*(.*))?)?$/mg;
    let dm: RegExpExecArray | null;
    while ((dm = defRe.exec(src)) !== null) {
        const rawName = dm[1];
        if (/^(PI|M_PI|E|TAU)$/i.test(rawName) || /^P[0-9]+$/i.test(rawName)) continue;
        const val = parseFloat(dm[2]);
        if (!Number.isFinite(val)) continue;
        let min = 0.0, max = Math.max(2.0, Math.abs(val) * 2.0);
        if (val < 0) { min = val * 2.0; max = Math.abs(val) * 2.0; }
        if (val > 0 && val <= 1.0) { min = 0.0; max = 2.0; }
        if (/threshold|thresh|cutoff|limit|scale|strength|curve|amount|blend|mix|gain|sharp/i.test(rawName)) { min = 0.0; max = Math.max(2.0, Math.abs(val) * 2.0); }
        if (/^pattern$/i.test(rawName)) { min = 1.0; max = 9.0; }
        addSlider('u_def_' + rawName, val, min, max, rawName.replace(/_/g, ' '), dm.index);
    }

    const re2 = /\/\/\s*@select\s+float\s+(\w+)\s+([\d.eE+-]+)\s+"([^"]*)"\s+([\d.,:\w\s()-]+)/g;
    let m2: RegExpExecArray | null;
    while ((m2 = re2.exec(src)) !== null) {
        const options = m2[4].trim().split(',').map(o => {
            const [val, ...rest] = o.trim().split(':');
            return { value: parseFloat(val), label: rest.join(':').trim() };
        }).filter(o => !isNaN(o.value));
        if (options.length > 0 && !hasDef(m2[1])) defs.push({ name: m2[1], def: parseFloat(m2[2]), label: m2[3], kind: 'select', options, _pos: m2.index });
    }
    defs.sort((a, b) => a._pos - b._pos);
    return defs.map(({ _pos, ...d }) => { void _pos; return d; });
}

/** mpv / libplacebo hook shaders need the userscript's multi-pass compatibility layer (not ported). */
export function isMpvHookShader(src: string) {
    return /^\s*\/\/!\s*(?:HOOK|BIND|SAVE|DESC)\b/m.test(src) || /\bHOOKED_(?:tex|texOff|pos|pt|size)\b/.test(src);
}

/** _normalizeUserFrag() without the mpv compatibility layer. */
export function normalizeUserFrag(src: string): string {
    const uniformNames = parseUniformDefs(src).map(d => d.name);
    let s = src.replace(/^\s*\/\/\s*@uniform[^\r\n]*/mg, '').replace(/^\s*\/\/\s*@select[^\r\n]*/mg, '');
    // Numeric #define sliders stay plain defines (the userscript only rewires them for mpv hook shaders).
    uniformNames.forEach(name => {
        s = s.replace(new RegExp('^\\s*#define\\s+' + name + '\\b[^\\r\\n]*', 'mg'), '');
        s = s.replace(new RegExp('^\\s*(?:const\\s+)?float\\s+' + name + '\\s*=[^;]+;[^\\r\\n]*', 'mg'), '');
    });
    s = s.replace(/^\s*#version\s+\S+[\t ]*(?:es)?/mg, '');
    s = s.replace(/^\s*precision\s+\w+\s+\w+\s*;\s*/mg, '');
    for (const decl of [
        /^\s*uniform\s+sampler2D\s+u_video\s*;\s*/mg, /^\s*uniform\s+sampler2D\s+u_video_raw\s*;\s*/mg,
        /^\s*uniform\s+vec2\s+u_res\s*;\s*/mg, /^\s*uniform\s+float\s+u_time\s*;\s*/mg, /^\s*uniform\s+vec2\s+u_mouse\s*;\s*/mg,
        /^\s*uniform\s+float\s+u_strength\s*;\s*/mg, /^\s*uniform\s+float\s+u_layers\s*;\s*/mg, /^\s*uniform\s+float\s+u_zoom\s*;\s*/mg,
        /^\s*uniform\s+float\s+u_avg_lum\s*;\s*/mg, /^\s*uniform\s+float\s+u_avg_r\s*;\s*/mg, /^\s*uniform\s+float\s+u_avg_g\s*;\s*/mg,
        /^\s*uniform\s+float\s+u_avg_b\s*;\s*/mg, /^\s*uniform\s+float\s+u_contrast\s*;\s*/mg, /^\s*in\s+vec2\s+v_uv\s*;\s*/mg,
        /^\s*out\s+vec4\s+(?:fragColor|outColor)\s*;\s*/mg
    ]) s = s.replace(decl, '');
    s = s.replace(/\bgl_FragColor\b/g, 'fragColor').replace(/\bgl_FragData\s*\[\s*\d+\s*\]/g, 'fragColor')
        .replace(/\btexture2D\b/g, 'texture').replace(/\btextureCube\b/g, 'texture')
        .replace(/\btexture2DLod(?:EXT)?\b/g, 'textureLod').replace(/\btexture2DProj\b/g, 'textureProj')
        .replace(/\bvarying\b/g, 'in').replace(/\battribute\b/g, 'in')
        .replace(/\biChannel1\b/g, 'u_video_raw').replace(/\biChannel0\b/g, 'u_video')
        .replace(/\biResolution\b/g, 'vec3(u_res, 0.0)').replace(/\biTime\b/g, 'u_time')
        .replace(/\bvTexCoord\b/g, 'v_uv').replace(/\bvTextureCoord\b/g, 'v_uv').replace(/\btexCoord\b/g, 'v_uv');
    s = s.replace(/^\s*(?:varying|in)\s+vec[234]\s+v_uv\s*;\s*/mg, '');
    if (/\bmainImage\s*\(/.test(s) && !/\bvoid\s+main\s*\(/.test(s)) s += '\nvoid main(){\n    mainImage(fragColor, v_uv * u_res);\n}';
    s = s.replace(/\boutColor\b/g, 'fragColor');
    return s;
}

export const GLSL_HEADER_LINES = 22;

/** _buildFragSrc(): full GLSL ES 3.00 fragment source for one custom pass. */
export function buildFragSrc(userSrc: string): { src: string; headerLines: number } {
    const decls = parseUniformDefs(userSrc).filter(d => !/^u_gvf_/.test(d.name)).map(d => `uniform float ${d.name};`);
    const body = normalizeUserFrag(userSrc);
    let mainBlock: string;
    if (body.includes('void main')) {
        mainBlock = body;
    } else {
        const fnRe = /\b(vec4|vec3|vec2|float|void)\s+(\w+)\s*\(([^)]*)\)/g;
        let lastFn: { ret: string; name: string; params: string } | null = null, m: RegExpExecArray | null;
        while ((m = fnRe.exec(body)) !== null) lastFn = { ret: m[1], name: m[2], params: m[3] };
        if (lastFn && lastFn.ret !== 'void') {
            const argMap: Record<string, string> = { sampler2D: 'u_video', float: '1.0', vec3: 'vec3(1.0)', vec4: 'vec4(1.0)', int: '1', bool: 'false' };
            let vec2Count = 0;
            const args = lastFn.params.split(',').map(p => {
                p = p.trim(); if (!p) return '';
                const type = p.split(/\s+/)[0];
                if (type === 'vec2') return vec2Count++ === 0 ? 'v_uv' : 'u_res';
                return argMap[type] !== undefined ? argMap[type] : '0.0';
            }).filter(Boolean).join(', ');
            const call = `${lastFn.name}(${args})`;
            const assign = lastFn.ret === 'vec4' ? `fragColor = ${call};` : lastFn.ret === 'vec3' ? `fragColor = vec4(${call}, 1.0);`
                : `float _r = ${call}; fragColor = vec4(_r, _r, _r, 1.0);`;
            mainBlock = `${body}\nvoid main(){\n    ${assign}\n}`;
        } else {
            mainBlock = `void main(){\n${body}\n}`;
        }
    }
    const header = [
        '#version 300 es',
        'precision highp float;',
        'precision highp sampler2D;',
        'uniform sampler2D u_video;',
        'uniform sampler2D u_video_raw;',
        'uniform vec2 u_res;',
        'uniform float u_time;',
        'uniform vec2 u_mouse;',
        'uniform float u_strength;',
        'uniform float u_layers;',
        'uniform float u_zoom;',
        'uniform float u_avg_lum;',
        'uniform float u_avg_r;',
        'uniform float u_avg_g;',
        'uniform float u_avg_b;',
        'uniform float u_contrast;',
        'uniform float u_gvf_mix;',
        'uniform float u_gvf_offset_scale;',
        'in vec2 v_uv;',
        'out vec4 fragColor;',
        'vec4 gvfPassTex(vec2 uv) { return texture(u_video, uv); }',
        'vec4 gvfPassTexOff(vec2 offsetPx) { return texture(u_video, v_uv + ((offsetPx * u_gvf_offset_scale) / u_res)); }',
        ...decls
    ];
    return { src: header.join('\n') + '\n' + mainBlock, headerLines: header.length };
}

/** Static safety checks for user GLSL: limits size and rejects constructs that cannot be bounded. */
export function lintGlsl(src: string): string[] {
    const errs: string[] = [];
    if (src.length > MAX_CODE) errs.push('Shader source is larger than 512 KB.');
    if (isMpvHookShader(src)) errs.push('mpv/libplacebo hook shaders (//!HOOK) need the userscript multi-pass compatibility layer, which is not available in UVE Manager 1.0.0.');
    const loops = src.match(/for\s*\([^;]*;[^;]*<\s*(\d+)/g) || [];
    for (const l of loops) {
        const n = Number((l.match(/(\d+)\s*$/) || [])[1]);
        if (n > 4096) errs.push(`Loop bound ${n} exceeds the GPU watchdog limit (4096).`);
    }
    return errs;
}

// ---------------------------------------------------------------------------
// SVG primitive parser (safe subset; no external references, no feImage)
// ---------------------------------------------------------------------------
interface Tag { name: string; attrs: Record<string, string>; children: Tag[] }

function parseTags(code: string): Tag[] {
    const roots: Tag[] = [];
    const stack: Tag[] = [];
    const tagRe = /<(\/?)([a-zA-Z][a-zA-Z0-9]*)((?:\s+[^>]*?)?)\s*(\/?)>/g;
    let m: RegExpExecArray | null;
    while ((m = tagRe.exec(code)) !== null) {
        const closing = m[1] === '/';
        const name = m[2];
        if (closing) {
            while (stack.length && stack[stack.length - 1].name !== name) stack.pop();
            stack.pop();
            continue;
        }
        const attrs: Record<string, string> = {};
        const attrRe = /([a-zA-Z][a-zA-Z0-9_:-]*)\s*=\s*(?:"([\s\S]*?)"|'([\s\S]*?)')/g;
        let am: RegExpExecArray | null;
        while ((am = attrRe.exec(m[3] || '')) !== null) {
            attrs[am[1]] = (am[2] !== undefined ? am[2] : am[3]).replace(/[\r\n\t]+/g, ' ').replace(/ {2,}/g, ' ').trim();
        }
        const tag: Tag = { name, attrs, children: [] };
        const parent = stack[stack.length - 1];
        if (parent && (parent.name === 'feComponentTransfer' || parent.name === 'feMerge')) parent.children.push(tag);
        else if (name !== 'filter' && name !== 'svg' && name !== 'defs') roots.push(tag);
        if (m[4] !== '/' && (name === 'feComponentTransfer' || name === 'feMerge' || name === 'filter' || name === 'svg' || name === 'defs')) stack.push(tag);
    }
    return roots;
}

const nums = (s: string | undefined): number[] => (s || '').trim().split(/[\s,]+/).filter(Boolean).map(Number).filter(Number.isFinite);
const num = (s: string | undefined, d: number) => { const n = Number(s); return s !== undefined && s !== '' && Number.isFinite(n) ? n : d; };

function parseColor(c: string | undefined): [number, number, number] {
    const v = (c || 'black').trim().toLowerCase();
    const named: Record<string, [number, number, number]> = { black: [0, 0, 0], white: [1, 1, 1], red: [1, 0, 0], green: [0, 0.5, 0], blue: [0, 0, 1], gray: [0.5, 0.5, 0.5], grey: [0.5, 0.5, 0.5] };
    if (named[v]) return named[v];
    let m = v.match(/^#([0-9a-f]{3})$/);
    if (m) return [0, 1, 2].map(i => parseInt(m![1][i] + m![1][i], 16) / 255) as [number, number, number];
    m = v.match(/^#([0-9a-f]{6})$/);
    if (m) return [0, 2, 4].map(i => parseInt(m![1].slice(i, i + 2), 16) / 255) as [number, number, number];
    m = v.match(/^rgba?\(([^)]+)\)$/);
    if (m) {
        const p = m[1].split(',').map(x => x.trim());
        return [0, 1, 2].map(i => p[i]?.endsWith('%') ? parseFloat(p[i]) / 100 : (Number(p[i]) || 0) / 255) as [number, number, number];
    }
    return [0, 0, 0];
}

function transferFunc(t: Tag | undefined): TransferFunc {
    if (!t) return { type: 'identity' };
    const type = (t.attrs.type || 'identity') as TransferFunc['type'];
    if (type === 'table' || type === 'discrete') {
        const tv = nums(t.attrs.tableValues);
        return tv.length ? { type, table: tv.slice(0, 4096) } : { type: 'identity' };
    }
    if (type === 'linear') return { type, slope: num(t.attrs.slope, 1), intercept: num(t.attrs.intercept, 0) };
    if (type === 'gamma') return { type, amplitude: num(t.attrs.amplitude, 1), exponent: num(t.attrs.exponent, 1), offset: num(t.attrs.offset, 0) };
    return { type: 'identity' };
}

const STD_INPUTS = new Set(['SourceGraphic', 'SourceAlpha']);
const MAX_PRIMS = 64;

/**
 * Converts custom SVG filter code into graph primitives. Result names are namespaced so that
 * references inside the snippet keep working; unknown references fall back to the previous result
 * (SVG spec behaviour). Unsupported primitives are reported, never silently dropped.
 */
export function parseCustomSvgPrimitives(code: string, inputName: string, prefix: string): { prims: Prim[]; output: string; errors: string[] } {
    const errors: string[] = [];
    const prims: Prim[] = [];
    const nameMap: Record<string, string> = {};
    let last = inputName;
    const tags = parseTags(String(code || '')).slice(0, MAX_PRIMS);
    const ref = (n: string | undefined): string => {
        if (!n) return last;
        if (n === 'SourceGraphic') return 'SourceGraphic';
        if (n === 'SourceAlpha') return 'SourceAlpha';
        if (nameMap[n]) return nameMap[n];
        if (STD_INPUTS.has(n)) return n;
        return last; // BackgroundImage/FillPaint/unknown -> previous result
    };
    tags.forEach((t, i) => {
        const out = `${prefix}${i}`;
        const a = t.attrs;
        const inp = ref(a.in);
        let p: Prim | null = null;
        switch (t.name) {
            case 'feColorMatrix': {
                const type = a.type || 'matrix';
                if (type === 'matrix') {
                    const v = nums(a.values);
                    p = { op: 'matrix', in: inp, out, m: v.length === 20 ? v : [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0] };
                } else if (type === 'saturate') p = { op: 'matrix', in: inp, out, m: matSaturateSpec(num(a.values, 1)) };
                else if (type === 'hueRotate') p = { op: 'matrix', in: inp, out, m: matHueRotateSpec(num(a.values, 0)) };
                else if (type === 'luminanceToAlpha') p = { op: 'matrix', in: inp, out, m: matLuminanceToAlpha() };
                break;
            }
            case 'feComponentTransfer': {
                const by = (n: string) => t.children.find(c => c.name === n);
                p = { op: 'ct', in: inp, out, f: [transferFunc(by('feFuncR')), transferFunc(by('feFuncG')), transferFunc(by('feFuncB')), transferFunc(by('feFuncA'))] };
                break;
            }
            case 'feGaussianBlur': {
                const sd = nums(a.stdDeviation);
                const sx = Math.min(Math.max(sd[0] ?? 0, 0), 64), sy = Math.min(Math.max(sd[1] ?? sd[0] ?? 0, 0), 64);
                p = { op: 'blur', in: inp, out, sx, sy };
                break;
            }
            case 'feComposite': {
                const op = a.operator || 'over';
                if (op === 'arithmetic') p = { op: 'arith', in: inp, in2: ref(a.in2), out, k1: num(a.k1, 0), k2: num(a.k2, 0), k3: num(a.k3, 0), k4: num(a.k4, 0) };
                else if (['over', 'in', 'out', 'atop', 'xor', 'lighter'].includes(op)) p = { op: 'composite', in: inp, in2: ref(a.in2), out, operator: op as 'over' };
                break;
            }
            case 'feBlend': {
                const mode = (a.mode || 'normal') as BlendMode;
                p = { op: 'blend', in: inp, in2: ref(a.in2), out, mode: BLEND_MODES.includes(mode) ? mode : 'normal' };
                break;
            }
            case 'feConvolveMatrix': {
                const ord = nums(a.order);
                const ox = Math.round(ord[0] ?? 3), oy = Math.round(ord[1] ?? ord[0] ?? 3);
                const k = nums(a.kernelMatrix);
                if (ox < 1 || oy < 1 || ox > 9 || oy > 9 || k.length !== ox * oy) { errors.push(`feConvolveMatrix #${i}: invalid order/kernelMatrix`); break; }
                const sum = k.reduce((s, v) => s + v, 0);
                let divisor = num(a.divisor, sum === 0 ? 1 : sum);
                if (divisor === 0) divisor = 1;
                p = {
                    op: 'convolve', in: inp, out, orderX: ox, orderY: oy, kernel: k, divisor, bias: num(a.bias, 0),
                    targetX: Math.round(num(a.targetX, Math.floor(ox / 2))), targetY: Math.round(num(a.targetY, Math.floor(oy / 2))),
                    edgeMode: (a.edgeMode === 'wrap' || a.edgeMode === 'none') ? a.edgeMode : 'duplicate', preserveAlpha: a.preserveAlpha === 'true'
                };
                break;
            }
            case 'feTurbulence': {
                const bf = nums(a.baseFrequency);
                p = {
                    op: 'turbulence', out, fx: Math.max(0, bf[0] ?? 0), fy: Math.max(0, bf[1] ?? bf[0] ?? 0),
                    octaves: Math.min(Math.max(Math.round(num(a.numOctaves, 1)), 0), 8), seed: num(a.seed, 0), fractal: a.type === 'fractalNoise'
                };
                break;
            }
            case 'feFlood':
                p = { op: 'flood', out, color: parseColor(a['flood-color']), opacity: Math.min(1, Math.max(0, num(a['flood-opacity'], 1))) };
                break;
            case 'feOffset':
                p = { op: 'offset', in: inp, out, dx: num(a.dx, 0), dy: num(a.dy, 0) };
                break;
            case 'feMorphology': {
                const r = nums(a.radius);
                p = { op: 'morphology', in: inp, out, erode: (a.operator || 'erode') === 'erode', rx: Math.min(Math.max(r[0] ?? 0, 0), 16), ry: Math.min(Math.max(r[1] ?? r[0] ?? 0, 0), 16) };
                break;
            }
            case 'feMerge':
                p = { op: 'merge', out, ins: t.children.filter(c => c.name === 'feMergeNode').map(c => ref(c.attrs.in)) };
                break;
            case 'feDisplacementMap': {
                const ch = (c: string | undefined) => ({ R: 0, G: 1, B: 2, A: 3 } as Record<string, number>)[c || 'A'] ?? 3;
                p = { op: 'displacement', in: inp, in2: ref(a.in2), out, scale: num(a.scale, 0), xch: ch(a.xChannelSelector), ych: ch(a.yChannelSelector) };
                break;
            }
            case 'feDropShadow': {
                const sd = nums(a.stdDeviation);
                const col = parseColor(a['flood-color']);
                prims.push({ op: 'matrix', in: inp, out: out + '_a', m: [0, 0, 0, 0, col[0], 0, 0, 0, 0, col[1], 0, 0, 0, 0, col[2], 0, 0, 0, Math.min(1, Math.max(0, num(a['flood-opacity'], 1))), 0] });
                prims.push({ op: 'blur', in: out + '_a', out: out + '_b', sx: sd[0] ?? 2, sy: sd[1] ?? sd[0] ?? 2 });
                prims.push({ op: 'offset', in: out + '_b', out: out + '_o', dx: num(a.dx, 2), dy: num(a.dy, 2) });
                p = { op: 'merge', out, ins: [out + '_o', inp] };
                break;
            }
            case 'feFuncR': case 'feFuncG': case 'feFuncB': case 'feFuncA': case 'feMergeNode':
                return; // orphan child, ignored like in the browser
            default:
                errors.push(`<${t.name}> is not supported (allowed: feColorMatrix, feComponentTransfer, feGaussianBlur, feComposite, feBlend, feConvolveMatrix, feTurbulence, feFlood, feOffset, feMorphology, feMerge, feDisplacementMap, feDropShadow)`);
        }
        if (!p) return;
        prims.push(p);
        if (a.result) nameMap[a.result] = out;
        last = out;
    });
    if (parseTags(String(code || '')).length > MAX_PRIMS) errors.push(`Only the first ${MAX_PRIMS} primitives are used.`);
    return { prims, output: last, errors };
}
