// v1.0.7: Video2X (https://github.com/k4yt3x/video2x, AGPL-3.0) as an optional "Fast Upscale" provider with the
// Anime4K v4 shaders (https://github.com/bloc97/Anime4K, MIT) running in libplacebo. Video2X is an external
// program: UVE Manager never bundles it, it is downloaded on request from the official GitHub release pinned in
// shared/video2x-cli.ts (or the user selects an existing executable) and started with spawn(shell: false).
//
// Detection: platform → executable → `--help` / `--version` (capabilities, not trusted from docs) →
// `--list-devices` (Vulkan adapters) → a short synthetic test job with the real shader (only that proves that
// Vulkan + libplacebo + the shader work). Results are cached per executable fingerprint (path, size, mtime).
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fsp } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {
    LOSSLESS_CANDIDATES, VIDEO2X_COMPAT, type Video2xCapabilities, assetUrl, buildArgs, cmpVersion, parseDevices, parseHelp, video2xPlatform
} from '../shared/video2x-cli';
import type { Video2xDevice, Video2xInstallInfo, Video2xPresetInfo, Video2xStatus } from '../shared/video2x-types';
import { DEFAULT_VIDEO2X_PRESET, type Video2xExportOptions, video2xProcessor } from '../shared/upscale-provider';
import {
    DEFAULT_MODEL, NEURAL_MODELS, type NeuralProcessorCaps, PROCESSOR_LABEL, type Video2xNeuralProcessor, buildNeuralArgs, comboKey, isNeural, modelFiles,
    neuralComboProblem, parseNeuralCaps
} from '../shared/video2x-processors';
import { capabilities } from './tools';
import { toolsDir, userDir } from './store';
import { download, fileExists, readJson, safeUnzip, writeJson } from './util';
import { type Video2xLauncher, execute, runQuick } from './video2x-process';
import { inspectPreset, loadLibrary, materializeChain, recordTest, shaderRoot } from './video2x-shaders';

interface Config {
    customExe?: string;
    managed?: { version: string; dir: string; exe: string; sourceUrl: string; assetId: number; releaseId: number; sha256: string; installedAt: number };
    verification?: { fingerprint: string; ok: boolean; at: number; message: string; codec: string; shader: string };
    /** built-in shaders of the last probed binary (shown without re-probing) */
    builtins?: { fingerprint: string; names: string[]; version: string };
    /** v1.0.8: Real-ESRGAN / Real-CUGAN test jobs, keyed "<fingerprint>|<processor>|<model>|<scale>|<noise>|<device>" */
    neuralTests?: Record<string, { ok: boolean; at: number; message: string; codec: string }>;
    /** v1.0.8: neural processor capabilities of the last probed binary */
    neural?: { fingerprint: string; caps: Record<Video2xNeuralProcessor, NeuralProcessorCaps> };
}

// UVE_VIDEO2X_CONFIG_DIR: test override (self test) so the user's settings are not touched
const cfgDir = () => process.env.UVE_VIDEO2X_CONFIG_DIR || path.join(userDir(), 'video2x');
const cfgFile = () => path.join(cfgDir(), 'video2x.json');
const readCfg = () => readJson<Config>(cfgFile(), {});
const writeCfg = (c: Config) => writeJson(cfgFile(), c);
export const logFile = () => process.env.UVE_VIDEO2X_CONFIG_DIR ? path.join(cfgDir(), 'video2x.log') : path.join(userDir(), 'logs', 'video2x.log');

let busy: { message: string; abort?: AbortController } | null = null;

/** Appends to the Video2X log (rotated at 2 MB). */
export async function v2xLog(line: string) {
    try {
        const f = logFile();
        await fsp.mkdir(path.dirname(f), { recursive: true });
        const st = await fsp.stat(f).catch(() => null);
        if (st && st.size > 2 * 1024 * 1024) await fsp.rename(f, f + '.1').catch(() => undefined);
        await fsp.appendFile(f, `[${new Date().toISOString()}] ${line.replace(/\r?\n/g, ' ⏎ ').slice(0, 4000)}\n`, 'utf8');
    } catch { /* logging must never break a job */ }
}

/* ------------------------------------------------------------------ executable */

export const managedRoot = async () => path.join(await toolsDir(), 'video2x');

/**
 * Executable to use: UVE_VIDEO2X_EXE (tests; UVE_VIDEO2X_PREFIX = JSON argv prefix, e.g. a mock script run by
 * Electron as Node), then a user-selected executable, then the app-managed installation.
 */
async function launcher(cfg?: Config): Promise<{ l: Video2xLauncher | null; source: Video2xStatus['source'] }> {
    const env = process.env.UVE_VIDEO2X_EXE;
    if (env) {
        let prefix: string[] = [];
        try { prefix = JSON.parse(process.env.UVE_VIDEO2X_PREFIX || '[]'); } catch { /* ignore */ }
        return { l: { exe: env, prefix, env: process.env.UVE_VIDEO2X_PREFIX ? { ELECTRON_RUN_AS_NODE: '1' } : undefined }, source: 'env' };
    }
    const c = cfg || await readCfg();
    if (c.customExe) return { l: { exe: c.customExe, prefix: [] }, source: 'custom' };
    if (c.managed?.exe) return { l: { exe: c.managed.exe, prefix: [] }, source: 'managed' };
    return { l: null, source: 'none' };
}

