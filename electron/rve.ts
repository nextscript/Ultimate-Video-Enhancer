// AI upscaling with REAL-Video-Enhancer (https://github.com/TNTwise/REAL-Video-Enhancer, AGPL-3.0).
// RVE runs as a separate portable tool in tools\rve, set up exactly like the RVE app does it:
//   tools\rve\python   portable CPython 3.12.9 (python-build-standalone, hosted by RVE)
//   tools\rve\backend  rve-backend.py (release asset backend-v<ver>.tar.gz)
//   tools\rve\models   upscale models (+ TensorRT engines cached next to them, NCNN models as folders)
// Python packages are installed with pip using RVE's pinned versions (src/DownloadDeps.py and
// src/BuiltInTorchVersions.py of the RVE app) for one of its inference backends:
//   tensorrt  NVIDIA (RTX 20 or newer), Windows / Linux x64           PyTorch CUDA 13 + TensorRT 10.12
//   ncnn      any Vulkan GPU: AMD, Intel, NVIDIA, Apple (MoltenVK)    ncnn + upscale-ncnn-py (small download)
//   pytorch   CUDA (NVIDIA), ROCm (AMD, Linux), XPU (Intel Arc), MPS (Apple Silicon)
import { type ChildProcess, execFile, spawn } from 'node:child_process';
import { promises as fsp } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { toolsDir } from './store';
import { download, fetchJson, fileExists, readJson, writeJson } from './util';
import { IS_MAC, IS_WIN, groupSpawn, killTree, tarCommand } from './platform';

export type RveBackend = 'tensorrt' | 'ncnn' | 'pytorch';
export type TorchFlavor = 'cuda' | 'rocm' | 'xpu' | 'mps';

const IS_LINUX = process.platform === 'linux';
const ARCH = process.arch;

/** Portable CPython build of RVE for this system (null where RVE has none, e.g. Windows on ARM). */
function pythonTriple(): string | null {
    if (IS_WIN) return ARCH === 'x64' ? 'x86_64-pc-windows-msvc' : null;
    if (IS_MAC) return ARCH === 'arm64' ? 'aarch64-apple-darwin' : 'x86_64-apple-darwin';
    if (IS_LINUX) return ARCH === 'arm64' ? 'aarch64-unknown-linux-gnu' : ARCH === 'x64' ? 'x86_64-unknown-linux-gnu' : null;
    return null;
}

export const RVE = {
    repoApi: 'https://api.github.com/repos/TNTwise/REAL-Video-Enhancer/releases',
    modelsUrl: 'https://github.com/TNTwise/real-video-enhancer-models/releases/download/models/',
    pythonVersion: '3.12.9',
    pythonUrl: `https://github.com/TNTwise/REAL-Video-Enhancer-models/releases/download/models/cpython-3.12.9+20250317-${pythonTriple() || 'unsupported'}-install_only.tar.gz`,
    // RVE BuiltInTorchVersions.Torch2_9 + DownloadDeps.downloadPythonDeps()
    torch: '2.9.0', torchvision: '0.24.0', tensorrt: '10.12.0.36',
    torchSuffix: { cuda: '+cu130', rocm: '+rocm6.4', xpu: '+xpu', mps: '' } as Record<TorchFlavor, string>,
    ncnnDeps: ['rife-ncnn-vulkan-python-tntwise==1.4.5', 'upscale_ncnn_py==1.2.0', 'ncnn==1.0.20250916', 'numpy==2.2.2'],
    source: 'https://github.com/TNTwise/REAL-Video-Enhancer'
};

export const BACKEND_LABEL: Record<RveBackend, string> = { tensorrt: 'TensorRT', ncnn: 'NCNN (Vulkan)', pytorch: 'PyTorch' };
export const FLAVOR_LABEL: Record<TorchFlavor, string> = { cuda: 'CUDA – NVIDIA', rocm: 'ROCm – AMD', xpu: 'XPU – Intel Arc', mps: 'MPS – Apple Silicon' };

/** Backends (and PyTorch builds) RVE offers on this operating system / CPU. */
export function platformSupport(): { backends: RveBackend[]; flavors: TorchFlavor[]; reason: string } {
    if (!pythonTriple()) return { backends: [], flavors: [], reason: `REAL-Video-Enhancer has no portable Python for ${process.platform} ${ARCH}.` };
    const flavors: TorchFlavor[] = [];
    if ((IS_WIN || IS_LINUX) && ARCH === 'x64') flavors.push('cuda', 'xpu');
    if (IS_LINUX && ARCH === 'x64') flavors.push('rocm');
    if (IS_MAC && ARCH === 'arm64') flavors.push('mps');
    const backends: RveBackend[] = [];
    if ((IS_WIN || IS_LINUX) && ARCH === 'x64') backends.push('tensorrt');
    backends.push('ncnn');
    if (flavors.length) backends.push('pytorch');
    return { backends, flavors, reason: '' };
}

export interface RveModel { id: string; name: string; file: string; scale: number; arch: string; kind: 'realistic' | 'animation'; custom?: boolean }

/**
 * Model catalog. `pth` is the PyTorch file used by the TensorRT and PyTorch backends (tensorrtUpscaleModels /
 * pytorchUpscaleModels of RVE src/ModelHandler.py), `ncnn` the folder (= <folder>.tar.gz) of ncnnUpscaleModels.
 * The same id is used for both, so a selected model keeps working when the backend is changed.
 */
