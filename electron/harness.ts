// Test harness (v1.0.1): capability probe, parity reference exports and benchmarks.
// CLI: electron . --probe-capabilities=<out.json> | --parity-export=<outDir> | --benchmark=<out.json>
// All exports go through the normal render queue (decode → effect graph → encode), never through capture.
import { execFile, spawn } from 'node:child_process';
import { promises as fsp } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { BrowserWindow, app } from 'electron';
import type { EffectState, JobSpec, JobState } from '../shared/types';
import { APP_VERSION, DEFAULT_EXPORT } from '../shared/types';
import { expertDefaults } from '../shared/uve/expert';
import { DEFAULT_SETTINGS, NEUTRAL_SETTINGS } from '../shared/uve/settings';
import { probe } from './media';
import { enqueue, listJobs } from './render';
import { capabilities } from './tools';

function run(file: string, args: string[]): Promise<{ code: number; out: string; err: string }> {
    return new Promise(resolve => {
        const p = spawn(file, args, { windowsHide: true });
        let out = '', err = '';
        p.stdout.on('data', d => { out += d; });
        p.stderr.on('data', d => { err += d; });
        p.on('exit', code => resolve({ code: code ?? 1, out, err }));
    });
}

async function waitJob(id: string, timeoutMs = 1800000): Promise<JobState> {
    const t0 = Date.now();
    for (;;) {
        const j = listJobs().find(x => x.id === id)!;
        if (['done', 'failed', 'canceled'].includes(j.status)) return j;
        if (Date.now() - t0 > timeoutMs) throw new Error(`job ${id} timed out`);
        await new Promise(r => setTimeout(r, 250));
    }
}

/** Effect presets shared by parity and benchmark runs (stable names = stable comparisons). */
export function harnessEffects(): Record<string, EffectState> {
    const lut = { name: 'harness', group: 'harness', matrix4x5: [1.05, 0.02, 0, 0, -0.01, 0, 1, 0.03, 0, 0, 0.02, 0, 0.97, 0, 0.01, 0, 0, 0, 1, 0], createdAt: 0, updatedAt: 0 };
    const base = (s: Partial<EffectState['settings']>, extra: Partial<EffectState> = {}): EffectState => ({ settings: { ...DEFAULT_SETTINGS, autoOn: false, ...s }, expert: expertDefaults(), customFilters: [], lut: null, ...extra });
    return {
        neutral: { settings: { ...NEUTRAL_SETTINGS }, expert: expertDefaults(), customFilters: [], lut: null },
        simple: base({ darkMoody: false, sl: 0, bl: 0, wl: 0, edge: 0, profile: 'off' }),
        default_look: base({}),
        heavy: { ...base({ dn: -0.6, hdr: 0.5, tealOrange: true, vibrantSat: true, u_contrast: 4, u_sat: 3, u_vib: 4, u_sharp: 5, u_grain: 2, u_hue: 2, cbFilter: 'protanopia', lutProfile: 'harness||harness' }), lut },
        auto_scene: base({ autoOn: true, autoStrength: 0.8 }),
        lut_glsl: {
            ...base({ lutProfile: 'harness||harness' }), lut,
            customFilters: [{ id: 'harness_glsl', label: 'Harness vignette', type: 'webgl', enabled: true, blendMode: 'normal', tags: [], category: '', uniforms: {},
                code: ['// @uniform float AMOUNT 0.6 0.0 1.0 "Amount"', 'void main(){ vec4 c = texture(u_video, v_uv); float d = distance(v_uv, vec2(0.5)); fragColor = vec4(c.rgb * (1.0 - AMOUNT * d * d), 1.0); }'].join('\n') }]
        }
    };
}

