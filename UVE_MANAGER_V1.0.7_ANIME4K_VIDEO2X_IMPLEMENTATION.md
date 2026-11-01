# Ultimate Video Enhancer — UVE Manager v1.0.7
## Video2X / Anime4K v4 High-Speed Upscaling Integration

**Status:** Implementation specification (not implemented by this document)  
**Target branch:** `uve-manager`  
**Baseline:** UVE Manager v1.0.6  
**Scope:** Only new v1.0.7 changes; existing v1.0.0–1.0.6 implementation must remain functional.  
**UI language:** English.

## 1. Objective and non-negotiable requirements

Integrate **Video2X + Anime4K v4** as an optional *Fast Upscale* provider, alongside the existing REAL-Video-Enhancer (RVE) integration. Add discovery and import of custom mpv-compatible `.glsl` shader files, preset management, hardware detection, safe installation, progress, cancelation, and reliable output handling. Do **not** replace or reimplement existing UVE effects, multi-track timeline, clip editing, transitions, audio mixing, RVE TensorRT/NCNN/PyTorch, render formats, keyboard shortcuts, WebGPU/WebGL2 rendering, or existing project files.

**Interpretation:** This release uses Video2X as an external process. **Native conversion of Anime4K GLSL to WebGL2 or WGSL is NOT part of v1.0.7.** Video2X already supplies libplacebo-based handling of supported mpv GLSL formats. The existing UVE WebGPU and WebGL2 graphics engines must not attempt to compile imported mpv shaders directly.

Definition of done: A user on a supported platform can choose `AI Upscale → Video2X (Anime4K v4)`, choose a preset or a trusted custom GLSL, render an existing edited timeline at the selected output resolution, observe real progress, cancel safely, and obtain synchronized video/audio without breaking previous exporters.

## 2. Verified baseline and integration points

Read the actual current code before modifying it:

- `README.md`: UVE Manager v1.0.6, supported systems, RVE installation and backend selection.
- `electron/render.ts`: render queue and jobs; `ActiveJob.stage1`, `.upscale`, existing RVE second-stage handling; timeline/audio mux logic.
- `electron/rve.ts`: existing RVE installation, detection, model selection, progress and cancelation patterns.
- `shared/types.ts`: `ExportSettings`, `JobSpec`, `JobState`, serialization types.
- `src/ui/RenderingSettings.tsx`: GPU preference, hardware discovery UI.
- Output settings component(s): current `AI Upscale` choice and settings.
- `docs/RENDER_PIPELINE.md`: FFmpeg → UVE GPU filter → encoder workflow; CPU↔GPU transfers; processing of timeline.
- `shared/uve/export-clip-graph.ts` and existing render-worker implementation: timeline composition and audio handling.
- Project settings persistence and project format documentation (`docs/PROJECT_SCHEMA.md`).

Do not assume older version layouts or function names. Locate current entry points and reuse existing job status/error handling.

### 2.1 Target architecture

```text
Media + tracks + transitions + UVE effects + audio
    |
    v
Existing UVE timeline renderer / export worker (unchanged effect parity)
    |
    +--> Upscale = Off -------> existing final encode/mux path
    |
    +--> Upscale = RVE -------> existing RVE stage 2 + mux path (unchanged)
    |
    +--> Upscale = Video2X --> lossless intermediate (stage 1)
                                |
                                v
                         Video2X CLI / Anime4K GLSL (stage 2)
                                |
                                v
                        probe output + audio/subtitle mux
                                |
                                v
                        atomic final file rename
```

Use one distinct `UpscaleProvider` discriminator (`off | rve | video2x`). The provider is selected explicitly, not guessed from the name of a shader or model. Avoid having two independent upscaling providers active simultaneously for the same export unless a future deliberately designed pipeline supports chaining.

**Performance limitation:** The baseline architecture needs an intermediate UVE render, so total time = UVE render + Video2X processing + remux. This release does not claim zero-copy or guarantee a specific FPS. Avoid lossy compression in the intermediate to protect quality. Keep storage requirements visible in UI and clean temporary files after success/cancel/failure.

