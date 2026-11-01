// Render queue: every job runs in its own hidden export-worker window which decodes the ORIGINAL
// file with FFmpeg, applies the UVE effect graph frame by frame on the GPU and pipes the frames
// into the encoder. No screen/player capture is involved anywhere.
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fsp } from 'node:fs';
import path from 'node:path';
import { BrowserWindow, ipcMain } from 'electron';
import { outputFps, parseFps } from '../shared/fps';
import { buildAudioGraph, buildPlan as buildTimelinePlan, type PlanClip } from '../shared/timeline';
import type { Capabilities, EffectState, ExportSettings, JobSpec, JobState, MediaInfo, VideoStreamInfo } from '../shared/types';
import { runAutoSceneMatch, samplesFromAnalysis } from '../shared/uve/auto';
import {
    type TimelineStream, type TimelineWorkerPlan, argNum, audioPassArgs, decodeStreamArgs, referencedVideoClips, reversePrepassArgs,
    samplerFilter, samplerStart, timelineMuxArgs
} from '../shared/uve/export-clip-graph';
import { analyze } from './analysis';
import { decodeFilter } from './media';
import { capabilities, runEncoderTest } from './tools';
import { type EncoderChoice, HW_FAMILY_LABEL, type HwCodec, VAAPI_DEVICE, encoderTestArgs, familyOrder, hwEncoderArgs, hwEncoderName, hwaccelOrder } from '../shared/hw';
import { cacheDir, getPrefs } from './store';
import { fileExists } from './util';
import { BACKEND_LABEL, type RveBackend, downloadModel, findModel, rveStatus, runUpscale } from './rve';
import { resolveUpscaleProvider, video2xOptions } from '../shared/upscale-provider';
import { buildArgs as video2xArgs, estimateTempBytes } from '../shared/video2x-cli';
import { type PreparedVideo2x, prepareJob as prepareVideo2x, probeFile, v2xLog } from './video2x';
import { execute as executeVideo2x } from './video2x-process';

/** AI upscaling options for the plan builders. `provider` video2x: the upscaler renders straight to the final size. */
export interface UpscalePlan { scale: number; provider?: 'rve' | 'video2x' }

/** What buildPlan / buildTimelineJob return for the upscale stages besides the worker plan. */
interface UpscaleOutputs {
    customEncoder?: string;
    muxArgs?: (videoFile: string, outFile: string) => string[];
    /** video2x: final encoder options (argv parts) applied to the upscaled lossless file */
    finalVideoArgs?: string[];
    /** size the upscaler renders (video2x: = final output size) */
    stage2Size?: { w: number; h: number };
    /** frames in the upscaled stream (before an output frame-rate change) / in the final file */
    stage2Frames?: number;
    finalFrames?: number;
    vaapi?: boolean;
    /** the final file gets an audio track */
    hasAudio?: boolean;
    outFps?: number;
}

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
    /** v1.0.1: filter engine preference (auto / webgpu / webgl2) */
    enginePref: 'auto' | 'webgpu' | 'webgl2';
}

export type { TimelineWorkerPlan } from '../shared/uve/export-clip-graph';
/** `worker:job` payload: the single-media plan, or a timeline plan (`mode: 'timeline'`). */
export type WorkerJob = WorkerPlan | TimelineWorkerPlan;

