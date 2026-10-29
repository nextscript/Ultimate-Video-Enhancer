// Export worker page: decoded source frames in → UVE effect graph on the GPU → encoded frames out.
// v1.0.1: the graph runs on the engine chosen by the facade (WebGPU, or the v1.0.0 WebGL2 engine as fallback).
import type { WorkerApi } from '../../electron/exportPreload';
import type { WorkerPlan } from '../../electron/render';
import { autoMatrixAt } from '../../shared/uve/auto';
import { createEngine } from '../engine/createEngine';
import { graphFromEffect } from '../engine/pipeline';
import type { FrameEngine } from '../engine/types';

declare global { interface Window { uveWorker: WorkerApi } }

const w = window.uveWorker;
let canceled = false;

w.onCancel(() => { canceled = true; });

w.onJob(plan => { void run(plan); });

async function run(plan: WorkerPlan) {
    let engine: FrameEngine | null = null;
    try {
        const choice = await createEngine(null, plan.enginePref || 'auto', plan.effect.customFilters);
        engine = choice.engine;
        w.log(`Filter engine: ${engine.kind} – ${engine.adapter}`);
        w.log(`Engine selection: ${choice.reason}`);
        w.engineInfo(engine.kind, engine.adapter);
        const graph = graphFromEffect(plan.effect);
        graph.warnings.forEach(x => w.log('Warning: ' + x));
        engine.setGraph(graph);
        const customs = engine.setCustomFilters(plan.effect.customFilters.filter(f => f.enabled));
        for (const c of customs) if (c.error && engine.kind === 'webgl2') w.log(`Warning: custom filter "${c.entry.label}" is bypassed – ${c.error.split('\n')[0]}`);
        for (const f of plan.effect.customFilters) if (f.enabled && f.type !== 'svg' && f.type !== 'webgl') w.log(`Warning: custom ${f.type} filter "${f.label}" is not supported by the offline renderer and was skipped.`);
        w.log(`Graph: ${graph.prims.length} primitives, ${plan.width}x${plan.height}, ${plan.totalFrames} frames`);

        w.start();
        let frames = 0, decodeWait = 0, encodeWait = 0;
        const t0 = performance.now();
        let lastReport = 0;
        // two alternating output buffers: the readback of frame N overlaps upload/processing of frame N+1
        const px = plan.width * plan.height * 4;
        const outBufs = plan.out16 ? [new Uint16Array(px), new Uint16Array(px)] : [new Uint8Array(px), new Uint8Array(px)];
        let slot = 0;
        let pending: Promise<Uint8Array> | null = null;
        const startRead = (): Promise<Uint8Array> => {
            const b = outBufs[slot];
            slot ^= 1;
            if (b instanceof Uint16Array) return engine!.readOutput16(b).then(r => new Uint8Array(r.buffer, r.byteOffset, r.byteLength));
            return engine!.readOutput8(b);
        };
        let processed = 0;
        for (;;) {
            if (canceled) break;
            let t = performance.now();
            const data = await w.readFrame();
            decodeWait += performance.now() - t;
            let next: Promise<Uint8Array> | null = null;
            if (data) {
                if (plan.in16) engine.uploadRGBA16(new Uint16Array(data.buffer, data.byteOffset, data.byteLength / 2), plan.width, plan.height);
                else engine.uploadRGBA8(data, plan.width, plan.height);
                const time = plan.start + processed / plan.fps;
                engine.process(plan.width, plan.height, {
                    autoMatrix: plan.effect.settings.autoOn ? autoMatrixAt(plan.autoKeys, time) : null,
                    time, pxScale: 1, quantize8: plan.parityMode
                });
                processed++;
                next = startRead();
            }
            if (pending) {
                const bytes = await pending;
                t = performance.now();
                await w.writeFrame(bytes);
                encodeWait += performance.now() - t;
                frames++;
            }
            pending = next;
            const now = performance.now();
            if (now - lastReport > 300) {
                lastReport = now;
                w.progress(frames, frames / ((now - t0) / 1000), await w.outSize());
            }
            if (!data && !pending) break;
        }
        if (canceled) { w.kill(); w.done(false, 'canceled'); return; }
        const r = await w.finish();
        const total = performance.now() - t0;
        w.progress(frames, frames / (total / 1000), await w.outSize());
        const per = (v: number) => (frames ? v / frames : 0).toFixed(2);
        const tm = engine.times;
        w.log(`Processed ${frames} frames in ${(total / 1000).toFixed(1)} s (${(frames / (total / 1000)).toFixed(1)} fps)`);
        w.log(`Decoder delivery: ${w.decoderFps().toFixed(1)} fps into the worker`);
        w.log(`Stage times per frame [ms]: decode-wait ${per(decodeWait)} · upload ${per(tm.upload)} · gpu-filter ${per(tm.process)} · readback ${per(tm.readback)} · encode-wait ${per(encodeWait)}`);
        const mem = (performance as Performance & { memory?: { usedJSHeapSize: number } }).memory;
        w.stats({ frames, totalMs: total, decodeWait, upload: tm.upload, gpu: tm.process, readback: tm.readback, encodeWait, heapMB: mem ? mem.usedJSHeapSize / 1048576 : 0, engine: engine.kind, adapter: engine.adapter });
        if (r.ok && frames === 0) { w.done(false, 'The decoder produced no frames.'); return; }
        w.done(r.ok, r.error);
    } catch (e) {
        w.kill();
        const lost = !!engine && 'lost' in engine && (engine as { lost?: boolean }).lost;
        w.done(false, lost ? 'The GPU device was lost during export (driver reset). Retry the job; set the filter engine to WebGL2 in Settings if it happens again.' : (e instanceof Error ? e.message : String(e)));
    } finally {
        try { engine?.dispose(); } catch { /* ignore */ }
    }
}
