# Transitions

Authoritative definition of every transition kind. The implementation is `shared/transitions.ts`; the
contract test is `tests/transitions.test.ts`. Both GPU engines (`src/engine/EffectEngine.ts`,
`src/engine/GpuEngine.ts`) and the export compositor evaluate the same uniform bundle, so the preview
frame and the exported frame are the same frame. No engine has its own transition maths.

All definitions are clean-room, written from the observable behaviour of an NLE transition. Nothing was
copied or disassembled from any other editor.

## Roles and window

A transition lives on the **left boundary of the right clip** and straddles the cut:

```
cut frame C, window W = [C - hL, C + hR), D = hL + hR = max(1, round(durSec * F))
hL = floor(D / 2)                       (left clip supplies hL frames)
hR = D - hL                             (right clip supplies hR frames)
```

* **Layer A** = the *outgoing* clip (left of the cut, role `a`), needs `handleNeed(clip, hR, F)` source
  seconds beyond its out point.
* **Layer B** = the *incoming* clip (the clip that carries the transition, role `b`), needs
  `handleNeed(clip, hL, F)` source seconds before its in point.
* Handle units are source seconds; `handleNeed(clip, frames, F) = frames / F * clip.speed`.
* `TimelinePlan.windows` carry both roles for the whole window with the same `transStartF` /
  `transFrames`, so progress is continuous even when a plan window is split at another clip's cut.

Progress is frame-centred: `transitionProgress(frame, transStartF, transFrames)` =
`clamp01((frame - transStartF + 0.5) / transFrames)`. The first frame of the window is at
`0.5 / transFrames`, the last at `1 - 0.5 / transFrames`, and a 1-frame window lands at `0.5` instead of
snapping to one side. `transFrames <= 0` means "no transition" and yields `1`. Audio crossfade ramps use
the identical convention, so picture and sound cross at the same instant.

## The one expression

Render space is `L = max(W, H)` render pixels. Render pixel `(px, py)` has its centre at
`(px + 0.5, py + 0.5)` and normalised coordinates `u = (px + 0.5) / L`, `v = (py + 0.5) / L`.

```glsl
float L      = max(float(W), float(H));
float u      = (px + 0.5) / L;
float v      = (py + 0.5) / L;
float sample = uMode == 1u ? (uAxis == 0u ? u : v)
             : uMode == 2u ? distance(vec2(u, v), vec2(uCx, uCy))
             : 0.0;
float g   = uMode == 0u ? 0.0 : smoothstep(uEdge - uFeather, uEdge + uFeather, sample);
float geoB = uHighSideIsB ? g : 1.0 - g;
float cA = uMode == 0u ? uWeightA : uWeightA * (1.0 - geoB);
float cB = uMode == 0u ? uWeightB : uWeightB * geoB;
vec4 a = texture(tA, uv + vec2(uDxA, uDyA) / L);
vec4 b = texture(tB, uv + vec2(uDxB, uDyB) / L);
vec4 colour = cA * a + cB * b;                      // additive, premultiplied
colour = mix(colour, vec4(veilColor, 1.0), uVeil);
```

`mode`: 0 = no boundary (frame-wide weights only), 1 = straight boundary (`axis` 0 = x, 1 = y),
2 = circular boundary (`edge` is the radius). `smoothstep` is the language built-in in GLSL ES 3.0 and
WGSL (`t*t*(3-2t)` clamped to `[0,1]`) and the identical reference copy in `shared/transitions.ts`, so
JS, GLSL and WGSL agree bit-for-bit. `feather` is `0.5 / L` — half a render pixel — and is the only
resolution-dependent term: every geometric quantity is a normalised frame coordinate.

**Why additive and not paint-order alpha.** Drawing A at opacity `a` and then B at opacity `b` over black
produces `a·(1-b)·A + b·B`, which fades A twice and dims the seam; a 50 % dissolve would land at 0.5·0.5
+ 0.5 = 0.75 total energy and the midpoint of a wipe between two different pictures would flicker. The
additive law `cA·A + cB·B` keeps every pixel's contribution linear in both layers, which is also what the
audio law does for a crossfade (`1-p` and `p` in amplitude, probed flat within 1.5 dB between two windows
of one continuous recording). `cA + cB <= 1` for every kind and progress — a transition never brightens
or darkens the frame (asserted in the test suite).

`transitionPixelWeights(kind, p, px, py, W, H)` returns the JS reference `{a, b, veil}` that the shaders
must reproduce; `transitionIncomingWeight` returns `geoB`. Both are used by the tests and by nothing in
the hot paths.

## Uniforms

`TransitionUniforms` (one bundle per layer per frame, from `transitionUniforms(kind, p, W, H)`):

| Field | Meaning |
| --- | --- |
| `weightA`, `weightB` | base opacity of the outgoing / incoming layer; `1-p` / `p` for frame-wide kinds, `1` / `1` for the hard geometric kinds |
| `mode` | 0 none, 1 linear boundary, 2 radial boundary |
| `axis` | boundary sweep direction for mode 1: 0 = x, 1 = y |
| `edge` | boundary position (mode 1) or radius (mode 2), normalised by `L` |
| `highSideIsB` | true when layer B owns the high side of the boundary, or the outside of the circle |
| `cx`, `cy` | circle centre, normalised by `L` |
| `feather` | half-width of the boundary ramp, `0.5 / L` |
| `dxA`, `dyA`, `dxB`, `dyB` | layer offsets in render pixels |
| `veil`, `veilColor` | veil strength applied over the composited frame, and its colour (`'black'` / `'white'` / `null`) |

