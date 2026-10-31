/**
 * Headless consistency contract of the timeline render engines (no GPU needed): the per-clip look
 * matrix law, the clip-prim chain break of `mergeChainedMatrices`, the composite expression against the
 * transition reference, and the parameter lists of both engines' timeline shaders (WGSL and GLSL)
 * against `src/engine/params.ts` and the fixture.
 */

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { dbToLin, makeClip, type PlanClip } from '../shared/timeline';
import { smoothstep, transitionPixelWeights, transitionUniforms, type TransitionUniforms } from '../shared/transitions';
import { TRANSITION_KINDS } from '../shared/types';
import {
    MAX_SLOTS, clipLookMatrix, compositeSteps, effectiveClipMatrix, engineFacade, layerSizeOf, veilColorOf,
    type CompositeEntry, type CompositeOptions, type LayerRenderOptions, type RenderEngine
} from '../shared/uve/engine-facade';
import { mergeChainedMatrices, type Prim } from '../shared/uve/graph';
import { matMul4x5, type Mat4x5 } from '../shared/uve/matrix';
import { GLSL_TIMELINE_SOURCES } from '../src/engine/EffectEngine';
import { WGSL_TIMELINE_SOURCES } from '../src/engine/GpuEngine';
import { ENGINE_PARAMS, SHADER_PARAM_NAMES } from '../src/engine/params';
import { PARAMETER_KINDS, PARAMETER_NAMES } from './fixtures/engine-params';

const F = 10;

function lookAt(t: number, dur: number, trackGainLin: number): Mat4x5 {
    const clip = { ...makeClip({ trackId: 'T', sourceId: 'S', start: 0, dur, srcIn: 0 }), opacity: 0.5, level: 0.25, fadeIn: 1, fadeOut: 1 };
    const pc = { clip, startF: 100 } as unknown as PlanClip;
    return clipLookMatrix(clip, pc, pc.startF + Math.round(t * F), F, trackGainLin);
}

test('(a) clipLookMatrix: RGB carries level x track gain, alpha carries opacity x the fade ramps', () => {
    const g6 = dbToLin(6);
    const rgb = 0.25 * g6;
    // 4 s clip: fadeIn half way at t = 0.5, fadeOut half way at t = 3.5
    for (const t of [0.5, 3.5]) {
        const m = lookAt(t, 4, g6);
        assert.equal(m[0], rgb); assert.equal(m[6], rgb); assert.equal(m[12], rgb);
        assert.equal(m[18], 0.5 * 0.5, 'alpha diagonal');
        for (const i of [1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 19]) assert.equal(m[i], 0, `m[${i}]`);
    }
    assert.equal(lookAt(2, 4, g6)[18], 0.5);
    // 1 s clip at t = 0.5: both ramps at 0.5 -> alpha 0.5 * 0.5 * 0.5; the premultiplied red the composite sees
    const m = lookAt(0.5, 1, g6);
    assert.equal(m[18], 0.5 * 0.5 * 0.5);
    assert.equal(m[0] * m[18], 0.5 * 0.25 * dbToLin(6) * 0.5 * 0.5);
    // `out` is filled in place, every entry rewritten
    const out = new Array<number>(20).fill(7);
    const clip = { ...makeClip({ trackId: 'T', sourceId: 'S', start: 0, dur: 4, srcIn: 0 }), opacity: 0.5, level: 0.25 };
    const r = clipLookMatrix(clip, { clip, startF: 0 } as unknown as PlanClip, 20, F, 1, out);
    assert.equal(r, out);
    assert.deepEqual(out, [0.25, 0, 0, 0, 0, 0, 0.25, 0, 0, 0, 0, 0, 0.25, 0, 0, 0, 0, 0, 0.5, 0]);
});

