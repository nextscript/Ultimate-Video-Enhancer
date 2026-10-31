/**
 * Contract tests for `shared/transitions.ts`: the uniform table and the pixel law that BOTH GPU
 * engines and the export compositor must reproduce. These are the definitions the shaders mirror, so
 * the direction of every wipe, the size of every slide and the reach of every circle are pinned here.
 */

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { TRANSITION_KINDS, type TransitionKind } from '../shared/types';
import {
    TRANSITION_MODE_LINEAR,
    TRANSITION_MODE_NONE,
    TRANSITION_MODE_RADIAL,
    smoothstep,
    transitionIncomingWeight,
    transitionPixelWeights,
    transitionProgress,
    transitionUniforms
} from '../shared/transitions';

const near = (a: number, b: number, eps = 1e-12): boolean => Math.abs(a - b) <= eps;
const W = 100;
const H = 50;

function weights(kind: TransitionKind, p: number, px: number, py: number): { a: number; b: number; veil: number } {
    return transitionPixelWeights(kind, p, px, py, W, H);
}

test('progress runs frame-centred across the transition window', () => {
    const start = 88;
    const frames = 25;
    assert.equal(transitionProgress(start, start, frames), 0.02);
    assert.equal(transitionProgress(start + frames - 1, start, frames), 0.98);
    assert.equal(transitionProgress(start + 12, start, frames), 0.5);
    assert.equal(transitionProgress(1, 1, 1), 0.5);
    assert.equal(transitionProgress(0, 0, 0), 1);
    assert.equal(transitionProgress(start - 10, start, frames), 0);
    assert.equal(transitionProgress(start + 100, start, frames), 1);
});

test('smoothstep follows the shading-language ramp', () => {
    assert.equal(smoothstep(0, 1, -0.5), 0);
    assert.equal(smoothstep(0, 1, 1.5), 1);
    assert.equal(smoothstep(0, 1, 0.5), 0.5);
    assert.equal(smoothstep(0, 1, 0.25), 0.15625);
    // a degenerate ramp behaves like a hard edge instead of dividing by zero
    assert.equal(smoothstep(0.5, 0.5, 0.5 - 1e-9), 0);
    assert.equal(smoothstep(0.5, 0.5, 0.5 + 1e-9), 1);
});

test('every kind shows only the outgoing layer at p=0 and only the incoming layer at p=1', () => {
    const pixels: [number, number][] = [[0, 0], [W - 1, 0], [0, H - 1], [W - 1, H - 1], [W >> 1, H >> 1]];
    for (const kind of TRANSITION_KINDS) {
        for (const [px, py] of pixels) {
            const start = weights(kind, 0, px, py);
            const end = weights(kind, 1, px, py);
            assert.equal(start.b, 0, `${kind} at ${px},${py} shows B at p=0`);
            assert.equal(start.a, 1, `${kind} at ${px},${py} lost A at p=0`);
            assert.equal(end.a, 0, `${kind} at ${px},${py} kept A at p=1`);
            assert.equal(end.b, 1, `${kind} at ${px},${py} lost B at p=1`);
        }
    }
});

test('a transition never brightens or darkens the frame it composites', () => {
    for (const kind of TRANSITION_KINDS) {
        for (let i = 0; i <= 20; i++) {
            const p = i / 20;
            for (let px = 0; px < W; px += 7) {
                for (let py = 0; py < H; py += 5) {
                    const { a, b } = weights(kind, p, px, py);
                    assert.ok(a >= 0 && b >= 0 && a <= 1 && b <= 1, `${kind} weight out of range at p=${p}`);
                    assert.ok(a + b <= 1 + 1e-12, `${kind} sums to ${a + b} at p=${p} pixel ${px},${py}`);
                }
            }
        }
    }
});

test('frame-wide kinds split the middle 50/50 and only the fades carry a veil', () => {
    const frameWide: TransitionKind[] = ['dissolve', 'fadeBlack', 'fadeWhite', 'smoothLeft', 'smoothRight'];
    for (const kind of frameWide) {
        const u = transitionUniforms(kind, 0.5, W, H);
        assert.equal(u.mode, TRANSITION_MODE_NONE, `${kind} must not carry a boundary`);
        const mid = weights(kind, 0.5, 3, 7);
        assert.equal(mid.a, 0.5, kind);
        assert.equal(mid.b, 0.5, kind);
        assert.equal(mid.veil, kind === 'fadeBlack' || kind === 'fadeWhite' ? 1 : 0, `${kind} veil in the middle`);
        assert.equal(transitionUniforms(kind, 0, W, H).veil, 0, `${kind} veil at the start`);
        assert.equal(transitionUniforms(kind, 1, W, H).veil, 0, `${kind} veil at the end`);
    }
    assert.equal(transitionUniforms('fadeBlack', 0.5, W, H).veilColor, 'black');
    assert.equal(transitionUniforms('fadeWhite', 0.5, W, H).veilColor, 'white');
    assert.equal(transitionUniforms('dissolve', 0.5, W, H).veilColor, null);
    const quarter = transitionUniforms('fadeBlack', 0.25, W, H);
    assert.equal(quarter.veil, 0.5, 'a fade-through-black is half veiled a quarter in');
    assert.equal(quarter.weightA, 0.75);
});

