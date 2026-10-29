import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import { unzipSync } from 'fflate';
import { ANALYSIS_H, ANALYSIS_W, autoMatrixAt, computeFrameStats, motionLuma, runAutoSceneMatch, type AnalysisSample } from '../shared/uve/auto';
import { parseCustomSvgPrimitives, parseFilterCodesJson, parseUniformDefs, buildFragSrc, exportFilterCodesJson } from '../shared/uve/customFilters';
import * as C from '../shared/uve/curves';
import { EXPERT_DEFAULTS, expertDefaults, expertDiff, expertFromDiff, importExpert, exportExpert } from '../shared/uve/expert';
import { buildUveGraph, isNothingOn, toneMatrix } from '../shared/uve/graph';
import { cubeTextToMatrix4x5, lutKeyOf, parseLutProfileObject } from '../shared/uve/lut';
import { matIdentity4x5, matMul4x5, matSaturateSpec } from '../shared/uve/matrix';
import { parseUserProfileObject } from '../shared/uve/profiles';
import { DEFAULT_SETTINGS, NEUTRAL_SETTINGS, sanitizeSettings, settingsFromGvfSettings, settingsToGvfSettings } from '../shared/uve/settings';
import { buildTurbulenceLattice } from '../src/engine/turbulence';

const fx = (n: string) => readFileSync(join(process.cwd(), 'tests', 'fixtures', n));
const e = expertDefaults();
const near = (a: number, b: number, eps = 1e-6) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);

test('grading curves keep black and white fixed', () => {
    for (const v of [-10, -5, 3, 10]) {
        const ct = C.userContrastAmount(v, e);
        near(C.contrastCurve(0, ct, e), 0); near(C.contrastCurve(1, ct, e), 1);
        near(C.contrastCurve(0.5, ct, e), 0.5, 1e-9);
        const sh = C.userShadowsAmount(v, e), hi = C.userHighlightsAmount(v, e);
        near(C.shadowsCurve(0, sh, e), 0); near(C.shadowsCurve(1, sh, e), 1);
        near(C.highlightsCurve(0, hi, e), 0); near(C.highlightsCurve(1, hi, e), 1);
        const g = C.userGammaValue(v, e);
        near(C.gammaCurve(0, g, e), 0); near(C.gammaCurve(1, g, e), 1);
    }
});

test('contrast slider +10 gives slope 1.40 at the pivot (perStep 0.04)', () => {
    near(C.userContrastAmount(10, e), 0.4, 1e-9);
    const d = 1e-4, a = C.userContrastAmount(10, e);
    near((C.contrastCurve(0.5 + d, a, e) - C.contrastCurve(0.5 - d, a, e)) / (2 * d), 1.4, 1e-3);
});

test('curves are monotonic for every slider value (no inversion)', () => {
    for (let v = -10; v <= 10; v += 0.5) {
        const sh = C.userShadowsAmount(v, e), hi = C.userHighlightsAmount(v, e), ct = C.userContrastAmount(v, e);
        let p = [-1, -1, -1];
        for (let i = 0; i <= 256; i++) {
            const x = i / 256;
            const y = [C.shadowsCurve(x, sh, e), C.highlightsCurve(x, hi, e), C.contrastCurve(x, ct, e)];
            y.forEach((yy, k) => assert.ok(yy >= p[k] - 1e-9, `curve ${k} not monotonic at v=${v} x=${x}`));
            p = y;
        }
    }
});

test('levels: black slider crushes, negative lifts', () => {
    const L = C.userLevels(10, 0, e);
    near(L.inBlack, 0.12, 1e-9);
    near(C.levelsCurve(0.05, L, e), 0);
    const L2 = C.userLevels(-10, 0, e);
    near(C.levelsCurve(0, L2, e), 0.12, 1e-9);
});

test('nothingOn mirrors the userscript applyFilter() condition', () => {
    assert.equal(isNothingOn(NEUTRAL_SETTINGS), true);
    assert.equal(buildUveGraph({ settings: NEUTRAL_SETTINGS, expert: e, lut: null, customFilters: [] }).prims.length, 0);
    assert.equal(isNothingOn(DEFAULT_SETTINGS), false);
});

