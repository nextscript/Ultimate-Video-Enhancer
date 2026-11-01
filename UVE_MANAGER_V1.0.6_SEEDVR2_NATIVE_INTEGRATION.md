# UVE Manager v1.0.6 — SeedVR2 Native Integration

**Implementation specification — additive change only**  
**New capability:** SeedVR2 3B FP8 native local video super-resolution **without ComfyUI**  
**UI language:** English; existing UI and settings retained.  
**Status:** Design/implementation instructions, **not implemented**.

## 0. Non-negotiable constraints

1. **Do not remove, replace, rename, or break** existing **REAL Video Enhancer (TensorRT)** integration, its models, `electron/rve.ts`, installation, status probes, render modes, settings, model downloads, paths, or projects. RVE must remain usable exactly as before, including its NVIDIA TensorRT optimization.
2. Add SeedVR2 as an **independent, optional AI Upscaling engine**, selectable beside RVE in the existing UI. Neither engine may silently substitute for the other. Never download SeedVR2 on app startup unless user opted in.
3. Preserve all current capabilities: multi-track timeline, effects and custom filters, LUTs, transitions, export worker, audio/subtitles, previews, queues, portable release, cancellation, progress, profiles, keyboard shortcuts, project load/save, cross-platform builds, and original-source safety.
4. Keep the existing UVE effect graph authoritative. **Do not reimplement** UVE filters or replace the Electron/TypeScript application with ComfyUI, Tauri, Rust, or a new editor.
5. Native inference must use **an isolated Python process and PyTorch runtime**, not ComfyUI, browser WebGPU model emulation, a ComfyUI HTTP server, or an external/cloud API.
6. Existing installations and projects must open unmodified. Schema changes must be optional, additive, and backward compatible, with RVE as the default for existing saved configurations.
7. Never imply SeedVR2 universally supports NVIDIA/AMD/Intel/macOS. Detect proven executable backends, show precise incompatibility messages, and leave RVE and existing non-AI export available.
8. Do not assume `.safetensors` by itself can run through TensorRT or RVE's Compact/SPAN model adapter: SeedVR2 requires its **own architecture, weights, VAE, dependencies, and processing logic**.
9. Support offline use after one-time validated installation. No model or dependency downloads during render. Offer explicit user-triggered repair/update/uninstall operations.
10. Preserve source FPS, frame order/timestamps, original audio sync, timeline duration and output codec/container settings. Do not interpolate frames unless the existing FPS-interpolation option explicitly requests it.

## 1. Confirmed existing integration and change surface

The v1.0.5 repository contains:

| Existing file | Role | v1.0.6 action |
|---|---|---|
| `electron/rve.ts` | RVE backend setup/status/model management/run | **Keep unchanged where possible.** No SeedVR2 code in RVE module. |
| `electron/render.ts` | Queue, UVE GPU export worker, AI second stage | Generalize AI stage dispatcher; preserve RVE behavior. |
| `electron/main.ts` | Main process and IPC integration | Register narrow SeedVR2 status/install/cancel methods. |
| `electron/preload.ts` / `electron/exportPreload.ts` | Renderer-worker bridges | Expose typed IPC via existing safe conventions. |
| `src/api.ts` | Frontend API | Add typed SeedVR2 API methods. |
| `shared/types.ts` | Job/settings/types | Add optional engine field and SeedVR2 options. |
| `electron/store.ts` | Preferences and tools directory | Isolated SeedVR2 storage directory. |
| `docs/RENDER_PIPELINE.md` | FFmpeg + UVE export documentation | Document new optional backend. |
| `electron/selftest.ts` | Tests | Add mocks/regression coverage. |
| `package.json`, release workflow | Build/distribution | Test optional payload and platform packaging. |

Also inspect **actual current frontend components and state flow** before changing UI; do not invent paths or replace existing controls. The branch can change after this specification was written. Reference files are authoritative over the proposed examples below.

## 2. Product behavior / UX

