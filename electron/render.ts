// Render queue: every job runs in its own hidden export-worker window which decodes the ORIGINAL
// file with FFmpeg, applies the UVE effect graph frame by frame on the GPU and pipes the frames
// into the encoder. No screen/player capture is involved anywhere.
import { spawn } from 'node:child_process';
import { promises as fsp } from 'node:fs';
import path from 'node:path';
import { BrowserWindow, ipcMain } from 'electron';
import type { Capabilities, ExportSettings, JobSpec, JobState, MediaInfo } from '../shared/types';
import { runAutoSceneMatch, samplesFromAnalysis } from '../shared/uve/auto';
import { analyze } from './analysis';
import { decodeFilter } from './media';
import { capabilities } from './tools';
import { fileExists } from './util';
import { downloadModel, findModel, rveStatus, runUpscale } from './rve';

export interface WorkerPlan {
    jobId: string;
    ffmpeg: string;
    decodeArgs: string[];
    encodeArgs: string[];
    width: number;
    height: number;
    in16: boolean;
    out16: boolean;
    fps: number;
    start: number;
    totalFrames: number;
    effect: JobSpec['effect'];
    autoKeys: JobSpec['autoKeys'];
    parityMode: boolean;
}

const jobs: JobState[] = [];
const specs = new Map<string, JobSpec>();
interface ActiveJob {
    id: string;
    win: BrowserWindow;
    tmp: string;
    canceled: boolean;
    /** AI upscaling: lossless intermediate written by the UVE worker, then upscaled by RVE */
    stage1?: string;
    upscale?: { modelPath: string; customEncoder: string; totalFrames: number; ffmpeg: string; stage2: string; muxArgs?: (videoFile: string, outFile: string) => string[] };
    kill?: () => void;
}
let active: ActiveJob | null = null;
let broadcast: (jobs: JobState[]) => void = () => undefined;
let workerUrl: () => string = () => '';
let workerPreload = '';

export function initRenderQueue(opts: { broadcast: (j: JobState[]) => void; workerUrl: () => string; workerPreload: string }) {
    broadcast = opts.broadcast;
    workerUrl = opts.workerUrl;
    workerPreload = opts.workerPreload;

    ipcMain.on('worker:progress', (e, p: { jobId: string; frames: number; fps: number; outSize: number }) => {
        const j = jobs.find(x => x.id === p.jobId);
        if (!j || !active || active.win.webContents.id !== e.sender.id) return;
        j.frames = p.frames;
        j.progress = j.totalFrames ? Math.min(1, p.frames / j.totalFrames) : 0;
        j.fps = p.fps;
        j.elapsed = (Date.now() - startedAt) / 1000;
        j.eta = p.fps > 0 ? Math.max(0, (j.totalFrames - p.frames) / p.fps) : 0;
        j.outSize = p.outSize;
        emit();
    });
    ipcMain.on('worker:log', (e, p: { jobId: string; line: string }) => {
        const j = jobs.find(x => x.id === p.jobId);
        if (!j || !active || active.win.webContents.id !== e.sender.id) return;
        j.log.push(String(p.line).slice(0, 2000));
        if (j.log.length > 500) j.log.splice(0, j.log.length - 500);
    });
    ipcMain.on('worker:done', (e, p: { jobId: string; ok: boolean; error?: string }) => {
        if (!active || active.win.webContents.id !== e.sender.id || active.id !== p.jobId) return;
        if (p.ok && active.stage1 && active.upscale && !active.canceled) void runStage2();
        else void finishJob(p.ok, p.error);
    });
}

let startedAt = 0;
const emit = () => broadcast(jobs.map(j => ({ ...j, log: j.log.slice(-200) })));
export const listJobs = () => jobs.map(j => ({ ...j }));

const COPY_OK: Record<string, string[]> = {
    mp4: ['aac', 'mp3', 'ac3', 'eac3', 'alac', 'opus', 'flac'],
    mov: ['aac', 'mp3', 'ac3', 'eac3', 'alac', 'pcm_s16le', 'pcm_s24le', 'pcm_s32le'],
    mkv: ['*'],
    webm: ['opus', 'vorbis']
};

export function outputExtension(s: ExportSettings) { return s.videoCodec === 'prores' ? 'mov' : s.container; }