test('default graph matches the Chromium SVG combo gvf_sm (moody) structure', () => {
    const g = buildUveGraph({ settings: DEFAULT_SETTINGS, expert: e, lut: null, customFilters: [] });
    const ops = g.prims.map(p => `${p.op}:${p.out}`);
    // sharpen (blur + arithmetic), black offset (BL 1.2), white table (WL 0.2), moody gamma + sat, edge 0.1, auto, tone
    assert.deepEqual(ops.slice(0, 5), ['blur:blur', 'arith:r0', 'ct:r_bl', 'ct:r_wl', 'ct:r1']);
    assert.ok(ops.includes('convolve:r_edge_sx') && ops.includes('blend:r_edge_final'));
    const auto = g.prims.find(p => p.op === 'matrix' && p.auto);
    assert.ok(auto, 'auto matrix node present');
    assert.equal(g.output, 'r_tone');
    const sharpen = g.prims[1] as { k2: number; k3: number };
    near(sharpen.k2, 2); near(sharpen.k3, -1); // SL 1.0 -> A = 1
    const bl = g.prims[2] as { f: { offset?: number }[] };
    near(bl.f[0].offset!, -0.048, 1e-12); // -(1.2) * 0.04
});

test('chained matrices are merged like mergeChainedColorMatrices()', () => {
    const s = { ...DEFAULT_SETTINGS, darkMoody: false, tealOrange: true, vibrantSat: true, edge: 0 };
    const g = buildUveGraph({ settings: s, expert: e, lut: null, customFilters: [] });
    const mats = g.prims.filter(p => p.op === 'matrix' && !p.auto);
    // teal + orange + teal-sat + vibrant collapse into one matrix before the auto node
    assert.equal(mats.length, 2, 'teal/vibrant merged + tone matrix');
});

test('tone matrix = CSS brightness/contrast/saturate composition', () => {
    const t = toneMatrix(DEFAULT_SETTINGS)!;
    const x = 0.3, r = t[0] * x + t[1] * x + t[2] * x + t[4];
    // CSS order: brightness() first, then contrast(); saturate() keeps grey unchanged
    near(r, 1.05 * (1.02 * x) + 0.5 - 0.5 * 1.05, 1e-6);
    assert.equal(toneMatrix({ ...DEFAULT_SETTINGS, enabled: false, profile: 'user' }), null);
});

test('user grading only active with profile "user"', () => {
    const s = { ...DEFAULT_SETTINGS, u_contrast: 5, u_sat: 4, u_hue: 3 };
    const on = buildUveGraph({ settings: s, expert: e, lut: null, customFilters: [] }).prims.map(p => p.out);
    const off = buildUveGraph({ settings: { ...s, profile: 'film' }, expert: e, lut: null, customFilters: [] }).prims.map(p => p.out);
    assert.ok(on.includes('r_utone') && on.some(o => o.startsWith('r_uhue')));
    assert.ok(!off.includes('r_utone'));
    assert.ok(off.includes('r_prof') || off.includes('r_prof_sat'));
});

test('missing LUT is reported, never silently applied', () => {
    const g = buildUveGraph({ settings: { ...DEFAULT_SETTINGS, lutProfile: 'Kodak||Missing' }, expert: e, lut: null, customFilters: [] });
    assert.ok(g.warnings.some(w => /missing/i.test(w)));
    assert.ok(!g.prims.some(p => p.out === 'r_lut'));
});

