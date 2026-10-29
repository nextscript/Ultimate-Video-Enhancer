// Export worker page: decoded source frames in → UVE effect graph on the GPU → encoded frames out.
import type { WorkerApi } from '../../electron/exportPreload';
import type { WorkerPlan } from '../../electron/render';
import { autoMatrixAt } from '../../shared/uve/auto';
import { EffectEngine } from '../engine/EffectEngine';
import { graphFromEffect } from '../engine/pipeline';

declare global { interface Window { uveWorker: WorkerApi } }

const w = window.uveWorker;
let canceled = false;
w.onCancel(() => { canceled = true; });

w.onJob(plan => { void run(plan); });

async function run(plan: WorkerPlan) {
    try {
        const canvas = document.createElement('canvas');
        canvas.width = 16; canvas.height = 16;
        const gl = canvas.getContext('webgl2', { antialias: false, depth: false, stencil: false, preserveDrawingBuffer: false, powerPreference: 'high-performance' });
        if (!gl) throw new Error('WebGL2 is not available in the export worker (GPU disabled?).');
        canvas.addEventListener('webglcontextlost', () => { canceled = true; w.done(false, 'The GPU context was lost during export (driver reset). Please retry.'); });
        const engine = new EffectEngine(gl);
        const graph = graphFromEffect(plan.effect);
        graph.warnings.forEach(x => w.log('Warning: ' + x));
        engine.setGraph(graph);
        const customs = engine.setCustomFilters(plan.effect.customFilters.filter(f => f.enabled));
        for (const c of customs) if (c.error) w.log(`Warning: custom filter "${c.entry.label}" is bypassed – ${c.error.split('\n')[0]}`);
        for (const f of plan.effect.customFilters) if (f.enabled && f.type !== 'svg' && f.type !== 'webgl') w.log(`Warning: custom ${f.type} filter "${f.label}" is not supported by the offline renderer and was skipped.`);
        w.log(`Graph: ${graph.prims.length} primitives, ${customs.filter(c => !c.error).length} GLSL passes, ${plan.width}x${plan.height}, ${plan.totalFrames} frames`);

        w.start();
        let frames = 0;
        const t0 = performance.now();
        let lastReport = 0;
        let out8: Uint8Array | undefined;
        let out16: Uint16Array | undefined;
        for (;;) {
            if (canceled) break;
            const data = await w.readFrame();
            if (!data) break;
            if (plan.in16) engine.uploadRGBA16(new Uint16Array(data.buffer, data.byteOffset, data.byteLength / 2), plan.width, plan.height);
            else engine.uploadRGBA8(data, plan.width, plan.height);
            const t = plan.start + frames / plan.fps;
            const tex = engine.process(plan.width, plan.height, {
                autoMatrix: plan.effect.settings.autoOn ? autoMatrixAt(plan.autoKeys, t) : null,
                time: t, pxScale: 1, quantize8: plan.parityMode
            });
            if (plan.out16) {
                out16 = engine.readRGBA16(tex, out16);
                await w.writeFrame(new Uint8Array(out16.buffer));
            } else {
                out8 = engine.readRGBA8(tex, out8);
                await w.writeFrame(out8);
            }
            frames++;
            const now = performance.now();
            if (now - lastReport > 300) {
                lastReport = now;
                w.progress(frames, frames / ((now - t0) / 1000), await w.outSize());
            }
        }
        if (canceled) { w.kill(); w.done(false, 'canceled'); return; }
        const r = await w.finish();
        w.progress(frames, frames / ((performance.now() - t0) / 1000), await w.outSize());
        w.log(`Processed ${frames} frames in ${((performance.now() - t0) / 1000).toFixed(1)} s`);
        if (r.ok && frames === 0) { w.done(false, 'The decoder produced no frames.'); return; }
        w.done(r.ok, r.error);
    } catch (e) {
        w.kill();
        w.done(false, e instanceof Error ? e.message : String(e));
    }
}
