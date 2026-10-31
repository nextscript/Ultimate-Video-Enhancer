/**
 * Transition math shared by the preview compositor, the export compositor and the tests.
 *
 * Clean-room definitions derived from the observable behaviour of an NLE transition; no third-party
 * implementation was consulted or copied. The point of this file is that preview and export agree:
 * both engines read the SAME uniform bundle produced here and evaluate the SAME expression, so what
 * is on screen is what is rendered.
 *
 * ## The expression every kind uses
 *
 * Render space: `L = max(W, H)` render pixels. Render pixel `(px, py)` has its centre at
 * `(px + 0.5, py + 0.5)` and normalised coordinates `u = (px + 0.5) / L`, `v = (py + 0.5) / L`.
 * The sample that a boundary is compared against is
 *
 *   mode NONE   : no boundary at all, both layers cover the whole frame
 *   mode LINEAR : s = axis == 0 ? u : v
 *   mode RADIAL : s = length(vec2(u, v) - vec2(cx, cy))
 *
 * and the two layer coefficients are
 *
 *   g    = smoothstep(edge - feather, edge + feather, s)
 *   geoB = highSideIsB ? g : 1 - g          // share of this pixel that belongs to layer B's region
 *   cA   = mode == NONE ? weightA : weightA * (1 - geoB)
 *   cB   = mode == NONE ? weightB : weightB * geoB
 *   colour = cA * A(uv + offsetA) + cB * B(uv + offsetB)          // additive, premultiplied
 *   colour = mix(colour, veilColor, veil)
 *
 * Coefficients are additive rather than paint-order alpha: `cA * A + cB * B` gives the true linear
 * crossfade, while "draw A at opacity a then B at opacity b" would fade A twice and dim the seam.
 * `feather` is half a render pixel and is the only resolution-dependent term; all geometry is
 * specified in normalised frame coordinates. `smoothstep` is the built-in of GLSL and WGSL
 * (`t*t*(3-2t)` clamped to [0,1]) and of the reference below, so all three agree exactly.
 */

import type { TransitionKind } from './types';

/** 0 = frame-wide weights only, 1 = straight boundary, 2 = circular boundary. */
export const TRANSITION_MODE_NONE = 0;
export const TRANSITION_MODE_LINEAR = 1;
export const TRANSITION_MODE_RADIAL = 2;
export type TransitionMode = 0 | 1 | 2;

export interface TransitionUniforms {
    /** base opacity of the outgoing layer (the clip left of the cut) */
    weightA: number;
    /** base opacity of the incoming layer (the clip that carries the transition) */
    weightB: number;
    mode: TransitionMode;
    /** 0 = boundary swept along x, 1 = boundary swept along y */
    axis: 0 | 1;
    /** boundary position in normalised (L-relative) coordinates; for RADIAL it is the radius */
    edge: number;
    /** true when layer B owns the high side of the boundary (or the outside of the circle) */
    highSideIsB: boolean;
    /** circle centre in normalised coordinates */
    cx: number;
    cy: number;
    /** half-width of the boundary ramp in normalised coordinates (half a render pixel) */
    feather: number;
    /** layer offsets in render pixels */
    dxA: number;
    dyA: number;
    dxB: number;
    dyB: number;
    /** 0..1 veil strength applied over the composited frame */
    veil: number;
    veilColor: 'black' | 'white' | null;
}

const clamp01 = (x: number): number => (x < 0 ? 0 : x > 1 ? 1 : x);

