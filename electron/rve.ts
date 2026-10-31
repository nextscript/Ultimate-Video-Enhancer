// AI upscaling with REAL-Video-Enhancer (https://github.com/TNTwise/REAL-Video-Enhancer, AGPL-3.0).
// RVE runs as a separate portable tool in tools\rve, set up exactly like the RVE app does it:
//   tools\rve\python   portable CPython 3.12.9 (python-build-standalone, hosted by RVE)
//   tools\rve\backend  rve-backend.py (release asset backend-v<ver>.tar.gz)
//   tools\rve\models   upscale models (+ TensorRT engines cached next to them)
// Python packages for the TensorRT backend are installed with pip using RVE's pinned versions.
import { type ChildProcess, execFile, spawn } from 'node:child_process';
import { promises as fsp } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { toolsDir } from './store';
import { download, fetchJson, fileExists, readJson, writeJson } from './util';
import { IS_MAC, IS_WIN, groupSpawn, killTree, tarCommand } from './platform';

export const RVE = {
    repoApi: 'https://api.github.com/repos/TNTwise/REAL-Video-Enhancer/releases',
    modelsUrl: 'https://github.com/TNTwise/real-video-enhancer-models/releases/download/models/',
    pythonVersion: '3.12.9',
    pythonUrl: `https://github.com/TNTwise/REAL-Video-Enhancer-models/releases/download/models/cpython-3.12.9+20250317-${process.platform === 'win32' ? 'x86_64-pc-windows-msvc' : 'x86_64-unknown-linux-gnu'}-install_only.tar.gz`,
    // RVE BuiltInTorchVersions.Torch2_9 + DownloadDeps.downloadPythonDeps("tensorrt")
    torch: '2.9.0', torchvision: '0.24.0', cuda: '+cu130', tensorrt: '10.12.0.36',
    source: 'https://github.com/TNTwise/REAL-Video-Enhancer'
};

export interface RveModel { id: string; name: string; file: string; scale: number; arch: string; kind: 'realistic' | 'animation'; custom?: boolean }

// tensorrtUpscaleModels of RVE src/ModelHandler.py
export const RVE_MODELS: RveModel[] = [
    { id: 'nomos-weak', name: 'Nomos8k (Realistic, high quality source) 4×', file: '4xNomos8k_span_otf_weak_no_update_params.pth', scale: 4, arch: 'SPAN', kind: 'realistic' },
    { id: 'nomos-medium', name: 'Nomos8k (Realistic, medium quality source) 4×', file: '4xNomos8k_span_otf_medium_no_update_params.pth', scale: 4, arch: 'SPAN', kind: 'realistic' },
    { id: 'nomos-strong', name: 'Nomos8k (Realistic, low quality source) 4×', file: '4xNomos8k_span_otf_strong_no_update_params.pth', scale: 4, arch: 'SPAN', kind: 'realistic' },
    { id: 'openproteus', name: 'OpenProteus (Realistic, HD input) 2×', file: '2x_OpenProteus_Compact_i2_70K.pth', scale: 2, arch: 'Compact', kind: 'realistic' },
    { id: 'realesr-general', name: 'RealisticVideo (RealESR general v3) 4×', file: 'realesr-general-x4v3.pth', scale: 4, arch: 'Compact', kind: 'realistic' },
    { id: 'janai-v2', name: 'AnimeJaNai V2 (Animation) 2×', file: '2x_AnimeJaNai_V2_Compact_36k.pth', scale: 2, arch: 'Compact', kind: 'animation' },
    { id: 'janai-v3', name: 'AnimeJaNai V3 Sharp (Animation) 2×', file: '2x_AnimeJaNai_HD_V3_Sharp1_Compact_430k.pth', scale: 2, arch: 'Compact', kind: 'animation' },
    { id: 'spanimation', name: 'Spanimation V2 (Animation) 2×', file: '2x_ModernSpanimationV2.pth', scale: 2, arch: 'SPAN', kind: 'animation' },
    { id: 'anisd-hq', name: 'AniSD (Old animation, high quality source) 2×', file: '2x_AniSD_G6i1_SPAN_215K.pth', scale: 2, arch: 'SPAN', kind: 'animation' },
    { id: 'anisd-mq', name: 'AniSD (Old animation, medium quality source) 2×', file: '2x_AniSD_AC_G6i2b_SPAN_190K.pth', scale: 2, arch: 'SPAN', kind: 'animation' },
    { id: 'anisd-lq', name: 'AniSD (Old animation, low quality source) 2×', file: '2x_AniSD_DC_SPAN_92500.pth', scale: 2, arch: 'SPAN', kind: 'animation' }
];

