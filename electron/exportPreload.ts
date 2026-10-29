// Preload of the hidden export worker window. Owns the two FFmpeg processes of one job
// (decoder → raw frames, raw frames → encoder) whose arguments were built by the main process.
// The worker page only receives frames and returns processed frames; it cannot choose commands.
import { type ChildProcessWithoutNullStreams, spawn } from 'node:child_process';
import { promises as fsp } from 'node:fs';
import { contextBridge, ipcRenderer } from 'electron';
import type { WorkerPlan } from './render';

let plan: WorkerPlan | null = null;
let dec: ChildProcessWithoutNullStreams | null = null;
let enc: ChildProcessWithoutNullStreams | null = null;
let frameBytes = 0;
const queue: Buffer[] = [];
let pending: Buffer[] = [];
let pendingLen = 0;
let decEnded = false;
let decError = '';
let encError = '';
let waiter: (() => void) | null = null;
let canceled = false;
let encClosed = false;
const MAX_QUEUE = 4;
/** v1.0.1 diagnostics: decoder delivery rate into the worker */
const diag = { produced: 0, firstAt: 0, lastAt: 0 };

function log(line: string) { if (plan) ipcRenderer.send('worker:log', { jobId: plan.jobId, line }); }

function wake() { const w = waiter; waiter = null; w?.(); }

function onDecData(chunk: Buffer) {
    pending.push(chunk);
    pendingLen += chunk.length;
    while (pendingLen >= frameBytes) {
        const all = pending.length === 1 ? pending[0] : Buffer.concat(pending, pendingLen);
        queue.push(Buffer.from(all.subarray(0, frameBytes)));
        diag.produced++; diag.lastAt = performance.now(); if (!diag.firstAt) diag.firstAt = diag.lastAt;
        const rest = all.subarray(frameBytes);
        pending = rest.length ? [rest] : [];
        pendingLen = rest.length;
    }
    if (queue.length >= MAX_QUEUE) dec?.stdout.pause();
    wake();
}

/** v1.0.2: decoder exit code once its process and pipes are closed */
let decCode: number | null = null;
let decDone: (() => void) | null = null;
const decFinished = new Promise<void>(r => { decDone = r; });

/** The same decode without -hwaccel (and its value), i.e. in software. */
function withoutHwaccel(args: string[]): string[] {
    const i = args.indexOf('-hwaccel');
    return i < 0 ? args : [...args.slice(0, i), ...args.slice(i + 2)];
}

function startDecoder(args: string[]) {
    const d = spawn(plan!.ffmpeg, args, { windowsHide: true });
    dec = d;
    let ended = false;
    d.stdout.on('data', onDecData);
    d.stderr.on('data', x => { decError += x; String(x).split(/\r?\n/).filter(Boolean).forEach(l => log('[decode] ' + l)); });
    const end = (code: number) => {
        if (ended || dec !== d) return;
        ended = true;
        // v1.0.2: a hardware decoder that dies before delivering any frame (e.g. "Invalid argument" with
        // qsv/d3d11va on Windows 11 laptops) is retried once in software instead of failing the export
        if (code !== 0 && !canceled && diag.produced === 0 && pendingLen === 0 && args.includes('-hwaccel')) {
            log(`[decode] hardware decoding failed (exit ${code}) – retrying with software decoding`);
            decError = '';
            startDecoder(withoutHwaccel(args));
            return;
        }
        decCode = code; decEnded = true; wake(); decDone?.();
    };
    d.on('error', e => { decError += e.message; end(1); });
    d.on('close', c => end(c ?? 1));
}

