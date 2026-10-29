# Ultimate Video Enhancer Desktop — v1.0.1 Migration Specification

**Document type:** Incremental upgrade contract for an existing, completed **v1.0.0** application.  
**Target:** v1.0.1 — Cross-Platform GPU Rendering & Hardware Acceleration.  
**Scope:** **Only changes required for v1.0.1.** This is NOT a reimplementation of v1.0.0. No previous specifications are included.  
**Baseline:** The actual v1.0.0 Git repository, executable, tests, user data and existing behavior are the source of truth. This document cannot verify that baseline without its code.  
**Language:** User-facing menus, labels and settings remain in **English**.

## 0. Non-negotiable release contract

1. **Preserve every existing v1.0.0 feature** and its observable behavior: UI/layout, drag-and-drop media import, player, seek/timeline/frame stepping, Before/After, all UVE filters and combinations, LUT imports/controls, custom effects and shader editing, profiles/presets, expert sliders, auto modes, scopes, settings, projects, undo/redo, export jobs, audio/video tracks, keyboard shortcuts, error states and configuration. This list illustrates the preservation requirement; **the codebase determines the exhaustive list**. Do not assume absent features exist.
2. **No functionality may disappear, be silently disabled or replaced with a visual-only mock** during migration. If a feature cannot yet run on the new backend, retain the old working implementation behind a compatible legacy/fallback adapter until parity is proven. A release cannot claim full parity if any previously functioning feature is missing.
3. Preserve **existing settings, project files, presets, LUTs and custom filter assets without data loss**. Make schema migrations additive, versioned, backed up, and reversible where feasible; keep v1.0.0 project opening functional.
4. App must remain a **file-based enhancement editor**. Export decodes source video frames, executes the effect graph and encodes **a new actual file**. **Never** implement screenshot output, screen recording, video capture, canvas-recording or MediaRecorder-based video export. Intermediate decoded frames, caches and thumbnails used internally are permitted.
5. Do **not** turn NVIDIA/CUDA into a hard dependency. NVIDIA, AMD, Intel and Apple GPUs must be supported through a common processing abstraction, as capabilities permit.
6. Do not promise universal hardware decoding/encoding or zero-copy: the runtime must probe hardware, OS, driver, codec, bit depth and interop support. Provide a **working software decoder/encoder fallback** and a tested filter fallback when the preferred GPU path is unavailable.
7. Do not force a Rust rewrite of fully functional v1.0.0 code just for this release. Preserve the existing host UI and architecture unless a specific measured or portability-driven reason justifies modification; introduce Rust/Tauri incrementally only if accepted by the project architecture. **An engine migration and a complete UI rewrite must not be coupled.**
8. Release gates: unit, integration, parity, export-quality, stability and three-OS packaging tests. All user-facing operations must remain functional.

## 1. Upgrade goal / non-goals

**Goal:** Introduce a vendor-neutral GPU effect engine with portable shader definitions and runtime-selected hardware decode/encode to accelerate exports across Windows, Linux and macOS, while keeping **all v1.0.0 capabilities** intact.

**Explicit non-goals:** new recording/screenshot functionality; new AI-upscaling claims; introducing a new editing workflow; redesigning the existing UI; breaking existing project files; rewriting every filter from scratch without a compatibility path.

**Primary stack recommendation (adapt to existing application):** Rust for new backend modules, `wgpu` for portable GPU compute/rendering (D3D12/Vulkan/Metal where supported), FFmpeg for decoding, encoding and multiplexing, existing UI retained. Tauri 2 is an **optional** host replacement, not a prerequisite for GPU acceleration. Keep C++/C ABI interoperability available where required for performant FFmpeg/hardware-surface interop. Validate third-party redistribution and codec licensing for every shipped build.

## 2. Mandatory baseline inventory BEFORE changing code

Automate a **v1.0.0 feature inventory** from the actual source and functional UI. Store a machine-readable `migration/feature-parity-v1.0.0.json` with entries:

`id, display_name, entry_point, current_module, inputs, defaults, allowed_values, serialized_schema, current_preview_path, current_export_path, platform_assumptions, v1_0_0_test, v1_0_1_test, supported_backends, parity_status, differences`.

Inventory every existing:

- Media import/drag-and-drop, supported formats, codec probing, track selection, metadata, playback/seek, timeline/frame stepping, thumbnails, timecode, audio sync, fullscreen, zoom and player interactions.
- All UVE effects (basic, detail, color, HDR-like, auto/scene-specific, texture/denoise, expert controls), every order/enable/strength/reset option, parameter limits, saved defaults and keyboard shortcuts.
- All LUT formats, interpolation/color management, LUT mixing/stacking, import/export, file handling and relinking.
- All custom effects including existing GLSL, SVG and Canvas behavior, custom selectors/uniform controls, code validation, editor interaction, fallback and any user-managed filter library.
- Profile/preset creation, editing, deletion, import/export, JSON/ZIP handling, scopes, Before/After, any diagnostic/benchmark features **actually present in v1.0.0**.
- Project load/save, migrations, undo/redo, autosave/recovery if present, export codecs/container/settings, progress/cancel, audio/subtitle handling, log/errors, preferences and app updates.

