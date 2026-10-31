// Engine facade (v1.0.1): one interface for the GPU filter backends that execute the UVE effect graph.
//   webgpu – vendor-neutral WGSL engine (Chromium WebGPU / Dawn → D3D12, Vulkan, Metal)
//   webgl2 – the unchanged v1.0.0 engine (ANGLE), kept as legacy/fallback path and for custom GLSL
import type { CustomFilterEntry } from '../../shared/uve/customFilters';
import type { RenderEngine } from '../../shared/uve/engine-facade';
import type { UveGraph } from '../../shared/uve/graph';
import type { Mat4x5 } from '../../shared/uve/matrix';

export type EngineKind = 'webgpu' | 'webgl2';
export type EnginePreference = 'auto' | 'webgpu' | 'webgl2';

export interface ProcessOptions {
    autoMatrix: Mat4x5 | null;
    time: number;
    /** processing pixels per source pixel (preview quality); 1 = exact */
    pxScale: number;
    quantize8: boolean;
    /** matrix substituted for `clip: true` matrix prims (timeline per-clip look); identity when null/absent */
    clipMatrix?: Mat4x5 | null;
}

export interface CustomDiagnostic { entry: CustomFilterEntry; error: string | null }

export interface StageTimes { upload: number; process: number; readback: number; frames: number }

export interface FrameEngine {
    readonly kind: EngineKind;
    /** Adapter / renderer description reported by the browser (vendor, architecture, device) */
    readonly adapter: string;
    readonly times: StageTimes;
    uploadVideo(video: HTMLVideoElement): void;
    uploadRGBA8(data: Uint8Array, w: number, h: number): void;
    uploadRGBA16(data: Uint16Array, w: number, h: number): void;
    setGraph(graph: UveGraph): void;
    /** Compiles custom GLSL entries (webgl2) or reports them as unsupported (webgpu). */
    setCustomFilters(entries: CustomFilterEntry[]): CustomDiagnostic[];
    updateCustomUniforms(entries: CustomFilterEntry[]): void;
    /** Runs the graph on the uploaded frame; the result stays inside the engine until the next call. */
    process(outW: number, outH: number, opts: ProcessOptions): void;
    readonly hasFrame: boolean;
    readonly outputSize: { w: number; h: number };
    readOutput8(into?: Uint8Array): Promise<Uint8Array>;
    readOutput16(into?: Uint16Array): Promise<Uint16Array>;
    /** Small resampled readback of the processed output, the original or the composited timeline frame (scopes, LUT previews, tests). */
    readScope(which: 'output' | 'source' | 'timeline', w: number, h: number): Promise<Uint8Array>;
    /** mode 0 = enhanced, 1 = original, 2 = split wipe */
    present(canvasW: number, canvasH: number, mode: 0 | 1 | 2, split: number): void;
    dispose(): void;
    /**
     * The timeline render surface of this engine (layer slots + composite). Same object on every call.
     * Wrap it with `engineFacade(engine.asRenderEngine(), 'preview' | 'export')` from
     * shared/uve/engine-facade, or use `timelineEngineOf` from ./createEngine.
     */
    asRenderEngine(): RenderEngine;
}

export function hasCustomGlsl(entries: CustomFilterEntry[]) {
    return entries.some(e => e.enabled && e.type === 'webgl');
}