export interface RveStatus {
    dir: string;
    python: boolean;
    backend: boolean;
    backendVersion: string;
    packages: boolean;
    torch: string;
    tensorrt: string;
    torchTensorrt: string;
    cuda: boolean;
    gpu: string;
    ready: boolean;
    models: (RveModel & { installed: boolean; size: number; engines: number })[];
    message: string;
    busy: boolean;
}

interface RveInfo { backendVersion?: string; backendTag?: string; installedAt?: number }

// UVE_RVE_DIR / UVE_RVE_PYTHON: test overrides used by the self test (mock backend)
export async function rveDir() { return process.env.UVE_RVE_DIR || path.join(await toolsDir(), 'rve'); }
const pythonExe = async () => process.env.UVE_RVE_PYTHON || (IS_WIN ? path.join(await rveDir(), 'python', 'python.exe') : path.join(await rveDir(), 'python', 'bin', 'python3'));
const backendScript = async () => path.join(await rveDir(), 'backend', 'rve-backend.py');
const modelsDir = async () => path.join(await rveDir(), 'models');

function run(file: string, args: string[], opts: { cwd?: string; timeoutMs?: number } = {}): Promise<{ code: number; out: string; err: string }> {
    return new Promise(resolve => {
        execFile(file, args, { cwd: opts.cwd, timeout: opts.timeoutMs ?? 120000, windowsHide: true, maxBuffer: 64 * 1024 * 1024 }, (e, out, err) => {
            resolve({ code: e ? Number((e as unknown as { code?: unknown }).code) || 1 : 0, out: String(out), err: String(err) });
        });
    });
}

let busy: { message: string } | null = null;
let probeCache: Partial<RveStatus> | null = null;

/** Imports torch / tensorrt inside the portable Python and reports versions and CUDA availability. */
async function probePackages(): Promise<Partial<RveStatus>> {
    if (probeCache) return probeCache;
    const py = await pythonExe();
    const code = [
        'import json,importlib',
        'r={}',
        'def v(m):',
        '    try:',
        '        return importlib.import_module(m).__version__',
        '    except Exception as e:',
        '        return ""',
        'r["torch"]=v("torch"); r["tensorrt"]=v("tensorrt"); r["torch_tensorrt"]=v("torch_tensorrt")',
        'try:',
        '    import torch',
        '    r["cuda"]=torch.cuda.is_available()',
        '    r["gpu"]=torch.cuda.get_device_name(0) if r["cuda"] else ""',
        'except Exception:',
        '    r["cuda"]=False; r["gpu"]=""',
        'print(json.dumps(r))'
    ].join('\n');
    const r = await run(py, ['-c', code], { timeoutMs: 180000 });
    try {
        const j = JSON.parse(r.out.trim().split(/\r?\n/).pop() || '{}');
        probeCache = { torch: j.torch, tensorrt: j.tensorrt, torchTensorrt: j.torch_tensorrt, cuda: !!j.cuda, gpu: j.gpu, packages: !!(j.torch && j.tensorrt && j.torch_tensorrt) };
    } catch {
        probeCache = { packages: false, torch: '', tensorrt: '', torchTensorrt: '', cuda: false, gpu: '' };
    }
    return probeCache;
}

export async function rveStatus(deep = true): Promise<RveStatus> {
    const dir = await rveDir();
    const python = await fileExists(await pythonExe());
    const backend = await fileExists(await backendScript());
    const info = await readJson<RveInfo>(path.join(dir, 'rve-manager.json'), {});
    const pk = python && deep && !busy ? await probePackages() : (probeCache || {});
    const md = await modelsDir();
    const custom = await customModels();
    const models = await Promise.all([...RVE_MODELS, ...custom].map(async m => {
        const st = await fsp.stat(path.join(md, m.file)).catch(() => null);
        const engines = (await fsp.readdir(md).catch(() => [] as string[])).filter(f => f.startsWith(m.file.replace(/\.(pth|safetensors)$/i, '')) && f.endsWith('.engine')).length;
        return { ...m, installed: !!st, size: st?.size ?? 0, engines };
    }));
    const ready = python && backend && !!pk.packages && !!pk.cuda;
    return {
        dir, python, backend, backendVersion: info.backendVersion || '', packages: !!pk.packages,
        torch: pk.torch || '', tensorrt: pk.tensorrt || '', torchTensorrt: pk.torchTensorrt || '', cuda: !!pk.cuda, gpu: pk.gpu || '',
        ready, models, busy: !!busy, message: busy?.message || (ready ? 'Ready' : !python ? 'Not installed' : !backend ? 'Backend missing' : !pk.packages ? 'Python packages missing' : 'CUDA not available')
    };
}