Use the **v1.0.0 implementation** as the authoritative scope. The GreasyFork userscript is a secondary reference for tracing source semantics, not a license to delete desktop-specific features. Produce coverage accounting, including v1.0.0 baseline SHA and total inventoried items. **100% of previously passing v1.0.0 tests must still pass** before the release.

## 3. Proposed architecture

```text
Existing v1.0.0 GUI / Player / Timeline / Inspector / Projects
                | (unchanged UX + stable application API)
   Engine Facade: import, preview, seek, evaluate, export
                |
       Shared Effect Graph + Versioned Parameter Schema
                |
        +-------+--------------------+
        |                            |
   GPU Filter Engine            Legacy/CPU Filter Adapter
   wgpu WGSL + compatibility    (retained until parity)
        |
Hardware Surface / Transfer Abstraction (capability-gated)
        |
FFmpeg Decode -> Timestamped Frame Graph -> FFmpeg Encode/Mux
        |                                         |
 NVDEC / D3D11VA / QSV / VAAPI / VideoToolbox    NVENC / AMF / QSV / VAAPI / VideoToolbox
               + Software decode/encode fallback
```

**One effect graph, two uses:** preview and offline export share parameters, stage ordering, color pipeline, seeds and effect implementations wherever possible. If presentation and export differ in backend/precision, compare frames at equal source PTS using tolerance-defined image metrics; fix discrepancies rather than claiming identical bitwise pixels.

### Recommended module boundaries

```text
engine/core/         Filter graph, versioned parameters, project adapters
engine/media/        FFmpeg probe, streams, PTS/timebases, decoder and mux
engine/gpu/          wgpu device/context, textures, pipelines, shader cache
engine/interop/      OS/backend-specific hardware surface exchange
engine/filters/      Built-ins, LUTs, automatic/temporal, custom adapters
engine/encode/       Encoder capabilities, jobs, audio/subtitle, cancel
engine/fallback/     Existing v1.0.0 filter path + CPU/software fallbacks
engine/diagnostics/  Capabilities, profiling, logs and crash diagnostics
migration/           Baseline inventory and schema upgrade tests
```

Adapt folder names to the actual repository. Do not create a second source of truth for filter parameters.

## 4. Portable GPU backend and fallback selection

| OS | Filter backend preference | HW decoding candidates | HW encoding candidates |
|---|---|---|---|
| Windows NVIDIA | wgpu/D3D12 or Vulkan | D3D11VA / NVIDIA NVDEC | NVENC |
| Windows AMD | wgpu/D3D12 or Vulkan | D3D11VA | AMF |
| Windows Intel | wgpu/D3D12 or Vulkan | D3D11VA / QSV | QSV |
| Linux NVIDIA/AMD/Intel | wgpu/Vulkan | VAAPI / vendor-specific supported path | VAAPI / supported vendor encoder |
| macOS Intel / Apple Silicon | wgpu/Metal | VideoToolbox | VideoToolbox |
| Unsupported/remote/VM | available compatible backend or legacy path | FFmpeg software decoder | FFmpeg software encoder |

These are **candidate paths, not promises**. Actual availability must be probed per codec, input pixel format, chroma subsampling, profile, bit depth, resolution, driver/runtime, permissions and FFmpeg build.

**Backend selection:** probe → validate actual decoding of a small sample → check GPU filter format and hardware-surface interop → validate selected encoder using a short scratch encode → choose fastest **verified** compatible path; prefer a stable path over an unverified zero-copy path. Keep user preferences `Automatic / Prefer Hardware / Software` with informative fallback explanations; preserve existing export options and persisted defaults.

**Interop reality:** `wgpu` does not inherently share FFmpeg decoder/encoder surfaces zero-copy on every platform. For first release implement bounded transfers with correct synchronization; add D3D12/D3D11, Vulkan/DMABUF or Metal/IOSurface bridges **only** where a proven, secure and measured implementation is possible. Never label CPU-readback/upload as zero-copy. Implement device-loss recovery and adapter change handling.

## 5. Shared UVE filter parity