test('repository profiles import (gvf-settings and user profile formats)', () => {
    const my = JSON.parse(fx('My_Profile.json').toString());
    const s = settingsFromGvfSettings(my);
    assert.equal(s.darkMoody, true);
    assert.equal(s.sl, 1.2);
    assert.equal(s.lutProfile, 'Fujifilm||DCI-P3 Fujifilm 3513DI D55');
    assert.equal(s.u_r_gain, 128);
    const back = settingsFromGvfSettings(settingsToGvfSettings(s));
    assert.deepEqual(back, s);
    const p = parseUserProfileObject(my, 'My_Profile');
    assert.ok(p && p.name === 'My_Profile');
    for (const f of ['GPU_Mode_Profile.json', 'firefox_fix.json']) assert.ok(parseUserProfileObject(JSON.parse(fx(f).toString()), f));
});

test('sanitizeSettings clamps and rejects unknown values', () => {
    const s = sanitizeSettings({ sl: 99, profile: 'evil', u_hue: -50, cbFilter: 'protanopia', foo: 1 });
    assert.equal(s.sl, 2); assert.equal(s.profile, DEFAULT_SETTINGS.profile); assert.equal(s.u_hue, -10); assert.equal(s.cbFilter, 'protanopia');
    assert.ok(!('foo' in s));
});

test('LUTsProfiles_v3.0.zip: every entry is a valid 4x5 matrix profile', () => {
    const files = unzipSync(new Uint8Array(fx('LUTsProfiles_v3.0.zip')));
    const profiles = Object.entries(files).filter(([n]) => n.endsWith('.json')).map(([, d]) => parseLutProfileObject(JSON.parse(Buffer.from(d).toString()), 'repo')).filter(Boolean);
    assert.equal(profiles.length, 83);
    assert.ok(profiles.some(p => lutKeyOf(p!) === 'Fujifilm||DCI-P3 Fujifilm 3513DI D55'), 'profile referenced by My_Profile.json exists');
});

test('.cube → 4x5 matrix fit (identity cube gives identity, NaturalBoost is stable)', () => {
    const N = 5, lines = ['LUT_3D_SIZE 5'];
    for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) lines.push(`${r / 4} ${g / 4} ${b / 4}`);
    const id = cubeTextToMatrix4x5(lines.join('\n')).matrix4x5;
    matIdentity4x5().forEach((v, i) => near(id[i], v, 1e-9));
    const nb = cubeTextToMatrix4x5(fx('NaturalBoost.cube').toString());
    assert.equal(nb.size, 33);
    assert.equal(nb.matrix4x5.length, 20);
    assert.throws(() => cubeTextToMatrix4x5('LUT_3D_SIZE 4\n0 0 0'));
});

test('matrix math: saturate(1) is identity, multiplication associates', () => {
    matSaturateSpec(1).forEach((v, i) => near(v, matIdentity4x5()[i]));
    const a = matSaturateSpec(1.3), b = matSaturateSpec(0.7), c = matSaturateSpec(1.1);
    const l = matMul4x5(matMul4x5(a, b), c), r = matMul4x5(a, matMul4x5(b, c));
    l.forEach((v, i) => near(v, r[i], 1e-12));
});

test('custom filter codes: repository example imports and round-trips', () => {
    const list = parseFilterCodesJson(fx('gvf_filter_codes_example.json').toString());
    assert.equal(list.length, 4);
    assert.ok(list.every(f => f.type === 'webgl'));
    const bil = list.find(f => f.label.startsWith('Bilateral'))!;
    assert.deepEqual(parseUniformDefs(bil.code).map(d => d.name), ['SIGMA_S', 'SIGMA_R', 'SHARPEN']);
    near(bil.uniforms.SIGMA_S, 1.173);
    const src = buildFragSrc(bil.code).src;
    assert.ok(src.startsWith('#version 300 es') && src.includes('uniform float SIGMA_S;') && !src.includes('@uniform'));
    const again = parseFilterCodesJson(JSON.stringify(exportFilterCodesJson(list)));
    assert.deepEqual(again.map(f => [f.label, f.code, f.uniforms]), list.map(f => [f.label, f.code, f.uniforms]));
});

