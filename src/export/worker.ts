// Export worker page: decoded source frames in → UVE effect graph on the GPU → encoded frames out.
// v1.0.1: the graph runs on the engine chosen by the facade (WebGPU, or the v1.0.0 WebGL2 engine as fallback).
// v1.0.3: timeline jobs — one decoded stream per clip, per-clip effect graph + look, transition composite.
import type { WorkerApi } from '../../electron/exportPreload';
import type { WorkerPlan } from '../../electron/render';
import { type PlanClip, sampleCurve, secOf, srcTimeAt } from '../../shared/timeline';
import { autoMatrixAt } from '../../shared/uve/auto';
import type { CustomFilterEntry } from '../../shared/uve/customFilters';
import { type FrameSource, type TimelineRenderEngine, MAX_SLOTS, clipLookMatrix, engineFacade, trackGainLinOf } from '../../shared/uve/engine-facade';
import { HoldCursor, type TimelineStream, type TimelineWorkerPlan, frameComposite, windowAt } from '../../shared/uve/export-clip-graph';
import type { UveGraph } from '../../shared/uve/graph';
import { type Mat4x5, matIdentity4x5 } from '../../shared/uve/matrix';
import { createEngine } from '../engine/createEngine';
import { graphFromEffect } from '../engine/pipeline';
import type { FrameEngine } from '../engine/types';

declare global { interface Window { uveWorker: WorkerApi } }

const w = window.uveWorker;
let canceled = false;

w.onCancel(() => { canceled = true; });

w.onJob(p => { void ('mode' in p && p.mode === 'timeline' ? runTimeline(p) : run(p as WorkerPlan)); });

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

/* ------------------------------------------------------------------ timeline jobs */

/** The engine's timeline surface wrapped by the shared facade (same entry points as the preview). */
function exportSurface(engine: FrameEngine): TimelineRenderEngine {
    return engineFacade(engine.asRenderEngine(), 'export');
}

/**
 * Timeline job: for every frame of the plan range, each video layer of its window reads its clip stream
 * (CFR hold law: a stream that ended early repeats its last frame), renders through that clip's own effect
 * graph and look matrix into a layer slot, and the slots are composited with the shared transition law.
 * Gaps without any picture layer are black. Streams are opened at their window and closed after their
 * last frame, so only the decoders of the current and the next window run.
 */
