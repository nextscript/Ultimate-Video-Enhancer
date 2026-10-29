// Media probing (ffprobe), timeline thumbnails, preview proxies and the color-accurate
// decode filter shared by the analysis pass and the export decoder.
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fsp } from 'node:fs';
import path from 'node:path';
import type { AudioStreamInfo, MediaInfo, SubtitleStreamInfo, VideoStreamInfo } from '../shared/types';
import { cacheDir } from './store';
import { capabilities } from './tools';
import { type GpuVendor, type HwFamily, familyOrder, hwEncoderArgs } from '../shared/hw';
import { fileExists } from './util';

type Json = Record<string, any>; // eslint-disable-line @typescript-eslint/no-explicit-any

function ratio(s: string | undefined): number {
    if (!s) return 0;
    const [a, b] = s.split('/').map(Number);
    return b ? a / b : Number(a) || 0;
}

function bitDepthOf(pix: string, raw?: string): number {
    const n = Number(raw);
    if (n > 0) return n;
    const m = pix.match(/p(\d{2})(le|be)?$/) || pix.match(/(\d{2})(le|be)$/);
    return m ? Number(m[1]) : 8;
}

function rotationOf(s: Json): number {
    const sd = (s.side_data_list as Json[] | undefined)?.find(d => d.rotation !== undefined);
    const r = sd ? Number(sd.rotation) : Number(s.tags?.rotate || 0);
    return ((Math.round(r) % 360) + 360) % 360;
}

export async function probe(file: string): Promise<MediaInfo> {
    const caps = await capabilities();
    if (!caps.ok) throw new Error(caps.error || 'FFprobe not available');
    const st = await fsp.stat(file);
    const out = await new Promise<string>((resolve, reject) => {
        const p = spawn(caps.ffprobePath, ['-v', 'error', '-print_format', 'json', '-show_format', '-show_streams', file], { windowsHide: true });
        let o = '', e = '';
        const t = setTimeout(() => { p.kill(); reject(new Error('ffprobe timed out after 30 s')); }, 30000);
        p.stdout.on('data', d => { o += d; if (o.length > 32 * 1024 * 1024) p.kill(); });
        p.stderr.on('data', d => { e += d; });
        p.on('error', err => { clearTimeout(t); reject(err); });
        p.on('exit', code => { clearTimeout(t); code === 0 ? resolve(o) : reject(new Error(e.trim() || `ffprobe exited with code ${code}`)); });
    });
    const j = JSON.parse(out) as Json;
    const fmt = (j.format || {}) as Json;
    const streams = (j.streams || []) as Json[];
    const warnings: string[] = [];
    const videoStreams: VideoStreamInfo[] = streams.filter(s => s.codec_type === 'video' && !(s.disposition?.attached_pic)).map(s => {
        const fpsR = ratio(s.r_frame_rate), fpsA = ratio(s.avg_frame_rate);
        const [fn, fd] = String(s.avg_frame_rate && fpsA ? s.avg_frame_rate : s.r_frame_rate || '0/1').split('/').map(Number);
        const rot = rotationOf(s);
        const sar = ratio(s.sample_aspect_ratio) || 1;
        let dw = Math.round(Number(s.width) * (sar > 0 ? sar : 1)), dh = Number(s.height);
        if (rot === 90 || rot === 270) [dw, dh] = [dh, dw];
        const pix = String(s.pix_fmt || '');
        const trc = String(s.color_transfer || '');
        return {
            index: Number(s.index), codec: String(s.codec_name || '?'), codecLong: String(s.codec_long_name || ''), profile: String(s.profile || ''),
            width: Number(s.width) || 0, height: Number(s.height) || 0, displayWidth: dw, displayHeight: dh,
            sar: String(s.sample_aspect_ratio || '1:1'), fps: fpsA || fpsR, fpsNum: fn || 0, fpsDen: fd || 1, avgFps: fpsA,
            vfr: !!(fpsR && fpsA && Math.abs(fpsR - fpsA) / fpsR > 0.005),
            pixFmt: pix, bitDepth: bitDepthOf(pix, s.bits_per_raw_sample), colorSpace: String(s.color_space || ''),
            colorPrimaries: String(s.color_primaries || ''), colorTransfer: trc, colorRange: String(s.color_range || ''),
            rotation: rot, hdr: trc === 'smpte2084' || trc === 'arib-std-b67', alpha: /yuva|rgba|argb|bgra|gbrap/.test(pix),
            frames: Number(s.nb_frames) || 0, bitRate: Number(s.bit_rate) || 0, title: String(s.tags?.title || '')
        };
    });
    const audio: AudioStreamInfo[] = streams.filter(s => s.codec_type === 'audio').map(s => ({
        index: Number(s.index), codec: String(s.codec_name || '?'), codecLong: String(s.codec_long_name || ''),
        channels: Number(s.channels) || 0, layout: String(s.channel_layout || ''), sampleRate: Number(s.sample_rate) || 0,
        bitRate: Number(s.bit_rate) || 0, language: String(s.tags?.language || ''), title: String(s.tags?.title || '')
    }));
    const subtitles: SubtitleStreamInfo[] = streams.filter(s => s.codec_type === 'subtitle').map(s => ({
        index: Number(s.index), codec: String(s.codec_name || '?'), language: String(s.tags?.language || ''), title: String(s.tags?.title || '')
    }));
    const video = videoStreams[0] || null;
    if (!video) warnings.push(audio.length ? 'This file contains audio only – UVE Manager needs a video stream.' : 'No video stream found.');
    if (video?.vfr) warnings.push('Variable frame rate detected. UVE Manager 1.0.0 exports at the average frame rate (constant).');
    if (video?.hdr) warnings.push(`HDR source (${video.colorTransfer}). It is tone-mapped to SDR BT.709 for processing; the UVE "HDR" effect is a look, not HDR10 output.`);
    if (video && video.bitDepth > 8) warnings.push(`${video.bitDepth}-bit source: processed in 16-bit; choose 10-bit output to keep the precision.`);
    if (video?.alpha) warnings.push('The source has an alpha channel; it is flattened (opaque) on export.');
    const encrypted = streams.some(s => /encrypt|drm/i.test(JSON.stringify(s.tags || {}))) || /encrypt/i.test(JSON.stringify(fmt.tags || {}));
    if (encrypted) warnings.push('The file appears to be DRM-protected/encrypted. Protected content cannot be processed.');
    const duration = Number(fmt.duration) || Number(streams.find(s => s.codec_type === 'video')?.duration) || 0;
    if (video && !video.frames && duration && video.fps) video.frames = Math.round(duration * video.fps);
    return {
        path: file, name: path.basename(file), size: st.size, mtimeMs: st.mtimeMs,
        container: String(fmt.format_name || ''), containerLong: String(fmt.format_long_name || ''),
        duration, startTime: Number(fmt.start_time) || 0, bitRate: Number(fmt.bit_rate) || 0,
        video, videoStreams, audio, subtitles, encrypted, warnings
    };
}