### AI Upscaling (existing location, not a duplicate menu)

Add a single **Engine** selector in the current AI Upscaling settings:

```text
AI Upscaling
  Enable AI Upscaling                     [ ON ]
  Engine                                  [ REAL Video Enhancer (TensorRT)  v ]
                                           REAL Video Enhancer (TensorRT)
                                           SeedVR2 Native (PyTorch)

  When REAL Video Enhancer selected:
    [Current RVE model selector / install / settings — UNCHANGED]

  When SeedVR2 Native selected:
    Model                                  [ SeedVR2 3B FP8 ]
    Model Status                           Not Installed / Ready / Error
    [ Install Engine ] [ Download Model ] [ Verify ] [ Remove ]
    Device                                 [ Automatic ]
    Output Resolution                      [ 2160p / 1440p / 1080p / Custom ]
    Temporal Batch Size                    [ Auto ]
    Temporal Overlap                       [ Auto ]
    Chunk Size                             [ Auto ]
    VAE Tiling                             [ Auto ]
    VAE Tile Size                          [ Auto ]
    VAE Tile Overlap                       [ Auto ]
    Block Swap                             [ Auto ]
    CPU Offload                            [ Auto ]
    Advanced Settings                      [ Expand ]
    Compatibility                          [ NVIDIA CUDA: Ready ]
    Estimated VRAM / Benchmark             [ Run Test ]
```

- Default existing users to their existing RVE engine and model **without resetting preferences**. Newly enabled AI Upscaling should keep current default behavior unless user explicitly selects SeedVR2.
- When selecting SeedVR2, show compatibility first, then install controls, then available settings. Disable Start Render with specific actionable error if unavailable; allow non-AI export and RVE.
- Settings display current values, capability-dependent effective values, and warnings separately. Never label rough VRAM guesses as measured.
- Preserve **existing original/enhanced preview behavior**. If full temporal inference is too expensive for live preview, show original/UVE preview and explicitly mark **AI final-render only**; optionally provide an explicit short-range **Render AI Preview** command. Do not mislabel WebGPU preview as SeedVR2 output.
- Present download sizes/checksums/license disclosure and disk free space before model installation; report progress, cancellation, retry, failures.
- Render queue shows active engine/model, frame counts, processing FPS, elapsed/ETA, VRAM when measured, logs, stage and output path. Avoid promising a valid ETA until processing throughput has stabilized.

## 3. Proposed architecture

```text
Current UI / Timeline / Existing AI Upscaling Selector
                   |
        typed engine selection
                   |
    electron/render.ts  — existing queue
                   |
     FFmpeg → UVE effect worker → lossless/intermediate source
                   |
           AI Engine Dispatcher
            /              \
     RVE TensorRT       SeedVR2 Native
     existing rve.ts     new seedvr2.ts
            |               |
       existing RVE      isolated Python 3.12 env
       backend           seedvr2 standalone adapter
                            |
                    SeedVR2 3B FP8 + VAE
                            |
                   FFmpeg mux / final export
                   preserved audio + subtitles
```

**Separate stages:** SeedVR2 consumes the UVE-filtered intermediate at correct source timestamps and produces the requested enhanced video. The original user's existing choices about ordering must be preserved; if RVE currently uses an after-UVE stage, use that same ordering for SeedVR2. Avoid unnecessary dual encoding by using lossless or mathematically safe intermediate formats wherever feasible; measure intermediate file size and disk limits.

**Do not assume that SeedVR2 output has exactly 2x/4x architecture scale.** Its interface has target-resolution semantics; map the UI to tested model CLI resolution arguments and then verify final pixel dimensions. Carefully determine whether `--resolution` means long side, short side or a target limit in the **pinned CLI commit** rather than guessing. Maintain aspect ratio; handle rotation, SAR/DAR, pixel aspect ratio, odd dimensions, color metadata and alpha consistently.

### New modules (suggested)