test('custom SVG parser: nesting, result names, unsupported primitives', () => {
    const r = parseCustomSvgPrimitives(`<feGaussianBlur stdDeviation="2" result="b"/>
<feComponentTransfer in="b"><feFuncR type="linear" slope="2"/></feComponentTransfer>
<feComposite in="SourceGraphic" in2="b" operator="arithmetic" k2="1" k3="0.5"/>
<feImage href="http://evil"/>`, 'r_auto', 'c_');
    assert.equal(r.prims.length, 3);
    const ct = r.prims[1] as { in: string; f: { type: string; slope?: number }[] };
    assert.equal(ct.in, 'c_0');
    assert.equal(ct.f[0].slope, 2);
    assert.equal(ct.f[1].type, 'identity');
    assert.equal((r.prims[2] as { in2: string }).in2, 'c_0');
    assert.ok(r.errors.some(x => x.includes('feImage')));
});

test('expert settings diff/import/export round trip', () => {
    const x = expertDefaults();
    x.GVF_CONTRAST_SETTINGS.pivot = 0.4;
    x.GVF_HUE_SETTINGS.reverse = true;
    const d = expertDiff(x);
    assert.deepEqual(d, { GVF_CONTRAST_SETTINGS: { pivot: 0.4 }, GVF_HUE_SETTINGS: { reverse: true } });
    assert.deepEqual(expertFromDiff(d), x);
    assert.deepEqual(importExpert(exportExpert(x)), x);
    assert.equal(importExpert({ type: 'something-else' }), null);
    assert.deepEqual(expertFromDiff({ GVF_CONTRAST_SETTINGS: { pivot: 'x', bogus: 1 } }), expertDefaults());
    assert.equal(EXPERT_DEFAULTS.GVF_SHARPEN_SETTINGS.maxAmount, 1.5);
});

function synthSamples(n: number): AnalysisSample[] {
    const out: AnalysisSample[] = [];
    for (let i = 0; i < n; i++) {
        const rgb = new Uint8Array(ANALYSIS_W * ANALYSIS_H * 3);
        const scene = i < n / 2 ? 0 : 1; // hard cut in the middle
        for (let p = 0; p < ANALYSIS_W * ANALYSIS_H; p++) {
            const x = p % ANALYSIS_W;
            const base = scene ? 40 : 170;
            rgb[p * 3] = base + ((x + i * 3) % 30);
            rgb[p * 3 + 1] = base - 10 + ((x + i) % 20);
            rgb[p * 3 + 2] = scene ? base + 40 : base - 60;
        }
        out.push({ t: i / 10, stats: computeFrameStats(rgb), luma: motionLuma(rgb) });
    }
    return out;
}

test('auto-scene-match is deterministic and reacts to scenes', () => {
    const s = synthSamples(200);
    const k1 = runAutoSceneMatch(s, { strength: 0.65, lockWB: true });
    const k2 = runAutoSceneMatch(s, { strength: 0.65, lockWB: true });
    assert.deepEqual(k1, k2);
    assert.ok(k1.length > 3, 'corrections are produced');
    const dark = autoMatrixAt(k1, 19.9), bright = autoMatrixAt(k1, 9.9);
    assert.ok(dark[0] > bright[0], 'dark scene is brightened more than the bright scene');
    const none = runAutoSceneMatch(s, { strength: 0, lockWB: true });
    none.forEach(k => k.m.forEach((v, i) => near(v, matIdentity4x5()[i], 1e-9)));
});

test('feTurbulence lattice matches the SVG reference setup (deterministic per seed)', () => {
    const a = buildTurbulenceLattice(2), b = buildTurbulenceLattice(2), c = buildTurbulenceLattice(3);
    assert.deepEqual(a.data, b.data);
    assert.notDeepEqual(a.data, c.data);
    // lattice is a permutation of 0..255 repeated
    const sel = Array.from({ length: 256 }, (_, i) => a.data[i * 4]).sort((x, y) => x - y);
    sel.forEach((v, i) => assert.equal(v, i));
    // gradients are unit vectors
    for (let i = 0; i < 256; i++) { const o = (514 + i) * 4; near(Math.hypot(a.data[o], a.data[o + 1]), 1, 1e-5); }
});