interface CatalogEntry { id: string; name: string; scale: number; arch: string; kind: 'realistic' | 'animation'; pth?: string; ncnn?: string }
const CATALOG: CatalogEntry[] = [
    { id: 'nomos-weak', name: 'Nomos8k (Realistic, high quality source) 4×', pth: '4xNomos8k_span_otf_weak_no_update_params.pth', ncnn: '4xNomos8k_span_otf_weak', scale: 4, arch: 'SPAN', kind: 'realistic' },
    { id: 'nomos-medium', name: 'Nomos8k (Realistic, medium quality source) 4×', pth: '4xNomos8k_span_otf_medium_no_update_params.pth', ncnn: '4xNomos8k_span_otf_medium', scale: 4, arch: 'SPAN', kind: 'realistic' },
    { id: 'nomos-strong', name: 'Nomos8k (Realistic, low quality source) 4×', pth: '4xNomos8k_span_otf_strong_no_update_params.pth', ncnn: '4xNomos8k_span_otf_strong', scale: 4, arch: 'SPAN', kind: 'realistic' },
    { id: 'openproteus', name: 'OpenProteus (Realistic, HD input) 2×', pth: '2x_OpenProteus_Compact_i2_70K.pth', ncnn: '2x_OpenProteus_Compact_i2_70K', scale: 2, arch: 'Compact', kind: 'realistic' },
    { id: 'realesr-general', name: 'RealisticVideo (RealESR general v3) 4×', pth: 'realesr-general-x4v3.pth', ncnn: 'realesr-general-x4v3', scale: 4, arch: 'Compact', kind: 'realistic' },
    { id: 'realesrgan-x4plus', name: 'ESRGAN Plus (General, slow) 4×', ncnn: 'realesrgan-x4plus', scale: 4, arch: 'ESRGAN', kind: 'realistic' },
    { id: 'janai-v2', name: 'AnimeJaNai V2 (Animation) 2×', pth: '2x_AnimeJaNai_V2_Compact_36k.pth', ncnn: '2x_AnimeJaNai_V2_Compact_36k', scale: 2, arch: 'Compact', kind: 'animation' },
    { id: 'janai-v3', name: 'AnimeJaNai V3 Sharp (Animation) 2×', pth: '2x_AnimeJaNai_HD_V3_Sharp1_Compact_430k.pth', ncnn: '2x_AnimeJaNai_HD_V3_Sharp1_Compact_430k', scale: 2, arch: 'Compact', kind: 'animation' },
    { id: 'spanimation', name: 'Spanimation V2 (Animation) 2×', pth: '2x_ModernSpanimationV2.pth', ncnn: '2x_ModernSpanimationV2', scale: 2, arch: 'SPAN', kind: 'animation' },
    { id: 'anisd-hq', name: 'AniSD (Old animation, high quality source) 2×', pth: '2x_AniSD_G6i1_SPAN_215K.pth', ncnn: '2x_AniSD_G6i1_SPAN_215K.ncnn', scale: 2, arch: 'SPAN', kind: 'animation' },
    { id: 'anisd-mq', name: 'AniSD (Old animation, medium quality source) 2×', pth: '2x_AniSD_AC_G6i2b_SPAN_190K.pth', ncnn: '2x_AniSD_AC_G6i2b_SPAN_190K.ncnn', scale: 2, arch: 'SPAN', kind: 'animation' },
    { id: 'anisd-lq', name: 'AniSD (Old animation, low quality source) 2×', pth: '2x_AniSD_DC_SPAN_92500.pth', ncnn: '2x_AniSD_DC_SPAN_92500.ncnn', scale: 2, arch: 'SPAN', kind: 'animation' },
    { id: 'animevideo-v3-x2', name: 'ESRGAN AnimeVideo V3 (Animation) 2×', ncnn: 'realesr-animevideov3-x2', scale: 2, arch: 'Compact', kind: 'animation' },
    { id: 'animevideo-v3-x3', name: 'ESRGAN AnimeVideo V3 (Animation) 3×', ncnn: 'realesr-animevideov3-x3', scale: 3, arch: 'Compact', kind: 'animation' },
    { id: 'animevideo-v3-x4', name: 'ESRGAN AnimeVideo V3 (Animation) 4×', ncnn: 'realesr-animevideov3-x4', scale: 4, arch: 'Compact', kind: 'animation' },
    { id: 'realesrgan-x4plus-anime', name: 'ESRGAN Plus (Animation, slow) 4×', ncnn: 'realesrgan-x4plus-anime', scale: 4, arch: 'ESRGAN', kind: 'animation' }
];

/** Models of the backend: PyTorch files for tensorrt / pytorch, NCNN folders for ncnn. */
export function catalogFor(backend: RveBackend): RveModel[] {
    return CATALOG.flatMap(c => {
        const file = backend === 'ncnn' ? c.ncnn : c.pth;
        return file ? [{ id: c.id, name: c.name, file, scale: c.scale, arch: c.arch, kind: c.kind }] : [];
    });
}
/** v1.0.5 export: the TensorRT model list (kept for callers that do not know the backend). */
export const RVE_MODELS: RveModel[] = catalogFor('tensorrt');

export interface RveStatus {
    dir: string;
    python: boolean;
    backend: boolean;
    backendVersion: string;
    /** inference backend used for rendering, and the backends / PyTorch builds this system can use */
    engine: RveBackend;
    torchFlavor: TorchFlavor | '';
    supported: RveBackend[];
    flavors: TorchFlavor[];
    unsupportedReason: string;
    /** installed Python packages per backend */
    installed: Record<RveBackend, boolean>;
    /** packages of the active backend are installed */
    packages: boolean;
    torch: string;
    tensorrt: string;
    torchTensorrt: string;
    ncnn: string;
    /** PyTorch device of the installed build: cuda / rocm / xpu / mps / cpu */
    torchDevice: string;
    cuda: boolean;
    /** GPU the active backend renders on (empty when none was found) */
    gpu: string;
    ncnnGpus: string[];
    ready: boolean;
    models: (RveModel & { installed: boolean; size: number; engines: number })[];
    message: string;
    busy: boolean;
}

