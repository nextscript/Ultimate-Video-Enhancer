// Preload of the hidden export worker window. Owns the FFmpeg processes of one job
// (decoder(s) → raw frames, raw frames → encoder) whose arguments were built by the main process.
// The worker page only receives frames and returns processed frames; it cannot choose commands.
// Timeline jobs (v1.0.3): one decoder per clip stream of the plan, addressed by its stream key.
import { type ChildProcessWithoutNullStreams, spawn } from 'node:child_process';
import { promises as fsp } from 'node:fs';
import { contextBridge, ipcRenderer } from 'electron';
import type { TimelineWorkerPlan, WorkerJob, WorkerPlan } from './render';

let plan: WorkerPlan | null = null;
let tplan: TimelineWorkerPlan | null = null;
let jobId = '';
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

function log(line: string) { if (jobId) ipcRenderer.send('worker:log', { jobId, line }); }

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

/* ------------------------------------------------------------------ timeline streams */

/** One decoder of a timeline job: raw frames of one clip stream, with the same back-pressure as the single path. */
interface ClipStream {
    key: string;
    label: string;
    frameBytes: number;
    proc: ChildProcessWithoutNullStreams | null;
    queue: Buffer[];
    pending: Buffer[];
    pendingLen: number;
    produced: number;
    ended: boolean;
    /** closed by the worker (or the job end): its exit code no longer matters */
    closed: boolean;
    error: string;
    stderr: string;
    waiter: (() => void) | null;
}
const streams = new Map<string, ClipStream>();
const streamErrors: string[] = [];

function wakeStream(st: ClipStream) { const w = st.waiter; st.waiter = null; w?.(); }

function startStream(st: ClipStream, args: string[]) {
    const d = spawn(tplan!.ffmpeg, args, { windowsHide: true });
    st.proc = d;
    let ended = false;
    d.stdout.on('data', (chunk: Buffer) => {
        if (st.closed) return;
        st.pending.push(chunk);
        st.pendingLen += chunk.length;
        while (st.pendingLen >= st.frameBytes) {
            const all = st.pending.length === 1 ? st.pending[0] : Buffer.concat(st.pending, st.pendingLen);
            st.queue.push(Buffer.from(all.subarray(0, st.frameBytes)));
            st.produced++;
            diag.produced++; diag.lastAt = performance.now(); if (!diag.firstAt) diag.firstAt = diag.lastAt;
            const rest = all.subarray(st.frameBytes);
            st.pending = rest.length ? [rest] : [];
            st.pendingLen = rest.length;
        }
        if (st.queue.length >= MAX_QUEUE) d.stdout.pause();
        wakeStream(st);
    });
    d.stderr.on('data', x => { st.stderr += x; String(x).split(/\r?\n/).filter(Boolean).forEach(l => log(`[decode ${st.label}] ` + l)); });
    const end = (code: number) => {
        if (ended || st.proc !== d) return;
        ended = true;
        // same rule as the single path: a hardware decoder that dies before its first frame is retried in software
        if (code !== 0 && !canceled && !st.closed && st.produced === 0 && st.pendingLen === 0 && args.includes('-hwaccel')) {
            log(`[decode ${st.label}] hardware decoding failed (exit ${code}) – retrying with software decoding`);
            st.stderr = '';
            startStream(st, withoutHwaccel(args));
            return;
        }
        if (code !== 0 && !canceled && !st.closed) {
            st.error = `Decoder failed for clip ${st.label} (${code}): ${st.stderr.trim().split('\n').slice(-3).join(' ')}`;
            streamErrors.push(st.error);
        }
        st.ended = true;
        wakeStream(st);
    };
    d.on('error', e => { st.stderr += e.message; end(1); });
    d.on('close', c => end(c ?? 1));
}

function closeStream(key: string) {
    const st = streams.get(key);
    if (!st || st.closed) return;
    st.closed = true;
    st.queue.length = 0;
    st.pending = [];
    st.pendingLen = 0;
    try { if (st.proc && st.proc.exitCode === null) st.proc.kill(); } catch { /* ignore */ }
    wakeStream(st);
}

function startEncoder(args: string[], ffmpeg: string) {
    enc = spawn(ffmpeg, args, { windowsHide: true });
    enc.stderr.on('data', d => { encError += d; String(d).split(/\r?\n/).filter(Boolean).forEach(l => log('[encode] ' + l)); });
    enc.on('error', e => { encError += e.message; });
    enc.stdin.on('error', e => { encError += 'stdin: ' + e.message; encClosed = true; });
    enc.on('exit', () => { encClosed = true; });
}