async function fingerprint(l: Video2xLauncher): Promise<string> {
    const st = await fsp.stat(l.exe).catch(() => null);
    return createHash('sha256').update(JSON.stringify([path.resolve(l.exe), st?.size ?? -1, st?.mtimeMs ?? -1, l.prefix])).digest('hex').slice(0, 24);
}

const probeCache = new Map<string, { caps: Video2xCapabilities; help: string }>();

/** `--help` + `--version` of the binary (both exit with code 1 by design – the output is what counts). */
export async function probe(l: Video2xLauncher): Promise<{ caps: Video2xCapabilities; fp: string; error: string; help: string }> {
    const fp = await fingerprint(l);
    const hit = probeCache.get(fp);
    if (hit) return { caps: hit.caps, fp, error: '', help: hit.help };
    const help = await runQuick(l, ['--help'], 20000);
    const ver = await runQuick(l, ['--version'], 20000);
    if (help.spawnError) return { caps: parseHelp('', ''), fp, error: `Video2X could not be started: ${help.spawnError}`, help: '' };
    if (help.timedOut) return { caps: parseHelp('', ''), fp, error: 'Video2X did not answer --help within 20 s.', help: '' };
    const text = help.out + '\n' + help.err;
    const caps = parseHelp(text, ver.out + '\n' + ver.err);
    if (!text.trim()) return { caps, fp, error: `Video2X printed no help text (exit ${help.code}).${/\.AppImage$/i.test(l.exe) ? ' AppImages need FUSE or APPIMAGE_EXTRACT_AND_RUN support.' : ''}`, help: '' };
    probeCache.set(fp, { caps, help: text });
    await v2xLog(`probe ${l.exe}: version ${caps.version || '?'}; ${caps.problem || 'options ok'}; built-in shaders: ${caps.builtinShaders.join(', ')}`);
    return { caps, fp, error: '', help: text };
}

async function listDevices(l: Video2xLauncher, caps: Video2xCapabilities): Promise<{ devices: Video2xDevice[]; error: string }> {
    if (!caps.hasListDevices) return { devices: [], error: '' };
    const r = await runQuick(l, ['--list-devices'], 30000);
    const devices = parseDevices(r.out);
    const err = devices.length ? '' : (r.err || r.out).trim().split(/\r?\n/).slice(-3).join(' ') || `--list-devices exit ${r.code}`;
    return { devices, error: err };
}

/* ------------------------------------------------------------------ helpers */

function runFfmpeg(file: string, args: string[], timeoutMs = 120000): Promise<{ code: number; out: string; err: string }> {
    return new Promise(resolve => {
        const p = spawn(file, args, { windowsHide: true, shell: false });
        let out = '', err = '';
        const t = setTimeout(() => p.kill(), timeoutMs);
        p.stdout.on('data', d => { out += d; });
        p.stderr.on('data', d => { err += d; });
        p.on('error', e => { clearTimeout(t); resolve({ code: 1, out, err: e.message }); });
        p.on('close', code => { clearTimeout(t); resolve({ code: code ?? 1, out, err }); });
    });
}

export interface StreamProbe { width: number; height: number; frames: number; duration: number; fps: number; audio: number; audioDuration: number; videoDuration: number }

/** Exact video properties of a file (packets counted, not estimated from the header). */
export async function probeFile(ffprobe: string, file: string): Promise<StreamProbe> {
    const r = await runFfmpeg(ffprobe, ['-v', 'error', '-count_packets', '-show_entries', 'stream=codec_type,width,height,nb_read_packets,avg_frame_rate,r_frame_rate,duration:format=duration', '-of', 'json', file], 600000);
    if (r.code !== 0) throw new Error(`ffprobe failed for ${path.basename(file)}: ${r.err.trim().slice(0, 300)}`);
    const j = JSON.parse(r.out || '{}') as { streams?: Record<string, string | number>[]; format?: { duration?: string } };
    const v = (j.streams || []).find(s => s.codec_type === 'video');
    const a = (j.streams || []).filter(s => s.codec_type === 'audio');
    const rate = String(v?.avg_frame_rate && v.avg_frame_rate !== '0/0' ? v.avg_frame_rate : v?.r_frame_rate || '0/1').split('/').map(Number);
    return {
        width: Number(v?.width) || 0, height: Number(v?.height) || 0, frames: Number(v?.nb_read_packets) || 0,
        duration: Number(j.format?.duration) || 0, fps: rate[1] ? rate[0] / rate[1] : 0, audio: a.length,
        audioDuration: Number(a[0]?.duration) || 0, videoDuration: Number(v?.duration) || 0
    };
}

/**
 * Short synthetic job through the real binary: 10 frames 128x72 → 256x144 with `shader`. Tries the lossless
 * intermediate formats in order and returns the first that works. This is the only proof that Vulkan, libplacebo
 * and the shader work on this machine.
 */
async function testRun(l: Video2xLauncher, caps: Video2xCapabilities, shader: string, device: number): Promise<{ ok: boolean; codec: string; message: string }> {
    return testRunWith(l, (input, output, lossless) => buildArgs(caps, { input, output, shader, width: 256, height: 144, device, lossless }), 256, 144,
        shader.length > 40 ? 'custom shader chain' : shader);
}

