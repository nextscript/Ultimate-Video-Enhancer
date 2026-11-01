// v1.0.7: types shared by the Video2X provider (main process) and the settings / export UI.

/** One shader file of a preset chain. */
export interface Video2xShaderFile {
    /** display name (original file name) */
    name: string;
    /** absolute path: the UVE-managed copy (mode copy) or the user's file (mode reference) */
    path: string;
    /** SHA-256 recorded at import time */
    sha256: string;
    size: number;
    enabled: boolean;
    mode: 'copy' | 'reference';
    /** where the file came from (copy mode: the original path) */
    origin?: string;
}

export interface Video2xPreset {
    id: string;
    displayName: string;
    kind: 'built-in' | 'custom';
    /** built-in presets: the Video2X shader name passed to --libplacebo-shader */
    builtinShader?: string;
    /** custom presets: ordered shader passes (files are concatenated in this order) */
    shaderFiles: Video2xShaderFile[];
    source?: string;
    author?: string;
    license?: string;
    createdAt?: number;
}

export type Video2xFileState = 'ok' | 'missing' | 'modified' | 'invalid';

/** Preset as shown in the UI, with the result of the last file / directive check. */
export interface Video2xPresetInfo extends Video2xPreset {
    available: boolean;
    /** why the preset cannot be used right now ('' = usable) */
    problem: string;
    files: { state: Video2xFileState; message: string }[];
    /** enabled passes in the chain */
    passes: number;
    /** last test run with the actual Video2X binary (null = never tested) */
    tested: { ok: boolean; at: number; message: string } | null;
}

export interface Video2xDevice { index: number; name: string; type: string }

export type Video2xState = 'installed' | 'not-installed' | 'unsupported' | 'verification-failed' | 'unverified';

export interface Video2xInstallInfo {
    version: string;
    asset: string;
    url: string;
    size: number;
    sha256: string;
    releaseId: number;
    assetId: number;
    license: string;
    vendor: string;
    /** human readable statement about checksum / signature verification */
    integrity: string;
}

export interface Video2xStatus {
    /** Video2X can be used on this OS / CPU at all */
    platformSupported: boolean;
    platformReason: string;
    state: Video2xState;
    /** short status line, e.g. "Video2X 6.4.0 | Vulkan verified" */
    summary: string;
    /** exact diagnostic of the last failure ('' = none) */
    diagnostic: string;
    source: 'managed' | 'custom' | 'env' | 'none';
    exe: string;
    version: string;
    versionSupported: boolean;
    pinnedVersion: string;
    updateAvailable: boolean;
    managed: { version: string; dir: string; installedAt: number; sourceUrl: string; sha256: string } | null;
    customExe: string;
    vulkan: { status: 'verified' | 'devices-found' | 'none' | 'unknown' | 'error'; message: string; devices: Video2xDevice[] };
    /** e.g. "libplacebo · 7 built-in Anime4K shaders" */
    shaderSupport: string;
    builtinShaders: string[];
    verification: { ok: boolean; at: number; message: string; codec: string } | null;
    presets: Video2xPresetInfo[];
    install: Video2xInstallInfo | null;
    warnings: string[];
    busy: boolean;
    message: string;
    logFile: string;
    shaderDir: string;
}