interface RveInfo { backendVersion?: string; backendTag?: string; installedAt?: number; engine?: RveBackend; torchFlavor?: TorchFlavor }

// UVE_RVE_DIR / UVE_RVE_PYTHON: test overrides used by the self test (mock backend)
export async function rveDir() { return process.env.UVE_RVE_DIR || path.join(await toolsDir(), 'rve'); }
const pythonExe = async () => process.env.UVE_RVE_PYTHON || (IS_WIN ? path.join(await rveDir(), 'python', 'python.exe') : path.join(await rveDir(), 'python', 'bin', 'python3'));
const backendScript = async () => path.join(await rveDir(), 'backend', 'rve-backend.py');
const modelsDir = async () => path.join(await rveDir(), 'models');
const infoFile = async () => path.join(await rveDir(), 'rve-manager.json');
const readInfo = async () => readJson<RveInfo>(await infoFile(), {});

function run(file: string, args: string[], opts: { cwd?: string; timeoutMs?: number } = {}): Promise<{ code: number; out: string; err: string }> {
    return new Promise(resolve => {
        execFile(file, args, { cwd: opts.cwd, timeout: opts.timeoutMs ?? 120000, windowsHide: true, maxBuffer: 64 * 1024 * 1024 }, (e, out, err) => {
            resolve({ code: e ? Number((e as unknown as { code?: unknown }).code) || 1 : 0, out: String(out), err: String(err) });
        });
    });
}

let busy: { message: string } | null = null;

interface Probe {
    torch: string; tensorrt: string; torchTensorrt: string; ncnn: string; upscaleNcnn: boolean;
    torchDevice: string; torchGpu: string; ncnnGpus: string[];
}
const EMPTY_PROBE: Probe = { torch: '', tensorrt: '', torchTensorrt: '', ncnn: '', upscaleNcnn: false, torchDevice: '', torchGpu: '', ncnnGpus: [] };
let probeCache: Probe | null = null;

/**
 * Imports the packages of every backend inside the portable Python and reports versions and the GPUs they see
 * (the same detection as RVE backend/src/utils/BackendDetect.py).
 */
async function probePackages(): Promise<Probe> {
    if (probeCache) return probeCache;
    const py = await pythonExe();
    const code = [
        'import json,importlib,sys',
        'r={"ncnn_gpus":[]}',
        'def v(m):',
        '    try:',
        '        x=importlib.import_module(m)',
        '        return str(getattr(x,"__version__","") or "installed")',
        '    except Exception:',
        '        return ""',
        'r["torch"]=v("torch"); r["tensorrt"]=v("tensorrt") if r["torch"] else ""; r["torch_tensorrt"]=v("torch_tensorrt") if r["tensorrt"] else ""',
        'r["device"]=""; r["gpu"]=""',
        'if r["torch"]:',
        '    try:',
        '        import torch',
        '        ver=torch.__version__',
        '        if "rocm" in ver or getattr(torch.version,"hip",None): d="rocm"',
        '        elif "+cu" in ver or getattr(torch.version,"cuda",None): d="cuda"',
        '        elif hasattr(torch,"xpu") and torch.xpu.is_available(): d="xpu"',
        '        elif torch.backends.mps.is_available(): d="mps"',
        '        else: d="cpu"',
        '        if d in ("cuda","rocm") and not torch.cuda.is_available(): d=d+"-unavailable"',
        '        r["device"]=d',
        '        if d in ("cuda","rocm"): r["gpu"]=torch.cuda.get_device_name(0)',
        '        elif d=="xpu": r["gpu"]=torch.xpu.get_device_name(0)',
        '        elif d=="mps": r["gpu"]="Apple MPS"',
        '    except Exception as e:',
        '        r["device"]="error"',
        'r["ncnn"]=v("ncnn"); r["upscale_ncnn"]=bool(v("upscale_ncnn_py"))',
        'if r["ncnn"]:',
        '    try:',
        '        import ncnn',
        '        for i in range(ncnn.get_gpu_count()):',
        '            r["ncnn_gpus"].append(ncnn.get_gpu_device(i).info().device_name())',
        '    except Exception:',
        '        pass',
        'print(json.dumps(r), flush=True)'
    ].join('\n');
    // the exit code is not checked: ncnn can crash while unloading its Vulkan driver at process exit,
    // after the JSON line was written
    const r = await run(py, ['-c', code], { timeoutMs: 180000 });
    try {
        const j = JSON.parse(r.out.trim().split(/\r?\n/).pop() || '{}');
        probeCache = {
            torch: j.torch || '', tensorrt: j.tensorrt || '', torchTensorrt: j.torch_tensorrt || '', ncnn: j.ncnn || '', upscaleNcnn: !!j.upscale_ncnn,
            torchDevice: j.device || '', torchGpu: j.gpu || '', ncnnGpus: Array.isArray(j.ncnn_gpus) ? j.ncnn_gpus.map(String) : []
        };
    } catch {
        probeCache = { ...EMPTY_PROBE };
    }
    return probeCache;
}