/** The synthetic test job with any argv builder (v1.0.8: also used for Real-ESRGAN / Real-CUGAN). */
async function testRunWith(l: Video2xLauncher, makeArgs: (input: string, output: string, lossless: string) => string[], expectW: number, expectH: number, label: string): Promise<{ ok: boolean; codec: string; message: string }> {
    const ff = await capabilities();
    if (!ff.ok) return { ok: false, codec: '', message: `FFmpeg is needed for the Video2X test: ${ff.error || 'not available'}` };
    const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'uve-v2x-test-'));
    try {
        const input = path.join(dir, 'in.mkv');
        const g = await runFfmpeg(ff.ffmpegPath, ['-v', 'error', '-nostdin', '-f', 'lavfi', '-i', 'testsrc2=size=128x72:rate=10', '-frames:v', '10', '-c:v', 'ffv1', '-pix_fmt', 'gbrp', '-f', 'matroska', '-y', input]);
        if (g.code !== 0) return { ok: false, codec: '', message: `Creating the test clip failed: ${g.err.trim().slice(0, 300)}` };
        const errors: string[] = [];
        for (const c of LOSSLESS_CANDIDATES) {
            const out = path.join(dir, `out-${c.id}.mkv`);
            let args: string[];
            try { args = makeArgs(input, out, c.id); }
            catch (e) { errors.push(`${c.label}: ${e instanceof Error ? e.message : e}`); continue; }
            await v2xLog(`test: ${l.exe} ${args.join(' ')}`);
            const run = execute(l, args, { onLog: line => void v2xLog(`[test] ${line}`) });
            const timer = setTimeout(() => run.kill(), 180000);
            const res = await run.done;
            clearTimeout(timer);
            if (!res.ok) { errors.push(`${c.label}: ${res.error}`); continue; }
            const pr = await probeFile(ff.ffprobePath, out).catch(e => { errors.push(`${c.label}: ${e.message}`); return null; });
            if (!pr) continue;
            if (pr.width !== expectW || pr.height !== expectH || Math.abs(pr.frames - 10) > 1) { errors.push(`${c.label}: test output is ${pr.width}x${pr.height}, ${pr.frames} frames (expected ${expectW}x${expectH}, 10 frames)`); continue; }
            return { ok: true, codec: c.id, message: `Test job passed (${c.label}, ${label}).` };
        }
        return { ok: false, codec: '', message: errors.join(' · ') || 'The test job failed.' };
    } finally {
        await fsp.rm(dir, { recursive: true, force: true }).catch(() => undefined);
    }
}

/* ------------------------------------------------------------------ v1.0.8 neural processors */

/**
 * Model files of a combination next to the executable (Windows zip layout). An AppImage or another layout keeps
 * its models elsewhere – then the test job is the only check.
 */
async function missingModelFiles(l: Video2xLauncher, p: Video2xNeuralProcessor, model: string, scale: number, noise: number): Promise<string> {
    const root = path.dirname(l.exe);
    if (!(await fileExists(path.join(root, 'models')))) return '';
    const missing: string[] = [];
    for (const f of modelFiles(p, model, scale, noise)) if (!(await fileExists(path.join(root, f)))) missing.push(f);
    return missing.length ? `Model files missing next to Video2X (${root}): ${missing.join(', ')}. Reinstall Video2X or choose another model / scale.` : '';
}

/** 10-frame 128x72 test of one Real-ESRGAN / Real-CUGAN combination; never reuses an Anime4K result. */
async function neuralTest(l: Video2xLauncher, caps: Video2xCapabilities, neural: Record<Video2xNeuralProcessor, NeuralProcessorCaps>, p: Video2xNeuralProcessor, model: string, scale: number, noise: number | undefined, device: number) {
    const miss = await missingModelFiles(l, p, model, scale, noise ?? 0);
    if (miss) return { ok: false, codec: '', message: miss };
    return testRunWith(l, (input, output, lossless) => buildNeuralArgs(caps, neural, { input, output, processor: p, modelId: model, scale, noiseLevel: noise, device, lossless }),
        128 * scale, 72 * scale, `${PROCESSOR_LABEL[p]} ${model} ${scale}x${p === 'realcugan' ? ` noise ${noise ?? 0}` : ''}`);
}

async function neuralCapsOf(l: Video2xLauncher) {
    const pr = await probe(l);
    const neural = parseNeuralCaps(pr.help, pr.caps);
    const c = await readCfg();
    if (!pr.error && JSON.stringify(c.neural) !== JSON.stringify({ fingerprint: pr.fp, caps: neural })) { c.neural = { fingerprint: pr.fp, caps: neural }; await writeCfg(c); }
    return { pr, neural };
}

/** Settings → Video2X → "Test" for one processor / model / scale / noise combination. */
export async function testProcessor(p: Video2xNeuralProcessor, model: string, scale: number, noise: number | undefined, device = -1): Promise<{ ok: boolean; message: string }> {
    if (busy) throw new Error('Video2X is busy.');
    if (!isNeural(p)) throw new Error('Unknown processor.');
    const { l } = await launcher();
    if (!l) throw new Error('Video2X is not installed.');
    busy = { message: `Testing ${PROCESSOR_LABEL[p]}…` };
    try {
        const { pr, neural } = await neuralCapsOf(l);
        if (pr.error || !pr.caps.versionSupported) throw new Error(pr.error || pr.caps.problem);
        if (!neural[p].available) throw new Error(neural[p].problem);
        const combo = neuralComboProblem(p, model, scale, noise);
        if (combo) throw new Error(combo);
        const r = await neuralTest(l, pr.caps, neural, p, model, scale, noise, device);
        await v2xLog(`test ${comboKey(p, model, scale, noise, device)}: ${r.ok ? 'OK' : 'FAILED'} ${r.message}`);
        const c = await readCfg();
        c.neuralTests = { ...(c.neuralTests || {}), [`${pr.fp}|${comboKey(p, model, scale, noise, device)}`]: { ok: r.ok, at: Date.now(), message: r.message.slice(0, 2000), codec: r.codec } };
        await writeCfg(c);
        return { ok: r.ok, message: r.message };
    } finally { busy = null; }
}