```text
electron/seedvr2.ts                # public install/status/render controller
electron/aiEngine.ts               # engine dispatch + common AI result/status types
shared/seedvr2.ts                 # serializable settings/capability types
engine/seedvr2/manifest.json       # pinned code revision, hashes, compatible deps/models
engine/seedvr2/runner.py          # small adapter to validated standalone CLI
engine/seedvr2/progress.py        # only if needed; machine-readable progress
tests/seedvr2/                    # CLI mocks, installation, processing, parity tests
docs/SEEDVR2_NATIVE.md            # developer/end-user instructions
```

Adapt paths to the actual source tree; files above are **proposed**, not claims that they already exist.

## 4. Model/runtime acquisition and isolation

### Source of standalone inference

- Upstream integration: https://github.com/numz/ComfyUI-SeedVR2_VideoUpscaler
- Standalone entry point: `inference_cli.py`, documented to operate without a ComfyUI server/UI.
- Model hosting: https://huggingface.co/numz/SeedVR2_comfyUI
- Original research/project provenance: verify official ByteDance SeedVR2 upstream and redistribution rights at integration time.

**Mandatory:** Pin a tested exact commit, dependency lock, model filename(s) and SHA-256, and compatible VAE; `main` and latest CUDA/PyTorch are not a reproducible release specification. Implementer must inspect `python inference_cli.py --help` at that commit and derive all CLI flags from it. Do **not** assume the names of FP8 files, VAE files, attention implementations or defaults are stable. The model install manifest shall contain:

```json
{
  "engine": "seedvr2-native",
  "engineVersion": "1.0.0",
  "upstreamRepo": "https://github.com/numz/ComfyUI-SeedVR2_VideoUpscaler",
  "pinnedCommit": "REPLACE_WITH_TESTED_FULL_SHA",
  "python": "3.12.x-tested",
  "modelId": "seedvr2-3b-fp8",
  "weights": [{"filename": "REPLACE_AFTER_VERIFICATION", "sha256": "REPLACE"}],
  "vae": [{"filename": "REPLACE_AFTER_VERIFICATION", "sha256": "REPLACE"}],
  "supportedOS": ["REPLACE_AFTER_TESTING"],
  "supportedDevices": ["REPLACE_AFTER_TESTING"]
}
```

The above is a template. A production install MUST reject unresolved placeholders.

- Prefer user-space `tools/seedvr2/` (or the established tools directory), containing an isolated interpreter/env, source, weights, VAE, manifest, logs, temp and caches. **Do not use RVE's portable Python runtime** unless its package/ABI versions are proven compatible and isolated; default to separation to prevent dependency collisions.
- Provide verified download URLs, resume/range support where server permits, atomic `.part` -> final move, checksum verification, zip/tar path-traversal defenses, reasonable download-size limits, HTTPS, disk-space checks, retries/backoff, repair and safe removal.
- Avoid `curl | sh`, arbitrary remote scripts or user-wide package installation. Dependencies are installed from a pinned, reviewed lockfile; user confirmation for storage/bandwidth.
- Verify version/commit, Python import test, torch device, model + VAE load, short inference smoke test, real export and log redaction before marking Ready.
- Model licensing and redistribution must be separately reviewed, including custom node code vs weights vs dependencies; display notices in `docs/LICENSES.md`. Keep downloading optional until legal clearance.

## 5. Runtime capability matrix (truthful labels)

| GPU | Target support | Shipping policy |
|---|---|---|
| NVIDIA Windows/Linux | CUDA/PyTorch, FP8 if tested | **Primary MVP**; real inference smoke-test required. |
| AMD Linux | ROCm, model/operator-dependent | Experimental/disabled until tested on representative GPU. |
| AMD Windows | Not assumed CUDA-compatible | Unsupported unless an independently validated backend exists. |
| Intel Arc | PyTorch/XPU or other proven implementation | Not advertised without end-to-end tests. |
| Apple Silicon | MPS only if each model/op/dtype is compatible | Experimental/disabled until tested; FP8 may require alternative precision. |
| CPU | Optional dev/test fallback | No guarantee of practical video throughput. |