const api = {
    /** `worker:job` delivers either the single-media plan or a timeline plan (`mode: 'timeline'`). */
    onJob(cb: (p: WorkerJob) => void) {
        ipcRenderer.on('worker:job', (_e, p: WorkerJob) => {
            jobId = p.jobId;
            if ('mode' in p && p.mode === 'timeline') {
                tplan = p;
            } else {
                plan = p as WorkerPlan;
                frameBytes = plan.width * plan.height * 4 * (plan.in16 ? 2 : 1);
            }
            cb(p);
        });
    },
    onCancel(cb: () => void) { ipcRenderer.on('worker:cancel', () => { canceled = true; api.kill(); cb(); }); },
    start(): void {
        if (!plan) throw new Error('no job');
        startDecoder(plan.decodeArgs);
        startEncoder(plan.encodeArgs, plan.ffmpeg);
    },
    /** Timeline job: starts the encoder only; decoders start per stream with `openStream`. */
    startTimeline(): void {
        if (!tplan) throw new Error('no timeline job');
        startEncoder(tplan.encodeArgs, tplan.ffmpeg);
    },
    /** Starts the decoder of one plan stream (idempotent). Its arguments come from the main process. */
    openStream(key: string): void {
        if (!tplan) throw new Error('no timeline job');
        if (streams.has(key) || canceled) return;
        const spec = tplan.streams.find(x => x.key === key);
        if (!spec) throw new Error('Unknown timeline stream.');
        const st: ClipStream = {
            key, label: spec.clipIds.join('+'), frameBytes: spec.width * spec.height * 4 * (spec.in16 ? 2 : 1),
            proc: null, queue: [], pending: [], pendingLen: 0, produced: 0, ended: false, closed: false, error: '', stderr: '', waiter: null
        };
        streams.set(key, st);
        startStream(st, spec.args);
    },
    /** Next frame of a stream, or null at its end (the worker then holds the last frame). */
    async readStreamFrame(key: string): Promise<Uint8Array | null> {
        const st = streams.get(key);
        if (!st) throw new Error('Timeline stream was not opened.');
        for (;;) {
            if (canceled || st.closed) return null;
            const f = st.queue.shift();
            if (f) {
                if (st.queue.length < MAX_QUEUE / 2) st.proc?.stdout.resume();
                return new Uint8Array(f.buffer, f.byteOffset, f.byteLength);
            }
            if (st.ended) {
                if (st.error) throw new Error(st.error);
                return null;
            }
            await new Promise<void>(r => { st.waiter = r; });
        }
    },
    /** The worker is done with a stream: stop its decoder and free its frames. */
    closeStream(key: string): void { closeStream(key); },
    async finishTimeline(): Promise<{ ok: boolean; error?: string }> {
        if (!enc) return { ok: false, error: 'not started' };
        const e = enc;
        const encExit = new Promise<number>(r => { if (e.exitCode !== null) r(e.exitCode); else e.once('exit', c => r(c ?? 1)); });
        e.stdin.end();
        for (const key of streams.keys()) closeStream(key);
        const ec = await encExit;
        if (canceled) return { ok: false, error: 'canceled' };
        if (streamErrors.length) return { ok: false, error: streamErrors[0] };
        if (ec !== 0) return { ok: false, error: `Encoder failed (${ec}): ${encError.trim().split('\n').slice(-3).join(' ')}` };
        return { ok: true };
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
        for (const key of streams.keys()) closeStream(key);
        try { enc?.stdin.destroy(); enc?.kill(); } catch { /* ignore */ }
        wake();
    },
    async outSize(): Promise<number> {
        const args = plan?.encodeArgs ?? tplan?.encodeArgs;
        if (!args) return 0;
        const m = args[args.length - 1];
        try { return (await fsp.stat(m)).size; } catch { return 0; }
    },
    engineInfo(kind: string, adapter: string) { if (jobId) ipcRenderer.send('worker:engine', { jobId, kind, adapter }); },
    stats(stats: Record<string, unknown>) { if (jobId) ipcRenderer.send('worker:stats', { jobId, stats }); },
    decoderFps() { const span = (diag.lastAt - diag.firstAt) / 1000; return span > 0 ? diag.produced / span : 0; },
    progress(frames: number, fps: number, outSize: number) { if (jobId) ipcRenderer.send('worker:progress', { jobId, frames, fps, outSize }); },
    log,
    done(ok: boolean, error?: string) { if (jobId) ipcRenderer.send('worker:done', { jobId, ok, error }); }
};

contextBridge.exposeInMainWorld('uveWorker', api);
export type WorkerApi = typeof api;