/** Reference copy of the shading-language smoothstep. */
export function smoothstep(e0: number, e1: number, x: number): number {
    const t = e1 === e0 ? (x < e0 ? 0 : 1) : clamp01((x - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}

/**
 * Progress through one transition window, frame-centred: frame `startF` sits at `0.5 / frames` and the
 * last frame at `1 - 0.5 / frames`, so a 1-frame window lands exactly at its midpoint instead of
 * snapping to one side. The audio ramps use the same convention.
 */
export function transitionProgress(frame: number, startF: number, frames: number): number {
    if (frames <= 0) return 1;
    return clamp01((frame - startF + 0.5) / frames);
}

/**
 * Uniforms for `kind` at progress `p` on a frame of W x H render pixels. Every value is a
 * deterministic function of (kind, p, W, H); nothing here depends on the engine.
 */
export function transitionUniforms(kind: TransitionKind, p: number, W: number, H: number): TransitionUniforms {
    const prog = clamp01(p);
    const L = Math.max(1, W, H);
    // one render pixel past the corner so the ramp is fully past every pixel centre at p = 1
    const maxR = (Math.sqrt(W * W + H * H) / 2 + 1) / L;
    const u: TransitionUniforms = {
        weightA: 1 - prog,
        weightB: prog,
        mode: TRANSITION_MODE_NONE,
        axis: 0,
        edge: 0,
        highSideIsB: true,
        cx: (W / 2) / L,
        cy: (H / 2) / L,
        feather: 0.5 / L,
        dxA: 0,
        dyA: 0,
        dxB: 0,
        dyB: 0,
        veil: 0,
        veilColor: null
    };

    /** Hard-edged boundary: the two layers swap pixels, they do not also dissolve. */
    const linear = (axis: 0 | 1, e: number, highIsB: boolean): void => {
        u.mode = TRANSITION_MODE_LINEAR;
        u.axis = axis;
        u.edge = e;
        u.highSideIsB = highIsB;
        u.weightA = 1;
        u.weightB = 1;
    };
    /** Hard-edged circle of normalised radius `r`; `highIsB` picks inside vs outside for layer B. */
    const radial = (r: number, highIsB: boolean): void => {
        u.mode = TRANSITION_MODE_RADIAL;
        u.edge = r;
        u.highSideIsB = highIsB;
        u.weightA = 1;
        u.weightB = 1;
    };
    /** Layer B sweeps in from the right / left, so its slide direction is fixed here. */
    const slide = (leftward: boolean): void => {
        u.dxA = (leftward ? -1 : 1) * W * prog;
        u.dxB = (leftward ? 1 : -1) * W * (1 - prog);
    };

    switch (kind) {
        case 'dissolve':
            return u;

        // through black: both layers collapse into the veil at the middle of the window
        case 'fadeBlack':
            u.veil = 1 - Math.abs(2 * prog - 1);
            u.veilColor = 'black';
            return u;

        case 'fadeWhite':
            u.veil = 1 - Math.abs(2 * prog - 1);
            u.veilColor = 'white';
            return u;

        // boundary travels right -> left and layer B takes the side it has passed
        case 'wipeLeft':
            linear(0, (W * (1 - prog)) / L, true);
            return u;

        case 'wipeRight':
            linear(0, (W * prog) / L, false);
            return u;

        // v grows downwards, so a boundary sweeping upward keeps the large-v side for B
        case 'wipeUp':
            linear(1, (H * (1 - prog)) / L, true);
            return u;

        case 'wipeDown':
            linear(1, (H * prog) / L, false);
            return u;

        case 'slideLeft':
            linear(0, (W * (1 - prog)) / L, true);
            slide(true);
            return u;

        case 'slideRight':
            linear(0, (W * prog) / L, false);
            slide(false);
            return u;

        // a slide that dissolves instead of showing a seam: frame-wide weights, layers still move
        case 'smoothLeft':
            slide(true);
            return u;

        case 'smoothRight':
            slide(false);
            return u;

        // layer B grows out of the frame centre
        case 'circleOpen':
            radial(prog * maxR, false);
            return u;

        // the last patch of layer A shrinks away in the centre
        case 'circleClose':
            radial((1 - prog) * maxR, true);
            return u;

        default:
            return u;
    }
}

/**
 * Share of one render pixel that belongs to layer B's region: the reference implementation of the
 * expression the shaders evaluate. `px`, `py` are render pixel indices.
 */
export function transitionIncomingWeight(uniforms: TransitionUniforms, px: number, py: number, W: number, H: number): number {
    if (uniforms.mode === TRANSITION_MODE_NONE) return 1;
    const L = Math.max(1, W, H);
    const u = (px + 0.5) / L;
    const v = (py + 0.5) / L;
    const sample =
        uniforms.mode === TRANSITION_MODE_LINEAR
            ? uniforms.axis === 0
                ? u
                : v
            : Math.hypot(u - uniforms.cx, v - uniforms.cy);
    const g = smoothstep(uniforms.edge - uniforms.feather, uniforms.edge + uniforms.feather, sample);
    return uniforms.highSideIsB ? g : 1 - g;
}

/** Coefficients the compositor must apply to each layer at one render pixel. */
export function transitionPixelWeights(kind: TransitionKind, p: number, px: number, py: number, W: number, H: number): { a: number; b: number; veil: number } {
    const uniforms = transitionUniforms(kind, p, W, H);
    if (uniforms.mode === TRANSITION_MODE_NONE) return { a: uniforms.weightA, b: uniforms.weightB, veil: uniforms.veil };
    const geoB = transitionIncomingWeight(uniforms, px, py, W, H);
    return { a: uniforms.weightA * (1 - geoB), b: uniforms.weightB * geoB, veil: uniforms.veil };
}