## Per-kind definition

`p` is the frame-centred progress, `prog = clamp01(p)`, `L = max(W,H)`.

| Kind | mode / weights | geometry | offsets |
| --- | --- | --- | --- |
| `dissolve` | none, `weightA = 1-p`, `weightB = p` | — | — |
| `fadeBlack` | none, `1-p` / `p` | `veil = 1 - abs(2p-1)`, `veilColor = black` | — |
| `fadeWhite` | none, `1-p` / `p` | `veil = 1 - abs(2p-1)`, `veilColor = white` | — |
| `wipeLeft` | linear axis 0, weights 1 / 1 | `edge = W(1-p)/L`, `highSideIsB = true` | — |
| `wipeRight` | linear axis 0, weights 1 / 1 | `edge = W·p/L`, `highSideIsB = false` | — |
| `wipeUp` | linear axis 1, weights 1 / 1 | `edge = H(1-p)/L`, `highSideIsB = true` | — |
| `wipeDown` | linear axis 1, weights 1 / 1 | `edge = H·p/L`, `highSideIsB = false` | — |
| `slideLeft` | linear axis 0, weights 1 / 1 | `edge = W(1-p)/L`, `highSideIsB = true` | `dxA = -W·p`, `dxB = W(1-p)` |
| `slideRight` | linear axis 0, weights 1 / 1 | `edge = W·p/L`, `highSideIsB = false` | `dxA = W·p`, `dxB = -W(1-p)` |
| `smoothLeft` | **none**, `weightA = 1-p`, `weightB = p` | no boundary at all | `dxA = -W·p`, `dxB = W(1-p)` |
| `smoothRight` | **none**, `weightA = 1-p`, `weightB = p` | no boundary at all | `dxA = W·p`, `dxB = -W(1-p)` |
| `circleOpen` | radial, weights 1 / 1 | `edge = p·maxR`, `highSideIsB = false` | — |
| `circleClose` | radial, weights 1 / 1 | `edge = (1-p)·maxR`, `highSideIsB = true` | — |

`maxR = (hypot(W, H) / 2 + 1) / L`: the circle reaches exactly one render pixel past the frame corner at
`p = 1`, so the half-pixel feather never leaves a residue at the first or last frame of the window. The
`+1` is the only quantity in the whole transition model that is measured in pixels rather than frame
fractions, and it is resolution invariant in the sense that the *relative* radius is identical at any
render size (`tests/transitions.test.ts` asserts both statements).

Directions, spelled out so there is no ambiguity:

* `v` grows **downwards** in both shading languages, so `wipeUp` (B owns the high-v side) reveals B from
  the bottom edge upward, and `wipeDown` reveals B from the top edge downward.
* `wipeLeft`'s boundary travels from the right edge to the left edge, and B occupies the region the
  boundary has already passed; `wipeRight` travels left to right with B on its left.
* Slides keep the same boundary as their wipe twin and additionally translate layer A out of the frame
  and layer B into it, measured in frame widths, so a doubled render size doubles the pixel offsets and
  the picture is unchanged.
* `smoothLeft` / `smoothRight` travel exactly like the corresponding slide but carry **no boundary**:
  they are frame-wide dissolves of two moving layers, which is why they never show a hard seam and never
  dim at the middle (`cA + cB = 1` everywhere).
* `circleOpen` grows B out of the frame centre; `circleClose` shrinks the last patch of A away in the
  centre, i.e. B owns the outside.
* `fadeBlack` / `fadeWhite` are not dissolves through a colour layer: the composited frame is mixed
  toward black or white with `veil = 1 - |2p-1|`, so both ends of the window are un-veiled and the middle
  is fully veiled.

## Kinds that are deliberately not supported

Transitions whose reference behaviour is not reproducible in a fragment shader were measured against
ffmpeg's own transition filters and excluded rather than approximated:

| Excluded | Measured behaviour | Why it is out |
| --- | --- | --- |
| `dissolve` (ffmpeg) | scattered per-pixel noise dither between the two pictures | a random per-pixel mask is not a transition an NLE user can preview deterministically, and preview would not equal export |
| `hblur` | a horizontal blur band travelling across the frame | needs a separable blur pass per frame, i.e. a second render target and a resolution-dependent kernel |
| `radial` | a conic sweep around the centre with a soft angular ramp | angular ramps around a normalised centre are not resolution invariant under `L`-normalisation |
| `pixelize` | block-mosaic quantisation that shrinks toward the transition mid-point | block size depends on the render resolution, so the same progress would look different in preview and export |
| `smoothleft` (ffmpeg) | soft ramp lagging its wipe boundary, ramp width tied to the filter's own internal geometry | our `smoothLeft` is defined as the seam-free slide above instead of copying that ramp shape |

Everything supported is expressible as one `smoothstep` on a normalised coordinate plus two coefficients,
which is what keeps preview, export and tests in agreement.