test('wipes sweep in the named direction and keep a hard seam', () => {
    // `lowIsB`: which layer owns the low corner (left edge / top edge) once the boundary has travelled half way
    const cases: { kind: TransitionKind; axis: 0 | 1; lowIsB: boolean }[] = [
        { kind: 'wipeLeft', axis: 0, lowIsB: false },
        { kind: 'wipeRight', axis: 0, lowIsB: true },
        { kind: 'wipeUp', axis: 1, lowIsB: false },
        { kind: 'wipeDown', axis: 1, lowIsB: true }
    ];
    for (const c of cases) {
        const u = transitionUniforms(c.kind, 0.5, W, H);
        assert.equal(u.mode, TRANSITION_MODE_LINEAR, c.kind);
        assert.equal(u.axis, c.axis, `${c.kind} axis`);
        assert.equal(u.highSideIsB, !c.lowIsB, `${c.kind} ownership of the low side`);
        assert.equal(u.edge, ((c.axis === 0 ? W : H) * 0.5) / Math.max(W, H), `${c.kind} boundary must sit mid-axis at p=0.5`);
        assert.equal(u.weightA, 1, `${c.kind} must not also dissolve`);
        assert.equal(u.weightB, 1, `${c.kind} must not also dissolve`);
        const low = c.axis === 0 ? { px: 1, py: H >> 1 } : { px: W >> 1, py: 1 };
        const high = c.axis === 0 ? { px: W - 2, py: H >> 1 } : { px: W >> 1, py: H - 2 };
        const atLow = weights(c.kind, 0.5, low.px, low.py);
        const atHigh = weights(c.kind, 0.5, high.px, high.py);
        assert.equal(atLow.b, c.lowIsB ? 1 : 0, `${c.kind} low corner owner`);
        assert.equal(atLow.a, c.lowIsB ? 0 : 1, `${c.kind} low corner loser`);
        assert.equal(atHigh.b, c.lowIsB ? 0 : 1, `${c.kind} high corner owner`);
        assert.equal(atHigh.a, c.lowIsB ? 1 : 0, `${c.kind} high corner loser`);
    }
});

test('slides move the outgoing layer off-frame and bring the incoming one on-frame', () => {
    const cases: { kind: TransitionKind; dir: 1 | -1 }[] = [
        { kind: 'slideLeft', dir: -1 },
        { kind: 'slideRight', dir: 1 }
    ];
    for (const { kind, dir } of cases) {
        const start = transitionUniforms(kind, 0, W, H);
        const mid = transitionUniforms(kind, 0.5, W, H);
        const end = transitionUniforms(kind, 1, W, H);
        assert.equal(mid.mode, TRANSITION_MODE_LINEAR, kind);
        assert.equal(mid.dxA, dir * W * 0.5, `${kind} outgoing offset`);
        assert.equal(mid.dxB, -dir * W * 0.5, `${kind} incoming offset`);
        assert.equal(start.dxA + 0, 0, `${kind} must start without movement`);
        assert.equal(start.dxB, -dir * W, `${kind} incoming must start off-frame`);
        assert.equal(end.dxA, dir * W, `${kind} outgoing must end off-frame`);
        assert.equal(end.dxB + 0, 0, `${kind} incoming must end aligned`);
    }
});

test('smooth slides dissolve instead of showing a seam but travel the same distance', () => {
    const cases: { kind: TransitionKind; hard: TransitionKind; dir: 1 | -1 }[] = [
        { kind: 'smoothLeft', hard: 'slideLeft', dir: -1 },
        { kind: 'smoothRight', hard: 'slideRight', dir: 1 }
    ];
    for (const { kind, hard, dir } of cases) {
        for (const p of [0, 0.25, 0.5, 1]) {
            const s = transitionUniforms(kind, p, W, H);
            const h = transitionUniforms(hard, p, W, H);
            assert.equal(s.mode, TRANSITION_MODE_NONE, kind);
            assert.equal(s.dxA, dir * W * p, `${kind} travel`);
            assert.equal(s.dxA, h.dxA, `${kind} travels like ${hard}`);
            assert.equal(s.dxB, h.dxB, `${kind} travels like ${hard}`);
        }
        assert.equal(weights(kind, 0.5, 0, 0).a + weights(kind, 0.5, 0, 0).b, 1, `${kind} must never leave a gap`);
    }
});