/** YUV→RGB matrix/range for the source (unspecified HD → BT.709, SD → BT.601, like browsers). */
export function sourceMatrix(v: VideoStreamInfo): { matrix: string; range: string } {
    const cs = v.colorSpace;
    let matrix = 'bt709';
    if (/bt470bg|smpte170m|bt601/.test(cs)) matrix = 'bt601';
    else if (/bt2020/.test(cs)) matrix = 'bt2020';
    else if (!cs || cs === 'unknown') matrix = v.height >= 720 ? 'bt709' : 'bt601';
    return { matrix, range: v.colorRange === 'pc' ? 'pc' : 'tv' };
}

/**
 * Filter chain that converts the source to full-range R'G'B' (BT.709 transfer) at the given size.
 * HDR (PQ/HLG) sources are tone-mapped with zscale+tonemap when available.
 */
export async function decodeFilter(v: VideoStreamInfo, w: number, h: number, pixFmt: 'rgba' | 'rgb24' | 'rgba64le', extra = ''): Promise<string> {
    const caps = await capabilities();
    const parts: string[] = [];
    if (extra) parts.push(extra);
    if (v.hdr && caps.filters.zscale && caps.filters.tonemap) {
        parts.push('zscale=t=linear:npl=100', 'format=gbrpf32le', 'zscale=p=bt709', 'tonemap=tonemap=hable:desat=0', 'zscale=t=bt709:m=bt709:r=tv', 'format=yuv444p16le');
        parts.push(`scale=${w}:${h}:in_color_matrix=bt709:in_range=tv:out_range=pc:flags=lanczos+accurate_rnd+full_chroma_int`);
    } else {
        const { matrix, range } = sourceMatrix(v);
        parts.push(`scale=${w}:${h}:in_color_matrix=${matrix}:in_range=${range}:out_range=pc:flags=lanczos+accurate_rnd+full_chroma_int`);
    }
    parts.push(`format=${pixFmt}`);
    return parts.join(',');
}