Never confuse existing RVE TensorRT availability with SeedVR2 availability. Implement a **capability probe per engine**, not just a generic `cuda: true` check. Auto chooses a tested compatible option; it must not silently switch away from selected model precision.

## 6. SeedVR2 option schema and validated CLI mapping

Suggested serializable schema (exact property names adapt to existing conventions):

```ts
type AiEngine = 'rve-tensorrt' | 'seedvr2-native';
interface SeedVr2Settings {
  modelId: 'seedvr2-3b-fp8';
  device: 'auto' | 'cuda' | 'rocm' | 'mps' | 'cpu';
  targetWidth?: number;
  targetHeight?: number;
  batchSize: 'auto' | number;
  chunkSize: 'auto' | number;
  temporalOverlap: 'auto' | number;
  vaeEncodeTiled: 'auto' | boolean;
  vaeDecodeTiled: 'auto' | boolean;
  vaeTileSize: 'auto' | number;
  blocksToSwap: 'auto' | number;
  cpuOffload: 'auto' | boolean;
  advanced?: Record<string, unknown>; // validated allowlist only
}
interface AiUpscaleSettings {
  engine?: AiEngine; // absence => legacy RVE behavior
  seedvr2?: SeedVr2Settings;
  // preserve ALL existing RVE fields unchanged
}
```

- No command-line shell strings constructed from UI input. Build a typed argv array against an **explicit allowlist** derived from the pinned upstream CLI `--help` and implementation. Reject unsupported options rather than passing them through.
- SeedVR2 CLI examples show `--resolution`, `--batch_size`, `--chunk_size`, `--temporal_overlap`, `--vae_encode_tiled`, `--vae_decode_tiled`, `--blocks_to_swap`, and device/offload options. **Verify each against pinned upstream version** and implement output-path discovery/verification rather than relying on undocumented naming conventions.
- Default settings should be **auto + conservative**, not hard-code 4K, 9 frames or 32 swap blocks for every source and GPU. Start with tested small temporal batch, VAE tiling, adjust based on VRAM and benchmark. Temporal batch and overlap semantics must be verified against model implementation.
- Validate temporal boundaries and overlap to avoid duplicated/missing frames and visual seams. Keep scene cuts and clip boundaries explicit; never fuse unrelated timeline clips into an artificial continuous temporal context without validating expected behavior.

## 7. Integration in existing render.ts queue

Introduce a narrow dispatch interface:

```ts
interface AiUpscaleAdapter {
  id: 'rve-tensorrt' | 'seedvr2-native';
  getStatus(): Promise<AiEngineStatus>;
  validate(job: JobSpec): Promise<void>;
  run(inputFile: string, outputFile: string, job: JobSpec,
      onProgress: (p: AiProgress) => void, signal: AbortSignal): Promise<AiResult>;
}
```

**Implementation approach:**

1. Capture baseline tests for a working RVE TensorRT export. Record exact prior defaults, output paths, queue progress, audio mux and cancellation.
2. Add explicit AI `engine` setting with absence -> RVE and bind existing RVE adapter without changing its invocation semantics.
3. Keep current Stage 1 lossless/intermediate UVE worker. Stage 2 dispatches to RVE or SeedVR2, with separate isolated temp/output files and logs.
4. SeedVR2 adapter spawns executable + argv (`spawn`, no shell), uses validated working directory, handles stdout/stderr incrementally with bounded buffers, translates true progress into existing UI job state, captures precise process exit/error codes and supports cancellation by terminating process tree. Never report 100% until model output validated and mux succeeded.
5. Validate finished video with ffprobe: width, height, number/duration/timestamps of frames, FPS, color profile, decodability, output codec/container. Handle variable-frame-rate sources according to the existing export contract; never claim exact frame parity if the existing timeline export normalizes VFR to CFR.
6. Reuse existing final audio/subtitle mux rules, render presets, temporary-file cleanup and atomic output rename. On error/cancel keep original untouched and retain RVE as an independent usable option.
7. Use a per-job snapshot of model/settings so mid-render UI changes cannot mutate running jobs. Resource management: initially **one heavy AI model job per GPU**, bounded CPU/RAM disk intermediate, explicit space preflight. Add parallel jobs only after correctness and contention benchmarks.
8. Recovery after process crash: mark failed, clean or safely register orphaned intermediates; provide logs/repair actions. Do not automatically rerun on lower quality without explicit consent.