/** Backend used when none was chosen yet: TensorRT on NVIDIA, otherwise NCNN (runs on every Vulkan GPU). */
export function recommendedBackend(gpuName = ''): RveBackend {
    const sup = platformSupport();
    if (/nvidia|geforce|rtx|quadro/i.test(gpuName) && sup.backends.includes('tensorrt')) return 'tensorrt';
    return 'ncnn';
}
export function recommendedFlavor(gpuName = ''): TorchFlavor | null {
    const { flavors } = platformSupport();
    const pick = (f: TorchFlavor) => (flavors.includes(f) ? f : null);
    if (IS_MAC) return pick('mps');
    if (/nvidia|geforce|rtx|quadro/i.test(gpuName)) return pick('cuda');
    if (/amd|radeon/i.test(gpuName)) return pick('rocm');
    if (/intel|arc/i.test(gpuName)) return pick('xpu');
    return flavors[0] || null;
}

const flavorOfDevice = (d: string): TorchFlavor | '' => {
    const base = d.replace(/-unavailable$/, '');
    return base === 'cuda' || base === 'rocm' || base === 'xpu' || base === 'mps' ? base : '';
};

export async function rveStatus(deep = true): Promise<RveStatus> {
    const dir = await rveDir();
    const sup = platformSupport();
    const python = await fileExists(await pythonExe());
    const backend = await fileExists(await backendScript());
    const info = await readInfo();
    const pk = python && deep && !busy ? await probePackages() : (probeCache || EMPTY_PROBE);
    // installations of 1.0.5 and older only had TensorRT and wrote no engine
    const engine: RveBackend = info.engine || 'tensorrt';
    const installed: Record<RveBackend, boolean> = {
        tensorrt: !!(pk.torch && pk.tensorrt && pk.torchTensorrt),
        ncnn: !!pk.ncnn,
        pytorch: !!pk.torch && !!flavorOfDevice(pk.torchDevice)
    };
    const cuda = pk.torchDevice === 'cuda';
    let usable = false, gpu = '', why = '';
    if (engine === 'tensorrt') { usable = installed.tensorrt && cuda; gpu = cuda ? pk.torchGpu : ''; why = 'CUDA not available (NVIDIA GPU and current driver required)'; }
    else if (engine === 'ncnn') { usable = installed.ncnn && pk.ncnnGpus.length > 0; gpu = pk.ncnnGpus[0] || ''; why = 'No Vulkan GPU found (install the current graphics driver)'; }
    else {
        usable = installed.pytorch && !pk.torchDevice.endsWith('-unavailable');
        gpu = usable ? pk.torchGpu : '';
        why = `PyTorch ${pk.torchDevice.replace(/-unavailable$/, '').toUpperCase() || ''} is installed but the GPU is not available (driver / ROCm / oneAPI runtime missing)`;
    }
    const md = await modelsDir();
    const custom = await customModels(engine);
    const all = await fsp.readdir(md).catch(() => [] as string[]);
    const models = await Promise.all([...catalogFor(engine), ...custom].map(async m => {
        const p = path.join(md, m.file);
        const st = await fsp.stat(p).catch(() => null);
        let size = st?.size ?? 0;
        if (st?.isDirectory()) size = (await Promise.all((await fsp.readdir(p).catch(() => [] as string[])).map(f => fsp.stat(path.join(p, f)).then(s => s.size, () => 0)))).reduce((a, b) => a + b, 0);
        const engines = engine === 'tensorrt' ? all.filter(f => f.startsWith(m.file.replace(/\.(pth|safetensors)$/i, '')) && f.endsWith('.engine')).length : 0;
        return { ...m, installed: !!st, size, engines };
    }));
    const packages = installed[engine];
    const ready = python && backend && usable;
    return {
        dir, python, backend, backendVersion: info.backendVersion || '',
        engine, torchFlavor: flavorOfDevice(pk.torchDevice) || info.torchFlavor || '', supported: sup.backends, flavors: sup.flavors, unsupportedReason: sup.reason,
        installed, packages,
        torch: pk.torch, tensorrt: pk.tensorrt, torchTensorrt: pk.torchTensorrt, ncnn: pk.ncnn, torchDevice: pk.torchDevice, cuda, gpu, ncnnGpus: pk.ncnnGpus,
        ready, models, busy: !!busy,
        message: busy?.message || (ready ? 'Ready' : !python ? 'Not installed' : !backend ? 'Backend missing' : !packages ? `Python packages for ${BACKEND_LABEL[engine]} missing` : why)
    };
}

/** Selects the backend used for rendering (its packages are installed with installRve). */
export async function setRveEngine(engine: RveBackend): Promise<RveStatus> {
    if (busy) throw new Error('RVE is busy.');
    if (!platformSupport().backends.includes(engine)) throw new Error(`The ${BACKEND_LABEL[engine]} backend is not available on this system.`);
    const info = await readInfo();
    info.engine = engine;
    await fsp.mkdir(await rveDir(), { recursive: true });
    await writeJson(await infoFile(), info);
    return rveStatus(false);
}

const customDir = (engine: RveBackend) => (engine === 'ncnn' ? 'custom-ncnn' : 'custom');