## 3. Platform / GPU support and capability discovery

Video2X is Vulkan-based. Distinguish UVE overall OS support from Video2X provider support:

| Platform | UVE Manager | Video2X provider in v1.0.7 |
|---|---|---|
| Windows x64 | Supported | Supported if compatible executable + Vulkan GPU/driver found |
| Linux x64 | Supported | Supported if compatible binary/AppImage + Vulkan available |
| macOS Intel/Apple Silicon | Supported | **Not presumed supported**; show disabled until an actually tested, distributable build and runtime path exists |

Do not label Apple Silicon as universally Video2X-compatible merely because UVE or other backends support macOS. Existing RVE options stay available on macOS.

Detection workflow:
1. Determine platform/architecture, CPU prerequisites, graphics adapter, Vulkan ICD/runtime availability (if feasible).
2. Find configured Video2X executable or an app-managed installation.
3. Execute `--version` and `--help` with timeouts and without shell invocation. Inspect supported options/features from the installed version, rather than hardcoding flags on trust.
4. Verify Anime4K/libplacebo capability and the requested shader preset using a short synthetic test job.
5. Expose `Installed`, `Not installed`, `Unsupported`, `Verification failed`, and the exact diagnostic.
6. Re-run checks after GPU/driver changes, binary updates, or custom path edits.

Do not pretend Vulkan is guaranteed available merely from detecting NVIDIA/AMD/Intel hardware. Never fall back silently to RVE if Video2X fails; report the issue and offer an explicit provider change.

## 4. Installer, tool management, security

New settings group: **Settings → AI Upscaling → Video2X**.

Controls:
- `Install Video2X` / `Update Video2X` / `Remove managed installation`
- `Select existing executable…`
- `Verify installation`
- `Installed version`, `Executable path`, `Vulkan status`, `Shader support`
- `Open logs` and `Reset to default`

Implementation:
- Retrieve only official upstream releases from `https://github.com/k4yt3x/video2x/releases` or official documented distribution channel. Prefer stable version pinned in the app compatibility matrix rather than “latest” without testing.
- Show license, vendor and archive size before install. Verify checksums/signatures when upstream publishes them; otherwise record source URL and immutable release/asset identifiers and clearly label signature verification as unavailable.
- Stage downloads/extraction in a dedicated temporary directory; validate paths to prevent archive traversal, then atomically move into a versioned app-managed directory.
- Allow install cancelation, checksum failure recovery and removal; never delete user-provided executable locations.
- Never download binaries from untrusted mirror URLs by default. No admin-right requirement for normal user-scoped installation.
- Use `spawn(executable, argv, { shell:false, ... })`; validate paths and arguments; never interpolate shader file names into shell scripts.
- Scope executable access to the known installed binary or explicitly selected user executable; display warnings for unverified builds.
- Include upstream AGPL-3.0 notices and dependency notices in About/Third-party licenses. Review redistribution and packaging obligations before bundling Video2X, particularly if distributing modified/combined binaries. A process boundary does not by itself decide licensing obligations.

## 5. GLSL shader and presets support

### 5.1 Preset definitions

Provide an initial built-in **Anime4K v4 (Fast)** preset, verified against the chosen installed Video2X build and its available preset names. Add only presets confirmed by an actual binary capability check. Suggested UI labels (not assumed upstream identifiers):

- `Fast – Anime4K v4` (default)
- `Balanced – Anime4K v4` (only if a distinct verified shader configuration exists)
- `Custom GLSL…`

Do not invent upstream built-in preset identifiers. Resolve UI labels to validated CLI arguments in the adapter compatibility layer. Preserve shader ordering; a preset can be an ordered sequence of shader passes, not necessarily one file.

### 5.2 Custom GLSL import

New **Shader Library** in `Settings → AI Upscaling → Video2X → Shaders`:

