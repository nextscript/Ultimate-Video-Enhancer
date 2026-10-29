// Portable tool management: FFmpeg / FFprobe live in <app>/tools/ffmpeg as a portable build.
// Installs/updates from the BtbN FFmpeg-Builds GitHub releases and detects encoder capabilities.
import { execFile, spawn } from 'node:child_process';
import { promises as fsp } from 'node:fs';
import path from 'node:path';
import { app } from 'electron';
import { type Capabilities, FFMPEG_BUILD, type ToolsStatus } from '../shared/types';
import { type HwCodec, allHwEncoderNames, encoderTestArgs, familyOrder, hwEncoderArgs, hwEncoderName, vendorOf } from '../shared/hw';
import { getPrefs, toolsDir } from './store';
import { download, fetchJson, fileExists, readJson, safeUnzip, writeJson } from './util';
import { IS_WIN, exe, whichExe } from './platform';

interface ToolsVersion { build: string; version: string; publishedAt: string; installedAt: number; source: string }

export async function ffmpegToolsDir() { return path.join(await toolsDir(), 'ffmpeg'); }

function run(file: string, args: string[], timeoutMs = 15000): Promise<{ code: number; stdout: string; stderr: string }> {
    return new Promise(resolve => {
        execFile(file, args, { timeout: timeoutMs, windowsHide: true, maxBuffer: 32 * 1024 * 1024 }, (err, stdout, stderr) => {
            const code = err ? Number((err as unknown as { code?: unknown }).code) || 1 : 0;
            resolve({ code: typeof code === 'number' ? code : 1, stdout: String(stdout), stderr: String(stderr) });
        });
    });
}

export async function toolsStatus(): Promise<ToolsStatus> {
    const dir = await ffmpegToolsDir();
    const ffmpeg = path.join(dir, exe('ffmpeg')), ffprobe = path.join(dir, exe('ffprobe'));
    const installed = (await fileExists(ffmpeg)) && (await fileExists(ffprobe));
    const v = await readJson<ToolsVersion | null>(path.join(dir, 'version.json'), null);
    return { dir, ffmpeg, ffprobe, installed, version: v?.version || '', build: v?.build || '', installedAt: v?.installedAt };
}

export interface LatestFfmpeg { build: string; publishedAt: string; url: string; size: number }

export async function latestFfmpegRelease(): Promise<LatestFfmpeg> {
    type Rel = { name: string; tag_name: string; published_at: string; assets: { name: string; browser_download_url: string; size: number; updated_at: string }[] };
    const rel = await fetchJson<Rel>(FFMPEG_BUILD.api);
    const asset = rel.assets.find(a => FFMPEG_BUILD.assetPattern.test(a.name));
    if (!asset) throw new Error('No Windows x64 GPL build found in the latest FFmpeg-Builds release.');
    return { build: asset.updated_at || rel.published_at, publishedAt: rel.published_at, url: asset.browser_download_url, size: asset.size };
}

let installing: Promise<ToolsStatus> | null = null;