async function customModels(engine: RveBackend): Promise<RveModel[]> {
    const md = await modelsDir();
    const sub = customDir(engine);
    const entries = await fsp.readdir(path.join(md, sub), { withFileTypes: true }).catch(() => []);
    const scaleOf = (f: string) => { const m = f.match(/(\d)x|x(\d)/i); return m ? Number(m[1] || m[2]) : 2; };
    if (engine === 'ncnn') {
        // RVE layout: <folder>/<folder>.param + <folder>.bin
        return entries.filter(e => e.isDirectory()).map(e => ({ id: 'custom:' + e.name, name: `${e.name} (custom)`, file: `${sub}/${e.name}`, scale: scaleOf(e.name), arch: 'auto', kind: 'realistic' as const, custom: true }));
    }
    return entries.filter(e => e.isFile() && /\.(pth|safetensors)$/i.test(e.name)).map(e => ({
        id: 'custom:' + e.name, name: `${e.name} (custom)`, file: `${sub}/${e.name}`, scale: scaleOf(e.name), arch: 'auto', kind: 'realistic' as const, custom: true
    }));
}

/** Model `id` for the given backend (default: the active one). */
export async function findModel(id: string, engine?: RveBackend): Promise<RveModel | null> {
    const e = engine || (await readInfo()).engine || 'tensorrt';
    return [...catalogFor(e), ...(await customModels(e))].find(m => m.id === id) || null;
}

/** tar.gz extraction with the Windows built-in bsdtar (Windows 10 1803+) or tar on macOS / Linux. */
async function extractTarGz(file: string, dest: string) {
    await fsp.mkdir(dest, { recursive: true });
    const tar = tarCommand();
    const r = await run(IS_WIN && !(await fileExists(tar)) ? 'tar' : tar, ['-xzf', file, '-C', dest], { timeoutMs: 30 * 60000 });
    if (r.code !== 0) throw new Error(`Extracting ${path.basename(file)} failed: ${r.err.trim().slice(0, 400)}`);
}

async function downloadTo(url: string, file: string, maxBytes: number, progress: (m: string, p: number) => void, label: string) {
    const data = await download(url, { maxBytes, onProgress: (r, t) => progress(`${label} ${(r / 1048576).toFixed(0)}${t ? ' / ' + (t / 1048576).toFixed(0) : ''} MB`, t ? r / t : 0) });
    await fsp.mkdir(path.dirname(file), { recursive: true });
    await fsp.writeFile(file, data);
}

type BackendRelease = { tag: string; version: string; url: string; size: number };

const BACKEND_ASSET = /^backend-v[\d.]+(-[\w.]+)?\.tar\.gz$/i;
const backendVersion = (name: string) => name.replace(/^backend-v|\.tar\.gz$/gi, '');

/**
 * Newest release that carries a backend-v*.tar.gz. Uses the GitHub API; when that fails – most often its
 * limit of 60 requests per hour and IP without sign-in (HTTP 403) – the public release pages are read
 * instead, which that limit does not cover.
 */
export async function latestBackendRelease(): Promise<BackendRelease | null> {
    type Rel = { tag_name: string; draft: boolean; assets: { name: string; browser_download_url: string; size: number }[] };
    try {
        const rels = await fetchJson<Rel[]>(`${RVE.repoApi}?per_page=15`);
        for (const r of rels) {
            if (r.draft) continue;
            const a = r.assets.find(x => BACKEND_ASSET.test(x.name));
            if (a) return { tag: r.tag_name, version: backendVersion(a.name), url: a.browser_download_url, size: a.size };
        }
        return null;
    } catch (apiError) {
        const rel = await backendReleaseFromPages().catch(() => null);
        if (rel) return rel;
        throw apiError;
    }
}

/** Same lookup through the HTML release pages: the release list, then each release's asset list. */
async function backendReleaseFromPages(): Promise<BackendRelease | null> {
    const text = async (url: string) => Buffer.from(await download(url, { maxBytes: 8 * 1024 * 1024, accept: 'text/html' })).toString('utf8');
    const repoPath = new URL(RVE.source).pathname.replace(/\/$/, '');
    const list = await text(`${RVE.source}/releases`);
    const tags = [...new Set([...list.matchAll(new RegExp(`${repoPath}/releases/tag/([^"?#/]+)`, 'g'))].map(m => decodeURIComponent(m[1])))];
    for (const tag of tags.slice(0, 15)) {
        const assets = await text(`${RVE.source}/releases/expanded_assets/${encodeURIComponent(tag)}`);
        for (const m of assets.matchAll(new RegExp(`href="(${repoPath}/releases/download/[^"]+/([^"/]+))"`, 'g'))) {
            const name = decodeURIComponent(m[2]);
            if (BACKEND_ASSET.test(name)) return { tag, version: backendVersion(name), url: `https://github.com${m[1]}`, size: 0 };
        }
    }
    return null;
}

let pipProc: ChildProcess | null = null;

function pip(py: string, args: string[], cwd: string, onLine: (l: string) => void): Promise<number> {
    return new Promise((resolve, reject) => {
        const tmp = path.join(cwd, 'tmp');
        void fsp.mkdir(tmp, { recursive: true });
        const p = spawn(py, ['-m', 'pip', ...args], { cwd, windowsHide: true, ...groupSpawn, env: { ...process.env, TMPDIR: tmp, TEMP: tmp, TMP: tmp, PYTHONNOUSERSITE: '1', PIP_DISABLE_PIP_VERSION_CHECK: '1' } });
        pipProc = p;
        const feed = (d: Buffer) => String(d).split(/\r?\n/).filter(s => s.trim()).forEach(onLine);
        p.stdout.on('data', feed);
        p.stderr.on('data', feed);
        p.on('error', reject);
        p.on('exit', code => { pipProc = null; resolve(code ?? 1); });
    });
}

