// v1.0.7: version-aware Video2X command-line adapter (pure functions, no I/O).
//
// The raw CLI syntax lives only here. Everything is derived from the installed binary's own `--help` /
// `--version` output instead of being trusted from documentation; the option names below were checked against
// the Video2X 6.4.0 sources (tools/video2x/src/argparse.cpp, tag 6.4.0):
//   -i/--input -o/--output -p/--processor libplacebo --libplacebo-shader <name|path> -w/--width -h/--height
//   -c/--codec --pix-fmt -e/--extra-encoder-option key=value -d/--device <vk index> --no-copy-streams
//   --log-level --list-devices/-l --version/-V
// Notes from the sources: `--help` and `--version` exit with code 1 (not 0); progress is printed to stdout as
//   "\r\033[Kframe=<n>/<total> (<pct>%); fps=<f>; elapsed=hh:mm:ss; remaining=hh:mm:ss"
// and a custom shader path is embedded in an FFmpeg filter string as custom_shader_path='<path>', so a path
// handed to Video2X must never contain a quote (see isFilterSafePath).
import type { Video2xDevice } from './video2x-types';

/** Release UVE Manager installs and has tested. Updating the pin is a deliberate code change. */
export const VIDEO2X_COMPAT = {
    pinnedVersion: '6.4.0',
    /** oldest version the adapter accepts (if its --help also lists every required option) */
    minVersion: '6.1.0',
    /** newest major the adapter knows; a 7.x CLI is rejected until it is checked */
    maxMajor: 6,
    releaseId: 196728175,
    repo: 'https://github.com/k4yt3x/video2x',
    releasesPage: 'https://github.com/k4yt3x/video2x/releases',
    license: 'GNU AGPL-3.0',
    vendor: 'K4YT3X and contributors (github.com/k4yt3x/video2x)',
    assets: {
        // SHA-256 of the official release assets, computed by UVE Manager when the pin was set (upstream publishes
        // no checksums or signatures for these files)
        'win32-x64': {
            name: 'video2x-windows-amd64.zip', kind: 'zip' as const, assetId: 223235051, size: 196989277,
            sha256: '0337b6dcae2bad2fd13e43fbdb73ef728d2f6289f37cc3617e87ac3a1a5cf793', exe: 'video2x.exe'
        },
        'linux-x64': {
            name: 'Video2X-x86_64.AppImage', kind: 'appimage' as const, assetId: 223239426, size: 231232704,
            sha256: '8238fc38325107f0a08a140d069baee3db80c5ac7470b0a516cc683183f5f92d', exe: 'Video2X-x86_64.AppImage'
        }
    } as Record<string, { name: string; kind: 'zip' | 'appimage'; assetId: number; size: number; sha256: string; exe: string }>
};

export const assetUrl = (version: string, name: string) => `${VIDEO2X_COMPAT.repo}/releases/download/${encodeURIComponent(version)}/${encodeURIComponent(name)}`;

/** Video2X provider support of this platform (independent of UVE Manager's own OS support). */
export function video2xPlatform(platform: string, arch: string): { supported: boolean; key: string; reason: string } {
    const key = `${platform}-${arch}`;
    if (VIDEO2X_COMPAT.assets[key]) return { supported: true, key, reason: '' };
    if (platform === 'darwin') {
        return { supported: false, key, reason: 'Video2X is not available on macOS in UVE Manager 1.0.7: there is no tested, distributable macOS build of the Video2X CLI. REAL-Video-Enhancer (NCNN / PyTorch MPS) remains available.' };
    }
    return { supported: false, key, reason: `Video2X has no tested build for ${platform} ${arch} (supported: Windows x64, Linux x64 with Vulkan).` };
}

/* ------------------------------------------------------------------ version / help parsing */

export function parseVersion(out: string): string {
    const m = out.match(/Video2X\s+version\s+v?(\d+\.\d+\.\d+[\w.-]*)/i) || out.match(/\bv?(\d+\.\d+\.\d+)\b/);
    return m ? m[1] : '';
}

const num = (v: string) => v.split(/[.-]/).slice(0, 3).map(x => Number.parseInt(x, 10) || 0);
export function cmpVersion(a: string, b: string): number {
    const x = num(a), y = num(b);
    for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return (x[i] ?? 0) - (y[i] ?? 0);
    return 0;
}