### Audio, timestamps and metadata

- Maintain source audio and timeline mix through the existing FFmpeg mux path, not through SeedVR2's default audio behavior unless verified equivalent.
- Preserve CFR frame count and PTS from the UVE stage; use frame-index/PTS manifest if the SeedVR2 runner emits a new stream without timestamps.
- Check for frame count drift, missing or repeated frames, temporal joins, black frames and video/audio duration differences.
- Preserve color space (BT.709/BT.2020), transfer function, range, SAR/rotation and HDR bit depth as supported. When model inference requires SDR RGB, explicitly report the transform and correctly convert back; **do not silently promise HDR preservation**.

## 8. Runner/progress protocol

Preferred robust solution: a small UVE-owned Python wrapper around pinned standalone inference, with versioned JSON Lines messages to stdout and diagnostics to stderr. Example wire format (your adapter contract, **not an upstream CLI promise**):

```json
{"type":"status","stage":"load","message":"Loading SeedVR2 3B FP8"}
{"type":"progress","stage":"inference","processedFrames":120,"totalFrames":900,"fps":0.75}
{"type":"result","outputFile":"...","frames":900,"width":3840,"height":2160}
{"type":"error","code":"CUDA_OUT_OF_MEMORY","message":"...","retryable":true}
```

- Wrapper should call the upstream CLI as an isolated subprocess initially; only import internal Python functions if they are part of a pinned, stable interface. If the CLI gives no per-frame progress, use chunk-based **measured** progress; never fabricate frame rate/percent or parse undocumented console formatting as authoritative.
- Support JSON schema version, partial/unexpected output, unicode paths, spaces, Windows long paths, SIGTERM/CTRL_BREAK/process-tree cleanup, GPU OOM errors, user cancellation and timeout on stalled startup.
- Record runtime provenance (upstream SHA, model hash, VAE hash, torch/CUDA versions, GPU name), but exclude sensitive file paths from exported diagnostic bundles unless user opts in.

## 9. Performance controls for RTX 5060 Ti 16 GB

Target reference device: NVIDIA RTX 5060 Ti 16 GB, 1080p 60 FPS source; **performance must be measured** rather than derived from RVE's AnimeJaNai TensorRT FPS. SeedVR2 is a substantially different temporal generative workload.

- Default first test: **5–10 second** representative clip, conservative small batch, VAE encode/decode tiling, limited chunk length, auto offload and optional block swap if OOM.
- Prioritize successful 1080p->2160p export (tested memory/quality) rather than an arbitrary claimed FPS target. Benchmark actual throughput and 1% slowest chunks, peak VRAM, host RAM, disk footprint, artifacts on camera pans and scene cuts.
- Add adaptive retry on CUDA OOM **only with user opt-in**: clear model cache/restart isolated process, reduce batch, lower tiles, increase offload/block swap, then retry one short test. A full job must not silently lower output resolution or switch model.
- Treat FP8 weights and compute dtype separately: FP8 storage does not guarantee native FP8 computation, VRAM fit, quality parity or a specific TensorRT speedup.
- Display `Estimated render time` only after at least several representative completed chunks, labeled an estimate.

## 10. Tests and release gates

**Existing RVE regression (mandatory):**

- AI select absent in old project -> exact old TensorRT path selected; all RVE model installs/load/status/export unaffected.
- RVE still renders via CUDA/TensorRT with original defaults and no SeedVR2 Python installed.
- RVE failure does not corrupt SeedVR2 installation and vice versa.
- UVE effect graph, timeline transitions, audio, custom filters and LUTs exactly retain their behavior independent of AI selection.