// ------------------------------------------------------------------ thumbnails
export async function thumbnails(media: MediaInfo, count: number, height = 72): Promise<{ t: number; url: string }[]> {
    const caps = await capabilities();
    if (!media.video || !caps.ok) return [];
    const key = createHash('sha1').update(`${media.path}|${media.size}|${media.mtimeMs}|${count}|${height}`).digest('hex').slice(0, 16);
    const dir = path.join(cacheDir(), 'thumbs', key);
    await fsp.mkdir(dir, { recursive: true });
    const n = Math.max(1, Math.min(200, count));
    const times = Array.from({ length: n }, (_, i) => (media.duration * (i + 0.5)) / n);
    const filter = await decodeFilter(media.video, -2, height, 'rgb24');
    const one = async (t: number, i: number) => {
        const file = path.join(dir, `${i}.jpg`);
        if (!(await fileExists(file))) {
            await new Promise<void>(resolve => {
                const p = spawn(caps.ffmpegPath, ['-hide_banner', '-loglevel', 'error', '-ss', t.toFixed(3), '-i', media.path, '-map', `0:${media.video!.index}`,
                    '-frames:v', '1', '-vf', filter, '-q:v', '4', '-y', file], { windowsHide: true });
                const to = setTimeout(() => p.kill(), 20000);
                p.on('exit', () => { clearTimeout(to); resolve(); });
                p.on('error', () => { clearTimeout(to); resolve(); });
            });
        }
        try { return { t, url: 'data:image/jpeg;base64,' + (await fsp.readFile(file)).toString('base64') }; } catch { return null; }
    };
    const out: { t: number; url: string }[] = [];
    for (let i = 0; i < times.length; i += 4) {
        const batch = await Promise.all(times.slice(i, i + 4).map((t, k) => one(t, i + k)));
        batch.forEach(b => b && out.push(b));
    }
    return out;
}

// ------------------------------------------------------------------ preview proxy
const proxies = new Map<string, Promise<string>>();

/**
 * Some containers/codecs cannot be decoded by Chromium (AVI, WMV, MPEG-2, ProRes, …). For the
 * live preview only, a cached H.264 proxy is created. Export always reads the original file.
 */
export function previewProxy(media: MediaInfo, onProgress: (pct: number) => void): Promise<string> {
    const key = createHash('sha1').update(`${media.path}|${media.size}|${media.mtimeMs}`).digest('hex').slice(0, 20);
    const existing = proxies.get(key);
    if (existing) return existing;
    const job = (async () => {
        const caps = await capabilities();
        const dir = path.join(cacheDir(), 'proxies');
        await fsp.mkdir(dir, { recursive: true });
        const file = path.join(dir, `${key}.mp4`);
        if (await fileExists(file)) return file;
        const tmp = file + '.part.mp4';
        const v = media.video!;
        // preview proxy: any verified hardware H.264 encoder (v1.0.1), else x264
        const hwH264 = familyOrder(caps.platform, caps.gpuVendor as GpuVendor).map(f => `h264_${f}`).find(n => caps.hwEncoders?.[n] && !n.endsWith('vaapi'));
        const gpu = !!hwH264;
        const args = ['-hide_banner', '-loglevel', 'error', '-progress', 'pipe:1', '-nostats', '-i', media.path, '-map', `0:${v.index}`];
        if (media.audio[0]) args.push('-map', `0:${media.audio[0].index}`);
        const { matrix, range } = sourceMatrix(v);
        const scaleH = Math.min(v.displayHeight, 2160);
        args.push('-vf', `scale=-2:${scaleH}:in_color_matrix=${matrix}:in_range=${range}:out_color_matrix=bt709:out_range=tv,format=yuv420p`,
            '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-color_range', 'tv');
        if (gpu) args.push(...hwEncoderArgs('h264', hwH264!.split('_')[1] as HwFamily, 18, 'fast', false)!.args);
        else args.push('-c:v', 'libx264', '-preset', 'veryfast', '-crf', '16');
        args.push('-g', String(Math.max(1, Math.round(v.fps || 25))), '-c:a', 'aac', '-b:a', '192k', '-movflags', '+faststart', '-y', tmp);
        await new Promise<void>((resolve, reject) => {
            const p = spawn(caps.ffmpegPath, args, { windowsHide: true });
            let err = '';
            p.stdout.on('data', d => {
                const m = String(d).match(/out_time_us=(\d+)/g);
                if (m && media.duration) onProgress(Math.min(1, Number(m[m.length - 1].split('=')[1]) / 1e6 / media.duration));
            });
            p.stderr.on('data', d => { err += d; });
            p.on('error', reject);
            p.on('exit', code => code === 0 ? resolve() : reject(new Error(err.trim().split('\n').slice(-3).join(' ') || `ffmpeg exited with ${code}`)));
        });
        await fsp.rename(tmp, file);
        return file;
    })();
    proxies.set(key, job);
    job.catch(() => proxies.delete(key));
    return job;
}

export async function clearCache(): Promise<number> {
    const dir = cacheDir();
    let bytes = 0;
    const walk = async (d: string) => {
        for (const e of await fsp.readdir(d, { withFileTypes: true }).catch(() => [])) {
            const p = path.join(d, e.name);
            if (e.isDirectory()) await walk(p); else bytes += (await fsp.stat(p).catch(() => ({ size: 0 }))).size;
        }
    };
    await walk(dir);
    await fsp.rm(dir, { recursive: true, force: true });
    proxies.clear();
    return bytes;
}