async function makeClip(ffmpeg: string, file: string, w: number, h: number, seconds: number, fps = 25) {
    if (await fsp.stat(file).catch(() => null)) return;
    const r = await run(ffmpeg, ['-v', 'error', '-f', 'lavfi', '-i', `testsrc2=size=${w}x${h}:rate=${fps}:duration=${seconds}`, '-f', 'lavfi', '-i', `sine=frequency=440:duration=${seconds}`,
        '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '14', '-preset', 'veryfast', '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-c:a', 'aac', '-shortest', '-y', file]);
    if (r.code !== 0) throw new Error(r.err);
}

async function renderJob(name: string, input: string, out: string, effect: EffectState, ex: Partial<typeof DEFAULT_EXPORT>, enginePref?: 'auto' | 'webgpu' | 'webgl2'): Promise<JobState> {
    const m = await probe(input);
    const spec: JobSpec & { enginePref?: string } = {
        id: `harness_${name}_${Date.now()}`, name, input, output: out, media: m, effect, autoKeys: null,
        exportSettings: { ...DEFAULT_EXPORT, ...ex }, range: { start: 0, end: m.duration }, parityMode: false
    };
    if (enginePref) spec.enginePref = enginePref;
    return waitJob(enqueue(spec as JobSpec).id);
}

// ------------------------------------------------------------------ capability probe
export async function probeCapabilities(outFile: string) {
    const caps = await capabilities(true);
    const win = new BrowserWindow({ show: false, webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true } });
    let webgpu: unknown = null;
    try {
        await win.loadURL(process.env.VITE_DEV_SERVER_URL ? `${process.env.VITE_DEV_SERVER_URL}/index.html?harness=1` : 'uve-app://app/index.html?harness=1');
        await new Promise(r => setTimeout(r, 1500));
        webgpu = await win.webContents.executeJavaScript('window.__uveTest.validateWebGpu()');
    } catch (e) { webgpu = { error: String(e) }; } finally { win.destroy(); }
    const gpuInfo = await app.getGPUInfo('complete').catch(() => null) as { gpuDevice?: unknown; auxAttributes?: Record<string, unknown> } | null;
    const result = {
        app: `UVE Manager ${APP_VERSION}`, probedAt: new Date().toISOString(), host: os.hostname(), platform: process.platform, arch: process.arch,
        os: `${os.type()} ${os.release()}`, cpu: os.cpus()[0]?.model, cores: os.cpus().length, ramGB: +(os.totalmem() / 1073741824).toFixed(1),
        electron: process.versions.electron, chrome: process.versions.chrome,
        gpu: { name: caps.gpuName, vendor: caps.gpuVendor, driver: caps.gpuDriver, devices: gpuInfo?.gpuDevice ?? null },
        ffmpeg: { version: caps.ffmpegVersion, path: caps.ffmpegPath, source: caps.source },
        hardwareEncoders: caps.hwEncoders, hardwareDecoders: caps.hwaccels, softwareEncoders: Object.fromEntries(Object.entries(caps.encoders).filter(([k]) => !/_(nvenc|amf|qsv|videotoolbox|vaapi)$/.test(k))),
        filters: caps.filters, webgpuValidation: webgpu
    };
    await fsp.mkdir(path.dirname(outFile), { recursive: true });
    await fsp.writeFile(outFile, JSON.stringify(result, null, 2));
    console.log(`capabilities written to ${outFile}`);
}

// ------------------------------------------------------------------ parity reference exports
/** Same specs in every version: compare the outputs of two versions with ffmpeg psnr/ssim at matched timestamps. */
export async function parityExport(outDir: string) {
    const caps = await capabilities(true);
    await fsp.mkdir(outDir, { recursive: true });
    const clip = path.join(outDir, 'source_720p.mp4');
    await makeClip(caps.ffmpegPath, clip, 1280, 720, 4);
    const fx = harnessEffects();
    const cases: [string, EffectState, Partial<typeof DEFAULT_EXPORT>][] = [
        ['neutral', fx.neutral, { videoCodec: 'h264', encoder: 'cpu', quality: 4, container: 'mkv' }],
        ['default_look', fx.default_look, { videoCodec: 'h264', encoder: 'cpu', quality: 4, container: 'mkv' }],
        ['heavy_lut', fx.heavy, { videoCodec: 'h264', encoder: 'cpu', quality: 4, container: 'mkv' }],
        ['auto_scene', fx.auto_scene, { videoCodec: 'h264', encoder: 'cpu', quality: 4, container: 'mkv' }],
        ['lut_glsl', fx.lut_glsl, { videoCodec: 'h264', encoder: 'cpu', quality: 4, container: 'mkv' }],
        ['hevc_hw_10bit', fx.default_look, { videoCodec: 'hevc', encoder: 'auto', quality: 18, tenBit: true, container: 'mp4' }]
    ];
    const log: Record<string, unknown> = {};
    for (const [name, effect, ex] of cases) {
        const out = path.join(outDir, `${name}.${ex.container}`);
        await fsp.rm(out, { force: true });
        const j = await renderJob(name, clip, out, effect, ex);
        log[name] = { status: j.status, encoder: j.encoder, engine: (j as JobState & { engine?: string }).engine ?? 'webgl2 (v1.0.0)', error: j.error, fps: j.fps };
        console.log(`parity ${name}: ${j.status} ${j.encoder} ${(j as JobState & { engine?: string }).engine ?? ''}`);
    }
    await fsp.writeFile(path.join(outDir, 'parity-run.json'), JSON.stringify({ app: APP_VERSION, at: new Date().toISOString(), cases: log }, null, 2));
}

// ------------------------------------------------------------------ benchmark
function cpuTimes() {
    return os.cpus().reduce((a, c) => { const t = c.times; a.idle += t.idle; a.total += t.user + t.nice + t.sys + t.irq + t.idle; return a; }, { idle: 0, total: 0 });
}

function vramUsedMB(): Promise<number | null> {
    return new Promise(resolve => execFile('nvidia-smi', ['--query-gpu=memory.used', '--format=csv,noheader,nounits'], { timeout: 3000, windowsHide: true },
        (e, out) => resolve(e ? null : Number(String(out).trim().split(/\r?\n/)[0]) || null)));
}

export async function benchmark(outFile: string) {
    const caps = await capabilities(true);
    const dir = path.join(os.tmpdir(), 'uve-bench');
    await fsp.mkdir(dir, { recursive: true });
    const clips = { '1080p': path.join(dir, 'bench_1080p.mp4'), '4K': path.join(dir, 'bench_2160p.mp4') };
    await makeClip(caps.ffmpegPath, clips['1080p'], 1920, 1080, 6);
    await makeClip(caps.ffmpegPath, clips['4K'], 3840, 2160, 4);
    const fx = harnessEffects();
    const hwHevc = !!caps.hwEncoders?.hevc_nvenc || Object.entries(caps.hwEncoders || {}).some(([k, v]) => v && k.startsWith('hevc_'));
    type Case = { res: '1080p' | '4K'; workload: 'simple' | 'heavy' | 'lut_glsl'; engine: 'webgl2' | 'webgpu'; encoder: 'auto' | 'cpu' };
    const cases: Case[] = [];
    for (const res of ['1080p', '4K'] as const) for (const workload of ['simple', 'heavy', 'lut_glsl'] as const) for (const engine of ['webgl2', 'webgpu'] as const) {
        if (workload === 'lut_glsl' && engine === 'webgpu') continue; // custom GLSL always runs on WebGL2
        cases.push({ res, workload, engine, encoder: hwHevc ? 'auto' : 'cpu' });
    }
    // separate filter speed from encoder speed: same heavy 1080p job with the software encoder
    cases.push({ res: '1080p', workload: 'heavy', engine: 'webgl2', encoder: 'cpu' }, { res: '1080p', workload: 'heavy', engine: 'webgpu', encoder: 'cpu' });
    const results: Record<string, unknown>[] = [];
    const only = process.env.UVE_BENCH_ONLY; // e.g. "1080p:heavy:webgpu" for a single profiling run
    for (const c of cases.filter(x => !only || `${x.res}:${x.workload}:${x.engine}`.startsWith(only))) {
        const out = path.join(dir, `out_${c.res}_${c.workload}_${c.engine}_${c.encoder}.mp4`);
        await fsp.rm(out, { force: true });
        let peakRam = 0, peakVram = 0;
        const vram0 = await vramUsedMB();
        const c0 = cpuTimes();
        const sampler = setInterval(() => {
            const ram = app.getAppMetrics().reduce((s, m) => s + (m.memory?.workingSetSize || 0), 0) / 1024;
            peakRam = Math.max(peakRam, ram);
            void vramUsedMB().then(v => { if (v !== null) peakVram = Math.max(peakVram, v); });
        }, 400);
        const t0 = Date.now();
        const j = await renderJob(`bench_${c.res}_${c.workload}_${c.engine}`, clips[c.res], out, fx[c.workload], { videoCodec: 'hevc', encoder: c.encoder, quality: 20, container: 'mp4' }, c.engine);
        const wall = (Date.now() - t0) / 1000;
        clearInterval(sampler);
        const c1 = cpuTimes();
        const cpu = 100 * (1 - (c1.idle - c0.idle) / Math.max(1, c1.total - c0.total));
        const st = j.stats;
        const r = {
            resolution: c.res, workload: c.workload, engineRequested: c.engine, engineUsed: j.engine, encoder: j.encoder, status: j.status, error: j.error,
            frames: st?.frames, wallSeconds: +wall.toFixed(2), renderSeconds: st ? +(st.totalMs / 1000).toFixed(2) : null, fps: st ? +(st.frames / (st.totalMs / 1000)).toFixed(1) : null,
            perFrameMs: st ? { decodeWait: +(st.decodeWait / st.frames).toFixed(2), upload: +(st.upload / st.frames).toFixed(2), gpuFilter: +(st.gpu / st.frames).toFixed(2), readback: +(st.readback / st.frames).toFixed(2), encodeWait: +(st.encodeWait / st.frames).toFixed(2) } : null,
            systemCpuPercent: +cpu.toFixed(1), peakAppRamMB: +peakRam.toFixed(0), peakVramMB: peakVram && vram0 !== null ? peakVram : null, vramBaselineMB: vram0
        };
        results.push(r);
        j.log.filter(l => /^(Decoder|Encoder|Decoder delivery|Stage times)/.test(l)).forEach(l => console.log(`  ${l}`));
        console.log(`bench ${c.res} ${c.workload} ${c.engine}/${c.encoder}: ${r.status} ${r.fps} fps (${r.engineUsed}, ${r.encoder})`);
        await fsp.rm(out, { force: true });
    }
    const report = {
        app: APP_VERSION, at: new Date().toISOString(), platform: process.platform, cpu: os.cpus()[0]?.model, cores: os.cpus().length, ramGB: +(os.totalmem() / 1073741824).toFixed(1),
        gpu: caps.gpuName, gpuDriver: caps.gpuDriver, ffmpeg: caps.ffmpegVersion, hwEncoders: caps.hwEncoders, results,
        notes: [
            'engineRequested webgl2 = the unchanged v1.0.0 filter engine inside v1.0.1; webgpu = the new WGSL engine.',
            'Encoder "auto" = verified hardware HEVC encoder when available; "cpu" = libx265 (separates filter speed from encoder speed).',
            'systemCpuPercent is whole-system CPU usage during the job (includes the FFmpeg decoder/encoder processes).',
            'peakVramMB is total GPU memory in use (nvidia-smi), not per-process; null where unavailable.'
        ]
    };
    await fsp.mkdir(path.dirname(outFile), { recursive: true });
    await fsp.writeFile(outFile, JSON.stringify(report, null, 2));
    console.log(`benchmark written to ${outFile}`);
}

// ------------------------------------------------------------------ settings/data migration check
/** Runs against a COPY of an older data folder (--migration-check=<dir>): additive migration + backup. */
export async function migrationCheck(): Promise<boolean> {
    const { getSettings, saveSettings, settingsFile, userDir } = await import('./store');
    const { lutLibrary } = await import('./luts');
    const before = await fsp.readFile(settingsFile(), 'utf8');
    const old = JSON.parse(before) as { app: string; prefs: Record<string, unknown>; window: unknown; editor: unknown };
    let ok = true;
    const check = (c: boolean, m: string) => { console.log(`${c ? 'PASS' : 'FAIL'} ${m}`); ok &&= c; };
    const st = await getSettings();
    const backup = path.join(userDir(), `settings.backup-v${old.app}.json`);
    const b = await fsp.readFile(backup, 'utf8').catch(() => null);
    check(old.app !== APP_VERSION, `source data is from an older version (${old.app})`);
    check(b === before, `untouched backup written (${path.basename(backup)})`);
    const prefs = st.prefs as unknown as Record<string, unknown>;
    for (const [k, v] of Object.entries(old.prefs)) {
        const same = JSON.stringify(k === 'defaultExport' || k === 'shortcuts' ? Object.fromEntries(Object.keys(v as object).map(x => [x, (prefs[k] as Record<string, unknown>)[x]])) : prefs[k]) === JSON.stringify(v);
        check(same, `pref ${k} kept`);
    }
    check(prefs.gpuEngine === 'auto' && prefs.hwDecode === 'auto', 'new prefs gpuEngine/hwDecode default to auto');
    check((prefs.defaultExport as Record<string, unknown>).fps === '', 'new export setting fps defaults to source');
    const ow = old.window as Record<string, unknown>, nw = st.window as unknown as Record<string, unknown>;
    check(Object.keys(ow).every(k => nw[k] === ow[k]), `window position/size kept (${JSON.stringify(ow)})`);
    check(JSON.stringify(st.editor) === JSON.stringify(old.editor), 'editor session kept');
    await saveSettings();
    const saved = JSON.parse(await fsp.readFile(settingsFile(), 'utf8')) as { app: string };
    check(saved.app === APP_VERSION, `settings re-saved as ${saved.app}`);
    check(await fsp.readFile(backup, 'utf8') === before, 'backup unchanged after save (not overwritten)');
    const luts = await lutLibrary();
    console.log(`INFO LUT library: ${luts.profiles.length} profiles in ${luts.groups.length} groups`);
    check(luts.profiles.length > 0, 'LUT library of the older version loads');
    return ok;
}