function processorStatus(cfg: Config, fp: string, neural: Record<Video2xNeuralProcessor, NeuralProcessorCaps> | undefined): Video2xStatus['processors'] {
    if (!neural) return undefined;
    const out = {} as NonNullable<Video2xStatus['processors']>;
    for (const p of ['realesrgan', 'realcugan'] as const) {
        const prefix = `${fp}|${p}|`;
        const tests = Object.entries(cfg.neuralTests || {}).filter(([k]) => k.startsWith(prefix));
        out[p] = {
            available: neural[p].available, problem: neural[p].problem, noise: neural[p].noise,
            models: neural[p].models.map(m => ({ id: m.id, label: m.label, scales: m.scales })),
            verified: tests.filter(([, v]) => v.ok).map(([k]) => k.slice(fp.length + 1)),
            failed: tests.filter(([, v]) => !v.ok).map(([k, v]) => ({ combo: k.slice(fp.length + 1), message: v.message }))
        };
    }
    return out;
}

/* ------------------------------------------------------------------ presets */

const BUILTIN_LABEL: Record<string, string> = {
    'anime4k-v4-a': 'Fast – Anime4K v4 (Mode A)',
    'anime4k-v4-a+a': 'Balanced – Anime4K v4 (Mode A+A)',
    'anime4k-v4-b': 'Anime4K v4 Mode B (soft / downscaled sources)',
    'anime4k-v4-b+b': 'Anime4K v4 Mode B+B',
    'anime4k-v4-c': 'Anime4K v4 Mode C (low-resolution sources)',
    'anime4k-v4-c+a': 'Anime4K v4 Mode C+A',
    'anime4k-v4.1-gan': 'Anime4K v4.1 GAN (experimental)'
};

function builtinPresets(names: string[], verified: Config['verification'] | undefined, fp: string): Video2xPresetInfo[] {
    return names.map(n => ({
        id: `builtin:${n}`, displayName: BUILTIN_LABEL[n] || `Anime4K – ${n}`, kind: 'built-in' as const, builtinShader: n, shaderFiles: [],
        source: 'Bundled with Video2X (github.com/bloc97/Anime4K)', author: 'bloc97 (Anime4K)', license: 'MIT',
        available: true, problem: '', files: [], passes: 1,
        tested: verified && verified.fingerprint === fp && verified.shader === n ? { ok: verified.ok, at: verified.at, message: verified.message } : null
    }));
}

async function customPresets(): Promise<Video2xPresetInfo[]> {
    const lib = await loadLibrary();
    return Promise.all(lib.presets.map(async p => {
        const i = await inspectPreset(p);
        return { ...p, available: !i.problem, problem: i.problem, files: i.files, passes: i.passes, tested: lib.tests[p.id] || null };
    }));
}

/* ------------------------------------------------------------------ status */

export function installInfo(): Video2xInstallInfo | null {
    const plat = video2xPlatform(process.platform, process.arch);
    const a = VIDEO2X_COMPAT.assets[plat.key];
    if (!plat.supported || !a) return null;
    return {
        version: VIDEO2X_COMPAT.pinnedVersion, asset: a.name, url: assetUrl(VIDEO2X_COMPAT.pinnedVersion, a.name), size: a.size, sha256: a.sha256,
        releaseId: VIDEO2X_COMPAT.releaseId, assetId: a.assetId, license: VIDEO2X_COMPAT.license, vendor: VIDEO2X_COMPAT.vendor,
        integrity: 'SHA-256 pinned by UVE Manager for this release asset (upstream publishes no checksums or signatures; signature verification is unavailable).'
    };
}

