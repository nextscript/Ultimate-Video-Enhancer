# UVE Manager — Video2X Additional Processors Implementation

**Target:** existing `uve-manager` branch / v1.0.7 codebase  
**Type:** ADDITIVE feature extension ONLY — no rewrite, no migration of existing modules  
**Goal:** Enable the *other* Video2X processing modes (Real-ESRGAN, Real-CUGAN, optional RIFE) using the already-installed Video2X integration. Anime4K integration is already implemented and MUST remain untouched from a user perspective.

> **IMPORTANT TO CODING AGENT:** This specification is for a *small, separate feature* layered onto the current repository. Before editing, inspect the current branch. `package.json` and `shared/types.ts` already identify v1.0.7; do not reset versions, duplicate Anime4K implementation, re-create shader management or follow older v1.0.7 specifications blindly. Preserve every functioning feature, setting and default. Do not modify unrelated subsystems.

## 1. Existing baseline — verified against `uve-manager`

- `electron/video2x.ts`: Video2X managed/custom executable, probe, Vulkan adapter selection, install and self-test, current Anime4K-specific preparation.
- `electron/video2x-process.ts`: existing safe child process runner, progress/error reporting, cancellation and process-tree handling.
- `shared/video2x-cli.ts`: currently assumes `-p libplacebo` and `--libplacebo-shader`; version and `--help` probing; lossless output encoders.
- `shared/upscale-provider.ts`: `UpscaleProvider = 'off' | 'rve' | 'video2x'`, Video2X `presetId`, `scale` and `device`, legacy RVE resolution logic.
- `electron/render.ts`: separate Video2X multi-stage render path; UVE output → lossless intermediate → Video2X → final encoder → mux/verify; stage cleanup and progress.
- Existing GLSL shader library and Anime4K presets are **complete** and remain fully functional.
- Existing REAL-Video-Enhancer (TensorRT/NCNN/PyTorch), WebGPU/WebGL2 effect graph, media editor, multitrack timeline, transitions, audio and encoder settings must remain exactly as they are.

**Invariants:** no change to older project behavior, exported file semantics, current shader names/IDs, RVE model IDs, legacy defaults or existing output presets. Existing projects missing new fields must work exactly as before.

## 2. Scope — only newly added features

1. **Real-ESRGAN through Video2X (ncnn + Vulkan)**, with supported models dynamically discovered/probed.
2. **Real-CUGAN through Video2X (ncnn + Vulkan)**, with model and noise/scale controls only when supported.
3. **RIFE frame interpolation through Video2X** as a separate *opt-in* processing mode; must not silently modify the existing FPS selector, timelines or AI upscalers. Enable only when timing, sound synchronization and output frame verification are proven correct.
4. A simple **Video2X Processor** selector under existing AI Upscaling section: `Anime4K (existing)`, `Real-ESRGAN (new)`, `Real-CUGAN (new)`. Put RIFE under an independent `Frame Interpolation (Video2X)` opt-in section *only after* end-to-end timeline integration passes.
5. New engine-specific model/options selectors, capability checks, descriptive errors and small benchmark/test workflow. No other new UI or generalized redesign.

**Explicitly out of scope:** reimplementation of Video2X; replacing UVE’s effect engine; converting `.glsl` to WGSL; any edits to existing shader manager; adopting another RVE implementation; generic plugin architecture; rewriting video/audio export; simultaneous RIFE + another upscale processor unless tested with fully deterministic semantics; new backends for macOS; changing version number without an explicit release decision.

## 3. Architecture — reuse existing pipeline

```text
UVE Editor / existing Render Queue (UNCHANGED)
     ↓ existing UVE effect render / timeline composition
Lossless UVE intermediate (existing)
     ↓ choose 'video2x' provider
Processor dispatcher — NEW, only inside Video2X adapter
   ├─ libplacebo + Anime4K  [EXISTING PATH: DO NOT CHANGE]
   ├─ realesrgan + model   [NEW]
   └─ realcugan + model/noise [NEW]
     ↓ existing Video2X process executor
     ↓ existing final encode / audio mux / verification
Final UVE output (UNCHANGED)
```