test('(a) scalar fallback look and layer working size', () => {
    const base: LayerRenderOptions = {
        sourceW: 1920, sourceH: 1080, opacity: 0.5, levelLin: 2, veil: 0, veilColor: 0, autoMatrix: null, clipMatrix: null,
        effectGraph: { prims: [], output: 'SourceGraphic', hasAuto: false, warnings: [] }, lutHandle: null, hwLutHandle: null, lut3dSize: null
    };
    assert.deepEqual(effectiveClipMatrix(base), [2, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0.5, 0]);
    const cm = [2, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0.5, 0];
    assert.equal(effectiveClipMatrix({ ...base, clipMatrix: cm }), cm);
    assert.deepEqual(layerSizeOf(base), { w: 1920, h: 1080 });
    assert.deepEqual(layerSizeOf({ ...base, pxScale: 0.5 }), { w: 960, h: 540 });
    assert.deepEqual(layerSizeOf({ ...base, outW: 640, outH: 360 }), { w: 640, h: 360 });
});

test('(b) mergeChainedMatrices never merges through a clip prim', () => {
    const A: Mat4x5 = [1.1, 0, 0, 0, 0.01, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0];
    const B: Mat4x5 = [1, 0.1, 0, 0, 0, 0, 0.9, 0, 0, 0.02, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0];
    const I: Mat4x5 = [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0];
    const clip: Prim = { op: 'matrix', in: 'b', out: 'c', m: I, clip: true };
    const prims: Prim[] = [
        { op: 'matrix', in: 'SourceGraphic', out: 'a', m: A },
        { op: 'matrix', in: 'a', out: 'b', m: B },
        clip,
        { op: 'matrix', in: 'c', out: 'd', m: B },
        { op: 'matrix', in: 'd', out: 'e', m: A }
    ];
    const merged = mergeChainedMatrices(prims, 'e');
    assert.equal(merged.length, 3);
    assert.equal(merged[1], clip, 'the clip prim survives unchanged');
    const [pre, , post] = merged;
    assert.ok(pre.op === 'matrix' && !pre.clip && pre.in === 'SourceGraphic' && pre.out === 'b');
    assert.ok(post.op === 'matrix' && !post.clip && post.in === 'c' && post.out === 'e');
    assert.deepEqual(pre.m, matMul4x5(B, A));
    assert.deepEqual(post.m, matMul4x5(A, B));
    // no merged (non-clip) matrix swallowed the clip stage
    assert.equal(merged.filter(p => p.op === 'matrix' && p.clip).length, 1);
    // a clip prim directly after / before a plain matrix stays separate as well
    const two = mergeChainedMatrices([{ op: 'matrix', in: 'SourceGraphic', out: 'a', m: A }, { op: 'matrix', in: 'a', out: 'b', m: I, clip: true }], 'b');
    assert.equal(two.length, 2);
});

/** The composite step expression exactly as both shaders write it (tlStep). */
function shaderCoefficients(un: TransitionUniforms, weightA: number, weightB: number, px: number, py: number, W: number, H: number) {
    const L = Math.max(W, H);
    const u = (px + 0.5) / L, v = (py + 0.5) / L;
    let cA = weightA, cB = weightB;
    if (un.mode !== 0) {
        const s = un.mode === 1 ? (un.axis === 0 ? u : v) : Math.hypot(u - un.cx, v - un.cy);
        const g = smoothstep(un.edge - un.feather, un.edge + un.feather, s);
        const geoB = un.highSideIsB ? g : 1 - g;
        cA = weightA * (1 - geoB);
        cB = weightB * geoB;
    }
    return { a: cA, b: cB };
}