/** Options the adapter needs; a binary whose --help lacks one of them is rejected. */
export const REQUIRED_OPTIONS = ['--input', '--output', '--processor', '--libplacebo-shader', '--width', '--height', '--codec', '--no-copy-streams'];

export interface Video2xCapabilities {
    version: string;
    versionSupported: boolean;
    /** reason when !versionSupported or options are missing ('' = usable) */
    problem: string;
    options: Set<string>;
    processors: string[];
    builtinShaders: string[];
    hasPixFmt: boolean;
    hasDevice: boolean;
    hasExtraEncoderOption: boolean;
    hasLogLevel: boolean;
    hasListDevices: boolean;
    hasNoProgress: boolean;
}

/** Capabilities from `--help` and `--version` output of the installed binary. */
export function parseHelp(help: string, versionOut: string): Video2xCapabilities {
    const options = new Set<string>([...help.matchAll(/(?:^|[\s,(])(--[a-z][\w-]*)/gm)].map(m => m[1]));
    const procLine = help.match(/--processor[^\n]*\n?[^\n]*/)?.[0] || '';
    const processors = [...new Set([...(procLine.match(/\(([^)]*)\)/)?.[1] || '').split(/[,\s]+/)].filter(x => /^[a-z][\w-]*$/.test(x)))];
    // built-in shader names listed after --libplacebo-shader (up to the next option)
    const shaderPart = help.split('--libplacebo-shader')[1]?.split(/\n\s*-{1,2}[a-z]/)[0] || '';
    const builtinShaders = [...new Set([...shaderPart.matchAll(/\banime4k-[\w.+-]*[\w+]/gi)].map(m => m[0].toLowerCase()))];
    const version = parseVersion(versionOut) || parseVersion(help);
    const missing = REQUIRED_OPTIONS.filter(o => !options.has(o));
    let problem = '';
    const major = num(version)[0];
    if (!version) problem = 'The Video2X version could not be determined (--version printed no version).';
    else if (cmpVersion(version, VIDEO2X_COMPAT.minVersion) < 0) problem = `Video2X ${version} is too old. UVE Manager needs Video2X ${VIDEO2X_COMPAT.minVersion} or newer (tested: ${VIDEO2X_COMPAT.pinnedVersion}) – use "Install Video2X ${VIDEO2X_COMPAT.pinnedVersion}" or select a newer executable.`;
    else if (major > VIDEO2X_COMPAT.maxMajor) problem = `Video2X ${version} is newer than the versions this UVE Manager release knows (${VIDEO2X_COMPAT.maxMajor}.x, tested ${VIDEO2X_COMPAT.pinnedVersion}). Its command line has not been checked – install the tested version ${VIDEO2X_COMPAT.pinnedVersion} or update UVE Manager.`;
    else if (missing.length) problem = `This Video2X build does not offer the options UVE Manager needs (${missing.join(', ')} missing in --help).`;
    else if (processors.length && !processors.includes('libplacebo')) problem = 'This Video2X build has no libplacebo processor (Anime4K shaders need it).';
    return {
        version, versionSupported: !problem, problem, options, processors, builtinShaders,
        hasPixFmt: options.has('--pix-fmt'), hasDevice: options.has('--device'), hasExtraEncoderOption: options.has('--extra-encoder-option'),
        hasLogLevel: options.has('--log-level'), hasListDevices: options.has('--list-devices'), hasNoProgress: options.has('--no-progress')
    };
}

/** `--list-devices` output: "0. NVIDIA GeForce RTX 3080" followed by indented "Type: Discrete GPU" lines. */
export function parseDevices(out: string): Video2xDevice[] {
    const list: Video2xDevice[] = [];
    for (const line of out.split(/\r?\n/)) {
        const d = line.match(/^\s{0,2}(\d+)\.\s+(\S.*)$/);
        if (d) { list.push({ index: Number(d[1]), name: d[2].trim(), type: '' }); continue; }
        const t = line.match(/^\s+Type:\s*(.+)$/);
        if (t && list.length) list[list.length - 1].type = t[1].trim();
    }
    return list;
}

/* ------------------------------------------------------------------ arguments */

