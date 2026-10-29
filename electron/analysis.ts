// Auto-Scene-Match pre-analysis: decodes the clip once at 96x54 on a fixed 10 fps media-time grid
// and stores the userscript frame statistics + motion luma. Cached on disk per file.
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fsp } from 'node:fs';
import path from 'node:path';
import type { AnalysisResult, MediaInfo } from '../shared/types';
import { ANALYSIS_FPS, ANALYSIS_H, ANALYSIS_W, computeFrameStats, motionLuma } from '../shared/uve/auto';
import { decodeFilter } from './media';
import { cacheDir } from './store';
import { capabilities } from './tools';

const LUMA_LEN = Math.ceil(ANALYSIS_W / 2) * Math.ceil(ANALYSIS_H / 2);
const running = new Map<string, { promise: Promise<AnalysisResult>; kill: () => void }>();

export function analysisKey(media: MediaInfo) {
    return createHash('sha1').update(`v2|${media.path}|${media.size}|${media.mtimeMs}|${media.video?.index}`).digest('hex').slice(0, 20);
}

async function loadCached(key: string): Promise<AnalysisResult | null> {
    try {
        const buf = await fsp.readFile(path.join(cacheDir(), 'analysis', `${key}.bin`));
        const count = buf.readUInt32LE(0);
        const t0 = buf.readDoubleLE(4);
        const statsBytes = count * 8 * 4;
        const stats = new Float32Array(buf.buffer.slice(buf.byteOffset + 12, buf.byteOffset + 12 + statsBytes));
        const luma = new Uint8Array(buf.buffer.slice(buf.byteOffset + 12 + statsBytes, buf.byteOffset + 12 + statsBytes + count * LUMA_LEN));
        if (luma.length !== count * LUMA_LEN) return null;
        return { key, t0, count, stats, luma };
    } catch { return null; }
}

async function saveCached(r: AnalysisResult) {
    const dir = path.join(cacheDir(), 'analysis');
    await fsp.mkdir(dir, { recursive: true });
    const head = Buffer.alloc(12);
    head.writeUInt32LE(r.count, 0);
    head.writeDoubleLE(r.t0, 4);
    await fsp.writeFile(path.join(dir, `${r.key}.bin`), Buffer.concat([head, Buffer.from(r.stats.buffer), Buffer.from(r.luma.buffer)]));
}

export function cancelAnalysis(key: string) { running.get(key)?.kill(); }

export function analyze(media: MediaInfo, onProgress: (pct: number) => void): Promise<AnalysisResult> {
    const key = analysisKey(media);
    const cur = running.get(key);
    if (cur) return cur.promise;
    let killFn = () => { /* set below */ };
    const promise = (async () => {
        const cached = await loadCached(key);
        if (cached) { onProgress(1); return cached; }
        const caps = await capabilities();
        if (!caps.ok || !media.video) throw new Error(caps.error || 'No video stream');
        const vf = await decodeFilter(media.video, ANALYSIS_W, ANALYSIS_H, 'rgb24', `fps=${ANALYSIS_FPS}:round=down`);
        const frameBytes = ANALYSIS_W * ANALYSIS_H * 3;
        const statsList: number[] = [];
        const lumaChunks: Uint8Array[] = [];
        await new Promise<void>((resolve, reject) => {
            const p = spawn(caps.ffmpegPath, ['-hide_banner', '-loglevel', 'error', '-i', media.path, '-map', `0:${media.video!.index}`,
                '-vf', vf, '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'], { windowsHide: true });
            killFn = () => p.kill();
            let pending: Buffer = Buffer.alloc(0);
            let err = '';
            const expected = Math.max(1, Math.ceil(media.duration * ANALYSIS_FPS));
            p.stdout.on('data', (d: Buffer) => {
                pending = pending.length ? Buffer.concat([pending, d]) : d;
                while (pending.length >= frameBytes) {
                    const frame = new Uint8Array(pending.buffer, pending.byteOffset, frameBytes);
                    const s = computeFrameStats(frame);
                    statsList.push(s.mR, s.mG, s.mB, s.mY, s.sdY, s.mCh, s.mSat, s.skin);
                    lumaChunks.push(motionLuma(frame));
                    pending = pending.subarray(frameBytes);
                }
                onProgress(Math.min(0.999, lumaChunks.length / expected));
            });
            p.stderr.on('data', d => { err += d; });
            p.on('error', reject);
            p.on('exit', code => code === 0 ? resolve() : reject(new Error(err.trim().split('\n').slice(-2).join(' ') || `analysis aborted (${code})`)));
        });
        const count = lumaChunks.length;
        const luma = new Uint8Array(count * LUMA_LEN);
        lumaChunks.forEach((c, i) => luma.set(c, i * LUMA_LEN));
        const res: AnalysisResult = { key, t0: 0, count, stats: new Float32Array(statsList), luma };
        await saveCached(res).catch(() => undefined);
        onProgress(1);
        return res;
    })();
    running.set(key, { promise, kill: () => killFn() });
    promise.finally(() => running.delete(key)).catch(() => undefined);
    return promise;
}