/** Downloads the portable FFmpeg build into tools/ffmpeg (atomic replace of the exes). */
export function installFfmpeg(onProgress: (msg: string, pct: number) => void, release?: LatestFfmpeg): Promise<ToolsStatus> {
    if (installing) return installing;
    installing = (async () => {
        try {
            // the portable FFmpeg download is a Windows build; Linux/macOS use the system FFmpeg or a custom folder
            if (!IS_WIN) throw new Error(process.platform === 'darwin'
                ? 'Portable FFmpeg download is available for Windows only. Install FFmpeg (e.g. "brew install ffmpeg") or choose a folder in Settings → Portable tools.'
                : 'Portable FFmpeg download is available for Windows only. Install FFmpeg with your package manager (e.g. "sudo apt install ffmpeg") or choose a folder in Settings → Portable tools.');
            onProgress('Looking up the latest portable FFmpeg build…', 0);
            let rel = release;
            try { rel = rel ?? await latestFfmpegRelease(); } catch { rel = { build: 'latest', publishedAt: '', url: FFMPEG_BUILD.directUrl, size: 0 }; }
            const zip = await download(rel.url, {
                maxBytes: 600 * 1024 * 1024,
                onProgress: (r, t) => onProgress(`Downloading FFmpeg… ${(r / 1048576).toFixed(0)} MB`, t ? r / t : 0)
            });
            onProgress('Extracting FFmpeg…', 1);
            const wanted = new Set(['ffmpeg.exe', 'ffprobe.exe', 'LICENSE.txt']);
            const entries = safeUnzip(zip, {
                maxEntries: 5000, maxEntryBytes: 400 * 1024 * 1024, maxTotalBytes: 1500 * 1024 * 1024,
                filter: n => wanted.has(path.posix.basename(n)) && (/\/bin\//.test(n) || /LICENSE/.test(n))
            });
            const exe = (n: string) => entries.find(e => path.posix.basename(e.name) === n);
            if (!exe('ffmpeg.exe') || !exe('ffprobe.exe')) throw new Error('The downloaded archive does not contain ffmpeg.exe / ffprobe.exe.');
            const dir = await ffmpegToolsDir();
            await fsp.mkdir(dir, { recursive: true });
            for (const n of wanted) {
                const e = exe(n);
                if (!e) continue;
                const target = path.join(dir, n);
                const tmp = target + '.new';
                await fsp.writeFile(tmp, e.data);
                await fsp.rm(target, { force: true });
                await fsp.rename(tmp, target);
            }
            const ver = await run(path.join(dir, 'ffmpeg.exe'), ['-hide_banner', '-version']);
            const version = (ver.stdout.match(/ffmpeg version (\S+)/) || [])[1] || 'unknown';
            await writeJson(path.join(dir, 'version.json'), { build: rel.build, version, publishedAt: rel.publishedAt, installedAt: Date.now(), source: rel.url } satisfies ToolsVersion);
            capsCache = null;
            onProgress(`FFmpeg ${version} installed.`, 1);
            return await toolsStatus();
        } finally {
            installing = null;
        }
    })();
    return installing;
}

// ------------------------------------------------------------------ locating & capabilities
export interface FfPaths { ffmpeg: string; ffprobe: string; source: Capabilities['source'] }

const which = (name: string) => whichExe(name.replace(/\.exe$/i, ''));

export async function resolveFf(): Promise<FfPaths | null> {
    const prefs = await getPrefs();
    if (prefs.ffmpegDir) {
        const f = path.join(prefs.ffmpegDir, exe('ffmpeg')), p = path.join(prefs.ffmpegDir, exe('ffprobe'));
        if (await fileExists(f) && await fileExists(p)) return { ffmpeg: f, ffprobe: p, source: 'custom' };
    }
    const st = await toolsStatus();
    if (st.installed) return { ffmpeg: st.ffmpeg, ffprobe: st.ffprobe, source: 'portable' };
    if (app.isPackaged) {
        // tools shipped with the build (extraFiles) – relevant for the self-extracting portable exe
        const res = path.join(path.dirname(app.getPath('exe')), 'tools', 'ffmpeg');
        const f = path.join(res, exe('ffmpeg')), p = path.join(res, exe('ffprobe'));
        if (await fileExists(f) && await fileExists(p)) return { ffmpeg: f, ffprobe: p, source: 'portable' };
    }
    const f = await which('ffmpeg.exe'), p = await which('ffprobe.exe');
    if (f && p) return { ffmpeg: f, ffprobe: p, source: 'path' };
    // v1.0.2: apps started from Finder / a desktop launcher do not get the shell PATH (Homebrew, MacPorts, Linuxbrew)
    if (!IS_WIN) for (const dir of ['/opt/homebrew/bin', '/usr/local/bin', '/opt/local/bin', '/home/linuxbrew/.linuxbrew/bin', '/snap/bin']) {
        const f2 = path.join(dir, 'ffmpeg'), p2 = path.join(dir, 'ffprobe');
        if (await fileExists(f2) && await fileExists(p2)) return { ffmpeg: f2, ffprobe: p2, source: 'path' };
    }
    return null;
}

let capsCache: Capabilities | null = null;

export function invalidateCaps() { capsCache = null; }

/** Runs a short scratch encode; hardware is detected by actually encoding, never assumed. */
export function runEncoderTest(ffmpeg: string, args: string[], timeoutMs = 20000): Promise<{ ok: boolean; err: string }> {
    return new Promise(resolve => {
        const p = spawn(ffmpeg, args, { windowsHide: true });
        let err = '';
        p.stderr.on('data', d => { err += d; });
        const t = setTimeout(() => { p.kill(); resolve({ ok: false, err: 'timeout' }); }, timeoutMs);
        p.on('error', e => { clearTimeout(t); resolve({ ok: false, err: e.message }); });
        p.on('exit', code => { clearTimeout(t); resolve({ ok: code === 0, err: err.trim().split(/\r?\n/).slice(-2).join(' ') }); });
    });
}

async function gpuInfo(): Promise<{ name: string; driver: string }> {
    const r = await run('nvidia-smi', ['--query-gpu=name,driver_version', '--format=csv,noheader'], 8000);
    if (r.code === 0 && r.stdout.trim()) {
        const [name, driver] = r.stdout.trim().split(/\r?\n/)[0].split(',').map(s => s.trim());
        return { name, driver };
    }
    try {
        const info = await app.getGPUInfo('basic') as { gpuDevice?: { active?: boolean; deviceString?: string; driverVersion?: string }[] };
        const dev = info.gpuDevice?.find(d => d.active) || info.gpuDevice?.[0];
        if (dev?.deviceString) return { name: dev.deviceString, driver: dev.driverVersion || '' };
    } catch { /* ignore */ }
    return { name: 'Unknown GPU', driver: '' };
}

export async function capabilities(force = false): Promise<Capabilities> {
    if (capsCache && !force) return capsCache;
    const gpu = await gpuInfo();
    const ff = await resolveFf();
    if (!ff) {
        capsCache = {
            ok: false, error: 'FFmpeg was not found. Install the portable tools in Settings → Tools.', ffmpegPath: '', ffprobePath: '',
            ffmpegVersion: '', source: 'none', encoders: {}, nvencWorking: {}, hwEncoders: {}, platform: process.platform, gpuVendor: vendorOf(gpu.name), hwaccels: [], filters: {}, gpuName: gpu.name, gpuDriver: gpu.driver
        };
        return capsCache;
    }
    const [ver, enc, hw, flt] = await Promise.all([
        run(ff.ffmpeg, ['-hide_banner', '-version']),
        run(ff.ffmpeg, ['-hide_banner', '-encoders']),
        run(ff.ffmpeg, ['-hide_banner', '-hwaccels']),
        run(ff.ffmpeg, ['-hide_banner', '-filters'])
    ]);
    const encoders: Record<string, boolean> = {};
    for (const e of ['libx264', 'libx265', 'libsvtav1', 'libaom-av1', 'libvpx-vp9', 'prores_ks', ...allHwEncoderNames(), 'aac', 'libopus', 'libfdk_aac']) {
        encoders[e] = new RegExp(`\\s${e.replace(/[-]/g, '\\-')}\\s`).test(enc.stdout);
    }
    // verify every hardware encoder present in the build for this platform with a scratch encode (in parallel)
    const vendor = vendorOf(gpu.name);
    const fams = familyOrder(process.platform, vendor);
    const hwEncoders: Record<string, boolean> = {};
    await Promise.all((['h264', 'hevc', 'av1'] as HwCodec[]).flatMap(c => fams.map(async f => {
        const name = hwEncoderName(c, f);
        if (!encoders[name]) return;
        const choice = hwEncoderArgs(c, f, 23, 'fast', false);
        hwEncoders[name] = !!choice && (await runEncoderTest(ff.ffmpeg, encoderTestArgs(choice))).ok;
    })));
    const nvencWorking: Record<string, boolean> = {};
    for (const e of ['h264_nvenc', 'hevc_nvenc', 'av1_nvenc']) nvencWorking[e] = !!hwEncoders[e];
    const filters: Record<string, boolean> = {};
    for (const f of ['zscale', 'tonemap', 'scale', 'libplacebo']) filters[f] = new RegExp(`\\s${f}\\s`).test(flt.stdout);
    capsCache = {
        ok: true, ffmpegPath: ff.ffmpeg, ffprobePath: ff.ffprobe, source: ff.source,
        ffmpegVersion: (ver.stdout.match(/ffmpeg version (\S+)/) || [])[1] || 'unknown',
        encoders, nvencWorking, hwEncoders, platform: process.platform, gpuVendor: vendor,
        hwaccels: hw.stdout.split(/\r?\n/).slice(1).map(s => s.trim()).filter(Boolean),
        filters, gpuName: gpu.name, gpuDriver: gpu.driver
    };
    return capsCache;
}