test('(c) the composite expression on transitionUniforms reproduces transitionPixelWeights exactly', () => {
    const W = 64, H = 36;
    for (const kind of TRANSITION_KINDS) {
        for (const p of [0, 0.2, 0.5, 1]) {
            const un = transitionUniforms(kind, p, W, H);
            const entries: CompositeEntry[] = [
                { slot: 0, weightA: un.weightA, weightB: un.weightB, uniforms: un, role: 'a' },
                { slot: 1, weightA: un.weightA, weightB: un.weightB, uniforms: un, role: 'b' }
            ];
            const withRoles = compositeSteps(entries);
            const legacy = compositeSteps(entries.map(e => ({ slot: e.slot, weightA: e.weightA, weightB: e.weightB, uniforms: e.uniforms })));
            assert.equal(withRoles.length, 1);
            const st = withRoles[0];
            assert.deepEqual([st.a, st.b, st.over], [0, 1, false]);
            assert.equal(legacy.length, 2);
            assert.deepEqual([legacy[1].a, legacy[1].b], ['acc', 1]);
            for (let py = 0; py < H; py++) {
                for (let px = 0; px < W; px++) {
                    const ref = transitionPixelWeights(kind, p, px, py, W, H);
                    const got = shaderCoefficients(st.uniforms, st.weightA, st.weightB, px, py, W, H);
                    if (got.a !== ref.a || got.b !== ref.b) assert.fail(`${kind} p=${p} px=${px},${py}: ${got.a},${got.b} != ${ref.a},${ref.b}`);
                    const lg = shaderCoefficients(legacy[1].uniforms, legacy[1].weightA, legacy[1].weightB, px, py, W, H);
                    if (lg.a !== ref.a || lg.b !== ref.b) assert.fail(`legacy ${kind} p=${p}`);
                }
            }
            // the veil reaches the picture once, through CompositeOptions
            assert.equal(un.veil, transitionPixelWeights(kind, p, 0, 0, W, H).veil);
            assert.ok(veilColorOf(un.veilColor) === 0 || veilColorOf(un.veilColor) === 1);
        }
    }
    // every uniform the expression needs is a declared engine parameter
    const names = new Set(ENGINE_PARAMS.filter(p => p.block === 'composite').map(p => p.name));
    for (const key of Object.keys(transitionUniforms('dissolve', 0.5, W, H))) assert.ok(names.has(key), `ENGINE_PARAMS lacks ${key}`);
    // both shaders use the language built-in smoothstep on edge +- feather
    assert.match(WGSL_TIMELINE_SOURCES.tlStep, /smoothstep\(u\.edge - u\.feather, u\.edge \+ u\.feather, s\)/);
    assert.match(GLSL_TIMELINE_SOURCES.tlStep, /smoothstep\(uEdge - uFeather, uEdge \+ uFeather, s\)/);
});

test('(c) compositeSteps stacks tracks over each other and pairs a/b layers', () => {
    const un = transitionUniforms('wipeLeft', 0.5, 64, 36);
    const single = transitionUniforms('dissolve', 1, 64, 36);
    const steps = compositeSteps([
        { slot: 0, weightA: 0, weightB: 1, uniforms: single, role: 'single' },
        { slot: 1, weightA: 1, weightB: 1, uniforms: un, role: 'a' },
        { slot: 2, weightA: 1, weightB: 1, uniforms: un, role: 'b' },
        { slot: 3, weightA: 0, weightB: 1, uniforms: single, role: 'single' }
    ]);
    assert.deepEqual(steps.map(s => [s.a, s.b, s.over]), [[null, 0, false], [1, 2, true], [null, 3, true]]);
    assert.equal(steps[1].uniforms, un);
    assert.deepEqual(compositeSteps([{ slot: 2, weightA: 0, weightB: 1, uniforms: single }]).map(s => [s.a, s.b, s.over]), [[null, 2, false]]);
});

/** Shader-declared parameters: name -> kind, with mat4 + vec4 `Off` merged into mat4x5. */
function declared(fields: [string, string][]): Map<string, string> {
    const kinds = new Map<string, string>();
    for (const [name, kind] of fields) kinds.set(name, kind);
    for (const [name, kind] of [...kinds]) {
        if (kind === 'mat4' && kinds.get(name + 'Off') === 'vec4') { kinds.set(name, 'mat4x5'); kinds.delete(name + 'Off'); }
    }
    return kinds;
}
const clean = (n: string) => n.replace(/[_0]+$/, '');
const WGSL_KIND: Record<string, string> = { f32: 'float', i32: 'int', u32: 'int', vec2f: 'vec2', vec3f: 'vec3', vec4f: 'vec4', mat4x4f: 'mat4' };
const GLSL_KIND: Record<string, string> = { float: 'float', int: 'int', bool: 'bool', vec2: 'vec2', vec3: 'vec3', vec4: 'vec4', mat4: 'mat4' };