export async function video2xStatus(deep = true): Promise<Video2xStatus> {
    const cfg = await readCfg();
    const plat = video2xPlatform(process.platform, process.arch);
    const { l, source } = await launcher(cfg);
    const warnings: string[] = [];
    const base: Video2xStatus = {
        platformSupported: plat.supported, platformReason: plat.reason, state: 'not-installed', summary: '', diagnostic: '',
        source, exe: l?.exe || '', version: '', versionSupported: false, pinnedVersion: VIDEO2X_COMPAT.pinnedVersion, updateAvailable: false,
        managed: cfg.managed ? { version: cfg.managed.version, dir: cfg.managed.dir, installedAt: cfg.managed.installedAt, sourceUrl: cfg.managed.sourceUrl, sha256: cfg.managed.sha256 } : null,
        customExe: cfg.customExe || '',
        vulkan: { status: 'unknown', message: '', devices: [] }, shaderSupport: '', builtinShaders: [], verification: null,
        presets: await customPresets(), install: installInfo(), warnings, busy: !!busy, message: busy?.message || '', logFile: logFile(), shaderDir: shaderRoot()
    };
    if (cfg.managed && cmpVersion(cfg.managed.version, VIDEO2X_COMPAT.pinnedVersion) < 0) base.updateAvailable = true;
    if (!plat.supported && source !== 'env') {
        return { ...base, state: 'unsupported', summary: 'Unsupported on this system', diagnostic: plat.reason };
    }
    if (!l) return { ...base, state: 'not-installed', summary: 'Not installed' };
    if (!(await fileExists(l.exe))) {
        return { ...base, state: 'not-installed', summary: 'Not installed', diagnostic: `The Video2X executable was not found: ${l.exe}` };
    }
    if (source === 'custom') warnings.push('User-selected executable: UVE Manager did not download this build and cannot vouch for it. It is only used after a successful test job.');
    const fp = await fingerprint(l);
    const ver = cfg.verification && cfg.verification.fingerprint === fp ? cfg.verification : undefined;
    const cachedBuiltins = cfg.builtins?.fingerprint === fp ? cfg.builtins : undefined;
    if (!deep || busy) {
        const names = cachedBuiltins?.names || [];
        return {
            ...base, version: cachedBuiltins?.version || '', versionSupported: !!cachedBuiltins, builtinShaders: names,
            presets: [...builtinPresets(names, ver, fp), ...base.presets],
            processors: processorStatus(cfg, fp, cfg.neural?.fingerprint === fp ? cfg.neural.caps : undefined),
            verification: ver ? { ok: ver.ok, at: ver.at, message: ver.message, codec: ver.codec } : null,
            vulkan: { status: ver?.ok ? 'verified' : 'unknown', message: '', devices: [] },
            state: ver ? (ver.ok ? 'installed' : 'verification-failed') : 'unverified',
            summary: `Video2X ${cachedBuiltins?.version || ''} | ${ver?.ok ? 'Vulkan verified' : ver ? 'verification failed' : 'not verified'}`,
            diagnostic: ver && !ver.ok ? ver.message : ''
        };
    }
    const pr = await probe(l);
    if (pr.error || !pr.caps.versionSupported) {
        return { ...base, version: pr.caps.version, state: 'verification-failed', summary: `Video2X ${pr.caps.version || '?'} | not usable`, diagnostic: pr.error || pr.caps.problem };
    }
    const caps = pr.caps;
    if (!cachedBuiltins || cachedBuiltins.names.join() !== caps.builtinShaders.join()) {
        cfg.builtins = { fingerprint: fp, names: caps.builtinShaders, version: caps.version };
        await writeCfg(cfg);
    }
    const dev = await listDevices(l, caps);
    const { neural } = await neuralCapsOf(l);
    const vulkan: Video2xStatus['vulkan'] = ver?.ok
        ? { status: 'verified', message: 'Test job passed on this GPU', devices: dev.devices }
        : dev.devices.length ? { status: 'devices-found', message: `${dev.devices.length} Vulkan device(s) found – not verified by a test job yet`, devices: dev.devices }
            : { status: caps.hasListDevices ? 'none' : 'unknown', message: dev.error || (caps.hasListDevices ? 'No Vulkan device found' : 'Device listing not supported by this build'), devices: [] };
    if (cmpVersion(caps.version, VIDEO2X_COMPAT.pinnedVersion) !== 0) warnings.push(`Video2X ${caps.version} has not been tested with UVE Manager (tested: ${VIDEO2X_COMPAT.pinnedVersion}). Its options were checked from --help.`);
    const state: Video2xStatus['state'] = ver ? (ver.ok ? 'installed' : 'verification-failed') : 'unverified';
    return {
        ...base, version: caps.version, versionSupported: true, builtinShaders: caps.builtinShaders,
        shaderSupport: `libplacebo · ${caps.builtinShaders.length} built-in Anime4K shader(s) · custom mpv GLSL`,
        presets: [...builtinPresets(caps.builtinShaders, ver, fp), ...base.presets],
        processors: processorStatus(await readCfg(), fp, neural),
        verification: ver ? { ok: ver.ok, at: ver.at, message: ver.message, codec: ver.codec } : null,
        vulkan, state,
        summary: `Video2X ${caps.version} | ${ver?.ok ? 'Vulkan verified' : ver ? 'verification failed' : dev.devices.length ? 'Vulkan device found, not verified' : 'Vulkan not verified'}`,
        diagnostic: ver && !ver.ok ? ver.message : vulkan.status === 'none' ? vulkan.message : ''
    };
}

/** Verify installation: probe + device list + synthetic test job with the default (or given) built-in shader. */
export async function verifyInstallation(device = -1): Promise<Video2xStatus> {
    if (busy) throw new Error('Video2X is busy.');
    const cfg = await readCfg();
    const { l } = await launcher(cfg);
    if (!l) throw new Error('Video2X is not installed.');
    busy = { message: 'Verifying Video2X…' };
    try {
        probeCache.clear();
        const pr = await probe(l);
        if (pr.error) throw new Error(pr.error);
        if (!pr.caps.versionSupported) throw new Error(pr.caps.problem);
        const shader = pr.caps.builtinShaders.includes('anime4k-v4-a') ? 'anime4k-v4-a' : pr.caps.builtinShaders[0];
        if (!shader) throw new Error('This Video2X build lists no built-in Anime4K shader.');
        const r = await testRun(l, pr.caps, shader, device);
        await v2xLog(`verify: ${r.ok ? 'OK' : 'FAILED'} ${r.message}`);
        const c = await readCfg();
        c.verification = { fingerprint: pr.fp, ok: r.ok, at: Date.now(), message: r.message, codec: r.codec, shader };
        await writeCfg(c);
    } finally { busy = null; }
    return video2xStatus(true);
}