1. Capture an effect graph from v1.0.0 preserving exact filter order, activation flags, parameter defaults/ranges, profiles, per-effect reset semantics and saved user values. Maintain stable effect IDs; unknown project fields must be preserved when possible.
2. Port built-in numeric/texture effects to **WGSL** with explicit alpha, color-space, channel-order, clamping, gamma/transfer function and precision rules. Combine compatible effects in fused passes **only if** same visual output and effect order are preserved.
3. LUTs: retain all existing import formats and behavior; define texture layout/interpolation/domain, 1D vs 3D handling, precision and strength/mix; test with reference LUT charts. Do not silently reinterpret `.cube` or PNG LUTs.
4. Auto Scene Match/other adaptive functions: preserve v1.0.0 analysis windows, temporal smoothing, scene boundaries, reproducible timeline seeks and final-export look. If parallel processing changes temporal state, implement deterministic checkpoints or state replay.
5. Custom GLSL: avoid assuming every shader converts to WGSL. Compile/translate validated compatible shaders where possible; otherwise retain the original functioning GLSL-compatible renderer or report explicit, actionable backend constraints **without removing existing usability**. Never execute arbitrary untrusted code with OS/file/network privileges.
6. SVG/Canvas: not automatically equivalent to WGSL. Keep the existing renderer for unsupported constructs, or implement feature-complete replacements and compare reference frames. Preserve UI-generated sliders, selectors and custom uniform definitions.
7. Scopes/diagnostics and Before/After: keep all existing tools. Ensure original/enhanced use same frame timestamp and correct color conversion.
8. Features not present in current source must **not** be invented to satisfy a checklist. Features present in v1.0.0 must **not** be discarded because this document does not name them individually.

**Hard rule:** Each inventoried entry must pass on both preview and exported output, or use a proven fallback. Do not ship a generic shader adapter with hidden unsupported features.

## 6. Frame processing and rendering performance

- **Media decode:** asynchronous FFmpeg demux and decoding with bounded frame queues, preserved stream PTS/timebases, VFR handling and rotation/pixel aspect metadata; seek from keyframes and decode-forward for accurate presented frames.
- **Color correctness:** preserve transfer function, color primaries, matrix, full/limited range, 8/10/12-bit where supported, YUV↔RGB conversions and HDR metadata policy. Do not claim HDR output from HDR-like visual grading alone.
- **GPU pipeline:** double/triple buffered surfaces as measured; reuse allocations, cache pipeline states and shaders, use async compute where it actually helps, avoid per-frame recompile, group passes conservatively and profile GPU/CPU work separately.
- **Audio & subtitles:** keep v1.0.0 audio/subtitle behavior, timestamps and sync; copy audio losslessly if target container permits, else explicit re-encode. Maintain stream selections and metadata behavior.
- **Encoding:** discover H.264/H.265/AV1 and hardware support per adapter; preserve all existing quality/bitrate/preset/container controls; never silently switch codec or alter resolution/FPS. Offer software encode fallback.
- **Reliability:** cancellation, pause/resume **only if already present or correctly implemented**, job retries, safe destination writing, no overwrite of input, partial-output cleanup and crash recovery based on existing app contract.
- **Measurement:** record stage times (decode, transfer, GPU pass, readback, encode, mux), throughput FPS, peak RAM/VRAM, total wall clock and output hashes/metrics. Show actual selected hardware in diagnostics, not a hard-coded GPU name.

**Pipeline optimization order:** profile actual bottleneck → reuse buffers → reduce transfers → fuse provably equivalent passes → implement measured platform-specific interop → tune encoder/decoder concurrency. A Rust rewrite alone is not a rendering-speed optimization.

## 7. Platform behavior and distribution

**Windows:** support NVIDIA/AMD/Intel paths; install/discovery of required runtimes; portable Windows installer/package consistent with v1.0.0 behavior. **macOS:** Apple Silicon and Intel where supported by dependencies; handle signing, notarization, hardened runtime, permission and FFmpeg/VideoToolbox compatibility; bundle correct architectures or universal assets as appropriate. **Linux:** document required VAAPI/Vulkan drivers, display/headless limitations, deployment target and codecs; distribute only after tested dependency resolution. Platform support is a **release promise only after CI plus on-device tests**; if v1.0.0 ships Windows-only, do not make v1.0.1 appear macOS/Linux-ready without evidence.

**Versioning:** bump application and package metadata to `1.0.1` only after gates; no forced renaming of existing application identifiers, projects, profile paths, UI assets, keyboard shortcuts or icons. Migrate settings with backward-compatible defaults, keeping originals/backups. Ensure no existing user content is overwritten.

## 8. Tests and acceptance gates (must pass)

### A. Regression / preservation