- `Import .glsl` (one file or multi-file chain), `Import folder`, `Create preset`, `Rename`, `Duplicate`, `Remove`, `Open shader folder`.
- Show the ordered chain of shaders and allow `Move Up / Move Down`, `Enable / Disable` for each pass.
- Store custom shaders in a dedicated UVE-managed data directory or use read-only references to their source locations, according to the user's selected import behavior.
- Persist a content hash for each imported file and detect missing/modified source files.
- Parse/validate known mpv/libplacebo shader directives, detect missing dependencies/textures and reject invalid or unsupported variants with actionable errors.
- Show actual compiler/runtime diagnostics from a test run; a `.glsl` extension alone is **not** proof of compatibility.
- Prevent import outside user-chosen paths, path traversal in shader bundles, excessive shader file sizes and inadvertent execution of unrelated script formats.
- Treat shader code as GPU-executable untrusted content: warn before using external shader packs, avoid automatically running downloads, and support resetting to built-in presets.
- Record original source/author/license per preset; retain original license texts on redistribution.

### 5.3 Rendering and preview

Imported mpv-style GLSL is executed by Video2X/libplacebo in stage 2. The Electron player preview must **not** falsely claim pixel-identical shader preview. Offer either:

1. `Render 5-second preview` via the actual Video2X pipeline (preferred v1.0.7); compare with original and cache by source hash, timeline range, effects, shaders, and output settings; or
2. UI note: `Full-resolution preview requires a test render`.

Keep the existing live UVE preview, effects, before/after wipe, and scopes intact. Any thumbnail preview should be labeled `approximate` unless produced by the exact target pipeline.

## 6. Export UI design (English)

Add Video2X as an option under the existing `Output Settings → AI Upscale` flow without disturbing RVE model selection.

```text
AI Upscale
  Provider: [Off | REAL-Video-Enhancer | Video2X (Anime4K)]

  When Video2X selected:
  Engine:        Video2X / libplacebo
  Preset:        [Fast – Anime4K v4      v]
  Shader Chain:  [1 shader / custom chain]
                 [Manage Shaders] [Import GLSL]
  Scale:         [2x v]  or  Target Resolution: [3840 x 2160]
  Processing:    [GPU: Auto v]
  Preview:       [Render 5-second preview]
  Status:        Video2X 6.x | Vulkan verified
  [Install / Verify / Configure]

  Export:
  Encoder:       [existing codec / hardware settings]
  Output FPS:    [preserve source/timeline FPS]
  [Add to Render Queue]
```

- Preserve user-defined output dimensions. Validate even dimensions and codec requirements.
- Show source-to-output resolution and realistic estimated disk requirements for intermediate material.
- Maintain the existing encoder selection and final codec/container controls. Do not force a specific codec or FPS without user consent.
- Hide/disable invalid options based on platform and tested executable capabilities, with an explanatory tooltip.
- Accessible keyboard navigation, localization readiness, usable progress and no duplicate incompatible provider dropdowns.

## 7. Code changes: suggested module structure

Use the actual source tree and adapt names to fit its conventions:

```text
electron/
  video2x.ts                  # provider adapter: probe, install, validate, launch, cancel, cleanup
  video2x-installer.ts        # optional separate installer
  video2x-shaders.ts          # import, chain validation, hash, license metadata
  render.ts                   # provider dispatch and Video2X stage-2 orchestration
shared/
  upscale-provider.ts        # discriminated provider settings / job union
  video2x-types.ts           # preset, capability, progress and failure types
src/ui/
  ... existing Output Settings component (+ Video2X section)
  Video2xSettings.tsx
  Video2xShaderLibrary.tsx
  Video2xPreview.tsx
  ... existing job queue (+ provider phase label)
tests/
  video2x.test.ts
  video2x-shaders.test.ts
  video2x-render.test.ts
  fixtures/video2x-mock/      # deterministic fake executable and log output
```

Suggested types (illustrative; map into existing type system without breaking wire format):