/** Test run of one preset (custom chains are compiled by libplacebo only here). */
export async function testPreset(presetId: string, device = -1): Promise<{ ok: boolean; message: string }> {
    if (busy) throw new Error('Video2X is busy.');
    const { l } = await launcher();
    if (!l) throw new Error('Video2X is not installed.');
    busy = { message: 'Testing shader preset…' };
    try {
        const pr = await probe(l);
        if (pr.error || !pr.caps.versionSupported) throw new Error(pr.error || pr.caps.problem);
        const shader = presetId.startsWith('builtin:') ? presetId.slice(8) : (await materializeChain(presetId.slice(7))).file;
        if (presetId.startsWith('builtin:') && !pr.caps.builtinShaders.includes(shader)) throw new Error(`This Video2X build has no built-in shader "${shader}".`);
        const r = await testRun(l, pr.caps, shader, device);
        await v2xLog(`test preset ${presetId}: ${r.ok ? 'OK' : 'FAILED'} ${r.message}`);
        if (presetId.startsWith('custom:')) await recordTest(presetId.slice(7), r);
        else if (r.ok || shader === (await readCfg()).verification?.shader) {
            const c = await readCfg();
            c.verification = { fingerprint: pr.fp, ok: r.ok, at: Date.now(), message: r.message, codec: r.codec, shader };
            await writeCfg(c);
        }
        return { ok: r.ok, message: r.message };
    } finally { busy = null; }
}

/* ------------------------------------------------------------------ install / remove / custom */

/** Downloads the pinned official release, verifies its SHA-256, extracts it in a staging folder and moves it into tools/video2x/<version>. */
export async function installVideo2x(progress: (m: string, pct: number) => void): Promise<Video2xStatus> {
    if (busy) throw new Error('Video2X is busy.');
    const info = installInfo();
    if (!info) throw new Error(video2xPlatform(process.platform, process.arch).reason);
    const asset = VIDEO2X_COMPAT.assets[video2xPlatform(process.platform, process.arch).key];
    const abort = new AbortController();
    busy = { message: 'Downloading Video2X…', abort };
    const say = (m: string, p: number) => { if (busy) busy.message = m; progress(m, p); };
    const root = await managedRoot();
    const staging = path.join(root, `.staging-${Date.now().toString(36)}`);
    try {
        await v2xLog(`install: ${info.url} (asset ${info.assetId}, ${info.size} bytes, sha256 ${info.sha256})`);
        const data = await download(info.url, {
            maxBytes: info.size + 1024 * 1024, signal: abort.signal,
            onProgress: (r, t) => say(`Downloading Video2X ${info.version}: ${(r / 1048576).toFixed(0)} / ${((t || info.size) / 1048576).toFixed(0)} MB`, r / (t || info.size))
        });
        const got = createHash('sha256').update(data).digest('hex');
        if (got !== info.sha256) throw new Error(`Checksum mismatch for ${info.asset}: expected ${info.sha256}, got ${got}. Nothing was installed – try again; if it persists the download was altered.`);
        say('Checksum verified – extracting…', 1);
        await fsp.mkdir(staging, { recursive: true });
        if (asset.kind === 'zip') {
            const entries = safeUnzip(data, { maxEntries: 5000, maxEntryBytes: 400 * 1024 * 1024, maxTotalBytes: 2 * 1024 * 1024 * 1024 });
            for (const e of entries) {
                const dest = path.resolve(staging, e.name);
                if (!dest.startsWith(path.resolve(staging) + path.sep)) throw new Error(`Unsafe archive path: ${e.name}`);
                await fsp.mkdir(path.dirname(dest), { recursive: true });
                await fsp.writeFile(dest, e.data);
            }
        } else {
            await fsp.writeFile(path.join(staging, asset.exe), data, { mode: 0o755 });
        }
        if (!(await fileExists(path.join(staging, asset.exe)))) throw new Error(`${asset.exe} not found in ${info.asset}.`);
        await fsp.writeFile(path.join(staging, 'UVE-SOURCE.txt'), [
            `Video2X ${info.version} – ${info.vendor}`, `License: ${info.license} (source: ${VIDEO2X_COMPAT.repo}/tree/${info.version})`,
            `Downloaded from: ${info.url}`, `GitHub release id ${info.releaseId}, asset id ${info.assetId}`, `SHA-256: ${info.sha256}`, info.integrity,
            'Anime4K shaders: MIT license, https://github.com/bloc97/Anime4K', ''
        ].join('\n'));
        const final = path.join(root, info.version);
        const old = final + `.old-${Date.now().toString(36)}`;
        if (await fileExists(final)) await fsp.rename(final, old);
        await fsp.rename(staging, final);
        await fsp.rm(old, { recursive: true, force: true }).catch(() => undefined);
        const cfg = await readCfg();
        const prev = cfg.managed;
        cfg.managed = { version: info.version, dir: final, exe: path.join(final, asset.exe), sourceUrl: info.url, assetId: info.assetId, releaseId: info.releaseId, sha256: info.sha256, installedAt: Date.now() };
        await writeCfg(cfg);
        // the previous managed version (never a user executable) is removed after the switch
        if (prev?.dir && path.resolve(prev.dir) !== path.resolve(final) && path.resolve(prev.dir).startsWith(path.resolve(root) + path.sep)) await fsp.rm(prev.dir, { recursive: true, force: true }).catch(() => undefined);
        await v2xLog(`install: ok → ${final}`);
    } catch (e) {
        await fsp.rm(staging, { recursive: true, force: true }).catch(() => undefined);
        const msg = abort.signal.aborted ? 'Installation canceled.' : e instanceof Error ? e.message : String(e);
        await v2xLog(`install: failed – ${msg}`);
        throw new Error(msg);
    } finally { busy = null; }
    probeCache.clear();
    if ((await readCfg()).customExe) return video2xStatus(true);
    return verifyInstallation().catch(() => video2xStatus(true));
}

