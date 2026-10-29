# Source-to-Desktop Audit

| | |
|---|---|
| Upstream | https://github.com/nextscript/Ultimate-Video-Enhancer |
| Commit | `38b2ed18c62e6cbd45f0672e0183cb1d1c4a5da1` (2026-10-16) |
| Script | `Ultimate-Video-Enhancer-(Sharpen,-HDR,-Color-Boost).user.js` v1.17.5, 20 551 lines |
| SHA-256 | `28b37dfa45131f9aa51f5f392b3131f9c533e869351e495a23768817524069a6` |
| Audited | 2026-10-29 |

The machine-readable matrix with source symbols, line numbers, ranges, defaults, preview/render status and
deviations is `UVE_FEATURE_MATRIX.csv`. This document summarises the findings.

## Render modes in the userscript

The userscript has two render paths:

1. **SVG mode (default, `renderMode = 'svg'`)** – `buildFilter()` (line 19360) builds an SVG filter graph
   (8 pre-built combinations for Moody/Teal/Vibrant) plus a CSS tone matrix. This is the authoritative path and is
   ported 1:1 to `shared/uve/graph.ts`, including `ensureSvgFilter()` rounding, `mergeChainedColorMatrices()` and
   the Chromium in-filter tone matrix (`svgToneMatrix()`).
2. **GPU mode (`Ctrl+Alt+X`)** – a WebGL2 canvas overlay with its own shader (line 10023) that approximates the SVG
   chain and additionally runs a fixed bilateral/CAS pass. UVE Manager does not port this second approximation;
   the desktop engine always runs the SVG-mode graph **on the GPU** (WebGL2), so there is a single canonical look.

## Execution semantics reproduced

`src/engine/EffectEngine.ts` interprets the graph with SVG filter semantics:
premultiplied alpha for `feComposite`/`feBlend`/`feConvolveMatrix`/`feGaussianBlur`/`feMorphology`,
non-premultiplied for `feColorMatrix`/`feComponentTransfer`, 180°-rotated convolution kernels, per-primitive
clamping, color clamped to alpha after arithmetic composites (Skia behaviour), `feComponentTransfer`
table/discrete/linear/gamma per spec, and `feTurbulence` with the SVG 1.1 reference lattice (seeded, deterministic).

Consequence worth noting: the userscript's Anime line-art branch uses a Sobel `feConvolveMatrix` without
`preserveAlpha`, which in Chromium produces a transparent result – the branch is a visual no-op in the browser and
therefore also in UVE Manager.

## Deliberate desktop differences

| Area | Browser | UVE Manager | Why |
|---|---|---|---|
| Pixel units | CSS pixels of the displayed `<video>` | source pixels | display size is meaningless for export |
| Filter edges | transparent padding (−10 % region) | edge extension | avoids dark borders in exported frames |
| Precision | 8-bit per primitive | float16 per primitive (8-bit "parity mode" in Settings) | higher quality; parity mode for comparisons |
| Auto-Scene-Match | wall-clock loop, 2–10 fps, only while playing | pre-analysis (96×54, 10 fps grid) + identical state machine on media time | deterministic preview = export |
| Expert settings | global (GM storage) | per project / profile (+ export/import) | reproducible jobs |
| Custom SVG parser | flat tag list (child `feFunc*` become separate nodes) | proper nesting | filters work as written |
| GLSL `u_time`/`u_mouse` | wall clock / mouse | media time / centre | deterministic export |

## Not ported (clearly reported in the UI)

* mpv/libplacebo hook shader compatibility layer (FSR, Anime4K, KrigBilateral … single-pass approximations).
* Canvas2D and audio custom filters (they execute arbitrary JavaScript in the page).
* MBLook (Magic Bullet) LUT import.
* Recorder, screenshot, DRM detection, site blacklists, browser overlay icons – not applicable / forbidden by the V4 spec.

## Profiles and presets

Looks (Base, Dark & Moody, Teal & Orange, Vibrant, HDR) and tone profiles (Movie, Anime, Gaming, EyeCare, User)
use the exact source constants. The repository samples `My_Profile.json`, `GPU_Mode_Profile.json`,
`firefox_fix.json` (gvf-settings) import correctly (unit test), as do user-profile JSON/ZIP exports of the
userscript. The default effect settings equal `DEFAULT_USER_PROFILE` (Chromium variant).
