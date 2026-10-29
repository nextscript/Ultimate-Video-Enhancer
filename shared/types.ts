import type { UveSettings } from './uve/settings';
import type { ExpertSettings } from './uve/expert';
import type { CustomFilterEntry } from './uve/customFilters';
import type { LutProfile } from './uve/lut';
import type { AutoKey } from './uve/auto';

export const APP_NAME = 'UVE Manager';
export const APP_VERSION = '1.0.2';
export const UVE_SOURCE = {
    name: 'Ultimate Video Enhancer (Sharpen, HDR, Color Boost)',
    version: '1.17.5',
    repo: 'https://github.com/nextscript/Ultimate-Video-Enhancer',
    commit: '38b2ed18c62e6cbd45f0672e0183cb1d1c4a5da1',
    sha256: '28b37dfa45131f9aa51f5f392b3131f9c533e869351e495a23768817524069a6'
};
export const LUT_PACKAGE = {
    file: 'LUTsProfiles_v3.0.zip',
    url: 'https://raw.githubusercontent.com/nextscript/Ultimate-Video-Enhancer/main/LUTsProfiles_v3.0.zip',
    fallbackUrl: 'https://github.com/nextscript/Ultimate-Video-Enhancer/raw/refs/heads/main/LUTsProfiles_v3.0.zip'
};

/** Portable FFmpeg build placed in tools/ffmpeg (GPL build with NVENC, x264, x265, SVT-AV1, zimg). */
export const FFMPEG_BUILD = {
    api: 'https://api.github.com/repos/BtbN/FFmpeg-Builds/releases/latest',
    assetPattern: /^ffmpeg-master-latest-win64-gpl\.zip$/,
    directUrl: 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip'
};

/** v1.0.2: UVE Manager releases (Windows, macOS, Linux) are published in the UVE repository */
export const DEFAULT_UPDATE_REPO = 'nextscript/Ultimate-Video-Enhancer';

export interface VideoStreamInfo {
    index: number;
    codec: string;
    codecLong: string;
    profile: string;
    width: number;
    height: number;
    /** Display size after rotation and sample aspect ratio */
    displayWidth: number;
    displayHeight: number;
    sar: string;
    fps: number;
    fpsNum: number;
    fpsDen: number;
    avgFps: number;
    vfr: boolean;
    pixFmt: string;
    bitDepth: number;
    colorSpace: string;
    colorPrimaries: string;
    colorTransfer: string;
    colorRange: string;
    rotation: number;
    hdr: boolean;
    alpha: boolean;
    frames: number;
    bitRate: number;
    title: string;
}

export interface AudioStreamInfo {
    index: number;
    codec: string;
    codecLong: string;
    channels: number;
    layout: string;
    sampleRate: number;
    bitRate: number;
    language: string;
    title: string;
}

export interface SubtitleStreamInfo { index: number; codec: string; language: string; title: string }

export interface MediaInfo {
    path: string;
    name: string;
    size: number;
    mtimeMs: number;
    container: string;
    containerLong: string;
    duration: number;
    startTime: number;
    bitRate: number;
    video: VideoStreamInfo | null;
    videoStreams: VideoStreamInfo[];
    audio: AudioStreamInfo[];
    subtitles: SubtitleStreamInfo[];
    encrypted: boolean;
    warnings: string[];
}

export type Container = 'mp4' | 'mkv' | 'mov' | 'webm';
export type VideoCodec = 'h264' | 'hevc' | 'av1' | 'vp9' | 'prores';

export interface ExportSettings {
    container: Container;
    videoCodec: VideoCodec;
    encoder: 'auto' | 'gpu' | 'cpu';
    /** CRF (CPU) / CQ (NVENC) – lower = better quality */
    quality: number;
    preset: 'fast' | 'medium' | 'slow';
    tenBit: boolean;
    resolution: 'source' | '2160' | '1440' | '1080' | '720' | '480' | 'x2';
    audio: 'copy' | 'aac' | 'opus' | 'none';
    audioBitrate: number;
    audioTrack: number;
    subtitles: 'none' | 'copy';
    outputDir: string;
    fileName: string;
    useInOut: boolean;
    /** REAL-Video-Enhancer TensorRT model id for AI upscaling, '' = off */
    aiUpscale: string;
    /** Output frame rate (e.g. "30", "29.97", "24000/1001"); '' = source. Frames are duplicated/dropped, not interpolated. */
    fps: string;
}

export const DEFAULT_EXPORT: ExportSettings = {
    container: 'mp4', videoCodec: 'hevc', encoder: 'auto', quality: 20, preset: 'medium', tenBit: false,
    resolution: 'source', audio: 'copy', audioBitrate: 192, audioTrack: -1, subtitles: 'none',
    outputDir: '', fileName: '{name}_UVE', useInOut: false, aiUpscale: '', fps: ''
};