async function customModels(): Promise<RveModel[]> {
    const md = await modelsDir();
    const files = await fsp.readdir(path.join(md, 'custom')).catch(() => [] as string[]);
    return files.filter(f => /\.(pth|safetensors)$/i.test(f)).map(f => {
        const m = f.match(/(\d)x|x(\d)/i);
        const scale = m ? Number(m[1] || m[2]) : 2;
        return { id: 'custom:' + f, name: `${f} (custom)`, file: `custom/${f}`, scale, arch: 'auto', kind: 'realistic' as const, custom: true };
    });
}

export async function findModel(id: string): Promise<RveModel | null> {
    return [...RVE_MODELS, ...(await customModels())].find(m => m.id === id) || null;
}

/** tar.gz extraction with the Windows built-in bsdtar (Windows 10 1803+). */
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

/**
 * Installs (or repairs) the complete RVE TensorRT stack into tools\rve. Each step is skipped when it is
 * already present, so the call is also used to resume an interrupted installation.
 */
export async function installRve(progress: (m: string, pct: number) => void, log: (l: string) => void, opts: { reinstallBackend?: boolean } = {}): Promise<RveStatus> {
    if (busy) throw new Error('An RVE installation is already running.');
    if (IS_MAC || process.arch !== 'x64') throw new Error('The REAL-Video-Enhancer TensorRT backend needs an NVIDIA GPU on Windows or Linux (x64); it is not available on this system.');
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
            if (!(await fileExists(py))) throw new Error('python.exe not found after extraction');
        }
        // 2. backend
        const info = await readJson<RveInfo>(path.join(dir, 'rve-manager.json'), {});
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
            await writeJson(path.join(dir, 'rve-manager.json'), info);
        }
        // 3. Python packages (RVE DownloadDeps pins for the TensorRT backend)
        const pk = await probePackages();
        if (!pk.packages) {
            probeCache = null;
            const base = ['install', '--no-warn-script-location', '--isolated', '--extra-index-url', 'https://download.pytorch.org/whl/test/',
                '--extra-index-url', 'https://download.pytorch.org/whl/', '--trusted-host', 'download.pytorch.org'];
            const steps: { label: string; deps: string[] }[] = [
                {
                    label: 'PyTorch (CUDA) and helpers', deps: ['testresources==2.0.1', 'requests==2.32.3', 'opencv-python-headless==4.11.0.86', 'pypresence==4.3.0',
                        'scenedetect==0.6.5.2', 'numpy==2.2.2', 'sympy', 'tqdm==4.67.1', 'typing_extensions==4.12.2', 'packaging==24.2', 'mpmath==1.3.0', 'pillow==11.1.0',
                        `torch==${RVE.torch}${RVE.cuda}`, 'safetensors==0.5.3', 'einops==0.8.1']
                },
                { label: 'torchvision', deps: ['--no-deps', `torchvision==${RVE.torchvision}${RVE.cuda}`] },
                {
                    label: 'TensorRT', deps: [`tensorrt==${RVE.tensorrt}`, `tensorrt_cu12==${RVE.tensorrt}`, `tensorrt-cu12_libs==${RVE.tensorrt}`, `tensorrt_cu12_bindings==${RVE.tensorrt}`,
                        '--no-deps', 'dllist', `torch-tensorrt==${RVE.torch.replace(/\d$/, '0')}${RVE.cuda}`]
                }
            ];
            for (let i = 0; i < steps.length; i++) {
                const s = steps[i];
                say(`Installing ${s.label} (step ${i + 1}/${steps.length}) – this downloads several GB…`, i / steps.length);
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
        say('Verifying CUDA / TensorRT…', 1);
        const st = await rveStatus(true);
        if (!st.packages) throw new Error('torch / tensorrt / torch_tensorrt could not be imported after the installation.');
        if (!st.cuda) throw new Error('PyTorch was installed but CUDA is not available (NVIDIA GPU and current driver required).');
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

export async function downloadModel(id: string, progress: (m: string, p: number) => void): Promise<string> {
    const m = await findModel(id);
    if (!m) throw new Error(`Unknown model ${id}`);
    const file = path.join(await modelsDir(), m.file);
    if (await fileExists(file)) return file;
    if (m.custom) throw new Error(`Custom model file is missing: ${file}`);
    await downloadTo(RVE.modelsUrl + encodeURIComponent(m.file), file + '.part', 500 * 1024 * 1024, progress, `Downloading model ${m.file}`);
    await fsp.rename(file + '.part', file);
    return file;
}

export async function deleteModel(id: string) {
    const m = await findModel(id);
    if (!m) return;
    const md = await modelsDir();
    const stem = path.basename(m.file).replace(/\.(pth|safetensors)$/i, '');
    for (const f of await fsp.readdir(path.dirname(path.join(md, m.file))).catch(() => [] as string[])) {
        if (f === path.basename(m.file) || (f.startsWith(stem) && f.endsWith('.engine'))) await fsp.rm(path.join(path.dirname(path.join(md, m.file)), f), { force: true });
    }
}

export async function importCustomModel(src: string): Promise<void> {
    if (!/\.(pth|safetensors)$/i.test(src)) throw new Error('Only .pth or .safetensors PyTorch models can be used with the TensorRT backend.');
    const st = await fsp.stat(src);
    if (st.size > 2 * 1024 * 1024 * 1024) throw new Error('Model file is too large.');
    const dest = path.join(await modelsDir(), 'custom', path.basename(src));
    await fsp.mkdir(path.dirname(dest), { recursive: true });
    await fsp.copyFile(src, dest);
}

export interface RveRun { kill: () => void; done: Promise<{ ok: boolean; error?: string }> }

/**
 * Upscales `input` into `output` with the RVE backend. `customEncoder` is passed verbatim to
 * --custom_encoder (whitespace-separated FFmpeg output options, no spaces inside values).
 */
export async function runUpscale(o: {
    input: string; output: string; modelPath: string; ffmpeg: string; customEncoder: string; totalFrames: number;
    onProgress: (frames: number, fps: number) => void; onLog: (l: string) => void;
}): Promise<RveRun> {
    const py = await pythonExe();
    const script = await backendScript();
    if (!(await fileExists(py)) || !(await fileExists(script))) throw new Error('REAL-Video-Enhancer is not installed (Settings → AI Upscaling).');
    const args = [script, '--input', o.input, '--output', o.output, '--backend', 'tensorrt', '--upscale_model', o.modelPath,
        '--precision', 'auto', '--tensorrt_opt_profile', '3', '--ffmpeg_path', o.ffmpeg, '--overwrite',
        '--audio_encoder_preset', 'copy_audio', '--subtitle_encoder_preset', 'copy_subtitle', '--custom_encoder', o.customEncoder];
    o.onLog(`RVE: python ${args.map(a => (/\s/.test(a) ? `"${a}"` : a)).join(' ')}`);
    const p = spawn(py, args, { cwd: path.dirname(script), windowsHide: true, ...groupSpawn, env: { ...process.env, PYTHONNOUSERSITE: '1', PYTHONUNBUFFERED: '1' } });
    let errTail = '';
    let lastFrames = 0;
    const feed = (d: Buffer, isErr: boolean) => {
        const s = String(d);
        for (const part of s.split(/[\r\n]+/)) {
            const line = part.trim();
            if (!line) continue;
            const m = line.match(/FPS:\s*([\d.]+)\s+Current Frame:\s*(\d+)/);
            if (m) { lastFrames = Number(m[2]); o.onProgress(lastFrames, Number(m[1])); continue; }
            o.onLog(`[rve] ${line}`);
            if (isErr) errTail = (errTail + '\n' + line).slice(-1500);
        }
    };
    p.stdout.on('data', d => feed(d, false));
    p.stderr.on('data', d => feed(d, true));
    const done = new Promise<{ ok: boolean; error?: string }>(resolve => {
        p.on('error', e => resolve({ ok: false, error: e.message }));
        p.on('exit', code => resolve(code === 0 ? { ok: true } : { ok: false, error: `REAL-Video-Enhancer exited with code ${code}: ${errTail.trim().split('\n').slice(-4).join(' ')}` }));
    });
    return {
        kill: () => killTree(p.pid),
        done
    };
}

export const rveTempDir = () => path.join(os.tmpdir(), 'uve-manager-rve');