// RVE DownloadDeps.getPlatformIndependentDeps()
const COMMON_DEPS = ['testresources==2.0.1', 'requests==2.32.3', 'opencv-python-headless==4.11.0.86', 'pypresence==4.3.0',
    'scenedetect==0.6.5.2', 'numpy==2.2.2', 'sympy', 'tqdm==4.67.1', 'typing_extensions==4.12.2', 'packaging==24.2', 'mpmath==1.3.0', 'pillow==11.1.0'];

/** pip steps of RVE DownloadDeps.downloadPythonDeps() for a backend. */
function pipSteps(engine: RveBackend, flavor: TorchFlavor): { label: string; deps: string[] }[] {
    if (engine === 'ncnn') return [{ label: 'NCNN (Vulkan) and helpers', deps: [...COMMON_DEPS, ...RVE.ncnnDeps] }];
    const sfx = RVE.torchSuffix[engine === 'tensorrt' ? 'cuda' : flavor];
    const steps = [
        { label: `PyTorch (${engine === 'tensorrt' ? 'CUDA' : flavor.toUpperCase()}) and helpers`, deps: [...COMMON_DEPS, `torch==${RVE.torch}${sfx}`, 'safetensors==0.5.3', 'einops==0.8.1'] },
        { label: 'torchvision', deps: ['--no-deps', `torchvision==${RVE.torchvision}${sfx}`] }
    ];
    if (engine === 'tensorrt') {
        const t = RVE.tensorrt;
        steps.push({
            label: 'TensorRT', deps: [`tensorrt==${t}`, `tensorrt_cu12==${t}`, `tensorrt-cu12_libs==${t}`, `tensorrt_cu12_bindings==${t}`,
                '--no-deps', 'dllist', `torch-tensorrt==${RVE.torch.replace(/\d$/, '0')}${sfx}`]
        });
    }
    return steps;
}

/** Rough download size shown before the installation starts. */
export const INSTALL_SIZE: Record<RveBackend, string> = { tensorrt: '8–16 GB', ncnn: 'about 400 MB', pytorch: '3–10 GB' };

/**
 * Installs (or repairs) RVE with the packages of one backend into tools\rve and makes it the active backend.
 * Each step is skipped when it is already present, so the call is also used to resume an interrupted installation.
 */
export async function installRve(progress: (m: string, pct: number) => void, log: (l: string) => void,
    opts: { reinstallBackend?: boolean; engine?: RveBackend; flavor?: TorchFlavor } = {}): Promise<RveStatus> {
    if (busy) throw new Error('An RVE installation is already running.');
    const sup = platformSupport();
    if (!sup.backends.length) throw new Error(sup.reason);
    const prev = await readInfo();
    const engine: RveBackend = opts.engine || prev.engine || recommendedBackend();
    if (!sup.backends.includes(engine)) {
        throw new Error(engine === 'tensorrt'
            ? 'The TensorRT backend needs an NVIDIA GPU on Windows or Linux (x64). Use the NCNN (Vulkan) backend on this system – it runs on AMD, Intel, NVIDIA and Apple GPUs.'
            : `The ${BACKEND_LABEL[engine]} backend is not available on this system.`);
    }
    const flavor: TorchFlavor = engine === 'tensorrt' ? 'cuda' : (opts.flavor || prev.torchFlavor || recommendedFlavor() || sup.flavors[0]);
    if (engine === 'pytorch' && !sup.flavors.includes(flavor)) throw new Error(`PyTorch ${FLAVOR_LABEL[flavor]} is not available on this system (available: ${sup.flavors.map(f => FLAVOR_LABEL[f]).join(', ') || 'none'}).`);
    busy = { message: 'Installing…' };
    probeCache = null;
    const say = (m: string, p: number) => { busy = { message: m }; progress(m, p); };
    try {
        const dir = await rveDir();
        await fsp.mkdir(dir, { recursive: true });
        const py = await pythonExe();
        // 1. portable Python
        if (!(await fileExists(py))) {
            const tgz = path.join(dir, 'python.tar.gz');
            await downloadTo(RVE.pythonUrl, tgz, 200 * 1024 * 1024, say, 'Downloading portable Python');
            say('Extracting Python…', 1);
            await extractTarGz(tgz, dir);
            await fsp.rm(tgz, { force: true });
            if (!(await fileExists(py))) throw new Error(`${path.basename(py)} not found after extraction`);
        }
        // 2. backend
        const info = await readInfo();
        if (opts.reinstallBackend || !(await fileExists(await backendScript()))) {
            say('Looking up the RVE backend release…', 0);
            const rel = await latestBackendRelease();
            if (!rel) throw new Error('No backend release asset found on GitHub.');
            const tgz = path.join(dir, 'backend.tar.gz');
            await downloadTo(rel.url, tgz, 500 * 1024 * 1024, say, `Downloading RVE backend ${rel.version}`);
            say('Extracting backend…', 1);
            await fsp.rm(path.join(dir, 'backend'), { recursive: true, force: true });
            await extractTarGz(tgz, dir);
            await fsp.rm(tgz, { force: true });
            if (!(await fileExists(await backendScript()))) throw new Error('rve-backend.py not found after extraction');
            info.backendVersion = rel.version;
            info.backendTag = rel.tag;
            info.installedAt = Date.now();
            await writeJson(await infoFile(), info);
        }
        // 3. Python packages of the backend (RVE DownloadDeps pins)
        const pk = await probePackages();
        const have = engine === 'tensorrt' ? !!(pk.torch && pk.tensorrt && pk.torchTensorrt && pk.torchDevice.startsWith('cuda'))
            : engine === 'ncnn' ? !!pk.ncnn
                : !!pk.torch && flavorOfDevice(pk.torchDevice) === flavor;
        if (!have) {
            probeCache = null;
            const base = ['install', '--no-warn-script-location', '--isolated', '--extra-index-url', 'https://download.pytorch.org/whl/test/',
                '--extra-index-url', 'https://download.pytorch.org/whl/', '--trusted-host', 'download.pytorch.org'];
            const steps = pipSteps(engine, flavor);
            for (let i = 0; i < steps.length; i++) {
                const s = steps[i];
                say(`Installing ${s.label} (step ${i + 1}/${steps.length})${engine === 'ncnn' ? '…' : ' – this downloads several GB…'}`, i / steps.length);
                let lines = 0;
                const code = await pip(py, [...base, ...s.deps], dir, l => {
                    log(l);
                    lines++;
                    const m = l.match(/^(Downloading|Collecting|Installing collected packages|Successfully installed)\s*(.*)$/);
                    if (m) say(`${s.label}: ${m[1]} ${m[2].slice(0, 80)}`, Math.min(0.99, (i + Math.min(0.9, lines / 400)) / steps.length));
                });
                if (code !== 0) throw new Error(`pip failed while installing ${s.label} (exit ${code}). See the log for details.`);
            }
            await pip(py, ['cache', 'purge'], dir, log).catch(() => 0);
            await fsp.rm(path.join(dir, 'tmp'), { recursive: true, force: true });
            probeCache = null;
        }
        info.engine = engine;
        if (engine !== 'ncnn') info.torchFlavor = flavor;
        await writeJson(await infoFile(), info);
        say(`Verifying ${BACKEND_LABEL[engine]}…`, 1);
        // rveStatus() skips the import probe while `busy` is set (a probe during pip would see half-installed
        // packages), so the final check runs the probe itself; rveStatus then reports its fresh result
        probeCache = null;
        await probePackages();
        const st = await rveStatus(false);
        if (!st.packages) {
            throw new Error(engine === 'tensorrt' ? 'torch / tensorrt / torch_tensorrt could not be imported after the installation.'
                : engine === 'ncnn' ? 'ncnn could not be imported after the installation.' : 'torch could not be imported after the installation.');
        }
        if (!st.ready) throw new Error(`${BACKEND_LABEL[engine]} was installed, but: ${st.message}.`);
        return st;
    } finally {
        busy = null;
        pipProc = null;
    }
}