async function runTimeline(tp: TimelineWorkerPlan) {
    let engine: FrameEngine | null = null;
    let te: TimelineRenderEngine | null = null;
    try {
        const plan = tp.plan;
        const F = plan.frameRate;
        const W = tp.width, H = tp.height;
        // custom filter state is engine-wide: the union of every clip look decides the engine and is compiled once
        const customs = new Map<string, CustomFilterEntry>();
        for (const e of Object.values(tp.effects)) for (const f of e.customFilters) customs.set(JSON.stringify(f), f);
        const allCustom = [...customs.values()];
        const choice = await createEngine(null, tp.enginePref || 'auto', allCustom);
        engine = choice.engine;
        w.log(`Filter engine: ${engine.kind} – ${engine.adapter}`);
        w.log(`Engine selection: ${choice.reason}`);
        w.engineInfo(engine.kind, engine.adapter);
        const diags = engine.setCustomFilters(allCustom.filter(f => f.enabled));
        for (const c of diags) if (c.error && engine.kind === 'webgl2') w.log(`Warning: custom filter "${c.entry.label}" is bypassed – ${c.error.split('\n')[0]}`);
        for (const f of allCustom) if (f.enabled && f.type !== 'svg' && f.type !== 'webgl') w.log(`Warning: custom ${f.type} filter "${f.label}" is not supported by the offline renderer and was skipped.`);
        te = exportSurface(engine);
        await te.init();

        // one graph per distinct clip look (clip.effect ?? job effect); identical looks share the graph
        const graphs = new Map<string, UveGraph>();
        const byLook = new Map<string, UveGraph>();
        for (const [clipId, effect] of Object.entries(tp.effects)) {
            const k = JSON.stringify(effect);
            let g = byLook.get(k);
            if (!g) {
                g = graphFromEffect(effect);
                g.warnings.forEach(x => w.log(`Warning (clip ${clipId}): ${x}`));
                byLook.set(k, g);
            }
            graphs.set(clipId, g);
        }
        const clipsById = new Map<string, PlanClip>(plan.clips.map(p => [p.clip.id, p]));
        const streamByKey = new Map<string, TimelineStream>(tp.streams.map(s => [s.key, s]));
        w.log(`Timeline: ${plan.totalFrames} frames, ${W}x${H} at ${tp.fpsNum}/${tp.fpsDen}, ${tp.streams.length} clip stream(s), ${byLook.size} look(s)`);

        const cursors = new Map<string, HoldCursor<Uint8Array>>();
        const open = (key: string): HoldCursor<Uint8Array> => {
            let c = cursors.get(key);
            if (!c) {
                const st = streamByKey.get(key);
                if (!st) throw new Error('Internal error: clip stream missing from the plan.');
                w.openStream(key);
                c = new HoldCursor<Uint8Array>(st.streamFrames, () => w.readStreamFrame(key));
                cursors.set(key, c);
            }
            return c;
        };
        const close = (st: TimelineStream) => {
            const c = cursors.get(st.key);
            if (!c) return;
            w.closeStream(st.key);
            cursors.delete(st.key);
            w.log(`Clip stream [${st.clipIds.join(', ')}]: ${c.decoded} decoded, ${c.held} held (stream ${st.streamFrames} frames)`);
        };
        const prefetch = (index: number) => {
            for (const wi of [index, index + 1]) {
                const win = plan.windows[wi];
                if (!win) continue;
                for (const l of win.video) { const key = tp.streamOfClip[l.clipId]; if (key && !cursors.has(key)) open(key); }
            }
        };

        w.startTimeline();
        const px = W * H * 4;
        const out8 = new Uint8Array(px);
        const out16 = new Uint16Array(px);
        const black = new Uint8Array(px * (tp.out16 ? 2 : 1));
        const looks: Mat4x5[] = Array.from({ length: MAX_SLOTS }, () => matIdentity4x5());
        const autoOf = (p: PlanClip, f: number): Mat4x5 | null => {
            const eff = tp.effects[p.clip.id];
            // the auto-levels keys follow the media actually read: they exist only for the analysed source
            if (!eff?.settings.autoOn || !tp.autoKeys || tp.autoSource !== p.source.path) return null;
            return autoMatrixAt(tp.autoKeys, srcTimeAt(p, f, F));
        };
        let frames = 0, decodeWait = 0, gpu = 0, encodeWait = 0, gaps = 0, warnedDrop = false;
        const t0 = performance.now();
        let lastReport = 0;
        let hint = 0, curWin = -1;
        for (let i = 0; i < plan.totalFrames; i++) {
            if (canceled) break;
            const f = plan.rangeStartF + i;
            const at = windowAt(plan, f, hint);
            hint = at.index;
            if (at.window && at.index !== curWin) { curWin = at.index; prefetch(at.index); }
            let bytes: Uint8Array;
            const fc = at.window && at.window.video.length ? frameComposite(clipsById, at.window, f, W, H) : null;
            if (!fc || !fc.layers.length) {
                bytes = black;
                gaps++;
            } else {
                if (fc.dropped && !warnedDrop) { warnedDrop = true; w.log(`Warning: more than ${MAX_SLOTS} picture layers at frame ${f}; the ${fc.dropped} bottom-most are not drawn.`); }
                for (const { clip, slot } of fc.layers) {
                    const key = tp.streamOfClip[clip.clip.id];
                    const st = streamByKey.get(key);
                    if (!st) throw new Error(`Internal error: clip ${clip.clip.id} has no stream.`);
                    let t = performance.now();
                    const data = await open(key).at(f - st.streamStartF);
                    decodeWait += performance.now() - t;
                    t = performance.now();
                    const src: FrameSource = st.in16
                        ? { kind: 'rgba16', data: new Uint16Array(data.buffer, data.byteOffset, data.byteLength / 2), w: st.width, h: st.height }
                        : { kind: 'rgba8', data, w: st.width, h: st.height };
                    await te.setSource(slot, src);
                    const c = clip.clip;
                    const ts = secOf(f - clip.startF, F);
                    const gainLin = trackGainLinOf(clip.track.gainDb, tp.masterGainDb);
                    await te.renderLayer(slot, {
                        sourceW: st.width, sourceH: st.height,
                        opacity: c.opacity * (sampleCurve(c.opacityCurve, ts) ?? 1),
                        levelLin: c.level * (sampleCurve(c.levelCurve, ts) ?? 1) * gainLin,
                        veil: 0, veilColor: 0,
                        autoMatrix: autoOf(clip, f),
                        clipMatrix: clipLookMatrix(c, clip, f, F, gainLin, looks[slot]),
                        effectGraph: graphs.get(c.id)!,
                        lutHandle: null, hwLutHandle: null, lut3dSize: null,
                        time: f / F, pxScale: 1, quantize8: tp.parityMode
                    });
                    gpu += performance.now() - t;
                }
                const t = performance.now();
                await te.composite({ width: W, height: H, entries: fc.entries, veil: fc.veil, veilColor: fc.veilColor });
                if (tp.out16) { const r = await te.readOutput16(out16); bytes = new Uint8Array(r.buffer, r.byteOffset, r.byteLength); }
                else bytes = await te.readOutput8(out8);
                gpu += performance.now() - t;
            }
            const te0 = performance.now();
            await w.writeFrame(bytes);
            encodeWait += performance.now() - te0;
            frames++;
            for (const st of tp.streams) if (st.lastF <= f) close(st);
            const now = performance.now();
            if (now - lastReport > 300) {
                lastReport = now;
                w.progress(frames, frames / ((now - t0) / 1000), await w.outSize());
            }
        }
        for (const st of tp.streams) close(st);
        if (canceled) { w.kill(); w.done(false, 'canceled'); return; }
        const r = await w.finishTimeline();
        const total = performance.now() - t0;
        w.progress(frames, frames / (total / 1000), await w.outSize());
        const per = (v: number) => (frames ? v / frames : 0).toFixed(2);
        w.log(`Processed ${frames} timeline frames (${gaps} gap frames) in ${(total / 1000).toFixed(1)} s (${(frames / (total / 1000)).toFixed(1)} fps)`);
        w.log(`Stage times per frame [ms]: decode-wait ${per(decodeWait)} · gpu (layers + composite + readback) ${per(gpu)} · encode-wait ${per(encodeWait)}`);
        const mem = (performance as Performance & { memory?: { usedJSHeapSize: number } }).memory;
        w.stats({ frames, totalMs: total, decodeWait, upload: 0, gpu, readback: 0, encodeWait, heapMB: mem ? mem.usedJSHeapSize / 1048576 : 0, engine: engine.kind, adapter: engine.adapter });
        if (r.ok && frames !== plan.totalFrames) { w.done(false, `The timeline produced ${frames} of ${plan.totalFrames} frames.`); return; }
        w.done(r.ok, r.error);
    } catch (e) {
        w.kill();
        if (canceled) { w.done(false, 'canceled'); return; }
        const lost = !!engine && 'lost' in engine && (engine as { lost?: boolean }).lost;
        w.done(false, lost ? 'The GPU device was lost during export (driver reset). Retry the job; set the filter engine to WebGL2 in Settings if it happens again.' : (e instanceof Error ? e.message : String(e)));
    } finally {
        try { te?.destroy(); } catch { /* ignore */ }
        try { engine?.dispose(); } catch { /* ignore */ }
    }
}