Maintain the current Video2X stage handling including lossless intermediate, final codec, audio, frame verification, cancel and cleanup. **Do not call an upscale engine twice**. For RIFE implement a distinct explicitly enabled stage with duration/fps mapping, not a hidden preprocessor of the selected upscaler.

## 4. Data model — optional additive fields

Keep `UpscaleProvider` unchanged. Extend `Video2xExportOptions` *additively*:

```ts
type Video2xProcessor = 'libplacebo' | 'realesrgan' | 'realcugan';
interface Video2xExportOptions {
  presetId: string; // EXISTING; still means Anime4K shader ID
  scale: number;    // EXISTING; preserve meaning for Anime4K
  device: number;   // EXISTING
  processor?: Video2xProcessor; // undefined -> 'libplacebo' (legacy compatibility)
  modelId?: string;             // only for realesrgan / realcugan
  noiseLevel?: number;          // only if supported by realcugan CLI
}
```

- Never rename/delete `presetId`, `scale`, `device` or `aiUpscale`.
- Missing `processor` MUST mean `libplacebo`, exactly preserving v1.0.7 projects.
- Do not re-purpose `aiUpscale` (owned by RVE).
- Don't touch `providerPatch` behavior other than additive initialization for the *new* options if strictly necessary.
- Use allowlisted model IDs retrieved from actual `--help` / verified supported combinations; never accept arbitrary CLI switches typed via model IDs.
- Persist optional interpolation settings separately from `video2x`; disabled by default.

## 5. CLI adapter — tightly scoped new functionality

Keep existing `buildArgs()` for Anime4K **untouched**. Implement another pure function (or new file `shared/video2x-processors.ts`) for additional processors:

```ts
interface Video2xNeuralRun {
  input: string; output: string;
  processor: 'realesrgan' | 'realcugan';
  modelId: string;
  scale: 2 | 3 | 4;
  noiseLevel?: number;
  device: number;
  lossless: string;
}
function buildNeuralArgs(caps: Video2xCapabilities, spec: Video2xNeuralRun): string[];
```

- **Do not reuse `--libplacebo-shader` for other processors.**
- Video2X documentation demonstrates `-p realesrgan -s 4 --realesrgan-model realesr-animevideov3` and `-p realcugan -s 4 -n 0 --realcugan-model models-se`.
- Check the actual installed binary's `--help` for model/scale/noise switches before constructing argv. Documented flags may differ across releases; never assume a flag exists on an arbitrary custom binary.
- Use `spawn(..., { shell: false })` and string-array args through existing `electron/video2x-process.ts`. No shell concatenation, path injection, dynamic eval or direct untrusted flags.
- Preserve existing lossless encoders and pixel-format validation; avoid accidentally changing codec or chroma conversion in final output.
- Reject unsupported model-scale combinations *before* rendering, with actionable messages.
- Keep the **existing pinned release and download logic unchanged** unless a necessary and independently verified compatibility change is explicitly required. Do not update to a different Video2X version automatically.
- If built-in models are missing, report capability unavailable; do not download arbitrary model assets independently without a verified origin and checksum.

### Supported CLI examples (for reference only; actual commands derived from probed binary)

```bash
video2x -i input.mp4 -o output.mkv -p realesrgan -s 4 --realesrgan-model realesr-animevideov3
video2x -i input.mp4 -o output.mkv -p realcugan -s 4 -n 0 --realcugan-model models-se
```

**These examples are not ready-to-use UVE final-export commands.** Continue using the existing lossless-stage and final-encode strategies.

## 6. Capability detection and first-run tests

