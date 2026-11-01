// v1.0.7: explicit AI upscale provider. `ExportSettings.aiUpscale` (the REAL-Video-Enhancer model id) is kept
// unchanged for compatibility; `upscaleProvider` selects which provider runs. Projects, presets and queued jobs
// written by 1.0.6 and older have no `upscaleProvider`: they keep their meaning (model id set = RVE, empty = off).
// A missing provider is NEVER interpreted as Video2X.
import type { ExportSettings } from './types';
import { VIDEO2X_PROCESSORS, type Video2xProcessor } from './video2x-processors';

export type UpscaleProvider = 'off' | 'rve' | 'video2x';

export const UPSCALE_PROVIDERS: UpscaleProvider[] = ['off', 'rve', 'video2x'];

export const PROVIDER_LABEL: Record<UpscaleProvider, string> = {
    off: 'Off',
    rve: 'REAL-Video-Enhancer',
    video2x: 'Video2X (Anime4K)'
};

/** Video2X export options stored in `ExportSettings.video2x`. */
export interface Video2xExportOptions {
    /** preset id: `builtin:<shader>` or `custom:<id>` */
    presetId: string;
    /** upscale factor relative to the rendered UVE frame; the Resolution choice (if not Source) wins */
    scale: number;
    /** Vulkan device index; -1 = automatic (device 0, the first adapter Video2X reports) */
    device: number;
    /** v1.0.8: processor; undefined = 'libplacebo' (Anime4K, exactly the 1.0.7 behaviour) */
    processor?: Video2xProcessor;
    /** v1.0.8: Real-ESRGAN / Real-CUGAN model id (allowlisted against the installed binary before use) */
    modelId?: string;
    /** v1.0.8: Real-CUGAN noise level (-1 conservative, 0 none, 1-3 denoise) */
    noiseLevel?: number;
    /**
     * v1.0.8: neural models have a fixed scale. When the Resolution choice differs from the model output, the
     * job is refused unless this is true (then FFmpeg resizes the model output with Lanczos, stated in the log).
     */
    postResize?: boolean;
}

export const VIDEO2X_SCALES = [2, 3, 4];
export const DEFAULT_VIDEO2X_PRESET = 'builtin:anime4k-v4-a';
export const DEFAULT_VIDEO2X: Video2xExportOptions = { presetId: DEFAULT_VIDEO2X_PRESET, scale: 2, device: -1 };

/** The provider an export uses. Absent provider = legacy meaning of `aiUpscale`. */
export function resolveUpscaleProvider(s: Pick<ExportSettings, 'aiUpscale' | 'upscaleProvider'>): UpscaleProvider {
    const p = s.upscaleProvider;
    if (p === 'video2x') return 'video2x';
    if (p === 'off') return 'off';
    // 'rve' or absent / unknown: RVE only when a model is chosen
    return s.aiUpscale ? 'rve' : 'off';
}

/** Sanitized Video2X options (defaults for missing / invalid values). */
export function video2xOptions(s: Pick<ExportSettings, 'video2x'>): Video2xExportOptions {
    const o = s.video2x;
    const presetId = typeof o?.presetId === 'string' && /^(builtin|custom):[\w.+-]{1,80}$/.test(o.presetId) ? o.presetId : DEFAULT_VIDEO2X.presetId;
    const scale = VIDEO2X_SCALES.includes(Number(o?.scale)) ? Number(o!.scale) : DEFAULT_VIDEO2X.scale;
    const device = Number.isInteger(o?.device) && o!.device >= -1 && o!.device < 64 ? o!.device : -1;
    const out: Video2xExportOptions = { presetId, scale, device };
    // v1.0.8 optional fields: only carried when present and valid (unknown values fall back to the Anime4K path)
    if (o?.processor && VIDEO2X_PROCESSORS.includes(o.processor)) out.processor = o.processor;
    if (typeof o?.modelId === 'string' && /^[\w.+-]{1,64}$/.test(o.modelId)) out.modelId = o.modelId;
    if (Number.isInteger(o?.noiseLevel) && o!.noiseLevel! >= -1 && o!.noiseLevel! <= 3) out.noiseLevel = o!.noiseLevel;
    if (o?.postResize === true) out.postResize = true;
    return out;
}

/** v1.0.8: processor of a Video2X export (missing / unknown = libplacebo, i.e. Anime4K). */
export const video2xProcessor = (o: Pick<Video2xExportOptions, 'processor'> | undefined): Video2xProcessor =>
    o?.processor && VIDEO2X_PROCESSORS.includes(o.processor) ? o.processor : 'libplacebo';

/**
 * Patch that switches the provider without touching unrelated settings. Switching to Off also clears the RVE model
 * (so the settings keep their pre-1.0.7 meaning); switching to Video2X keeps it, so RVE can be selected again later.
 */
export function providerPatch(s: ExportSettings, p: UpscaleProvider): Partial<ExportSettings> {
    const patch: Partial<ExportSettings> = { upscaleProvider: p };
    if (p === 'off') patch.aiUpscale = '';
    if (p === 'video2x' && !s.video2x) patch.video2x = { ...DEFAULT_VIDEO2X };
    // the Lanczos 2x choice is meaningless together with an AI upscaler
    if (p !== 'off' && s.resolution === 'x2') patch.resolution = 'source';
    return patch;
}