export function cancelInstall() {
    killTree(pipProc?.pid);
}

export async function uninstallRve(keepModels: boolean) {
    if (busy) throw new Error('RVE is busy.');
    const dir = await rveDir();
    for (const sub of ['python', 'backend', 'tmp']) await fsp.rm(path.join(dir, sub), { recursive: true, force: true });
    if (!keepModels) await fsp.rm(path.join(dir, 'models'), { recursive: true, force: true });
    await fsp.rm(path.join(dir, 'rve-manager.json'), { force: true });
    probeCache = null;
}

/** Downloads model `id` of the active backend; returns the path passed to --upscale_model (file or NCNN folder). */
export async function downloadModel(id: string, progress: (m: string, p: number) => void): Promise<string> {
    const engine = (await readInfo()).engine || 'tensorrt';
    const m = await findModel(id, engine);
    if (!m) throw new Error(`Model ${id} is not available for the ${BACKEND_LABEL[engine]} backend.`);
    const md = await modelsDir();
    const target = path.join(md, m.file);
    if (engine === 'ncnn') {
        const param = path.join(target, `${path.basename(m.file)}.param`);
        if (await fileExists(param)) return target;
        if (m.custom) throw new Error(`Custom NCNN model is incomplete: ${param} is missing`);
        const tgz = path.join(md, `${m.file}.tar.gz.part`);
        await downloadTo(RVE.modelsUrl + encodeURIComponent(`${m.file}.tar.gz`), tgz, 500 * 1024 * 1024, progress, `Downloading NCNN model ${m.file}`);
        try { await extractTarGz(tgz, md); } finally { await fsp.rm(tgz, { force: true }); }
        if (!(await fileExists(param))) throw new Error(`NCNN model ${m.file}: ${path.basename(param)} not found after extraction`);
        return target;
    }
    if (await fileExists(target)) return target;
    if (m.custom) throw new Error(`Custom model file is missing: ${target}`);
    await downloadTo(RVE.modelsUrl + encodeURIComponent(m.file), target + '.part', 500 * 1024 * 1024, progress, `Downloading model ${m.file}`);
    await fsp.rename(target + '.part', target);
    return target;
}

export async function deleteModel(id: string) {
    const engine = (await readInfo()).engine || 'tensorrt';
    const m = await findModel(id, engine);
    if (!m) return;
    const md = await modelsDir();
    const full = path.join(md, m.file);
    if (engine === 'ncnn') { await fsp.rm(full, { recursive: true, force: true }); return; }
    const stem = path.basename(m.file).replace(/\.(pth|safetensors)$/i, '');
    for (const f of await fsp.readdir(path.dirname(full)).catch(() => [] as string[])) {
        if (f === path.basename(m.file) || (f.startsWith(stem) && f.endsWith('.engine'))) await fsp.rm(path.join(path.dirname(full), f), { force: true });
    }
}