- Extend `Video2xCapabilities` *optionally*, without making `libplacebo` require neural engine flags.
- Probe processors individually (`libplacebo`, `realesrgan`, `realcugan`, optionally `rife`).
- Maintain current `Video2X available` / `Anime4K verified` states. Report separately `Real-ESRGAN available/verified`, `Real-CUGAN available/verified`, `RIFE available/verified`.
- One 10-frame 128×72 synthetic **input**, processed to valid allowed output dimensions, must confirm each selected processor/model combination and verify frame count, resolution, output readability and stable exit code.
- Discover and record the GPU using existing Vulkan probing. Avoid assumptions that an NVIDIA GPU implies TensorRT for Video2X; Video2X neural backends here use Vulkan/ncnn.
- Cache results per executable fingerprint, processor, model, scale, noise and GPU; do not accept a cached Anime4K test as proof of Real-CUGAN support.
- No network activity merely from changing the dropdown. Explicit opt-in needed for any component download.

## 7. UI changes — only current Video2X settings section

**Do not redesign settings or output panels.** Add only these controls inside existing Video2X area:

- `Processor`: `Anime4K v4` (default, existing), `Real-ESRGAN`, `Real-CUGAN`.
- `Model`: engine-specific, filtered to verified installed/supported models.
- `Scale`: only compatible factors; preserve original scaling-resolution precedence and warn if target dimensions are incompatible with fixed-scale model requirements.
- `Noise Level`: Real-CUGAN only, when supported by the running CLI.
- `GPU`: reuse the existing Video2X Vulkan adapter selector; do not duplicate it.
- Show engine status, whether a model is verified, and short human-readable reasons when unavailable.

On switching from Anime4K to a neural model, **keep** the Anime4K preset and custom GLSL list in storage; restoring Anime4K must restore the prior choice. On switching between providers, keep RVE's selected model intact.

## 8. Scale & dimension correctness (critical)

Existing Anime4K can target explicit `width/height`, whereas neural models often have fixed intrinsic scale. Treat them differently:

- Determine input dimensions from the **UVE intermediate actually fed into Video2X**.
- Calculate neural output dimensions using the selected model scale; verify them after processing.
- If user-selected target size differs, **do not silently** down/upscale with FFmpeg or resize the original input and then claim native neural output quality. Instead show a warning and require an explicit supported post-resize policy; default should be no implicit resize.
- Preserve existing frame rate, duration, orientation, aspect ratio and audio synchronization. Avoid integer rounding that changes aspect ratio.
- Reject unsupported dimensions/VRAM constraints early when detectably impossible, otherwise fail cleanly with meaningful logs.

## 9. Video2X process, errors, cancellation, memory

Reuse `electron/video2x-process.ts` as-is wherever possible. Reuse current video2x `runVideo2xStages()` in `electron/render.ts`; extend processor dispatch at the narrowest point, *not* by adding a parallel render queue.

- Progress must remain monotonic and continue to show per-stage FPS, elapsed and ETA.
- Abort kills the whole subprocess tree; remove only UVE-owned temporary artifacts; never delete source media, external model files or shader library.
- Only atomically publish the final output on full success; on error preserve prior output.
- On GPU OOM, incompatible Vulkan driver, unsupported ncnn model or missing asset: explain the exact failure and offer switching processor, scale or GPU. Do not auto-fallback to a different visual model without user confirmation.
- Avoid concurrent engine executions by default; preserve existing queue ordering.

## 10. Optional RIFE (separate opt-in; must not destabilize exports)

Video2X also supports `rife` frame interpolation. This is **not** an upscale model and must be represented separately.

- Guard behind a disabled-by-default feature flag until all tests pass.
- Offer target-FPS multiplier or explicit FPS *only when permitted by Video2X CLI and existing export semantics*.
- Check audio duration, frame timestamps, CFR/VFR normalization, cut boundaries, reverse clips, speed changes, and transition timing.
- Video2X RIFE CLI example: `-p rife -m 4 --rife-model rife-v4.6`, but validate processor/model flags with the actual binary.
- Do not introduce interpolation on audio or inadvertently repeat interpolation during timeline rendering.
- If full compatibility is uncertain, **ship Real-ESRGAN and Real-CUGAN only; RIFE stays disabled** rather than degrading established editing behavior.

## 11. Tests (required; regression gate)

### Add new, isolated tests