export function cancelVideo2xInstall() { busy?.abort?.abort(); }

/** Removes the app-managed installation only (a user-selected executable is never deleted). */
export async function removeManaged(): Promise<Video2xStatus> {
    if (busy) throw new Error('Video2X is busy.');
    const cfg = await readCfg();
    const root = path.resolve(await managedRoot());
    if (cfg.managed?.dir && path.resolve(cfg.managed.dir).startsWith(root + path.sep)) await fsp.rm(cfg.managed.dir, { recursive: true, force: true });
    delete cfg.managed;
    if (cfg.verification && !cfg.customExe) delete cfg.verification;
    await writeCfg(cfg);
    probeCache.clear();
    return video2xStatus(true);
}

export async function setCustomExe(file: string | null): Promise<Video2xStatus> {
    const cfg = await readCfg();
    if (file) {
        const st = await fsp.stat(file).catch(() => null);
        if (!st?.isFile()) throw new Error('Select the Video2X executable file.');
        if (process.platform === 'win32' && !/\.exe$/i.test(file)) throw new Error('Select video2x.exe.');
        cfg.customExe = path.resolve(file);
    } else delete cfg.customExe;
    await writeCfg(cfg);
    probeCache.clear();
    return video2xStatus(true);
}

/** Reset to default: forget the custom executable and all verification results (installed files are kept). */
export async function resetVideo2x(): Promise<Video2xStatus> {
    const cfg = await readCfg();
    delete cfg.customExe; delete cfg.verification; delete cfg.builtins;
    await writeCfg(cfg);
    probeCache.clear();
    return video2xStatus(true);
}

/* ------------------------------------------------------------------ render jobs */

export interface PreparedVideo2x {
    launcher: Video2xLauncher;
    caps: Video2xCapabilities;
    shader: string;
    shaderLabel: string;
    presetName: string;
    device: number;
    lossless: string;
    version: string;
    notes: string[];
    /** v1.0.8: set for Real-ESRGAN / Real-CUGAN jobs (then `shader` is unused) */
    neural?: { processor: Video2xNeuralProcessor; modelId: string; scale: number; noiseLevel?: number; caps: Record<Video2xNeuralProcessor, NeuralProcessorCaps> };
}

/**
 * Everything a render job needs, re-checked for every run (and every retry): executable, version/options,
 * preset availability, a passed test job for this binary and – for custom chains – for exactly this chain.
 */
export async function prepareJob(o: Video2xExportOptions): Promise<PreparedVideo2x> {
    if (busy) throw new Error(`Video2X is busy (${busy.message}) – try again when it has finished.`);
    const st0 = video2xPlatform(process.platform, process.arch);
    const cfg = await readCfg();
    const { l, source } = await launcher(cfg);
    if (!st0.supported && source !== 'env') throw new Error(st0.reason);
    if (!l) throw new Error('Video2X is not installed – install it in Settings → AI Upscaling → Video2X, or choose another AI Upscale provider.');
    if (!(await fileExists(l.exe))) throw new Error(`The Video2X executable was not found: ${l.exe}`);
    const pr = await probe(l);
    if (pr.error) throw new Error(pr.error);
    if (!pr.caps.versionSupported) throw new Error(pr.caps.problem);
    const processor = video2xProcessor(o);
    if (isNeural(processor)) return prepareNeural(l, source, pr, processor, o);
    const notes: string[] = [];
    const presetId = o.presetId || DEFAULT_VIDEO2X_PRESET;
    let shader: string, shaderLabel: string, presetName: string;
    if (presetId.startsWith('builtin:')) {
        shader = presetId.slice(8);
        if (!pr.caps.builtinShaders.includes(shader)) throw new Error(`The installed Video2X ${pr.caps.version} has no built-in shader "${shader}" (available: ${pr.caps.builtinShaders.join(', ') || 'none'}). Choose another preset.`);
        shaderLabel = shader; presetName = BUILTIN_LABEL[shader] || shader;
    } else {
        const m = await materializeChain(presetId.slice(7));
        shader = m.file; shaderLabel = `custom chain ${m.hash.slice(0, 12)} (${m.preset.shaderFiles.filter(f => f.enabled).length} file(s))`; presetName = m.preset.displayName;
    }
    // a working test job for this binary is required before real footage is processed
    let ver = cfg.verification?.fingerprint === pr.fp ? cfg.verification : undefined;
    if (!ver?.ok) {
        const def = pr.caps.builtinShaders.includes('anime4k-v4-a') ? 'anime4k-v4-a' : pr.caps.builtinShaders[0] || shader;
        notes.push('Video2X: running the verification test job…');
        const r = await testRun(l, pr.caps, def, o.device);
        const c = await readCfg();
        c.verification = { fingerprint: pr.fp, ok: r.ok, at: Date.now(), message: r.message, codec: r.codec, shader: def };
        await writeCfg(c);
        ver = c.verification;
        if (!r.ok) throw new Error(`Video2X verification failed: ${r.message}`);
    }
    if (presetId.startsWith('custom:')) {
        const id = presetId.slice(7);
        const lib = await loadLibrary();
        const t = lib.tests[id];
        if (!t?.ok) {
            const r = await testRun(l, pr.caps, shader, o.device);
            await recordTest(id, r);
            if (!r.ok) throw new Error(`The custom shader preset "${presetName}" failed its libplacebo test: ${r.message}`);
            notes.push(`Video2X: custom shader chain tested – ${r.message}`);
        }
    }
    if (o.device >= 0 && pr.caps.hasListDevices) {
        const d = await listDevices(l, pr.caps);
        if (d.devices.length && !d.devices.some(x => x.index === o.device)) throw new Error(`GPU #${o.device} is not available for Video2X (devices: ${d.devices.map(x => `${x.index}. ${x.name}`).join(', ')}). Choose another GPU in Output Settings.`);
    }
    if (source === 'custom') notes.push(`Video2X: user-selected executable ${l.exe} (not downloaded by UVE Manager).`);
    return { launcher: l, caps: pr.caps, shader, shaderLabel, presetName, device: o.device, lossless: ver.codec || LOSSLESS_CANDIDATES[0].id, version: pr.caps.version, notes };
}