- Run all pre-existing v1.0.0 tests unmodified and add integration cases covering every baseline feature ID.
- Load a real pre-v1.0.1 project, preset pack, LUT library and custom filters; verify exact parameter values and preview look; save/open again.
- Compare UI navigation, filter visibility, default slider values, keyboard shortcuts, Timeline/seek, drag-and-drop, export presets and error handling against v1.0.0.
- Prohibit screenshot/recording features and any export path based on screen capture, `MediaRecorder`, `getDisplayMedia` or display framebuffer recording.

### B. Multi-GPU parity

- Test at least one compatible NVIDIA, AMD and Intel GPU on Windows; Apple Silicon Metal/VideoToolbox on macOS; Linux Vulkan/VAAPI on a supported device. Use CI build checks plus physical/virtual device tests where practical. Flag untested hardware explicitly.
- Exercise hardware and software fallback with codec unsupported, hardware encoder busy/missing, device loss, unsupported LUT/custom shader and corrupted input.
- Test MP4/MKV/MOV/WebM plus other formats **currently supported by v1.0.0**, H.264/H.265/AV1 where available, VFR, 10-bit, HDR, unusual resolution/aspect and multiple tracks.

### C. Image quality and export semantics

- Reference clips: neutral color chart, sharp edges, low light, strong motion, scene cuts, LUT reference grid and custom GLSL/SVG/Canvas samples.
- Compare v1.0.0 export output to v1.0.1 export at matched **source presentation timestamps** with PSNR/SSIM and color-difference metrics; define calibrated tolerances by filter, precision and codec **before** running acceptance. Investigate any material change; lossy codec pixel identity is not required.
- Check Before/After/preview vs exported file at matching timestamps, color space and decode path; verify effect is actually burned into a newly encoded video when opened in an external player.
- Validate A/V sync, duration, rotation, audio track selection, subtitle handling and metadata against baseline.

### D. Performance and stability

- Benchmark identical workload/presets/encoder settings on v1.0.0 versus v1.0.1: 1080p SDR, 4K SDR, 10-bit/HDR if supported, simple filters, heavy UVE stack, LUT plus custom shader.
- Report frame throughput, whole-job duration, CPU load, peak RAM/VRAM and exact GPU/driver/FFmpeg versions; distinguish filter speed from hardware encoding speed. No invented percentage claims.
- Long-running export, concurrent UI usage, video seek while idle/previewing, device reset, render cancellation, out-of-disk-space, missing source or LUT and app restart must not corrupt projects.

**Release blocker:** any loss of existing feature; project/preset incompatibility; unhandled unsupported-device crash; preview/export material mismatch; export by capture; undisclosed codec changes; failure to create correct new output file.

## 9. Implementation order (incremental, reversible)

1. Freeze v1.0.0 baseline: tag commit, capture reference exports and screenshots *for internal test fixtures only*, run baseline tests, generate feature matrix.
2. Introduce `EngineFacade` without changing working UI and keep old implementation as fallback.
3. Add FFmpeg capability probing and versioned hardware backend selection with reliable software fallback.
4. Implement wgpu filter graph in vertical slices: one built-in filter, LUT, complex stack, auto/temporal, custom GLSL, SVG and Canvas compatibility; validate each against v1.0.0 before proceeding.
5. Connect the same graph to preview and real offline render. Implement surface transfer and synchronization; add vendor-specific interop only after baseline works.
6. Add profiling/diagnostics, cached shader compilation, bounded queues and selective shader fusion; measure improvements.
7. Validate Windows AMD/Intel/NVIDIA, macOS and Linux on real hardware; package platform builds; complete v1.0.0 regression matrix.
8. Release v1.0.1 only when all preserved features work. Otherwise keep v1.0.0 as stable release and treat new backend as opt-in experimental branch.

## 10. Required output from implementation agent

Deliver **source code** for the existing project (not a standalone demonstration), the updated v1.0.1 app, automatic tests and fixtures, `migration/feature-parity-v1.0.0.json`, capability probe results, per-platform build instructions, benchmark report, migration/rollback notes and a concise `CHANGELOG_1.0.1.md`. Do not include old V1–V4 implementation documents in this v1.0.1 change package. Maintain original license notices and review redistribution requirements of FFmpeg and any shipped codec libraries.

### Direct instruction to the coding agent

> Implement **v1.0.1 as a non-breaking migration on top of our completed v1.0.0 codebase**. Before edits, inventory and test **every existing feature**, using actual source and binary as truth. Preserve UI, projects, all UVE effects, LUTs, profiles, custom filters, timeline/player and offline video export in full. Add portable wgpu GPU filter processing and FFmpeg runtime-selected NVIDIA/AMD/Intel/Apple hardware encode/decode with tested software and old-engine fallbacks. No screenshot, recording, or capture export. Do not claim cross-platform compatibility, zero-copy or full parity before evidence. Deliver tests and benchmark comparisons, then version as 1.0.1.