- `buildNeuralArgs` snapshots: Real-ESRGAN / Real-CUGAN, valid/invalid scale, noise, special-character paths, mismatched model, missing flags.
- Capability probes from recorded `--help` fixtures for pinned binary, unsupported binary and missing model flags.
- Mock process tests: success, cancellation, non-zero exit, crashing Vulkan process, no device, missing model, timeout, incomplete output and OOM.
- 10-frame integration renders for each new supported engine, with exact frame count and dimensions.
- 30-second 1080p60 end-to-end tests on representative NVIDIA and AMD/Intel Vulkan GPUs where available; record actual measured FPS, peak VRAM, total time and result quality rather than promising a specific speedup.
- Persistence: old v1.0.7 projects without `processor` render with the **identical** Anime4K pipeline; current RVE projects remain identical; unknown values revert to safe defaults.
- Audio/video sync, HDR metadata, selected codec, cancellation and no accidental overwrite checks.

### Mandatory regression tests (must continue passing)

- `npm run typecheck`
- `npm test`
- `npm run build`
- Existing self-test (where runnable) and Video2X/Anime4K full render.
- Existing RVE TensorRT, NCNN and PyTorch selections, codec/export settings and audio mux verification.
- Existing WebGPU/WebGL2, multitrack editing, transitions, custom shader library, project save/load.

**No unrelated refactors to silence failing tests.** Fix only regression introduced by this feature.

## 12. Explicit change boundary

### Allowed to extend minimally

- `shared/upscale-provider.ts`: only optional Video2X-specific fields; backward-safe parsing.
- `shared/video2x-cli.ts`: optional capability metadata **or**, preferably, a new `shared/video2x-processors.ts` adapter.
- `electron/video2x.ts`: model/processor probe and run-preparation dispatch.
- `electron/render.ts`: only the narrow Video2X processor selection and verified dimensions/final output handling.
- Existing Video2X settings UI component(s): add processor/model/noise selector only, no visual redesign.
- New tests and **this feature's** documentation.

### Must not touch except absolutely unavoidable and tested

- `electron/rve.ts` and RVE backend installation/model management.
- `src/engine/**`, UVE effect processing, color science, LUT, custom effects.
- `electron/video2x-shaders.ts`, GLSL library and preset IDs.
- Timeline, transitions, audio engine, keyboard shortcuts, editor state management.
- Build targets, installer packaging, macOS fallback and existing downloader.
- Any prior changelog/documentation or previous version implementation MDs.

No wholesale formatting, rename of existing identifiers, migration of all settings, package upgrades, lockfile churn or unrelated UI changes. Keep patches targeted.

## 13. Release and acceptance criteria

Feature is complete only if:

1. Existing Anime4K behaves identically without user configuration changes.
2. Existing RVE and non-upscaled exports are unaffected.
3. Real-ESRGAN and Real-CUGAN are selectable only on compatible Video2X builds and successfully render to expected resolution and frame count.
4. Existing render codec, audio and UVE effect settings are preserved.
5. Cancel/failure leaves sources and prior outputs unchanged and cleans only owned temp files.
6. All existing automated tests remain green and newly added tests pass.
7. User can disable/ignore the extension without any behavioral difference.
8. RIFE is only enabled if separately validated; otherwise excluded from release.
9. Implementation PR contains a concise **diff summary limited to new Video2X processors** and measured benchmarks; it does not include previously implemented work.

## 14. Sources / exact technical reference

- Video2X CLI: https://github.com/k4yt3x/video2x/blob/master/docs/book/src/running/command-line.md
- Video2X repository/releases/license: https://github.com/k4yt3x/video2x
- Current UVE branch: https://github.com/nextscript/Ultimate-Video-Enhancer/tree/uve-manager
- Current source files: `shared/video2x-cli.ts`, `shared/upscale-provider.ts`, `electron/video2x.ts`, `electron/video2x-process.ts`, `electron/render.ts`.

---

**Implementation directive:** Treat this as an additive *Video2X processors extension*, not a new v1.0.7 implementation. Preserve all existing working features and file paths; make the smallest possible tested changes.