/** Imports a custom model for the active backend: .pth / .safetensors (TensorRT, PyTorch) or .param + .bin (NCNN). */
export async function importCustomModel(src: string): Promise<void> {
    const engine = (await readInfo()).engine || 'tensorrt';
    const md = await modelsDir();
    if (engine === 'ncnn') {
        if (!/\.(param|bin)$/i.test(src)) throw new Error('NCNN models consist of a .param and a .bin file – select one of them.');
        const stem = path.basename(src).replace(/\.(param|bin)$/i, '');
        const param = path.join(path.dirname(src), `${stem}.param`), bin = path.join(path.dirname(src), `${stem}.bin`);
        if (!(await fileExists(param)) || !(await fileExists(bin))) throw new Error(`${stem}.param and ${stem}.bin must be in the same folder.`);
        if ((await fsp.stat(bin)).size > 2 * 1024 * 1024 * 1024) throw new Error('Model file is too large.');
        const dest = path.join(md, customDir('ncnn'), stem);
        await fsp.mkdir(dest, { recursive: true });
        await fsp.copyFile(param, path.join(dest, `${stem}.param`));
        await fsp.copyFile(bin, path.join(dest, `${stem}.bin`));
        return;
    }
    if (!/\.(pth|safetensors)$/i.test(src)) throw new Error(`Only .pth or .safetensors PyTorch models can be used with the ${BACKEND_LABEL[engine]} backend.`);
    const st = await fsp.stat(src);
    if (st.size > 2 * 1024 * 1024 * 1024) throw new Error('Model file is too large.');
    const dest = path.join(md, customDir(engine), path.basename(src));
    await fsp.mkdir(path.dirname(dest), { recursive: true });
    await fsp.copyFile(src, dest);
}

export interface RveRun { kill: () => void; done: Promise<{ ok: boolean; error?: string }> }

/** Command-line options of rve-backend.py for the inference backend. */
export function backendArgs(engine: RveBackend): string[] {
    if (engine === 'tensorrt') return ['--backend', 'tensorrt', '--precision', 'auto', '--tensorrt_opt_profile', '3'];
    if (engine === 'ncnn') return ['--backend', 'ncnn', '--ncnn_gpu_id', '0'];
    return ['--backend', 'pytorch', '--device', 'auto', '--precision', 'auto'];
}

/**
 * Upscales `input` into `output` with the RVE backend. `customEncoder` is passed verbatim to
 * --custom_encoder (whitespace-separated FFmpeg output options, no spaces inside values).
 */
export async function runUpscale(o: {
    input: string; output: string; modelPath: string; ffmpeg: string; customEncoder: string; totalFrames: number; engine?: RveBackend;
    onProgress: (frames: number, fps: number) => void; onLog: (l: string) => void;
}): Promise<RveRun> {
    const py = await pythonExe();
    const script = await backendScript();
    if (!(await fileExists(py)) || !(await fileExists(script))) throw new Error('REAL-Video-Enhancer is not installed (Settings → AI Upscaling).');
    const engine = o.engine || (await readInfo()).engine || 'tensorrt';
    const args = [script, '--input', o.input, '--output', o.output, ...backendArgs(engine), '--upscale_model', o.modelPath,
        '--ffmpeg_path', o.ffmpeg, '--overwrite',
        '--audio_encoder_preset', 'copy_audio', '--subtitle_encoder_preset', 'copy_subtitle', '--custom_encoder', o.customEncoder];
    o.onLog(`RVE (${BACKEND_LABEL[engine]}): python ${args.map(a => (/\s/.test(a) ? `"${a}"` : a)).join(' ')}`);
    // PYTORCH_ENABLE_MPS_FALLBACK: operators MPS lacks run on the CPU instead of aborting (Apple Silicon)
    const p = spawn(py, args, { cwd: path.dirname(script), windowsHide: true, ...groupSpawn, env: { ...process.env, PYTHONNOUSERSITE: '1', PYTHONUNBUFFERED: '1', PYTORCH_ENABLE_MPS_FALLBACK: '1' } });
    let errTail = '';
    let lastFrames = 0;
    // rve-backend.py prints this after the writer closed the output file
    let completed = false;
    const feed = (d: Buffer, isErr: boolean) => {
        const s = String(d);
        for (const part of s.split(/[\r\n]+/)) {
            const line = part.trim();
            if (!line) continue;
            const m = line.match(/FPS:\s*([\d.]+)\s+Current Frame:\s*(\d+)/);
            if (m) { lastFrames = Number(m[2]); o.onProgress(lastFrames, Number(m[1])); continue; }
            if (/Time to complete render/i.test(line)) completed = true;
            o.onLog(`[rve] ${line}`);
            if (isErr) errTail = (errTail + '\n' + line).slice(-1500);
        }
    };
    p.stdout.on('data', d => feed(d, false));
    p.stderr.on('data', d => feed(d, true));
    const done = new Promise<{ ok: boolean; error?: string }>(resolve => {
        p.on('error', e => resolve({ ok: false, error: e.message }));
        p.on('exit', (code, signal) => {
            if (code === 0) { resolve({ ok: true }); return; }
            // upscale-ncnn-py / ncnn can crash while releasing the Vulkan device at interpreter exit (Windows:
            // 0xC0000005, POSIX: SIGSEGV) – after the video was written completely. The caller still checks the file.
            if (engine === 'ncnn' && completed) {
                o.onLog(`[rve] NCNN exited with ${code ?? signal} after the render had completed (Vulkan teardown) – output kept.`);
                resolve({ ok: true });
                return;
            }
            resolve({ ok: false, error: `REAL-Video-Enhancer exited with code ${code ?? signal}: ${errTail.trim().split('\n').slice(-4).join(' ')}` });
        });
    });
    return {
        kill: () => killTree(p.pid),
        done
    };
}

export const rveTempDir = () => path.join(os.tmpdir(), 'uve-manager-rve');