function wgslParams(): Map<string, string> {
    const fields: [string, string][] = [];
    for (const code of Object.values(WGSL_TIMELINE_SOURCES)) {
        const block = /struct U \{([^}]*)\}/.exec(code);
        assert.ok(block, 'uniform block');
        for (const f of block[1].split(',')) {
            const [name, type] = f.split(':').map(x => x.trim());
            if (!name || /^pad\d*$/.test(name)) continue;
            assert.ok(WGSL_KIND[type], `WGSL type ${type}`);
            fields.push([clean(name), WGSL_KIND[type]]);
        }
    }
    return declared(fields);
}

function glslParams(): Map<string, string> {
    const fields: [string, string][] = [];
    for (const code of Object.values(GLSL_TIMELINE_SOURCES)) {
        for (const m of code.matchAll(/uniform\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+u(\w+)\s*;/g)) {
            const [, type, raw] = m;
            if (/sampler/.test(type)) continue;
            assert.ok(GLSL_KIND[type], `GLSL type ${type}`);
            fields.push([clean(raw[0].toLowerCase() + raw.slice(1)), GLSL_KIND[type]]);
        }
    }
    return declared(fields);
}

/** `bool` parameters are carried as int in both shading languages. */
const compatible = (decl: string, want: string) => decl === want || (want === 'bool' && decl === 'int');

test('(d) both engines declare exactly the fixture parameters with matching kinds', () => {
    for (const [engine, params] of [['webgpu', wgslParams()], ['webgl2', glslParams()]] as const) {
        assert.deepEqual([...params.keys()].sort(), [...PARAMETER_NAMES], `${engine} parameter names`);
        for (const [name, kind] of params) {
            const want = PARAMETER_KINDS[name];
            assert.ok(compatible(kind, want), `${engine} ${name}: ${kind} vs ${want}`);
        }
    }
    assert.deepEqual([...SHADER_PARAM_NAMES].sort(), [...PARAMETER_NAMES]);
    for (const p of ENGINE_PARAMS.filter(x => x.stage === 'shader')) assert.equal(p.kind, PARAMETER_KINDS[p.name], p.name);
    assert.equal(new Set(ENGINE_PARAMS.map(p => p.name)).size, ENGINE_PARAMS.length, 'unique parameter names');
});

test('facade validates slots and forwards to the backend', async () => {
    const calls: string[] = [];
    const fake: RenderEngine = {
        beginTimeline: m => { calls.push(`begin:${m}`); },
        endTimeline: () => { calls.push('end'); },
        timelineReady: true,
        setSource: s => { calls.push(`src:${s}`); },
        renderLayer: s => { calls.push(`layer:${s}`); },
        composite: o => { calls.push(`comp:${o.entries?.length ?? 'null'}`); },
        uploadLut: () => 1,
        uploadLut3D: () => 2,
        readOutput8: () => Promise.resolve(new Uint8Array(4)),
        readOutput16: () => new Uint16Array(4)
    };
    const f = engineFacade(fake, 'export');
    await f.init();
    await f.setSource(MAX_SLOTS - 1, { kind: 'rgba8', data: new Uint8Array(4), w: 1, h: 1 });
    await assert.rejects(f.setSource(MAX_SLOTS, { kind: 'rgba8', data: new Uint8Array(4), w: 1, h: 1 }), RangeError);
    const opts: CompositeOptions = { width: 2, height: 2, entries: null, veil: 0, veilColor: 0 };
    await f.composite(opts);
    await assert.rejects(f.composite({ ...opts, entries: [] }));
    assert.equal((await f.readOutput8()).length, 4);
    assert.equal((await f.readOutput16()).length, 4);
    f.destroy();
    assert.deepEqual(calls, ['begin:export', `src:${MAX_SLOTS - 1}`, 'comp:null', 'end']);
});