```ts
type UpscaleProvider = 'off' | 'rve' | 'video2x';

type Video2xPreset = {
  id: string;
  displayName: string;
  kind: 'built-in' | 'custom';
  shaderFiles: Array<{ path: string; sha256: string; enabled: boolean }>;
  source?: string;
  license?: string;
};

type Video2xOptions = {
  provider: 'video2x';
  presetId: string;
  targetWidth: number;
  targetHeight: number;
  gpuPreference: 'auto' | string;
};
```

**Compatibility:** Migrate old projects and render queue jobs without interpreting absent `upscale.provider` as Video2X. Old RVE jobs must continue to select RVE; absent upscaling must retain `Off`. Version the schema only if necessary and document migration + round-trip support. Include provider settings in saved `.uveproj` and job-retry snapshot. Never silently rewrite unrelated settings.

## 8. Video2X CLI adapter (version-aware)

Do not use a hardcoded command from a blog or assume the CLI is identical across releases. Check the executable's official help output and upstream docs during implementation. Build a versioned adapter and test actual arguments for:

- Input/output file paths
- Filter mode (`libplacebo` / Anime4K)
- Built-in shader preset or ordered custom shader chain
- Output width/height and frame-rate handling
- GPU selection if supported
- Output codec/container choice if supported
- Verbose/structured progress, signals and exit status

**Illustrative command only — not an approved CLI contract:**

```text
video2x [verified input/output/resolution options] \
  [verified libplacebo filter options] \
  [verified anime4k preset or custom GLSL options]
```

Define adapter methods `probe()`, `validatePreset()`, `buildArgs()`, `execute()`, `cancel()` and `classifyError()`; keep the raw CLI syntax isolated from UI and orchestration. Explicitly reject unsupported installed versions with a clear upgrade path.

## 9. Render queue, audio and state machine

Extend the current `electron/render.ts` two-stage architecture to support a **provider-specific** second stage. Preserve existing `RVE` implementation and public job events.

Suggested job phases:

```text
queued → preparing → rendering-uve → upscaling-video2x → muxing → verifying → completed
                                 ↘ canceled / failed
```

Job runner requirements:

1. Stage 1 renders the exact edited timeline with UVE effects, tracks, transitions and intended source FPS to a lossless or visually lossless intermediate with known color metadata.
2. Stage 2 launches Video2X with the verified shader preset and target resolution. Write to a private temp output; never overwrite source.
3. Preserve temporal order, source/timeline fps, pixel aspect ratio and appropriate color transfer metadata. Confirm with `ffprobe` after render. Handle HDR content explicitly (no accidental PQ/HLG ↔ SDR conversion).
4. Restore timeline audio mix, synced with the output video. Retain audio/subtitle mux logic already used for RVE, adapting intermediate and remux semantics to Video2X format outputs.
5. Honor user-chosen output encoder/settings where the backend allows. If Video2X encodes using a different codec, provide a final encode/remux stage if needed; avoid accidental double lossy encoding when possible and show the cost.
6. Capture stdout/stderr, parse version-specific progress, provide `Render phase`, FPS, elapsed time, estimated remaining time **only if measurable**. Progress must remain monotonic across phases.
7. On cancel, signal/terminate the entire Video2X child-process tree and any remaining FFmpeg job; delete app-owned temp files only. Never delete the original source or external shader files.
8. On error, keep original untouched, show exact error + guidance, and clean up or make temp retention an explicit diagnostic setting.
9. Before success: check exit codes, presence of output, nonzero media duration, correct dimensions/FPS, video frames and audio synchronization tolerance; atomically rename stage artifact to final destination.
10. Retry reuses stable job snapshot, verifies version/preset availability again, and does not accidentally duplicate queue entries.

**Isolation:** Only one active global render is permitted if that is the existing queue policy. Do not create parallel GPU jobs behind the existing queue manager.

## 10. Tests and acceptance criteria

### Unit tests
- Backend discrimination, legacy project migration, Video2X preference persistence.
- CLI arg assembly for spaces, Unicode, quotes, long Windows paths, malicious shader names; `shell:false` enforcement.
- Capability parser for supported/unsupported versions and Vulkan absence.
- Ordered shader chains; preset import/export; duplicate names; missing files; hash mismatch; unsupported directives.
- Progress parsing, failed exit codes, missing outputs, cancellation and timeout.

