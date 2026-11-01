// v1.1.2: instant preview sound for files whose audio Chromium cannot decode (AC3 / E-AC3 / DTS /
// TrueHD in MKV, …). Instead of converting the file first, the sound is served as a *virtual* PCM WAV
// file of the full length: every HTTP range request is answered by an ffmpeg process that decodes the
// source from exactly the requested sample on. PCM has a fixed byte rate, so byte offset ↔ time is
// exact and Chromium can seek in it like in any local file. Starts in ~0.1 s, seeks in ~0.5 s.
import { spawn, type ChildProcess } from 'node:child_process';
import type { MediaInfo } from '../shared/types';

export const PCM_RATE = 48000;
const HEADER = 44;

export interface VirtualAudio {
    file: string;
    /** ffmpeg stream index of the audio stream */
    stream: number;
    channels: number;
    duration: number;
}

export function virtualAudioOf(m: MediaInfo, streamIndex?: number): VirtualAudio | null {
    const a = (streamIndex !== undefined ? m.audio.find(x => x.index === streamIndex) : undefined) ?? m.audio[0];
    if (!a || !(m.duration > 0)) return null;
    return { file: m.path, stream: a.index, channels: Math.max(1, Math.min(8, a.channels || 2)), duration: m.duration };
}

function layout(v: VirtualAudio) {
    const block = v.channels * 2;
    const frames = Math.ceil(v.duration * PCM_RATE);
    const dataLen = frames * block;
    return { block, dataLen, total: HEADER + dataLen };
}

function wavHeader(v: VirtualAudio): Buffer {
    const { block, dataLen, total } = layout(v);
    const b = Buffer.alloc(HEADER);
    b.write('RIFF', 0); b.writeUInt32LE(total - 8, 4); b.write('WAVE', 8);
    b.write('fmt ', 12); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(v.channels, 22);
    b.writeUInt32LE(PCM_RATE, 24); b.writeUInt32LE(PCM_RATE * block, 28); b.writeUInt16LE(block, 32); b.writeUInt16LE(16, 34);
    b.write('data', 36); b.writeUInt32LE(dataLen, 40);
    return b;
}

/** Answers one (range) request for the virtual WAV of `v`. */
export function serveVirtualAudio(req: Request, v: VirtualAudio, ffmpegPath: string): Response {
    const { block, total } = layout(v);
    const m = (req.headers.get('range') || '').match(/bytes=(\d*)-(\d*)/);
    let start = m && m[1] ? Number(m[1]) : 0;
    let end = m && m[2] ? Math.min(Number(m[2]), total - 1) : total - 1;
    if (m && !m[1] && m[2]) { start = Math.max(0, total - Number(m[2])); end = total - 1; }
    if (start > end || start >= total) return new Response(null, { status: 416, headers: { 'Content-Range': `bytes */${total}` } });

    let pos = start;
    let proc: ChildProcess | null = null;
    let done = false;
    const finish = (ctrl: ReadableStreamDefaultController<Uint8Array>) => {
        if (done) return;
        done = true;
        proc?.kill();
        try { ctrl.close(); } catch { /* already closed */ }
    };
    const body = new ReadableStream<Uint8Array>({
        start(ctrl) {
            if (pos < HEADER) {
                const h = wavHeader(v).subarray(pos, Math.min(HEADER, end + 1));
                ctrl.enqueue(h);
                pos += h.length;
            }
            if (pos > end) { finish(ctrl); return; }
            const frame = Math.floor((pos - HEADER) / block);
            let skip = pos - HEADER - frame * block;
            proc = spawn(ffmpegPath, ['-hide_banner', '-loglevel', 'error', '-nostdin',
                ...(frame > 0 ? ['-ss', (frame / PCM_RATE).toFixed(6)] : []), '-i', v.file,
                '-map', `0:${v.stream}`, '-vn', '-sn', '-dn', '-f', 's16le', '-acodec', 'pcm_s16le',
                '-ar', String(PCM_RATE), '-ac', String(v.channels), 'pipe:1'], { windowsHide: true });
            proc.stdout!.on('data', (d: Buffer) => {
                if (done) return;
                if (skip) { const k = Math.min(skip, d.length); d = d.subarray(k); skip -= k; }
                const left = end + 1 - pos;
                if (d.length > left) d = d.subarray(0, left);
                if (d.length) { ctrl.enqueue(d); pos += d.length; }
                if (pos > end) { finish(ctrl); return; }
                // back-pressure: Chromium reads ahead only so far; ffmpeg waits until it pulls again
                if ((ctrl.desiredSize ?? 1) <= 0) proc?.stdout?.pause();
            });
            // the decoded stream can end a few samples before the nominal length: pad with silence
            proc.stdout!.on('end', () => {
                if (done) return;
                let left = end + 1 - pos;
                while (left > 0) { const n = Math.min(left, 1 << 20); ctrl.enqueue(new Uint8Array(n)); left -= n; pos += n; }
                finish(ctrl);
            });
            proc.on('error', e => { if (!done) { done = true; ctrl.error(e); } });
        },
        pull() { proc?.stdout?.resume(); },
        cancel() { done = true; proc?.kill(); }
    }, { highWaterMark: 4 << 20, size: c => c.byteLength });

    return new Response(body, {
        status: m ? 206 : 200,
        headers: {
            'Content-Type': 'audio/wav', 'Accept-Ranges': 'bytes', 'Access-Control-Allow-Origin': '*',
            'Content-Length': String(end - start + 1), ...(m ? { 'Content-Range': `bytes ${start}-${end}/${total}` } : {})
        }
    });
}