**SeedVR2 acceptance:**

- Clean install with no ComfyUI on Windows NVIDIA; isolated runtime installs only with consent; model and VAE hash verified.
- Short video and long chunked video; source fps unchanged, correct output dimensions, no audio drift, all frames present, clean clip/scene joins.
- Successful cold start and second cached start; offline inference; UTF-8/spaces in filename; test multi-track timeline intermediate.
- Cancel during setup, download, model load, inference, mux and uninstall; no zombie child processes, temp artifacts recoverable/removed safely.
- Force wrong weights/hash, missing VAE, Torch import failure, driver/CUDA error, insufficient disk/VRAM and unsupported device; show actionable message, not generic crash.
- Compare processed frame samples against pinned upstream CLI on same hardware/options and tolerate expected numeric differences; flag major color/content mismatch.
- Test smoke exports (frames and A/V sync) at 720p, 1080p, 1440p/2160p where feasible and ensure 10-bit/HDR is not erroneously advertised.

**Platforms:**

- Windows x64 + supported NVIDIA = MVP release gate, on real machine, not just mocked tests.
- macOS arm64/x64, Linux x64 must still package/run existing UVE Manager and non-SeedVR2 features; SeedVR2 install selector clearly marked unavailable where untested. Never break existing macOS/Linux releases by bundling Windows CUDA dependencies.
- AMD and Intel backend labels remain experimental/unavailable until genuine integration test, not mere detection success.

**Definition of Done**

- [ ] Existing RVE TensorRT behavior unchanged and regression tests green.
- [ ] SeedVR2 selection displayed alongside (not replacing) RVE under existing AI Upscaling section.
- [ ] Dedicated native/standalone Python engine; no ComfyUI installation/service needed.
- [ ] Verified model + VAE download and integrity, offline repeated use.
- [ ] Correct full video export with UVE effects, existing queue controls, progress/cancel, output metadata and A/V sync.
- [ ] Tested NVIDIA 16 GB target without misleading speed/VRAM promises.
- [ ] Unsupported AMD/Intel/Mac clearly handled without breaking UVE/RVE.
- [ ] Backward-compatible project/settings migration, docs and licenses updated.
- [ ] Builds, smoke tests, runtime/source pinning and security checks passed.

## 11. Suggested implementation sequence (separate commits)

1. Baseline audit and TensorRT regression suite; document current RVE/UVE output characteristics.
2. Add typed engine selector/config persistence + untouched legacy RVE dispatch.
3. SeedVR2 native manifest, source/weights downloader, isolated environment + capability probe.
4. Build tested CLI adapter/wrapper with JSON progress, correct output and A/V mux.
5. Plug second-stage dispatcher into render queue, cancellation, diagnostics and error recovery.
6. Add advanced controls and hardware-aware conservative presets; short preview-render action if proven.
7. Run GPU benchmark, long-video temporal quality regression, cross-platform package tests and document limitations.

## 12. Source/reference checklist

- User project branch: https://github.com/nextscript/Ultimate-Video-Enhancer/tree/uve-manager
- Existing RVE connector: https://github.com/nextscript/Ultimate-Video-Enhancer/blob/uve-manager/electron/rve.ts
- Existing render queue: https://github.com/nextscript/Ultimate-Video-Enhancer/blob/uve-manager/electron/render.ts
- SeedVR2 standalone CLI: https://github.com/numz/ComfyUI-SeedVR2_VideoUpscaler
- SeedVR2 model repository: https://huggingface.co/numz/SeedVR2_comfyUI
- REAL Video Enhancer: https://github.com/TNTwise/REAL-Video-Enhancer

**Verification note:** CLI flags, model naming, pinning, runtimes and GPU support are version-dependent. Do not ship before resolving the manifest placeholders from an actually tested upstream commit. This document deliberately does not invent tested model hashes, timings or support claims.