/** Validates codec/container combinations; returns human readable warnings. */
export function validateExport(s: ExportSettings, caps: Capabilities): string[] {
    const w: string[] = [];
    if (s.container === 'webm' && !['vp9', 'av1'].includes(s.videoCodec)) w.push('WebM supports only VP9 or AV1 video.');
    if (s.videoCodec === 'prores' && s.container !== 'mov') w.push('ProRes is written into a .mov container.');
    if (s.videoCodec === 'h264' && s.tenBit && (s.encoder !== 'cpu' && caps.nvencWorking.h264_nvenc)) w.push('NVENC H.264 has no 10-bit mode; 8-bit is used (choose HEVC/AV1 for 10-bit).');
    if (s.videoCodec === 'av1' && !caps.nvencWorking.av1_nvenc && !caps.encoders.libsvtav1 && !caps.encoders['libaom-av1']) w.push('No AV1 encoder available in this FFmpeg build.');
    return w;
}

function pickVideoEncoder(s: ExportSettings, caps: Capabilities): { name: string; args: string[]; pix: string; tenBit: boolean } {
    const gpuOk = (e: string) => s.encoder !== 'cpu' && caps.nvencWorking[e];
    const cpuPreset = { fast: 'faster', medium: 'medium', slow: 'slow' }[s.preset];
    const nvPreset = { fast: 'p3', medium: 'p5', slow: 'p7' }[s.preset];
    const q = String(Math.round(s.quality));
    const forceGpu = s.encoder === 'gpu';
    switch (s.videoCodec) {
        case 'h264':
            if (gpuOk('h264_nvenc')) return { name: 'h264_nvenc', args: ['-c:v', 'h264_nvenc', '-preset', nvPreset, '-tune', 'hq', '-rc', 'vbr', '-cq', q, '-b:v', '0', '-profile:v', 'high'], pix: 'yuv420p', tenBit: false };
            if (forceGpu) throw new Error('NVENC H.264 is not available on this system.');
            return { name: 'libx264', args: ['-c:v', 'libx264', '-preset', cpuPreset, '-crf', q, ...(s.tenBit ? ['-profile:v', 'high10'] : ['-profile:v', 'high'])], pix: s.tenBit ? 'yuv420p10le' : 'yuv420p', tenBit: s.tenBit };
        case 'hevc':
            if (gpuOk('hevc_nvenc')) return { name: 'hevc_nvenc', args: ['-c:v', 'hevc_nvenc', '-preset', nvPreset, '-tune', 'hq', '-rc', 'vbr', '-cq', q, '-b:v', '0', '-profile:v', s.tenBit ? 'main10' : 'main', '-tag:v', 'hvc1'], pix: s.tenBit ? 'p010le' : 'yuv420p', tenBit: s.tenBit };
            if (forceGpu) throw new Error('NVENC HEVC is not available on this system.');
            return { name: 'libx265', args: ['-c:v', 'libx265', '-preset', cpuPreset, '-crf', q, '-tag:v', 'hvc1', '-x265-params', 'log-level=error'], pix: s.tenBit ? 'yuv420p10le' : 'yuv420p', tenBit: s.tenBit };
        case 'av1':
            if (gpuOk('av1_nvenc')) return { name: 'av1_nvenc', args: ['-c:v', 'av1_nvenc', '-preset', nvPreset, '-tune', 'hq', '-rc', 'vbr', '-cq', q, '-b:v', '0'], pix: s.tenBit ? 'p010le' : 'yuv420p', tenBit: s.tenBit };
            if (forceGpu) throw new Error('NVENC AV1 is not available on this GPU.');
            if (caps.encoders.libsvtav1) return { name: 'libsvtav1', args: ['-c:v', 'libsvtav1', '-preset', { fast: '10', medium: '8', slow: '5' }[s.preset], '-crf', q], pix: s.tenBit ? 'yuv420p10le' : 'yuv420p', tenBit: s.tenBit };
            return { name: 'libaom-av1', args: ['-c:v', 'libaom-av1', '-cpu-used', '6', '-row-mt', '1', '-crf', q, '-b:v', '0'], pix: s.tenBit ? 'yuv420p10le' : 'yuv420p', tenBit: s.tenBit };
        case 'vp9':
            return { name: 'libvpx-vp9', args: ['-c:v', 'libvpx-vp9', '-crf', q, '-b:v', '0', '-row-mt', '1', '-deadline', s.preset === 'slow' ? 'good' : 'realtime', '-cpu-used', s.preset === 'fast' ? '6' : '3'], pix: s.tenBit ? 'yuv420p10le' : 'yuv420p', tenBit: s.tenBit };
        case 'prores':
            return { name: 'prores_ks', args: ['-c:v', 'prores_ks', '-profile:v', '3', '-vendor', 'apl0'], pix: 'yuv422p10le', tenBit: true };
    }
}