/** Main-process steps of a timeline job that run outside the worker (FFmpeg only). */
interface TimelineSteps {
    /** reverse pre-pass temps (lossless), created before the worker starts */
    prepasses: { clipId: string; out: string; part: string; args: string[] }[];
    /** audio pass into a PCM wav; null = video only */
    audio: { args: string[]; wav: string } | null;
    /** final mux of the encoded video + wav into the job's temp output; null = the worker writes it directly */
    mux: string[] | null;
    /** files deleted at job end */
    temps: string[];
    ffmpeg: string;
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
    upscale?: { engine: RveBackend; modelPath: string; customEncoder: string; totalFrames: number; ffmpeg: string; stage2: string; muxArgs?: (videoFile: string, outFile: string) => string[] };
    kill?: () => void;
    /** timeline jobs: pre-pass/audio/mux steps and their temp files */
    timeline?: TimelineSteps;
    /** v1.0.7 Video2X provider: stage 2 (Video2X) → 3 (final encode) → 4 (mux) → verify */
    video2x?: {
        prep: PreparedVideo2x; stage2: string; stage3: string; ffmpeg: string; ffprobe: string;
        finalVideoArgs: string[]; muxArgs: (videoFile: string, outFile: string) => string[];
        size: { w: number; h: number }; stage2Frames: number; finalFrames: number; vaapi: boolean; audio: boolean; fps: number;
    };
    /** v1.0.7: maps the current phase's 0..1 progress into the job's overall progress (monotonic across phases) */
    span?: { base: number; size: number };
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
        const phase = j.totalFrames ? Math.min(1, p.frames / j.totalFrames) : 0;
        j.progress = active.span ? Math.max(j.progress, active.span.base + active.span.size * phase) : phase;
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
    ipcMain.on('worker:engine', (e, p: { jobId: string; kind: string; adapter: string }) => {
        const j = jobs.find(x => x.id === p.jobId);
        if (!j || !active || active.win.webContents.id !== e.sender.id) return;
        j.engine = `${p.kind} · ${p.adapter}`;
        emit();
    });
    ipcMain.on('worker:stats', (e, p: { jobId: string; stats: NonNullable<JobState['stats']> }) => {
        const j = jobs.find(x => x.id === p.jobId);
        if (!j || !active || active.win.webContents.id !== e.sender.id) return;
        j.stats = p.stats;
    });
    ipcMain.on('worker:done', (e, p: { jobId: string; ok: boolean; error?: string }) => {
        if (!active || active.win.webContents.id !== e.sender.id || active.id !== p.jobId) return;
        if (p.ok && active.stage1 && active.video2x && !active.canceled) void runVideo2xStages();
        else if (p.ok && active.stage1 && active.upscale && !active.canceled) void runStage2();
        else if (p.ok && active.timeline?.mux && !active.canceled) void runTimelineMux();
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
    if (s.videoCodec === 'h264' && s.tenBit && s.encoder !== 'cpu') w.push('Hardware H.264 encoders have no 10-bit mode; the software encoder (x264 High 10) is used for 10-bit H.264.');
    if (s.videoCodec === 'av1' && !Object.entries(caps.hwEncoders || {}).some(([k, v]) => v && k.startsWith('av1_')) && !caps.encoders.libsvtav1 && !caps.encoders['libaom-av1']) w.push('No AV1 encoder available in this FFmpeg build.');
    return w;
}

/** Software encoders (v1.0.0 behaviour, also the final fallback). */
function softwareEncoder(s: ExportSettings, caps: Capabilities): EncoderChoice {
    const cpuPreset = { fast: 'faster', medium: 'medium', slow: 'slow' }[s.preset];
    const q = String(Math.round(s.quality));
    const sw = (name: string, args: string[], pix: string, tenBit: boolean): EncoderChoice => ({ name, family: 'software', args, pix, tenBit, vaapi: false });
    switch (s.videoCodec) {
        case 'h264':
            return sw('libx264', ['-c:v', 'libx264', '-preset', cpuPreset, '-crf', q, ...(s.tenBit ? ['-profile:v', 'high10'] : ['-profile:v', 'high'])], s.tenBit ? 'yuv420p10le' : 'yuv420p', s.tenBit);
        case 'hevc':
            return sw('libx265', ['-c:v', 'libx265', '-preset', cpuPreset, '-crf', q, '-tag:v', 'hvc1', '-x265-params', 'log-level=error'], s.tenBit ? 'yuv420p10le' : 'yuv420p', s.tenBit);
        case 'av1':
            if (caps.encoders.libsvtav1) return sw('libsvtav1', ['-c:v', 'libsvtav1', '-preset', { fast: '10', medium: '8', slow: '5' }[s.preset], '-crf', q], s.tenBit ? 'yuv420p10le' : 'yuv420p', s.tenBit);
            return sw('libaom-av1', ['-c:v', 'libaom-av1', '-cpu-used', '6', '-row-mt', '1', '-crf', q, '-b:v', '0'], s.tenBit ? 'yuv420p10le' : 'yuv420p', s.tenBit);
        case 'vp9':
            return sw('libvpx-vp9', ['-c:v', 'libvpx-vp9', '-crf', q, '-b:v', '0', '-row-mt', '1', '-deadline', s.preset === 'slow' ? 'good' : 'realtime', '-cpu-used', s.preset === 'fast' ? '6' : '3'], s.tenBit ? 'yuv420p10le' : 'yuv420p', s.tenBit);
        case 'prores':
            return sw('prores_ks', ['-c:v', 'prores_ks', '-profile:v', '3', '-vendor', 'apl0'], 'yuv422p10le', true);
    }
}

/**
 * v1.0.1 encoder selection: Automatic / Prefer Hardware → first hardware encoder that was verified at start-up
 * AND passes a scratch encode at the job's real size and bit depth; otherwise (or with "Software") the
 * software encoder. The codec, resolution and frame rate are never changed by the selection.
 */
async function pickVideoEncoder(s: ExportSettings, caps: Capabilities, w: number, h: number, notes: string[]): Promise<EncoderChoice> {
    const sw = softwareEncoder(s, caps);
    if (s.encoder === 'cpu' || !['h264', 'hevc', 'av1'].includes(s.videoCodec)) return sw;
    const codec = s.videoCodec as HwCodec;
    const fams = familyOrder(caps.platform || process.platform, (caps.gpuVendor || 'other') as Parameters<typeof familyOrder>[1]);
    const tried: string[] = [];
    for (const fam of fams) {
        const name = hwEncoderName(codec, fam);
        if (!caps.hwEncoders?.[name]) continue;
        const choice = hwEncoderArgs(codec, fam, s.quality, s.preset, s.tenBit);
        if (!choice) { tried.push(`${name} (no ${s.tenBit ? '10-bit' : ''} mode)`); continue; }
        const t = await runEncoderTest(caps.ffmpegPath, encoderTestArgs(choice, Math.max(2, Math.round(w / 2) * 2), Math.max(2, Math.round(h / 2) * 2)));
        if (t.ok) { notes.push(`Encoder: ${HW_FAMILY_LABEL[fam]} (${name}) – verified with a ${w}x${h}${s.tenBit ? ' 10-bit' : ''} scratch encode.`); return choice; }
        tried.push(`${name} (${t.err || 'failed'})`);
    }
    notes.push(`Encoder: software ${sw.name}${tried.length ? ` – hardware candidates failed: ${tried.join('; ')}` : ' – no verified hardware encoder for this codec'}.`);
    if (s.encoder === 'gpu') notes.push(`Warning: "Prefer Hardware" was selected but no hardware ${s.videoCodec.toUpperCase()} encoder works for this job; the software encoder is used instead.`);
    return sw;
}

/**
 * Hardware decoding: verified on the actual input (first half second) before it is used.
 * v1.0.2: the probe runs the real decode filter chain – without it a hwaccel that hands out GPU frames
 * (e.g. qsv/d3d11va on Windows 11 laptops) passed the test and then failed in the export with EINVAL.
 */
async function pickHwaccel(caps: Capabilities, input: string, start: number, videoIndex: number, vf: string, notes: string[]): Promise<string | null> {
    const pref = (await getPrefs()).hwDecode || 'auto';
    if (pref === 'software') { notes.push('Decoder: software (selected in Settings).'); return null; }
    const order = hwaccelOrder(caps.platform || process.platform, (caps.gpuVendor || 'other') as Parameters<typeof hwaccelOrder>[1], caps.hwaccels);
    for (const hw of order) {
        const args = ['-hide_banner', '-loglevel', 'error', '-hwaccel', hw, ...(start > 0 ? ['-ss', start.toFixed(3)] : []), '-i', input, '-map', `0:${videoIndex}`, '-t', '0.5', '-vf', vf, '-f', 'null', '-'];
        const r = await runEncoderTest(caps.ffmpegPath, args, 20000);
        if (r.ok && !/fail|error|not supported|unsupported/i.test(r.err)) { notes.push(`Decoder: hardware ${hw} – verified on this file.`); return hw; }
    }
    notes.push(`Decoder: software${order.length ? ` – hardware decoders not usable for this file (${order.join(', ')})` : ''}.`);
    return null;
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

export async function buildPlan(spec: JobSpec, tmpOut: string, upscale?: UpscalePlan): Promise<{ plan: WorkerPlan; encoder: string; warnings: string[] } & UpscaleOutputs> {
    const caps = await capabilities();
    if (!caps.ok) throw new Error(caps.error || 'FFmpeg not available');
    const m = spec.media, v = m.video;
    if (!v) throw new Error('The source has no video stream.');
    if (m.encrypted) throw new Error('Encrypted/DRM-protected sources cannot be processed.');
    const s = spec.exportSettings;
    const warnings = validateExport(s, caps);
    const rot = v.rotation === 90 || v.rotation === 270;
    const pw = rot ? v.height : v.width, ph = rot ? v.width : v.height;
    const notes: string[] = [];
    const direct = upscale?.provider === 'video2x';
    const finalGuess = upscale ? targetSize(s.resolution === 'x2' ? { ...s, resolution: 'source' } : s, pw * upscale.scale, ph * upscale.scale) : { w: pw, h: ph };
    let enc = await pickVideoEncoder(s, caps, finalGuess.w, finalGuess.h, notes);
    if (upscale && !direct && enc.vaapi) {
        // RVE runs the final encode itself and cannot open a VA-API device
        enc = softwareEncoder(s, caps);
        notes.push(`Encoder: software ${enc.name} – VA-API is not available inside the REAL-Video-Enhancer encode step.`);
    }
    const in16 = v.bitDepth > 8 || enc.tenBit;
    // with AI upscaling the worker writes an 8-bit RGB lossless intermediate (RVE reads rgb24)
    const out16 = enc.tenBit && !upscale;
    const { num: fpsNum, den: fpsDen } = outputFps(s.fps, v.fpsNum, v.fpsDen);
    if (parseFps(s.fps)) notes.push(`Frame rate: ${fpsNum}/${fpsDen} (${(fpsNum / fpsDen).toFixed(3)} fps, source ${((v.fpsNum || 25) / (v.fpsDen || 1)).toFixed(3)} fps) – frames are duplicated or dropped, not interpolated.`);
    else if (s.fps && s.fps.trim()) notes.push(`Warning: invalid frame rate "${s.fps}" – the source frame rate is used.`);
    const fps = fpsNum / fpsDen;
    const start = Math.max(0, spec.range.start), end = Math.min(m.duration || spec.range.end, spec.range.end);
    const totalFrames = Math.max(1, Math.round((end - start) * fps));

    const vf = await decodeFilter(v, pw, ph, in16 ? 'rgba64le' : 'rgba');
    const decodeArgs = ['-hide_banner', '-loglevel', 'error', '-nostdin'];
    const hwaccel = await pickHwaccel(caps, m.path, start, v.index, vf, notes);
    if (hwaccel) decodeArgs.push('-hwaccel', hwaccel);
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
    // Video2X renders straight to the final size (no second resampling after Anime4K)
    if (!direct && (size.w !== baseW || size.h !== baseH)) scaleParts.push(`scale=${size.w}:${size.h}:flags=lanczos`);
    scaleParts.push(`scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd+full_chroma_int`, `format=${enc.pix}`);
    if (enc.vaapi) scaleParts.push('hwupload');
    const videoOut = ['-vf', scaleParts.join(','), ...enc.args, '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-color_range', 'tv'];
    const tailOut: string[] = [];
    let finalVideoArgs: string[] | undefined;
    if (ext === 'mp4' || ext === 'mov') tailOut.push('-movflags', '+faststart');
    if (v.sar && v.sar !== '1:1' && v.sar !== '0:1' && !rot) tailOut.push('-aspect', `${(v.displayWidth / v.displayHeight).toFixed(6)}`);
    const muxer = { mp4: 'mp4', mkv: 'matroska', mov: 'mov', webm: 'webm' }[ext as 'mp4'];

    const encodeArgs = ['-hide_banner', '-loglevel', 'warning', '-nostdin', '-y', ...(enc.vaapi && !upscale ? ['-vaapi_device', VAAPI_DEVICE] : []),
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
        finalVideoArgs = parts;
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
            a.push('-metadata', `comment=Processed with UVE Manager 1.0.7 (Ultimate Video Enhancer effect chain, ${direct ? 'Video2X Anime4K' : 'REAL-Video-Enhancer'} upscaling)`);
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
            effect: spec.effect, autoKeys: spec.autoKeys, parityMode: spec.parityMode,
            enginePref: spec.enginePref || (await getPrefs()).gpuEngine || 'auto'
        },
        encoder: enc.name,
        warnings: [...notes, ...warnings],
        customEncoder,
        muxArgs,
        finalVideoArgs,
        stage2Size: upscale ? size : undefined,
        stage2Frames: totalFrames,
        finalFrames: totalFrames,
        vaapi: enc.vaapi,
        hasAudio: !!audio,
        outFps: fps
    };
}

/** Display size of a video stream after rotation (what the decoder delivers). */
function displaySize(v: VideoStreamInfo): { w: number; h: number } {
    const rot = v.rotation === 90 || v.rotation === 270;
    return { w: rot ? v.height : v.width, h: rot ? v.width : v.height };
}

/** The timeline's frame grid as an ffmpeg rational: `tl.fps` when it names the same rate, else the plan rate. */
function gridRate(fps: string, F: number): { num: number; den: number } {
    const p = parseFps(fps);
    if (p && Math.abs(p.num / p.den - F) < 1e-6) return p;
    if (Number.isInteger(F)) return { num: F, den: 1 };
    for (const den of [1001, 1000, 100]) { const n = Math.round(F * den); if (Math.abs(n / den - F) < 1e-9) return { num: n, den }; }
    return { num: Math.round(F * 1000000), den: 1000000 };
}

/**
 * Timeline job (JobSpec.timeline set): plan the range with `buildPlan`, one decoder per distinct clip stream,
 * lossless reverse pre-pass temps, per-clip effect states, the audio pass rendered verbatim from
 * `buildAudioGraph`, then a `-c:v copy` mux. Encoder/decoder selection, sizes and the output naming are the
 * same as the single-media path.
 */
export async function buildTimelineJob(spec: JobSpec, tmpOut: string, upscale?: UpscalePlan): Promise<{
    plan: TimelineWorkerPlan; encoder: string; warnings: string[]; outFrames: number; steps: TimelineSteps;
} & UpscaleOutputs> {
    const caps = await capabilities();
    if (!caps.ok) throw new Error(caps.error || 'FFmpeg not available');
    const tl = spec.timeline;
    if (!tl) throw new Error('Internal error: not a timeline job.');
    const out = path.resolve(spec.output).toLowerCase();
    if (tl.sources.some(x => path.resolve(x.path).toLowerCase() === out)) throw new Error('The output must not overwrite a timeline source file.');
    const s = spec.exportSettings;
    const warnings = validateExport(s, caps);
    const notes: string[] = [];
    const plan = buildTimelinePlan(tl, Math.max(0, spec.range.start), spec.range.end);
    if (plan.issues.length) throw new Error(`The timeline cannot be rendered: ${plan.issues[0].message}`);
    if (plan.totalFrames < 1) throw new Error('The timeline range to render is empty.');
    const used = referencedVideoClips(plan);
    for (const u of used) if (u.clip.source.media.encrypted) throw new Error(`Encrypted/DRM-protected sources cannot be processed (${u.clip.source.path}).`);
    // composite size: the job's primary media (first source with a picture), else the first drawn clip
    const pv = spec.media.video ?? used[0]?.clip.source.media.video ?? tl.sources.find(x => x.media.video)?.media.video ?? null;
    if (!pv) throw new Error('The timeline has no picture to render: no source has a video stream.');
    const { w: pw, h: ph } = displaySize(pv);
    const direct = upscale?.provider === 'video2x';
    const finalGuess = upscale ? targetSize(s.resolution === 'x2' ? { ...s, resolution: 'source' } : s, pw * upscale.scale, ph * upscale.scale) : { w: pw, h: ph };
    let enc = await pickVideoEncoder(s, caps, finalGuess.w, finalGuess.h, notes);
    if (upscale && !direct && enc.vaapi) {
        enc = softwareEncoder(s, caps);
        notes.push(`Encoder: software ${enc.name} – VA-API is not available inside the REAL-Video-Enhancer encode step.`);
    }
    const in16 = enc.tenBit || used.some(u => (u.clip.source.media.video?.bitDepth ?? 8) > 8);
    const out16 = enc.tenBit && !upscale;
    const fmt: 'rgba' | 'rgba64le' = in16 ? 'rgba64le' : 'rgba';
    const grid = gridRate(tl.fps, plan.frameRate);
    const fpsArg = `${grid.num}/${grid.den}`;
    const outRate = parseFps(s.fps);
    const retime = !!outRate && Math.abs(outRate.num / outRate.den - grid.num / grid.den) > 1e-9;
    if (outRate && retime) notes.push(`Frame rate: ${outRate.num}/${outRate.den} (${(outRate.num / outRate.den).toFixed(3)} fps, timeline ${(grid.num / grid.den).toFixed(3)} fps) – frames are duplicated or dropped, not interpolated.`);
    else if (s.fps && s.fps.trim() && !outRate) notes.push(`Warning: invalid frame rate "${s.fps}" – the timeline frame rate is used.`);
    const outFrames = outRate && retime ? Math.max(1, Math.round(plan.totalFrames / (grid.num / grid.den) * (outRate.num / outRate.den))) : plan.totalFrames;
    notes.push(`Timeline: ${plan.totalFrames} frames at ${fpsArg} from frame ${plan.rangeStartF}, ${used.length} picture clip(s), ${plan.windows.length} window(s).`);

    const ext = outputExtension(s);
    const muxer = { mp4: 'mp4', mkv: 'matroska', mov: 'mov', webm: 'webm' }[ext as 'mp4'];
    const temps: string[] = [];
    const prepasses: TimelineSteps['prepasses'] = [];
    const pathOverride = new Map<string, string>();
    const preDir = path.join(cacheDir(), 'timeline');
    await fsp.mkdir(preDir, { recursive: true });

    // decoders: one per distinct stream; hardware decoding verified once per source file
    const hwBySource = new Map<string, Promise<string | null>>();
    const streams = new Map<string, TimelineStream>();
    const streamOfClip: Record<string, string> = {};
    for (const u of used) {
        const p: PlanClip = u.clip;
        const media = p.source.media;
        const v = media.video;
        if (!v) continue;
        const { w, h } = displaySize(v);
        const span = p.decodeEnd - p.decodeStart;
        if (!(span > 0)) throw new Error(`Clip "${p.clip.id}" has no media left to decode (${argNum(p.decodeStart)}..${argNum(p.decodeEnd)} s).`);
        const srcFrameSec = v.fpsNum > 0 && v.fpsDen > 0 ? v.fpsDen / v.fpsNum : 1 / plan.frameRate;
        let input: string, ss: number, readT: number, map: string, vf: string, hw: string | null = null;
        if (p.reversePrepass) {
            const preVf = await decodeFilter(v, w, h, 'rgba64le');
            const digest = createHash('sha256').update(JSON.stringify([
                path.resolve(p.source.path), media, p.clip.id,
                { srcIn: p.clip.srcIn, dur: p.clip.dur, speed: p.clip.speed, reverse: p.clip.reverse, decodeStart: p.decodeStart, decodeEnd: p.decodeEnd, frames: p.streamFrames, fps: fpsArg, vf: preVf }
            ])).digest('hex').slice(0, 32);
            const out = path.join(preDir, `rev_${digest}.mkv`);
            const part = path.join(preDir, `rev_${digest}.part.mkv`);
            const audioStream = media.audio[0];
            if (!prepasses.some(x => x.out === out)) {
                prepasses.push({
                    clipId: p.clip.id, out, part,
                    args: reversePrepassArgs({
                        input: p.source.path, decodeStart: p.decodeStart, decodeEnd: p.decodeEnd,
                        speed: p.clip.speed, frames: p.streamFrames, fps: fpsArg, F: grid.num / grid.den,
                        srcFrameSec,
                        videoMap: `0:${v.index}`, vf: preVf, audioMap: audioStream ? `0:${audioStream.index}` : null, out: part
                    })
                });
                temps.push(out, part);
            }
            if (audioStream) pathOverride.set(p.clip.id, out);
            // the temp already is the clip's stream at the timeline rate (speed applied, reversed)
            input = out; ss = 0; map = '0:v:0';
            readT = p.streamFrames * grid.den / grid.num;
            vf = `format=${fmt}`;
        } else {
            const base = await decodeFilter(v, w, h, fmt);
            // frame-exact sampler: stream frame k = the media frame displayed at srcTimeAt (speed included)
            const st = samplerStart(p.decodeStart, srcFrameSec);
            input = p.source.path; ss = st.ss; map = `0:${v.index}`;
            readT = p.decodeEnd - st.ss;
            vf = `${samplerFilter(p.clip.speed, fpsArg, st.off)},${base}`;
            let hwp = hwBySource.get(p.source.path);
            if (!hwp) { hwp = pickHwaccel(caps, p.source.path, p.decodeStart, v.index, base, notes); hwBySource.set(p.source.path, hwp); }
            hw = await hwp;
        }
        const streamStartF = p.startF - p.headF;
        const key = JSON.stringify([input, ss, readT, map, vf, p.streamFrames, streamStartF]);
        const existing = streams.get(key);
        if (existing) {
            existing.clipIds.push(p.clip.id);
            existing.firstF = Math.min(existing.firstF, u.firstF);
            existing.lastF = Math.max(existing.lastF, u.lastF);
        } else {
            streams.set(key, {
                key, clipIds: [p.clip.id], input, viaTemp: p.reversePrepass, streamStartF, streamFrames: p.streamFrames,
                firstF: u.firstF, lastF: u.lastF, width: w, height: h, in16,
                args: decodeStreamArgs({ input, ss, t: readT, map, vf, fps: fpsArg, frames: p.streamFrames, pixFmt: fmt, hwaccel: hw })
            });
        }
        streamOfClip[p.clip.id] = key;
    }

    // audio: the graph rendered verbatim into a wav, muxed after the picture
    let audio: TimelineSteps['audio'] = null;
    if (s.audio !== 'none') {
        const g = buildAudioGraph(tl, plan, { pathOverride });
        if ('error' in g) notes.push(`Audio: ${g.error} The output has no audio track.`);
        else {
            const wav = spec.output.replace(/(\.[^.]+)$/, '.uveaudio.wav');
            audio = { args: audioPassArgs(g, wav), wav };
            temps.push(wav);
            if (s.audio === 'copy' || (s.audio === 'opus' && ext !== 'webm')) notes.push(`Audio: the timeline mix is encoded as ${ext === 'webm' ? 'Opus' : 'AAC'} (a mix cannot be stream-copied).`);
        }
    }
    const audioCodec = ext === 'webm' ? ['-c:a', 'libopus', '-b:a', `${s.audioBitrate}k`] : ['-c:a', 'aac', '-b:a', `${s.audioBitrate}k`];
    const comment = `comment=Processed with UVE Manager 1.0.7 (timeline, Ultimate Video Enhancer effect chain${upscale ? `, ${direct ? 'Video2X Anime4K' : 'REAL-Video-Enhancer'} upscaling` : ''})`;
    const muxExtra = (): string[] => {
        const a = ['-metadata', comment];
        if (ext === 'mp4' || ext === 'mov') a.push('-movflags', '+faststart');
        if ((ext === 'mp4' || ext === 'mov') && /hevc|x265/.test(enc.name)) a.push('-tag:v', 'hvc1');
        return a;
    };

    const baseW = upscale ? pw * upscale.scale : pw, baseH = upscale ? ph * upscale.scale : ph;
    const size = targetSize(upscale && s.resolution === 'x2' ? { ...s, resolution: 'source' } : s, baseW, baseH);
    const scaleParts: string[] = [];
    if (outRate && retime) scaleParts.push(`fps=${outRate.num}/${outRate.den}`);
    if (!direct && (size.w !== baseW || size.h !== baseH)) scaleParts.push(`scale=${size.w}:${size.h}:flags=lanczos`);
    scaleParts.push(`scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd+full_chroma_int`, `format=${enc.pix}`);
    if (enc.vaapi) scaleParts.push('hwupload');
    const videoOut = ['-vf', scaleParts.join(','), ...enc.args, '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-color_range', 'tv'];
    const encodeArgs = ['-hide_banner', '-loglevel', 'warning', '-nostdin', '-y', ...(enc.vaapi && !upscale ? ['-vaapi_device', VAAPI_DEVICE] : []),
        '-f', 'rawvideo', '-pix_fmt', out16 ? 'rgba64le' : 'rgba', '-s', `${pw}x${ph}`, '-r', fpsArg, '-i', 'pipe:0', '-map', '0:v:0'];
    let customEncoder: string | undefined;
    let muxArgs: ((videoFile: string, outFile: string) => string[]) | undefined;
    let mux: string[] | null = null;
    let finalVideoArgs: string[] | undefined;
    if (upscale) {
        encodeArgs.push('-c:v', 'ffv1', '-level', '3', '-g', '1', '-slices', '16', '-slicecrc', '1', '-pix_fmt', 'gbrp', '-an', '-sn', '-f', 'matroska', tmpOut);
        const parts = ['-fps_mode', 'passthrough', ...videoOut, '-an', '-sn', '-f', 'matroska'];
        if (parts.some(x => /\s/.test(x))) throw new Error('Internal error: encoder option contains whitespace.');
        customEncoder = parts.join(' ');
        finalVideoArgs = parts;
        const a = audio;
        muxArgs = (videoFile, outFile) => a
            ? timelineMuxArgs({ video: videoFile, wav: a.wav, out: outFile, muxer, audioCodec, extra: muxExtra() })
            : ['-hide_banner', '-loglevel', 'warning', '-nostdin', '-y', '-i', videoFile, '-map', '0:v:0', '-c:v', 'copy', '-an', ...muxExtra(), '-f', muxer, outFile];
    } else {
        // with audio the worker writes a video-only intermediate that is muxed with the wav afterwards
        const videoTarget = audio ? spec.output.replace(/(\.[^.]+)$/, '.uvevideo$1') : tmpOut;
        if (audio) {
            temps.push(videoTarget);
            mux = timelineMuxArgs({ video: videoTarget, wav: audio.wav, out: tmpOut, muxer, audioCodec, extra: muxExtra() });
        }
        encodeArgs.push(...videoOut, '-an', '-sn', '-metadata', comment);
        if (ext === 'mp4' || ext === 'mov') encodeArgs.push('-movflags', '+faststart');
        encodeArgs.push('-f', muxer, videoTarget);
    }

    const effects: Record<string, EffectState> = {};
    for (const p of plan.clips) effects[p.clip.id] = p.clip.effect ?? spec.effect;
    return {
        plan: {
            mode: 'timeline', jobId: spec.id, ffmpeg: caps.ffmpegPath, encodeArgs, width: pw, height: ph, out16,
            fpsNum: grid.num, fpsDen: grid.den, plan, masterGainDb: tl.masterGainDb,
            streams: [...streams.values()], streamOfClip, effects,
            autoKeys: spec.autoKeys, autoSource: spec.autoKeys ? spec.media.path : null, parityMode: spec.parityMode,
            enginePref: spec.enginePref || (await getPrefs()).gpuEngine || 'auto'
        },
        encoder: enc.name,
        warnings: [...notes, ...warnings],
        outFrames,
        steps: { prepasses, audio, mux, temps, ffmpeg: caps.ffmpegPath },
        customEncoder,
        muxArgs,
        finalVideoArgs,
        stage2Size: upscale ? size : undefined,
        stage2Frames: plan.totalFrames,
        finalFrames: outFrames,
        vaapi: enc.vaapi,
        hasAudio: !!audio,
        outFps: outRate && retime ? outRate.num / outRate.den : grid.num / grid.den
    };
}

/** One FFmpeg step of a timeline job in the main process; cancellable through the active job. */
function runFfmpegStep(ffmpeg: string, args: string[], cur: ActiveJob): Promise<{ code: number; err: string }> {
    return new Promise(resolve => {
        const p = spawn(ffmpeg, args, { windowsHide: true });
        let err = '';
        cur.kill = () => p.kill();
        p.stderr.on('data', d => { err += d; });
        p.on('error', e => resolve({ code: 1, err: e.message }));
        p.on('exit', code => { cur.kill = undefined; resolve({ code: code ?? 1, err }); });
    });
}

/** Reverse pre-pass temps and the audio pass, before the worker starts. Throws on failure. */
async function runTimelinePrep(cur: ActiveJob, j: JobState): Promise<void> {
    const st = cur.timeline;
    if (!st) return;
    const tail = (e: string) => e.trim().split(/\r?\n/).slice(-3).join(' ');
    for (const pre of st.prepasses) {
        if (cur.canceled) throw new Error('canceled');
        if (await fileExists(pre.out)) { j.log.push(`Reverse pre-pass (clip ${pre.clipId}): reusing ${pre.out}`); continue; }
        j.log.push(`Reverse pre-pass (clip ${pre.clipId}): ffmpeg ${pre.args.join(' ')}`);
        emit();
        const r = await runFfmpegStep(st.ffmpeg, pre.args, cur);
        if (cur.canceled) throw new Error('canceled');
        if (r.code !== 0) throw new Error(`Reverse pre-pass failed for clip ${pre.clipId} (${r.code}): ${tail(r.err)}`);
        await fsp.rename(pre.part, pre.out);
    }
    if (st.audio) {
        if (cur.canceled) throw new Error('canceled');
        j.log.push(`Audio pass: ffmpeg ${st.audio.args.join(' ')}`);
        emit();
        const r = await runFfmpegStep(st.ffmpeg, st.audio.args, cur);
        if (cur.canceled) throw new Error('canceled');
        if (r.code !== 0) throw new Error(`Audio pass failed (${r.code}): ${tail(r.err)}`);
    }
}

/** After the worker: mux the encoded picture with the timeline audio into the job's temp output. */
async function runTimelineMux() {
    const cur = active;
    const st = cur?.timeline;
    if (!cur || !st?.mux) return;
    const j = jobs.find(x => x.id === cur.id);
    try { if (cur.win && !cur.win.isDestroyed()) cur.win.destroy(); } catch { /* ignore */ }
    if (!j) return;
    j.status = 'finalizing';
    j.log.push(`Mux: ffmpeg ${st.mux.join(' ')}`);
    emit();
    const r = await runFfmpegStep(st.ffmpeg, st.mux, cur);
    if (r.err.trim()) r.err.trim().split(/\r?\n/).slice(-20).forEach(l => j.log.push('[mux] ' + l));
    await finishJob(r.code === 0 && !cur.canceled, r.code === 0 ? undefined : `Muxing the timeline audio failed (${r.code}): ${r.err.trim().split('\n').slice(-2).join(' ')}`, cur.canceled);
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
    next.phase = undefined;
    try {
        capsCache = await capabilities();
        if (spec.effect.settings.autoOn && !spec.autoKeys) {
            next.status = 'analyzing';
            next.log.push('Auto-Scene-Match: analysing the clip…');
            emit();
            const r = await analyze(spec.media, p => { next.progress = p; emit(); });
            if (active?.canceled) throw new Error('canceled');
            spec.autoKeys = runAutoSceneMatch(samplesFromAnalysis(r), { strength: spec.effect.settings.autoStrength, lockWB: spec.effect.settings.autoLockWB });
        }
        let upscale: UpscalePlan | undefined;
        const provider = resolveUpscaleProvider(spec.exportSettings);
        next.provider = provider;
        let v2x: PreparedVideo2x | undefined;
        if (provider === 'video2x') {
            // explicit provider: a Video2X problem fails the job, it never falls back to RVE
            const o = video2xOptions(spec.exportSettings);
            next.status = 'upscaling';
            next.phase = 'Preparing Video2X';
            next.log.push(`AI upscaling with Video2X (Anime4K, libplacebo): preset ${o.presetId}, ${o.scale}x, GPU ${o.device < 0 ? 'automatic' : '#' + o.device}`);
            emit();
            v2x = await prepareVideo2x(o);
            if (active?.canceled) throw new Error('canceled');
            v2x.notes.forEach(n => next.log.push(n));
            next.log.push(`Video2X ${v2x.version}: ${v2x.launcher.exe} · shader ${v2x.shaderLabel} · intermediate ${v2x.lossless}`);
            upscale = { scale: o.scale, provider: 'video2x' };
            active.stage1 = spec.output.replace(/(\.[^.]+)$/, '.uvestage1.mkv');
        } else if (provider === 'rve') {
            const st = await rveStatus(false);
            if (!st.python || !st.backend) throw new Error('REAL-Video-Enhancer is not installed - install it in Settings > AI Upscaling.');
            const engine = st.engine;
            const model = await findModel(spec.exportSettings.aiUpscale, engine);
            if (!model) throw new Error(`AI upscale model "${spec.exportSettings.aiUpscale}" is not available for the ${BACKEND_LABEL[engine]} backend - choose another model in Output Settings.`);
            next.status = 'upscaling';
            next.log.push(`AI upscaling with REAL-Video-Enhancer (${BACKEND_LABEL[engine]}): ${model.name}`);
            emit();
            let dlLine = -1;
            const modelPath = await downloadModel(model.id, (msg, pct) => {
                next.progress = pct;
                if (dlLine < 0) { dlLine = next.log.length; next.log.push(msg); } else next.log[dlLine] = msg;
                emit();
            });
            if (active?.canceled) throw new Error('canceled');
            upscale = { scale: model.scale, provider: 'rve' };
            active.stage1 = spec.output.replace(/(\.[^.]+)$/, '.uvestage1.mkv');
            active.upscale = { engine, modelPath, customEncoder: '', totalFrames: 0, ffmpeg: '', stage2: spec.output.replace(/(\.[^.]+)$/, '.uvestage2.mkv') };
        }
        let payload: WorkerJob;
        if (spec.timeline) {
            const t = await buildTimelineJob(spec, active.stage1 || tmp, upscale);
            payload = t.plan;
            active.timeline = t.steps;
            if (v2x) setupVideo2x(active, spec, v2x, t, next);
            if (active.upscale && t.customEncoder) {
                active.upscale = { ...active.upscale, customEncoder: t.customEncoder, muxArgs: t.muxArgs, totalFrames: t.outFrames, ffmpeg: t.plan.ffmpeg };
                next.log.push(`Stage 1/2: UVE timeline -> lossless intermediate ${active.stage1}`);
                next.log.push(`Stage 2/2 encoder (RVE --custom_encoder): ${t.customEncoder}`);
            }
            next.encoder = upscale && active.upscale ? `${t.encoder} + RVE ${BACKEND_LABEL[active.upscale.engine]}` : v2x ? `${t.encoder} + Video2X ${v2x.version} (Anime4K)` : t.encoder;
            next.totalFrames = t.plan.plan.totalFrames;
            next.progress = 0;
            t.warnings.forEach(w => next.log.push(/^(Encoder|Decoder|Frame rate|Timeline|Audio|Warning):/.test(w) ? w : 'Warning: ' + w));
            t.plan.streams.forEach(st => next.log.push(`Decoder [${st.clipIds.join(', ')}]: ffmpeg ${st.args.join(' ')}`));
            next.log.push(`Encoder: ffmpeg ${t.plan.encodeArgs.join(' ')}`);
            next.status = 'rendering';
            if (active.video2x) next.phase = 'Rendering UVE timeline (1/4)';
            emit();
            await runTimelinePrep(active, next);
        } else {
            const built = await buildPlan(spec, active.stage1 || tmp, upscale);
            const { plan, encoder, warnings, customEncoder, muxArgs } = built;
            payload = plan;
            if (v2x) setupVideo2x(active, spec, v2x, built, next);
            if (active.upscale && customEncoder) {
                active.upscale = { ...active.upscale, customEncoder, muxArgs, totalFrames: plan.totalFrames, ffmpeg: plan.ffmpeg };
                next.log.push(`Stage 1/2: UVE effects -> lossless intermediate ${active.stage1}`);
                next.log.push(`Stage 2/2 encoder (RVE --custom_encoder): ${customEncoder}`);
            }
            next.encoder = upscale && active.upscale ? `${encoder} + RVE ${BACKEND_LABEL[active.upscale.engine]}` : v2x ? `${encoder} + Video2X ${v2x.version} (Anime4K)` : encoder;
            next.totalFrames = plan.totalFrames;
            next.progress = 0;
            warnings.forEach(w => next.log.push(/^(Encoder|Decoder|Frame rate|Warning):/.test(w) ? w : 'Warning: ' + w));
            next.log.push(`Decoder: ffmpeg ${plan.decodeArgs.join(' ')}`);
            next.log.push(`Encoder: ffmpeg ${plan.encodeArgs.join(' ')}`);
            next.status = 'rendering';
            if (active.video2x) next.phase = 'Rendering UVE effects (1/4)';
            emit();
        }
        if (active?.canceled) throw new Error('canceled');
        const win = new BrowserWindow({
            show: false, width: 320, height: 240, title: 'UVE Export Worker',
            webPreferences: { preload: workerPreload, contextIsolation: true, nodeIntegration: false, sandbox: false, backgroundThrottling: false, offscreen: false }
        });
        active.win = win;
        win.webContents.on('render-process-gone', (_e, d) => { if (active?.id === next.id) void finishJob(false, `Export worker crashed (${d.reason}).`); });
        await win.loadURL(workerUrl());
        win.webContents.send('worker:job', payload);
    } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        await finishJob(false, msg === 'canceled' ? undefined : msg, msg === 'canceled');
    }
}

/** Progress weights of the Video2X pipeline phases (UVE render, Video2X, final encode, mux + verify). */
const V2X_SPANS = { uve: { base: 0, size: 0.35 }, video2x: { base: 0.35, size: 0.5 }, encode: { base: 0.85, size: 0.12 }, mux: { base: 0.97, size: 0.03 } };

/** Stores the Video2X stage plan on the active job (called once the UVE stage plan is known). */
function setupVideo2x(cur: ActiveJob, spec: JobSpec, prep: PreparedVideo2x, b: UpscaleOutputs & { warnings: string[] }, j: JobState) {
    if (!b.finalVideoArgs || !b.muxArgs || !b.stage2Size || !b.stage2Frames) throw new Error('Internal error: the Video2X stage plan is incomplete.');
    const caps = capsCache;
    cur.video2x = {
        prep, stage2: spec.output.replace(/(\.[^.]+)$/, '.uvestage2.mkv'), stage3: spec.output.replace(/(\.[^.]+)$/, '.uvestage3.mkv'),
        ffmpeg: caps?.ffmpegPath || '', ffprobe: caps?.ffprobePath || '',
        finalVideoArgs: b.finalVideoArgs, muxArgs: b.muxArgs, size: b.stage2Size, stage2Frames: b.stage2Frames, finalFrames: b.finalFrames ?? b.stage2Frames,
        vaapi: !!b.vaapi, audio: !!b.hasAudio, fps: b.outFps || 0
    };
    cur.span = V2X_SPANS.uve;
    j.log.push(`Stage 1/4: UVE render -> lossless intermediate ${cur.stage1}`);
    j.log.push(`Stage 2/4: Video2X ${prep.presetName} -> ${b.stage2Size.w}x${b.stage2Size.h} (${prep.lossless} lossless)`);
    j.log.push(`Stage 3/4: final encode ffmpeg ${b.finalVideoArgs.join(' ')}`);
    j.log.push('Stage 4/4: mux audio/subtitles, verify, rename');
    const v = spec.timeline ? null : spec.media.video;
    const srcW = v ? (v.rotation % 180 ? v.height : v.width) : Math.round(b.stage2Size.w / 2), srcH = v ? (v.rotation % 180 ? v.width : v.height) : Math.round(b.stage2Size.h / 2);
    j.log.push(`Temporary disk space: about ${(estimateTempBytes(b.stage2Frames, srcW, srcH, b.stage2Size.w, b.stage2Size.h) / 1073741824).toFixed(1)} GB (intermediates are deleted afterwards).`);
}
let capsCache: Awaited<ReturnType<typeof capabilities>> | null = null;

/** FFmpeg step with `-progress pipe:1` frame reporting. */
function runFfmpegProgress(ffmpeg: string, args: string[], cur: ActiveJob, onFrame: (n: number) => void): Promise<{ code: number; err: string }> {
    return new Promise(resolve => {
        const p = spawn(ffmpeg, ['-progress', 'pipe:1', '-nostats', ...args], { windowsHide: true, shell: false });
        let err = '', buf = '';
        cur.kill = () => p.kill();
        p.stdout.on('data', d => {
            buf += d;
            const lines = buf.split(/\r?\n/);
            buf = lines.pop() ?? '';
            for (const l of lines) { const m = l.match(/^frame=(\d+)/); if (m) onFrame(Number(m[1])); }
        });
        p.stderr.on('data', d => { err = (err + d).slice(-20000); });
        p.on('error', e => resolve({ code: 1, err: e.message }));
        p.on('exit', code => { cur.kill = undefined; resolve({ code: code ?? 1, err }); });
    });
}

/**
 * Stages 2–4 of a Video2X job: Video2X upscales the lossless UVE render (Anime4K in libplacebo) into a lossless
 * file at the final size, FFmpeg encodes it with the job's encoder settings (one lossy encode), the audio and
 * subtitles are muxed, and the result is verified before finishJob renames it atomically.
 */
async function runVideo2xStages() {
    const cur = active;
    if (!cur || !cur.stage1 || !cur.video2x) return;
    const vx = cur.video2x;
    const j = jobs.find(x => x.id === cur.id);
    try { if (cur.win && !cur.win.isDestroyed()) cur.win.destroy(); } catch { /* ignore */ }
    if (!j) return;
    const setPhase = (status: JobState['status'], phase: string, span: { base: number; size: number }) => {
        j.status = status; j.phase = phase; cur.span = span;
        j.progress = Math.max(j.progress, span.base); j.frames = 0; j.fps = 0; j.eta = 0;
        emit();
    };
    const pushLog = (l: string) => { j.log.push(l.slice(0, 2000)); if (j.log.length > 500) j.log.splice(0, j.log.length - 500); };
    // the cancel fallback timer may already have finished this job (and started the next one)
    const finish = (ok: boolean, error?: string, canceled = false) => (active === cur ? finishJob(ok, error, canceled) : Promise.resolve());
    const fail = (msg: string) => finish(false, msg, cur.canceled);
    try {
        // ---- stage 2: Video2X
        setPhase('upscaling', `Upscaling – Video2X ${vx.prep.presetName} (2/4)`, V2X_SPANS.video2x);
        j.totalFrames = vx.stage2Frames;
        const args = video2xArgs(vx.prep.caps, { input: cur.stage1, output: vx.stage2, shader: vx.prep.shader, width: vx.size.w, height: vx.size.h, device: vx.prep.device, lossless: vx.prep.lossless });
        pushLog(`Video2X: ${[vx.prep.launcher.exe, ...vx.prep.launcher.prefix, ...args].map(a => (/\s/.test(a) ? `"${a}"` : a)).join(' ')}`);
        void v2xLog(`job ${cur.id}: ${vx.prep.launcher.exe} ${args.join(' ')}`);
        const t0 = Date.now();
        const run = executeVideo2x(vx.prep.launcher, args, {
            onProgress: pr => {
                j.frames = pr.frames; j.fps = pr.fps;
                const total = pr.total > 0 ? pr.total : vx.stage2Frames;
                j.progress = Math.max(j.progress, V2X_SPANS.video2x.base + V2X_SPANS.video2x.size * Math.min(1, pr.frames / Math.max(1, total)));
                j.elapsed = (Date.now() - startedAt) / 1000;
                // remaining time only when measurable (Video2X reports it once frames were processed)
                j.eta = pr.remaining >= 0 && pr.frames > 0 ? pr.remaining : pr.fps > 0 ? Math.max(0, (total - pr.frames) / pr.fps) : 0;
                emit();
            },
            onLog: l => { pushLog(`[video2x] ${l}`); void v2xLog(`[job ${cur.id}] ${l}`); }
        });
        cur.kill = run.kill;
        if (cur.canceled) run.kill();
        const res = await run.done;
        cur.kill = undefined;
        pushLog(`Stage 2 finished in ${((Date.now() - t0) / 1000).toFixed(1)} s (exit ${res.code ?? res.signal})`);
        if (cur.canceled) { await finish(false, undefined, true); return; }
        if (!res.ok) { await fail(res.error || 'Video2X failed.'); return; }
        if (!(await fileExists(vx.stage2))) { await fail('Video2X finished but wrote no output file.'); return; }
        const p2 = await probeFile(vx.ffprobe, vx.stage2);
        pushLog(`Video2X output: ${p2.width}x${p2.height}, ${p2.frames} frames, ${p2.fps.toFixed(3)} fps`);
        if (p2.width !== vx.size.w || p2.height !== vx.size.h) { await fail(`Video2X output is ${p2.width}x${p2.height}, expected ${vx.size.w}x${vx.size.h}.`); return; }
        // lossless stage: the frame count must match exactly
        if (p2.frames !== vx.stage2Frames) { await fail(`Video2X output has ${p2.frames} frames, expected ${vx.stage2Frames} – frames were dropped or duplicated.`); return; }

        // ---- stage 3: final encode with the job's encoder settings
        setPhase('finalizing', 'Encoding (3/4)', V2X_SPANS.encode);
        j.totalFrames = vx.finalFrames;
        const enc = ['-hide_banner', '-loglevel', 'warning', '-nostdin', '-y', ...(vx.vaapi ? ['-vaapi_device', VAAPI_DEVICE] : []), '-i', vx.stage2, '-map', '0:v:0', ...vx.finalVideoArgs, vx.stage3];
        pushLog(`Encode: ffmpeg ${enc.join(' ')}`);
        const e = await runFfmpegProgress(vx.ffmpeg, enc, cur, n => {
            j.frames = n;
            j.progress = Math.max(j.progress, V2X_SPANS.encode.base + V2X_SPANS.encode.size * Math.min(1, n / Math.max(1, vx.finalFrames)));
            j.elapsed = (Date.now() - startedAt) / 1000;
            emit();
        });
        if (e.err.trim()) e.err.trim().split(/\r?\n/).slice(-10).forEach(l => pushLog('[encode] ' + l));
        if (cur.canceled) { await finish(false, undefined, true); return; }
        if (e.code !== 0) { await fail(`Final encode failed (${e.code}): ${e.err.trim().split('\n').slice(-2).join(' ')}`); return; }
        await fsp.rm(vx.stage2, { force: true }).catch(() => undefined);

        // ---- stage 4: audio / subtitles, then verification
        setPhase('finalizing', 'Muxing (4/4)', V2X_SPANS.mux);
        const mux = vx.muxArgs(vx.stage3, cur.tmp);
        pushLog(`Mux: ffmpeg ${mux.join(' ')}`);
        const m = await runFfmpegStep(vx.ffmpeg, mux, cur);
        if (m.err.trim()) m.err.trim().split(/\r?\n/).slice(-10).forEach(l => pushLog('[mux] ' + l));
        if (cur.canceled) { await finish(false, undefined, true); return; }
        if (m.code !== 0) { await fail(`Muxing audio failed (${m.code}): ${m.err.trim().split('\n').slice(-2).join(' ')}`); return; }
        j.phase = 'Verifying output';
        emit();
        const f = await probeFile(vx.ffprobe, cur.tmp);
        const problems: string[] = [];
        if (f.width !== vx.size.w || f.height !== vx.size.h) problems.push(`size ${f.width}x${f.height} (expected ${vx.size.w}x${vx.size.h})`);
        if (!(f.duration > 0)) problems.push('zero duration');
        if (!f.frames) problems.push('no video frames');
        else if (Math.abs(f.frames - vx.finalFrames) > 1) problems.push(`${f.frames} frames (expected ${vx.finalFrames})`);
        if (vx.fps > 0 && f.fps > 0 && Math.abs(f.fps - vx.fps) > 0.01) problems.push(`${f.fps.toFixed(3)} fps (expected ${vx.fps.toFixed(3)})`);
        if (vx.audio && !f.audio) problems.push('audio track missing');
        // A/V sync tolerance: both streams must end within 0.25 s of each other (AAC padding is a few ms)
        if (vx.audio && f.audio && f.audioDuration > 0 && f.videoDuration > 0 && Math.abs(f.audioDuration - f.videoDuration) > 0.25) problems.push(`audio/video length differ by ${Math.abs(f.audioDuration - f.videoDuration).toFixed(2)} s`);
        pushLog(`Verify: ${f.width}x${f.height}, ${f.frames} frames, ${f.fps.toFixed(3)} fps, ${f.duration.toFixed(2)} s, ${f.audio} audio track(s)${problems.length ? ' – ' + problems.join('; ') : ' – OK'}`);
        if (problems.length) { await fail(`The finished file failed verification: ${problems.join('; ')}.`); return; }
        await finish(true);
    } catch (err) {
        await finish(false, err instanceof Error ? err.message : String(err), cur.canceled);
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
    j.log.push(up.engine === 'tensorrt'
        ? 'Stage 2/2: REAL-Video-Enhancer TensorRT upscaling (the first run per model and resolution builds a TensorRT engine, this can take several minutes)...'
        : `Stage 2/2: REAL-Video-Enhancer ${BACKEND_LABEL[up.engine]} upscaling...`);
    emit();
    const t0 = Date.now();
    try {
        const r = await runUpscale({
            input: cur.stage1, output: up.stage2, modelPath: up.modelPath, ffmpeg: up.ffmpeg, customEncoder: up.customEncoder, totalFrames: up.totalFrames, engine: up.engine,
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
    // Video2X intermediates (app-owned temp files only – never the source or shader files)
    for (const f of [cur.video2x?.stage2, cur.video2x?.stage3]) if (f) await fsp.rm(f, { force: true }).catch(() => undefined);
    for (const f of cur.timeline?.temps ?? []) await fsp.rm(f, { force: true }).catch(() => undefined);
    if (j) {
        j.elapsed = (Date.now() - startedAt) / 1000;
        if (ok && !cur.canceled) {
            try {
                await fsp.rename(cur.tmp, j.output);
                j.status = 'done';
                j.phase = undefined;
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
            if (j.provider === 'video2x') j.phase = j.status === 'canceled' ? 'Canceled' : `Failed${j.phase ? ` during: ${j.phase}` : ''}`;
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
    Object.assign(j, { status: 'queued', progress: 0, frames: 0, fps: 0, eta: 0, error: undefined, phase: undefined });
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