/**
 * v1.0.8: preparation of a Real-ESRGAN / Real-CUGAN job: processor and model offered by this binary, valid
 * model/scale/noise combination, model files present (when the layout allows checking), a passed 10-frame test of
 * exactly this combination on this GPU. No fallback to another model or processor.
 */
async function prepareNeural(l: Video2xLauncher, source: Video2xStatus['source'], pr: Awaited<ReturnType<typeof probe>>, p: Video2xNeuralProcessor, o: Video2xExportOptions): Promise<PreparedVideo2x> {
    const neural = parseNeuralCaps(pr.help, pr.caps);
    const name = PROCESSOR_LABEL[p];
    if (!neural[p].available) throw new Error(`${name} is not available: ${neural[p].problem} Choose another Video2X processor.`);
    const modelId = o.modelId || DEFAULT_MODEL[p];
    if (!neural[p].models.some(m => m.id === modelId)) throw new Error(`${name}: model "${modelId}" is not offered by this Video2X build (available: ${neural[p].models.map(m => m.id).join(', ')}). Choose another model.`);
    const noise = p === 'realcugan' ? (o.noiseLevel ?? (NEURAL_MODELS.realcugan.find(m => m.id === modelId)?.scales[o.scale]?.[0] ?? 0)) : undefined;
    const combo = neuralComboProblem(p, modelId, o.scale, noise);
    if (combo) throw new Error(`${name}: ${combo}`);
    const miss = await missingModelFiles(l, p, modelId, o.scale, noise ?? 0);
    if (miss) throw new Error(`${name}: ${miss}`);
    if (o.device >= 0 && pr.caps.hasListDevices) {
        const d = await listDevices(l, pr.caps);
        if (d.devices.length && !d.devices.some(x => x.index === o.device)) throw new Error(`GPU #${o.device} is not available for Video2X (devices: ${d.devices.map(x => `${x.index}. ${x.name}`).join(', ')}). Choose another GPU in Output Settings.`);
    }
    const notes: string[] = [];
    const key = `${pr.fp}|${comboKey(p, modelId, o.scale, noise, o.device)}`;
    let t = (await readCfg()).neuralTests?.[key];
    if (!t?.ok) {
        notes.push(`Video2X: testing ${name} ${modelId} ${o.scale}x on this GPU (10-frame test job)…`);
        const r = await neuralTest(l, pr.caps, neural, p, modelId, o.scale, noise, o.device);
        const c = await readCfg();
        t = { ok: r.ok, at: Date.now(), message: r.message.slice(0, 2000), codec: r.codec };
        c.neuralTests = { ...(c.neuralTests || {}), [key]: t };
        await writeCfg(c);
        await v2xLog(`test ${key}: ${r.ok ? 'OK' : 'FAILED'} ${r.message}`);
        if (!r.ok) throw new Error(`${name} ${modelId} ${o.scale}x failed its test job: ${r.message} Choose another model, scale or GPU.`);
    }
    if (source === 'custom') notes.push(`Video2X: user-selected executable ${l.exe} (not downloaded by UVE Manager).`);
    const label = `${name} ${modelId} ${o.scale}x${p === 'realcugan' ? ` noise ${noise}` : ''}`;
    return {
        launcher: l, caps: pr.caps, shader: '', shaderLabel: label, presetName: label, device: o.device, lossless: t.codec || LOSSLESS_CANDIDATES[0].id,
        version: pr.caps.version, notes, neural: { processor: p, modelId, scale: o.scale, noiseLevel: noise, caps: neural }
    };
}