/** Everything that changes the processed pixels; snapshotted into every render job. */
export interface EffectState {
    settings: UveSettings;
    expert: ExpertSettings;
    customFilters: CustomFilterEntry[];
    /** Embedded copy of the active LUT so projects and jobs stay reproducible. */
    lut: LutProfile | null;
}

export interface UveProject {
    schema: 'uve-project';
    version: 1;
    app: string;
    uveSource: typeof UVE_SOURCE;
    savedAt: string;
    media: { path: string; size: number; mtimeMs: number; videoStream: number; audioStream: number } | null;
    effect: EffectState;
    inPoint: number | null;
    outPoint: number | null;
    export: ExportSettings;
    ui: { wipe: number; compare: boolean };
}

export interface UserProfile {
    id: string;
    name: string;
    createdAt: number;
    updatedAt: number;
    settings: UveSettings;
    /** Desktop extension (ignored by the userscript importer): expert overrides, custom filter stack, embedded LUT */
    desktop?: { expert?: Record<string, unknown>; customFilters?: CustomFilterEntry[]; lut?: LutProfile | null };
}

export interface Capabilities {
    ok: boolean;
    error?: string;
    ffmpegPath: string;
    ffprobePath: string;
    ffmpegVersion: string;
    source: 'portable' | 'custom' | 'path' | 'none';
    encoders: Record<string, boolean>;
    /** v1.0.0 field, kept for compatibility (= hwEncoders restricted to NVENC) */
    nvencWorking: Record<string, boolean>;
    /** v1.0.1: every hardware encoder verified by a scratch encode, e.g. { hevc_nvenc: true, hevc_amf: false } */
    hwEncoders: Record<string, boolean>;
    platform: string;
    gpuVendor: string;
    hwaccels: string[];
    filters: Record<string, boolean>;
    gpuName: string;
    gpuDriver: string;
}

export interface JobSpec {
    id: string;
    name: string;
    input: string;
    output: string;
    media: MediaInfo;
    effect: EffectState;
    autoKeys: AutoKey[] | null;
    exportSettings: ExportSettings;
    range: { start: number; end: number };
    parityMode: boolean;
    /** v1.0.1: filter engine preference for this job (defaults to the Settings value) */
    enginePref?: 'auto' | 'webgpu' | 'webgl2';
}

export type JobStatus = 'queued' | 'analyzing' | 'rendering' | 'upscaling' | 'finalizing' | 'done' | 'failed' | 'canceled';

export interface JobState {
    id: string;
    name: string;
    input: string;
    output: string;
    status: JobStatus;
    progress: number;
    frames: number;
    totalFrames: number;
    fps: number;
    elapsed: number;
    eta: number;
    outSize: number;
    encoder: string;
    error?: string;
    log: string[];
    createdAt: number;
    /** v1.0.1: filter engine + adapter actually used */
    engine?: string;
    /** v1.0.1: measured stage times (ms, summed over all frames) */
    stats?: { frames: number; totalMs: number; decodeWait: number; upload: number; gpu: number; readback: number; encodeWait: number; heapMB: number; engine: string; adapter: string };
}

export interface LutLibraryState {
    profiles: LutProfile[];
    groups: string[];
    repo: { status: 'idle' | 'downloading' | 'ok' | 'error' | 'offline'; message: string; sha256?: string; downloadedAt?: number; count?: number };
}

export interface UpdateItem {
    id: 'app' | 'ffmpeg' | 'luts' | 'rve';
    label: string;
    current: string;
    latest: string;
    notes?: string;
    downloadUrl?: string;
    size?: number;
}

export interface UpdateCheckResult {
    checkedAt: number;
    items: UpdateItem[];
    errors: string[];
}

export interface ToolsStatus {
    dir: string;
    ffmpeg: string;
    ffprobe: string;
    installed: boolean;
    version: string;
    build: string;
    installedAt?: number;
}

export interface AppPrefs {
    ffmpegDir: string;
    /** v1.0.1: GPU filter engine – Automatic (WebGPU, validated) / WebGPU / WebGL2 (v1.0.0 engine) */
    gpuEngine: 'auto' | 'webgpu' | 'webgl2';
    /** v1.0.1: hardware decoding – Automatic / Prefer Hardware / Software */
    hwDecode: 'auto' | 'hardware' | 'software';
    autoUpdate: boolean;
    /** GitHub "owner/repo" whose releases provide UVE Manager updates */
    updateRepo: string;
    skippedVersions: Record<string, string>;
    autoDownloadLuts: boolean;
    previewQuality: 'full' | 'half' | 'quarter';
    parityMode: boolean;
    lastOutputDir: string;
    recentProjects: string[];
    recentFiles: string[];
    shortcuts: Record<string, string>;
    defaultExport: ExportSettings;
}

export interface AnalysisResult {
    key: string;
    t0: number;
    count: number;
    /** stats packed as Float32 [mR,mG,mB,mY,sdY,mCh,mSat,skin] per sample */
    stats: Float32Array;
    /** motion luma 48x27 per sample */
    luma: Uint8Array;
}