function targetSize(s: ExportSettings, w: number, h: number): { w: number; h: number } {
    const even = (n: number) => Math.max(2, Math.round(n / 2) * 2);
    if (s.resolution === 'source') return { w: even(w), h: even(h) };
    if (s.resolution === 'x2') return { w: even(w * 2), h: even(h * 2) };
    const th = Number(s.resolution);
    return { w: even(w * th / h), h: even(th) };
}

/** Collision-safe output path that never equals the input. */
export async function uniqueOutput(dir: string, base: string, ext: string, input: string): Promise<string> {
    const clean = base.replace(/[\\/:*?"<>|\u0000-\u001f]+/g, '_').trim() || 'output';
    for (let i = 1; i < 1000; i++) {
        const p = path.join(dir, `${clean}${i > 1 ? `_${i}` : ''}.${ext}`);
        if (path.resolve(p).toLowerCase() === path.resolve(input).toLowerCase()) continue;
        if (!(await fileExists(p)) && !jobs.some(j => j.output === p && !['done', 'failed', 'canceled'].includes(j.status))) return p;
    }
    throw new Error('Could not find a free output file name.');
}

export function fileNameFromTemplate(template: string, media: MediaInfo, profileName: string) {
    const d = new Date();
    const pad = (n: number) => String(n).padStart(2, '0');
    return template.replace(/\{name\}/g, media.name.replace(/\.[^.]+$/, ''))
        .replace(/\{profile\}/g, profileName)
        .replace(/\{date\}/g, `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}`)
        .replace(/\{time\}/g, `${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`);
}

export async function buildPlan(spec: JobSpec, tmpOut: string, upscale?: { scale: number }): Promise<{ plan: WorkerPlan; encoder: string; warnings: string[]; customEncoder?: string; muxArgs?: (videoFile: string, outFile: string) => string[] }> {
    const caps = await capabilities();
    if (!caps.ok) throw new Error(caps.error || 'FFmpeg not available');
    const m = spec.media, v = m.video;
    if (!v) throw new Error('The source has no video stream.');
    if (m.encrypted) throw new Error('Encrypted/DRM-protected sources cannot be processed.');
    const s = spec.exportSettings;
    const warnings = validateExport(s, caps);
    const enc = pickVideoEncoder(s, caps);
    const rot = v.rotation === 90 || v.rotation === 270;
    const pw = rot ? v.height : v.width, ph = rot ? v.width : v.height;
    const in16 = v.bitDepth > 8 || enc.tenBit;
    // with AI upscaling the worker writes an 8-bit RGB lossless intermediate (RVE reads rgb24)
    const out16 = enc.tenBit && !upscale;
    const fpsNum = v.fpsNum || 25, fpsDen = v.fpsDen || 1;
    const fps = fpsNum / fpsDen;
    const start = Math.max(0, spec.range.start), end = Math.min(m.duration || spec.range.end, spec.range.end);
    const totalFrames = Math.max(1, Math.round((end - start) * fps));

    const vf = await decodeFilter(v, pw, ph, in16 ? 'rgba64le' : 'rgba');
    const decodeArgs = ['-hide_banner', '-loglevel', 'error', '-nostdin'];
    if (caps.hwaccels.some(h => h === 'cuda' || h === 'd3d11va')) decodeArgs.push('-hwaccel', 'auto');
    if (start > 0) decodeArgs.push('-ss', start.toFixed(6));
    decodeArgs.push('-i', m.path, '-t', (end - start).toFixed(6), '-map', `0:${v.index}`, '-vf', vf, '-an', '-sn',
        '-fps_mode', 'cfr', '-r', `${fpsNum}/${fpsDen}`, '-frames:v', String(totalFrames), '-f', 'rawvideo', '-pix_fmt', in16 ? 'rgba64le' : 'rgba', 'pipe:1');

    const ext = outputExtension(s);
    const audioIdx = s.audioTrack >= 0 ? m.audio.findIndex(a => a.index === s.audioTrack) : 0;
    const audio = s.audio !== 'none' ? m.audio[audioIdx >= 0 ? audioIdx : 0] : undefined;
    // subtitle handling for the final container
    let subCodec: string | null = null;
    if (s.subtitles === 'copy' && m.subtitles.length) {
        if (ext === 'mkv') subCodec = 'copy';
        else if ((ext === 'mp4' || ext === 'mov') && m.subtitles.every(x => ['subrip', 'ass', 'ssa', 'mov_text', 'webvtt', 'text'].includes(x.codec))) subCodec = 'mov_text';
        else if (ext === 'webm' && m.subtitles.every(x => ['subrip', 'webvtt'].includes(x.codec))) subCodec = 'webvtt';
        else warnings.push('Subtitles are not compatible with the chosen container and were skipped.');
    }
    const audioArgs = (): string[] => {
        if (!audio) return ['-an'];
        const allowed = COPY_OK[ext] || [];
        const canCopy = s.audio === 'copy' && (allowed.includes('*') || allowed.includes(audio.codec));
        if (s.audio === 'copy' && !canCopy) warnings.push(`Audio "${audio.codec}" cannot be stream-copied into .${ext}; it is re-encoded.`);
        if (canCopy) return ['-c:a', 'copy'];
        if (s.audio === 'opus' || ext === 'webm') return ['-c:a', 'libopus', '-b:a', `${s.audioBitrate}k`];
        return ['-c:a', 'aac', '-b:a', `${s.audioBitrate}k`];
    };
    // final size: after the AI model scale (if any), then the resolution choice
    const baseW = upscale ? pw * upscale.scale : pw, baseH = upscale ? ph * upscale.scale : ph;
    const size = targetSize(upscale && s.resolution === 'x2' ? { ...s, resolution: 'source' } : s, baseW, baseH);
    const scaleParts = [];
    if (size.w !== baseW || size.h !== baseH) scaleParts.push(`scale=${size.w}:${size.h}:flags=lanczos`);
    scaleParts.push(`scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd+full_chroma_int`, `format=${enc.pix}`);
    const videoOut = ['-vf', scaleParts.join(','), ...enc.args, '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-color_range', 'tv'];
    const tailOut: string[] = [];
    if (ext === 'mp4' || ext === 'mov') tailOut.push('-movflags', '+faststart');
    if (v.sar && v.sar !== '1:1' && v.sar !== '0:1' && !rot) tailOut.push('-aspect', `${(v.displayWidth / v.displayHeight).toFixed(6)}`);
    const muxer = { mp4: 'mp4', mkv: 'matroska', mov: 'mov', webm: 'webm' }[ext as 'mp4'];

    const encodeArgs = ['-hide_banner', '-loglevel', 'warning', '-nostdin', '-y',
        '-f', 'rawvideo', '-pix_fmt', out16 ? 'rgba64le' : 'rgba', '-s', `${pw}x${ph}`, '-r', `${fpsNum}/${fpsDen}`, '-i', 'pipe:0'];
    const needSecond = !!audio || !!subCodec;
    if (needSecond && !upscale) {
        if (start > 0) encodeArgs.push('-ss', start.toFixed(6));
        encodeArgs.push('-t', (end - start).toFixed(6), '-i', m.path);
    }
    encodeArgs.push('-map', '0:v:0');
    let customEncoder: string | undefined;
    let muxArgs: ((videoFile: string, outFile: string) => string[]) | undefined;
    if (upscale) {
        // stage 1: video-only lossless RGB intermediate (FFV1). Audio is NOT put into it: RVE's reader forces CFR
        // and would duplicate a frame to cover AAC priming offsets; audio/subtitles are muxed in stage 3 instead.
        encodeArgs.push('-c:v', 'ffv1', '-level', '3', '-g', '1', '-slices', '16', '-slicecrc', '1', '-pix_fmt', 'gbrp', '-an', '-sn', '-f', 'matroska', tmpOut);
        // stage 2 (RVE --custom_encoder, whitespace separated): our video encoder and colours, video only
        // -fps_mode passthrough: keep exactly the decoded frames (RVE's writer adds -r)
        const parts = ['-fps_mode', 'passthrough', ...videoOut, '-an', '-sn', ...tailOut.filter((_x, i, a) => a[i] !== '-movflags' && a[i - 1] !== '-movflags'), '-f', 'matroska'];
        if (parts.some(x => /\s/.test(x))) throw new Error('Internal error: encoder option contains whitespace.');
        customEncoder = parts.join(' ');
        // stage 3: stream-copy the upscaled video and add audio/subtitles from the original file
        muxArgs = (videoFile, outFile) => {
            const a = ['-hide_banner', '-loglevel', 'warning', '-nostdin', '-y', '-i', videoFile];
            if (needSecond) {
                if (start > 0) a.push('-ss', start.toFixed(6));
                a.push('-t', (end - start).toFixed(6), '-i', m.path);
            }
            a.push('-map', '0:v:0', '-c:v', 'copy');
            if (audio) a.push('-map', `1:${audio.index}`);
            a.push(...audioArgs());
            if (subCodec) a.push('-map', '1:s?', '-c:s', subCodec);
            if (needSecond) a.push('-map_metadata', '1');
            a.push('-metadata', 'comment=Processed with UVE Manager 1.0.0 (Ultimate Video Enhancer effect chain, REAL-Video-Enhancer upscaling)');
            if (ext === 'mp4' || ext === 'mov') a.push('-movflags', '+faststart');
            if ((ext === 'mp4' || ext === 'mov') && /hevc|x265/.test(enc.name)) a.push('-tag:v', 'hvc1');
            a.push('-f', muxer, outFile);
            return a;
        };
    } else {
        encodeArgs.push(...videoOut);
        if (audio) encodeArgs.push('-map', `1:${audio.index}`);
        encodeArgs.push(...audioArgs());
        if (subCodec) encodeArgs.push('-map', '1:s?', '-c:s', subCodec);
        if (needSecond) encodeArgs.push('-map_metadata', '1');
        encodeArgs.push('-metadata', 'comment=Processed with UVE Manager 1.0.0 (Ultimate Video Enhancer effect chain)');
        encodeArgs.push(...tailOut, '-f', muxer, tmpOut);
    }

    return {
        plan: {
            jobId: spec.id, ffmpeg: caps.ffmpegPath, decodeArgs, encodeArgs, width: pw, height: ph, in16, out16, fps, start, totalFrames,
            effect: spec.effect, autoKeys: spec.autoKeys, parityMode: spec.parityMode
        },
        encoder: enc.name,
        warnings,
        customEncoder,
        muxArgs
    };
}

export function enqueue(spec: JobSpec): JobState {
    const j: JobState = {
        id: spec.id, name: spec.name, input: spec.input, output: spec.output, status: 'queued', progress: 0, frames: 0,
        totalFrames: 0, fps: 0, elapsed: 0, eta: 0, outSize: 0, encoder: '', log: [], createdAt: Date.now()
    };
    jobs.push(j);
    specs.set(spec.id, spec);
    emit();
    void pump();
    return j;
}

async function pump() {
    if (active) return;
    const next = jobs.find(j => j.status === 'queued');
    if (!next) return;
    const spec = specs.get(next.id)!;
    const tmp = spec.output.replace(/(\.[^.]+)$/, '.uvepart$1');
    active = { id: next.id, win: null as unknown as BrowserWindow, tmp, canceled: false };
    startedAt = Date.now();
    try {
        if (spec.effect.settings.autoOn && !spec.autoKeys) {
            next.status = 'analyzing';
            next.log.push('Auto-Scene-Match: analysing the clip…');
            emit();
            const r = await analyze(spec.media, p => { next.progress = p; emit(); });
            if (active?.canceled) throw new Error('canceled');
            spec.autoKeys = runAutoSceneMatch(samplesFromAnalysis(r), { strength: spec.effect.settings.autoStrength, lockWB: spec.effect.settings.autoLockWB });
        }
        let upscale: { scale: number } | undefined;
        if (spec.exportSettings.aiUpscale) {
            const model = await findModel(spec.exportSettings.aiUpscale);
            if (!model) throw new Error(`AI upscale model "${spec.exportSettings.aiUpscale}" is unknown.`);
            const st = await rveStatus(false);
            if (!st.python || !st.backend) throw new Error('REAL-Video-Enhancer (TensorRT) is not installed - install it in Settings > AI Upscaling.');
            next.status = 'upscaling';
            next.log.push(`AI upscaling with REAL-Video-Enhancer (TensorRT): ${model.name}`);
            emit();
            let dlLine = -1;
            const modelPath = await downloadModel(model.id, (msg, pct) => {
                next.progress = pct;
                if (dlLine < 0) { dlLine = next.log.length; next.log.push(msg); } else next.log[dlLine] = msg;
                emit();
            });
            if (active?.canceled) throw new Error('canceled');
            upscale = { scale: model.scale };
            active.stage1 = spec.output.replace(/(\.[^.]+)$/, '.uvestage1.mkv');
            active.upscale = { modelPath, customEncoder: '', totalFrames: 0, ffmpeg: '', stage2: spec.output.replace(/(\.[^.]+)$/, '.uvestage2.mkv') };
        }
        const { plan, encoder, warnings, customEncoder, muxArgs } = await buildPlan(spec, active.stage1 || tmp, upscale);
        if (active.upscale && customEncoder) {
            active.upscale = { ...active.upscale, customEncoder, muxArgs, totalFrames: plan.totalFrames, ffmpeg: plan.ffmpeg };
            next.log.push(`Stage 1/2: UVE effects -> lossless intermediate ${active.stage1}`);
            next.log.push(`Stage 2/2 encoder (RVE --custom_encoder): ${customEncoder}`);
        }
        next.encoder = upscale ? `${encoder} + RVE TensorRT` : encoder;
        next.totalFrames = plan.totalFrames;
        next.progress = 0;
        warnings.forEach(w => next.log.push('Warning: ' + w));
        next.log.push(`Decoder: ffmpeg ${plan.decodeArgs.join(' ')}`);
        next.log.push(`Encoder: ffmpeg ${plan.encodeArgs.join(' ')}`);
        next.status = 'rendering';
        emit();
        if (active?.canceled) throw new Error('canceled');
        const win = new BrowserWindow({
            show: false, width: 320, height: 240, title: 'UVE Export Worker',
            webPreferences: { preload: workerPreload, contextIsolation: true, nodeIntegration: false, sandbox: false, backgroundThrottling: false, offscreen: false }
        });
        active.win = win;
        win.webContents.on('render-process-gone', (_e, d) => { if (active?.id === next.id) void finishJob(false, `Export worker crashed (${d.reason}).`); });
        await win.loadURL(workerUrl());
        win.webContents.send('worker:job', plan);
    } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        await finishJob(false, msg === 'canceled' ? undefined : msg, msg === 'canceled');
    }
}

/** Stage 2 of an AI-upscale job: REAL-Video-Enhancer upscales the intermediate and encodes the final file. */
async function runStage2() {
    const cur = active;
    if (!cur || !cur.stage1 || !cur.upscale) return;
    const up = cur.upscale;
    const j = jobs.find(x => x.id === cur.id);
    try { if (cur.win && !cur.win.isDestroyed()) cur.win.destroy(); } catch { /* ignore */ }
    if (!j) return;
    j.status = 'upscaling';
    j.progress = 0; j.frames = 0; j.fps = 0;
    j.log.push('Stage 2/2: REAL-Video-Enhancer TensorRT upscaling (the first run per model and resolution builds a TensorRT engine, this can take several minutes)...');
    emit();
    const t0 = Date.now();
    try {
        const r = await runUpscale({
            input: cur.stage1, output: up.stage2, modelPath: up.modelPath, ffmpeg: up.ffmpeg, customEncoder: up.customEncoder, totalFrames: up.totalFrames,
            onProgress: (frames, fps) => {
                j.frames = frames; j.fps = fps;
                j.progress = Math.min(1, frames / Math.max(1, up.totalFrames));
                j.elapsed = (Date.now() - startedAt) / 1000;
                j.eta = fps > 0 ? Math.max(0, (up.totalFrames - frames) / fps) : 0;
                fsp.stat(cur.tmp).then(st => { j.outSize = st.size; }).catch(() => undefined);
                emit();
            },
            onLog: l => { j.log.push(l.slice(0, 2000)); if (j.log.length > 500) j.log.splice(0, j.log.length - 500); }
        });
        cur.kill = r.kill;
        if (cur.canceled) r.kill();
        const res = await r.done;
        j.log.push(`Stage 2 finished in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
        if (!res.ok || cur.canceled) { await finishJob(false, res.error, cur.canceled); return; }
        if (!(await fileExists(up.stage2))) { await finishJob(false, 'REAL-Video-Enhancer finished but no output file was written.'); return; }
        // stage 3: add audio/subtitles from the original (stream copy where possible)
        j.status = 'finalizing';
        j.log.push('Stage 3/3: muxing audio and subtitles from the original file');
        emit();
        const args = up.muxArgs!(up.stage2, cur.tmp);
        j.log.push(`Mux: ffmpeg ${args.join(' ')}`);
        const mux = await new Promise<{ code: number; err: string }>(resolve => {
            const p = spawn(up.ffmpeg, args, { windowsHide: true });
            let err = '';
            cur.kill = () => p.kill();
            p.stderr.on('data', d => { err += d; });
            p.on('error', e => resolve({ code: 1, err: e.message }));
            p.on('exit', code => resolve({ code: code ?? 1, err }));
        });
        if (mux.err.trim()) mux.err.trim().split(/\r?\n/).slice(-20).forEach(l => j.log.push('[mux] ' + l));
        await finishJob(mux.code === 0 && !cur.canceled, mux.code === 0 ? undefined : `Muxing audio failed (${mux.code}): ${mux.err.trim().split('\n').slice(-2).join(' ')}`, cur.canceled);
    } catch (e) {
        await finishJob(false, e instanceof Error ? e.message : String(e));
    }
}

async function finishJob(ok: boolean, error?: string, canceled = false) {
    if (!active) return;
    const cur = active;
    active = null;
    const j = jobs.find(x => x.id === cur.id);
    try { if (cur.win && !cur.win.isDestroyed()) cur.win.destroy(); } catch { /* ignore */ }
    if (cur.stage1) await fsp.rm(cur.stage1, { force: true }).catch(() => undefined);
    if (cur.upscale?.stage2) await fsp.rm(cur.upscale.stage2, { force: true }).catch(() => undefined);
    if (j) {
        j.elapsed = (Date.now() - startedAt) / 1000;
        if (ok && !cur.canceled) {
            try {
                await fsp.rename(cur.tmp, j.output);
                j.status = 'done';
                j.progress = 1;
                j.eta = 0;
                j.outSize = (await fsp.stat(j.output)).size;
                j.log.push(`Finished: ${j.output}`);
            } catch (e) {
                j.status = 'failed';
                j.error = `Could not finalize the output file: ${e instanceof Error ? e.message : e}`;
            }
        } else {
            await fsp.rm(cur.tmp, { force: true }).catch(() => undefined);
            j.status = (cur.canceled || canceled) ? 'canceled' : 'failed';
            if (j.status === 'failed') j.error = error || 'Unknown error';
            if (error) j.log.push('Error: ' + error);
            j.log.push(j.status === 'canceled' ? 'Canceled – the incomplete output was deleted.' : 'Failed – no output file was written.');
        }
    }
    emit();
    void pump();
}

export function cancelJob(id: string) {
    const j = jobs.find(x => x.id === id);
    if (!j) return;
    if (active?.id === id) {
        active.canceled = true;
        active.kill?.();
        if (active.win && !active.win.isDestroyed()) active.win.webContents.send('worker:cancel');
        setTimeout(() => { if (active?.id === id) void finishJob(false, undefined, true); }, 5000);
    } else if (j.status === 'queued') {
        j.status = 'canceled';
        emit();
    }
}

export function retryJob(id: string) {
    const j = jobs.find(x => x.id === id);
    if (!j || !['failed', 'canceled'].includes(j.status)) return;
    Object.assign(j, { status: 'queued', progress: 0, frames: 0, fps: 0, eta: 0, error: undefined });
    j.log.push('--- retry ---');
    emit();
    void pump();
}

export function removeJob(id: string) {
    const i = jobs.findIndex(x => x.id === id);
    if (i < 0 || active?.id === id) return;
    jobs.splice(i, 1);
    specs.delete(id);
    emit();
}

export function clearFinished() {
    for (let i = jobs.length - 1; i >= 0; i--) if (['done', 'failed', 'canceled'].includes(jobs[i].status)) { specs.delete(jobs[i].id); jobs.splice(i, 1); }
    emit();
}

export function isRendering() { return !!active; }