### Integration tests
- Windows x64 with NVIDIA RTX + Vulkan; AMD and Intel GPU tests where hardware available.
- Linux x64 Vulkan (NVIDIA, AMD or Intel).
- macOS: disabled state and unchanged RVE/export behavior unless Video2X is independently validated there.
- Single clip and multi-track timeline; transitions; reversed clips; variable source FPS; audio/video sync; transparent or rotated source metadata; 8-bit/HDR inputs.
- Output 1080p→2160p with 60 fps; compare final resolution, frame count, sample duration, color metadata.
- Render interrupted during stage 1, stage 2 and mux; verify no orphan FFmpeg/Video2X process, no corrupt final output, no source deletion.
- RVE TensorRT/NCNN/PyTorch and disabled-upscale regression tests.
- Test `.uveproj` save/reopen, queued-job persistence, installer update and offline operation after installation.

### Performance validation
Run the *same 30-second 1080p60 clip* on the same GPU/settings for:
- Existing RVE AnimeJaNai V2 2× TensorRT;
- New Video2X Anime4K v4 Fast;
- Optional Real-CUGAN when already available;
- Disabled AI upscale (UVE-only baseline).

Capture GPU name/driver, model/shader version, output resolution/fps, UVE stage time, upscale stage time, mux time, total wall time, peak VRAM and intermediate disk size. Report measured results; do **not** advertise 60–120 FPS or “X times faster” without benchmarks. Compare quality on identical frames and animated motion, not merely elapsed time.

## 11. Delivery checklist

- [ ] New `Video2X (Anime4K)` provider available alongside existing RVE.
- [ ] Optional installer, custom executable and compatibility checks implemented.
- [ ] Fast Anime4K preset validated against supported Video2X version(s).
- [ ] Custom mpv-compatible GLSL import, multi-pass chain and shader library functional.
- [ ] Timeline+UVE effects+audio preserved end to end.
- [ ] Render progress/cancel/error/cleanup and retry verified.
- [ ] Final output matches requested dimensions, fps, metadata and audio sync.
- [ ] Settings/projects backward-compatible; previous features untouched.
- [ ] Documentation updated: `README.md`, `docs/RENDER_PIPELINE.md`, provider usage notes, third-party licenses.
- [ ] Add **`CHANGELOG_1.0.7.md`** only for this release; do not remove or rewrite historical changelogs.
- [ ] Update app/package version to **1.0.7** and release workflow/package names without losing platform targets.
- [ ] Full test/build/packaging suite passes; unsupported platforms give accurate status.

## 12. Suggested implementation order

1. Baseline regression tests + identify current upscaling integration points.
2. Provider settings/types + backward-compatible serialization.
3. Video2X capability probe, installer and CLI compatibility adapter.
4. Renderer stage-2 provider dispatch + mux + cancel + atomic output.
5. Anime4K preset validation + GLSL library and custom shader chains.
6. Output Settings/Settings UI + progress and short exact-pipeline preview.
7. Multi-platform validation, comparative benchmarking, documentation and release v1.0.7.

## 13. Upstream sources and reference points

- UVE Manager branch: https://github.com/nextscript/Ultimate-Video-Enhancer/tree/uve-manager
- UVE render pipeline: https://github.com/nextscript/Ultimate-Video-Enhancer/blob/uve-manager/docs/RENDER_PIPELINE.md
- Video2X upstream: https://github.com/k4yt3x/video2x
- Video2X documentation: https://docs.video2x.org/
- Video2X releases: https://github.com/k4yt3x/video2x/releases
- Anime4K: https://github.com/bloc97/Anime4K
- REAL-Video-Enhancer: https://github.com/TNTwise/REAL-Video-Enhancer

**Important distinctions:** Anime4K GLSL is not equivalent to a large neural network model; speed/quality differ. Video2X support for Windows/Linux does not automatically mean macOS support. A two-stage implementation is not zero-copy and must not be marketed as such.