/** Lossless intermediate formats for the Video2X output, in order of preference. */
export const LOSSLESS_CANDIDATES: { id: string; codec: string; pixFmt?: string; extra: string[]; label: string }[] = [
    { id: 'ffv1', codec: 'ffv1', pixFmt: 'gbrp', extra: [], label: 'FFV1 RGB (lossless)' },
    { id: 'x264rgb', codec: 'libx264rgb', extra: ['qp=0', 'preset=ultrafast'], label: 'H.264 RGB QP 0 (lossless)' }
];

export interface Video2xRunSpec {
    input: string;
    output: string;
    /** built-in shader name or absolute path of a (UVE-managed) GLSL file */
    shader: string;
    width: number;
    height: number;
    /** Vulkan device index (-1 = do not pass, Video2X uses device 0) */
    device: number;
    lossless: string;
}

/** Characters allowed in a path that ends up inside Video2X's FFmpeg filter string (quoted with '…'). */
export function isFilterSafePath(p: string): boolean {
    return /^[\p{L}\p{N} _\-.:/\\()~+@#]+$/u.test(p) && !p.includes('..');
}

/** argv for one Video2X run. Never joined into a shell command line. Throws on unsafe or impossible input. */
export function buildArgs(caps: Video2xCapabilities, r: Video2xRunSpec): string[] {
    if (!caps.versionSupported) throw new Error(caps.problem || 'Unsupported Video2X version.');
    for (const [k, v] of [['input', r.input], ['output', r.output]] as const) {
        if (!v || /[\u0000-\u001f]/.test(v)) throw new Error(`Invalid ${k} path.`);
        // a leading dash would be read as an option by boost::program_options
        if (v.startsWith('-')) throw new Error(`The ${k} path must be absolute.`);
    }
    const isBuiltin = caps.builtinShaders.includes(r.shader);
    if (!isBuiltin) {
        if (!/^([a-zA-Z]:[\\/]|\/)/.test(r.shader)) throw new Error(`Shader "${r.shader}" is neither a built-in shader of this Video2X build (${caps.builtinShaders.join(', ') || 'none'}) nor an absolute file path.`);
        if (!isFilterSafePath(r.shader)) throw new Error('The shader path contains characters Video2X cannot pass to libplacebo (quotes or control characters).');
        if (!/\.(glsl|hook)$/i.test(r.shader)) throw new Error('Custom shaders must be .glsl files.');
    }
    if (!(Number.isInteger(r.width) && Number.isInteger(r.height) && r.width >= 2 && r.height >= 2 && r.width % 2 === 0 && r.height % 2 === 0 && r.width <= 16384 && r.height <= 16384)) {
        throw new Error(`Invalid output size ${r.width}x${r.height} (even width and height up to 16384 required).`);
    }
    const ll = LOSSLESS_CANDIDATES.find(c => c.id === r.lossless) || LOSSLESS_CANDIDATES[0];
    const a = ['-i', r.input, '-o', r.output, '-p', 'libplacebo', '--libplacebo-shader', r.shader, '-w', String(r.width), '-h', String(r.height), '-c', ll.codec];
    if (ll.pixFmt) {
        if (!caps.hasPixFmt) throw new Error('This Video2X build has no --pix-fmt option for the lossless intermediate.');
        a.push('--pix-fmt', ll.pixFmt);
    }
    if (ll.extra.length) {
        if (!caps.hasExtraEncoderOption) throw new Error('This Video2X build has no --extra-encoder-option.');
        for (const e of ll.extra) a.push('-e', e);
    }
    a.push('--no-copy-streams');
    if (r.device >= 0) {
        if (!caps.hasDevice) throw new Error('This Video2X build cannot select a GPU (--device missing).');
        a.push('-d', String(r.device));
    }
    if (caps.hasLogLevel) a.push('--log-level', 'info');
    return a;
}

/* ------------------------------------------------------------------ progress / errors */

export interface Video2xProgress { frames: number; total: number; percent: number; fps: number; remaining: number }

const hms = (s: string) => { const p = s.split(':').map(Number); return p.length === 3 && p.every(Number.isFinite) ? p[0] * 3600 + p[1] * 60 + p[2] : -1; };

/** Parses one progress segment ("frame=12/240 (5.00%); fps=30.00; elapsed=…; remaining=…"); null = not a progress line. */
export function parseProgress(segment: string): Video2xProgress | null {
    // the CLI prefixes each update with "\r\033[K"
    const s = segment.replace(/\u001b\[[0-9;]*[A-Za-z]/g, '');
    const m = s.match(/frame=\s*(\d+)\s*\/\s*(\d+)/);
    if (!m) return null;
    const frames = Number(m[1]), total = Number(m[2]);
    const pct = s.match(/\(\s*([\d.]+)\s*%\)/);
    const fps = s.match(/fps=\s*([\d.]+|inf|nan)/i);
    const rem = s.match(/remaining=\s*([\d:]+)/);
    const fpsV = fps && /^[\d.]+$/.test(fps[1]) ? Number(fps[1]) : 0;
    return { frames, total, percent: pct ? Number(pct[1]) : total ? frames * 100 / total : 0, fps: fpsV, remaining: rem ? hms(rem[1]) : -1 };
}

export type Video2xErrorKind = 'vulkan' | 'shader' | 'encoder' | 'input' | 'output' | 'canceled' | 'crash' | 'oom' | 'model' | 'unknown';

/** Classifies a failed run from its exit code and the tail of its log, with guidance for the user. */
export function classifyError(code: number | null, signal: string | null, log: string): { kind: Video2xErrorKind; message: string } {
    const t = log.toLowerCase();
    const last = log.trim().split(/\r?\n/).filter(l => /error|critical|fail|invalid/i.test(l)).slice(-3).join(' | ').slice(0, 600);
    const detail = last ? ` Video2X: ${last}` : '';
    if (code === 2 || /processing aborted/.test(t)) return { kind: 'canceled', message: 'Video2X processing was aborted.' };
    // v1.0.8 (Real-ESRGAN / Real-CUGAN): checked before the generic Vulkan / shader patterns
    if (/out_of_device_memory|out_of_host_memory|out of (gpu |device |video )?memory|bad_alloc|failed to allocate/.test(t)) {
        return { kind: 'oom', message: `The GPU ran out of memory in Video2X. Choose a smaller scale, another model or another GPU, or close other GPU applications.${detail}` };
    }
    if (/model (param|bin) file not found|failed to load (the )?model|model file .* not found/.test(t)) {
        return { kind: 'model', message: `The selected Real-ESRGAN / Real-CUGAN model files are missing in this Video2X installation. Reinstall Video2X or choose another model / scale.${detail}` };
    }
    if (/vulkan|vk_error|vkcreate|no (suitable )?(vulkan )?(gpu|device)|failed to (create|initialize) (the )?(vulkan|libplacebo)|vk_icd|loader/.test(t)) {
        return { kind: 'vulkan', message: `Vulkan is not usable for Video2X (no compatible GPU/driver, or the selected device index does not exist). Install the current graphics driver and check Settings → AI Upscaling → Video2X → Verify installation.${detail}` };
    }
    if (/shader|glsl|custom_shader|hook|pl_mpv|libplacebo.*(parse|compile)|failed to (load|parse)/.test(t)) {
        return { kind: 'shader', message: `The Anime4K / custom GLSL shader could not be loaded or compiled by libplacebo. Test the preset in Settings → AI Upscaling → Video2X → Shaders, or choose a built-in preset.${detail}` };
    }
    if (/invalid encoder|encoder|avcodec_open|codec/.test(t) && /error|fail|invalid/.test(t)) return { kind: 'encoder', message: `Video2X could not open the lossless intermediate encoder.${detail}` };
    if (/no such file|failed to open input|could not open input|invalid data found/.test(t)) return { kind: 'input', message: `Video2X could not read the intermediate file.${detail}` };
    if (/permission denied|no space|disk full|failed to open output|could not open output/.test(t)) return { kind: 'output', message: `Video2X could not write its output (disk space / permissions).${detail}` };
    if (signal || (code !== null && (code < 0 || code > 255 || code === 3221225477))) return { kind: 'crash', message: `Video2X crashed (${signal || code}).${detail}` };
    return { kind: 'unknown', message: `Video2X failed with exit code ${code ?? signal}.${detail}` };
}

/** Rough temporary disk space of a Video2X job (FFV1 RGB intermediates, about half of the raw size). */
export function estimateTempBytes(frames: number, srcW: number, srcH: number, outW: number, outH: number): number {
    return Math.round(frames * 3 * 0.5 * (srcW * srcH + outW * outH) + frames * 3 * 0.08 * outW * outH);
}