const api = {
    onJob(cb: (p: WorkerPlan) => void) {
        ipcRenderer.on('worker:job', (_e, p: WorkerPlan) => {
            plan = p;
            frameBytes = p.width * p.height * 4 * (p.in16 ? 2 : 1);
            cb(p);
        });
    },
    onCancel(cb: () => void) { ipcRenderer.on('worker:cancel', () => { canceled = true; api.kill(); cb(); }); },
    start(): void {
        if (!plan) throw new Error('no job');
        startDecoder(plan.decodeArgs);
        enc = spawn(plan.ffmpeg, plan.encodeArgs, { windowsHide: true });
        enc.stderr.on('data', d => { encError += d; String(d).split(/\r?\n/).filter(Boolean).forEach(l => log('[encode] ' + l)); });
        enc.on('error', e => { encError += e.message; });
        enc.stdin.on('error', e => { encError += 'stdin: ' + e.message; encClosed = true; });
        enc.on('exit', () => { encClosed = true; });
    },
    /** Next decoded frame, or null at the end of the stream. */
    async readFrame(): Promise<Uint8Array | null> {
        for (;;) {
            if (canceled) return null;
            const f = queue.shift();
            if (f) {
                if (queue.length < MAX_QUEUE / 2) dec?.stdout.resume();
                return new Uint8Array(f.buffer, f.byteOffset, f.byteLength);
            }
            if (decEnded) return null;
            await new Promise<void>(r => { waiter = r; });
        }
    },
    async writeFrame(data: Uint8Array): Promise<void> {
        if (!enc || canceled) return;
        // v1.0.1: a dead encoder used to leave the worker waiting for 'drain' forever
        const fail = () => new Error(`Encoder stopped: ${encError.trim().split('\n').slice(-3).join(' ') || `exit ${enc!.exitCode}`}`);
        if (encClosed || enc.stdin.destroyed) throw fail();
        const ok = enc.stdin.write(Buffer.from(data.buffer, data.byteOffset, data.byteLength));
        if (!ok) await new Promise<void>((resolve, reject) => {
            const e = enc!;
            const done = () => { e.stdin.off('drain', done); e.off('exit', dead); e.stdin.off('close', dead); resolve(); };
            const dead = () => { e.stdin.off('drain', done); e.off('exit', dead); e.stdin.off('close', dead); reject(fail()); };
            e.stdin.once('drain', done); e.once('exit', dead); e.stdin.once('close', dead);
        });
    },
    async finish(): Promise<{ ok: boolean; error?: string }> {
        if (!enc || !dec) return { ok: false, error: 'not started' };
        const decExit = decFinished.then(() => decCode ?? 1);
        const encExit = new Promise<number>(r => { if (enc!.exitCode !== null) r(enc!.exitCode); else enc!.once('exit', c => r(c ?? 1)); });
        enc.stdin.end();
        const [dc, ec] = await Promise.all([decExit, encExit]);
        if (canceled) return { ok: false, error: 'canceled' };
        if (dc !== 0) return { ok: false, error: `Decoder failed (${dc}): ${decError.trim().split('\n').slice(-3).join(' ')}` };
        if (ec !== 0) return { ok: false, error: `Encoder failed (${ec}): ${encError.trim().split('\n').slice(-3).join(' ')}` };
        return { ok: true };
    },
    kill() {
        try { dec?.kill(); } catch { /* ignore */ }
        try { enc?.stdin.destroy(); enc?.kill(); } catch { /* ignore */ }
        wake();
    },
    async outSize(): Promise<number> {
        if (!plan) return 0;
        const m = plan.encodeArgs[plan.encodeArgs.length - 1];
        try { return (await fsp.stat(m)).size; } catch { return 0; }
    },
    engineInfo(kind: string, adapter: string) { if (plan) ipcRenderer.send('worker:engine', { jobId: plan.jobId, kind, adapter }); },
    stats(stats: Record<string, unknown>) { if (plan) ipcRenderer.send('worker:stats', { jobId: plan.jobId, stats }); },
    decoderFps() { const span = (diag.lastAt - diag.firstAt) / 1000; return span > 0 ? diag.produced / span : 0; },
    progress(frames: number, fps: number, outSize: number) { if (plan) ipcRenderer.send('worker:progress', { jobId: plan.jobId, frames, fps, outSize }); },
    log,
    done(ok: boolean, error?: string) { if (plan) ipcRenderer.send('worker:done', { jobId: plan.jobId, ok, error }); }
};

contextBridge.exposeInMainWorld('uveWorker', api);
export type WorkerApi = typeof api;