test('circles grow from the frame centre and reach every corner exactly at the ends', () => {
    const open = transitionUniforms('circleOpen', 0.5, W, H);
    assert.equal(open.mode, TRANSITION_MODE_RADIAL, 'circleOpen mode');
    assert.equal(open.cx, 0.5, 'circleOpen centre x');
    assert.equal(open.cy, 0.25, 'circleOpen centre y, normalised by the longer axis');
    assert.equal(transitionUniforms('circleOpen', 0, W, H).edge, 0);
    assert.equal(transitionIncomingWeight(transitionUniforms('circleOpen', 1, W, H), W - 1, H - 1, W, H), 1, 'circleOpen must reach the corner');
    assert.equal(transitionIncomingWeight(transitionUniforms('circleClose', 0, W, H), W - 1, H - 1, W, H), 0, 'circleClose must start covering the whole frame');
    assert.equal(transitionIncomingWeight(open, W >> 1, H >> 1, W, H), 1, 'an open circle gives the centre to the incoming layer');
    assert.equal(transitionIncomingWeight(transitionUniforms('circleClose', 0.5, W, H), W >> 1, H >> 1, W, H), 0, 'a closing circle keeps the centre on the outgoing layer');
    assert.equal(transitionIncomingWeight(transitionUniforms('circleClose', 0.5, W, H), 0, 0, W, H), 1, 'a closing circle hands the corners to the incoming layer');
});

test('geometry is resolution independent apart from the half-pixel feather', () => {
    const frameOrLinear: TransitionKind[] = [
        'dissolve', 'fadeBlack', 'fadeWhite',
        'wipeLeft', 'wipeRight', 'wipeUp', 'wipeDown',
        'slideLeft', 'slideRight', 'smoothLeft', 'smoothRight'
    ];
    for (const kind of frameOrLinear) {
        const small = transitionUniforms(kind, 0.5, W, H);
        const large = transitionUniforms(kind, 0.5, W * 2, H * 2);
        assert.equal(small.mode, large.mode, `${kind} mode`);
        assert.equal(small.edge, large.edge, `${kind} normalised boundary`);
        assert.equal(small.axis, large.axis, `${kind} axis`);
        assert.equal(small.highSideIsB, large.highSideIsB, `${kind} side ownership`);
        assert.equal(small.weightA, large.weightA, `${kind} weightA`);
        assert.equal(small.weightB, large.weightB, `${kind} weightB`);
        assert.equal(small.dxA / W, large.dxA / (W * 2), `${kind} normalised travel`);
        assert.equal(small.dxB / W, large.dxB / (W * 2), `${kind} normalised travel`);
        assert.ok(near(small.feather, 0.5 / Math.max(W, H)), `${kind} feather is half a render pixel`);
        assert.ok(near(large.feather, 0.5 / (W * 2)), `${kind} feather scales with the render`);
    }
    // a circle's radius is a fraction of the frame diagonal, so the same progress means the same
    // fraction of the corner distance at any render size
    for (const kind of ['circleOpen', 'circleClose'] as TransitionKind[]) {
        for (const p of [0.25, 0.5, 0.75, 1]) {
            const small = transitionUniforms(kind, p, W, H);
            const large = transitionUniforms(kind, p, W * 2, H * 2);
            const reachSmall = small.edge * Math.max(W, H) / (Math.hypot(W, H) / 2 + 1);
            const reachLarge = large.edge * (W * 2) / (Math.hypot(W * 2, H * 2) / 2 + 1);
            assert.ok(near(reachSmall, reachLarge, 1e-12), `${kind} relative radius at p=${p}`);
        }
    }
    // away from the seam the same fraction of the frame behaves identically at either resolution
    for (const kind of ['wipeLeft', 'wipeRight', 'circleOpen', 'circleClose'] as TransitionKind[]) {
        const lowSmall = transitionIncomingWeight(transitionUniforms(kind, 0.5, W, H), 2, 2, W, H);
        const lowLarge = transitionIncomingWeight(transitionUniforms(kind, 0.5, W * 2, H * 2), 4, 4, W * 2, H * 2);
        const highSmall = transitionIncomingWeight(transitionUniforms(kind, 0.5, W, H), W - 3, H - 3, W, H);
        const highLarge = transitionIncomingWeight(transitionUniforms(kind, 0.5, W * 2, H * 2), 2 * W - 5, 2 * H - 5, W * 2, H * 2);
        assert.equal(lowSmall, lowLarge, `${kind} low corner share must not depend on resolution`);
        assert.equal(highSmall, highLarge, `${kind} high corner share must not depend on resolution`);
    }
});

test('boundary kinds hand over pixel coverage progressively', () => {
    const boundaryKinds: TransitionKind[] = ['wipeLeft', 'wipeRight', 'wipeUp', 'wipeDown', 'slideLeft', 'slideRight', 'circleOpen', 'circleClose'];
    const sampled = W * H;
    for (const kind of boundaryKinds) {
        let coverageA = sampled;
        let coverageB = 0;
        for (let i = 1; i < 25; i++) {
            const p = i / 25;
            const uniforms = transitionUniforms(kind, p, W, H);
            let a = 0;
            let b = 0;
            for (let px = 0; px < W; px++) {
                for (let py = 0; py < H; py++) {
                    const geoB = transitionIncomingWeight(uniforms, px, py, W, H);
                    a += uniforms.weightA * (1 - geoB);
                    b += uniforms.weightB * geoB;
                }
            }
            assert.ok(a < coverageA, `${kind} kept its coverage at p=${p}`);
            assert.ok(b > coverageB, `${kind} gained no coverage at p=${p}`);
            coverageA = a;
            coverageB = b;
        }
    }
});
