// Legacy backend: wraps the unchanged v1.0.0 EffectEngine (WebGL2) behind the v1.0.1 engine facade.
import type { CustomFilterEntry } from '../../shared/uve/customFilters';
import type { CompositeOptions, EngineSlot, FrameSource, LayerRenderOptions, RenderEngine } from '../../shared/uve/engine-facade';
import type { UveGraph } from '../../shared/uve/graph';
import { EffectEngine } from './EffectEngine';
import type { CustomDiagnostic, FrameEngine, ProcessOptions, StageTimes } from './types';

export class Webgl2Engine implements FrameEngine {
    readonly kind = 'webgl2' as const;
    readonly adapter: string;
    readonly times: StageTimes = { upload: 0, process: 0, readback: 0, frames: 0 };
    readonly inner: EffectEngine;
    private renderSurface: RenderEngine | null = null;

    constructor(gl: WebGL2RenderingContext) {
        this.inner = new EffectEngine(gl);
        const dbg = gl.getExtension('WEBGL_debug_renderer_info');
        this.adapter = `WebGL2 · ${dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER)}`;
    }

    uploadVideo(video: HTMLVideoElement) { const t = performance.now(); this.inner.uploadVideo(video); this.times.upload += performance.now() - t; }
    uploadRGBA8(d: Uint8Array, w: number, h: number) { const t = performance.now(); this.inner.uploadRGBA8(d, w, h); this.times.upload += performance.now() - t; }
    uploadRGBA16(d: Uint16Array, w: number, h: number) { const t = performance.now(); this.inner.uploadRGBA16(d, w, h); this.times.upload += performance.now() - t; }
    setGraph(g: UveGraph) { this.inner.setGraph(g); }
    setCustomFilters(entries: CustomFilterEntry[]): CustomDiagnostic[] {
        return this.inner.setCustomFilters(entries).map(c => ({ entry: c.entry, error: c.error }));
    }
    updateCustomUniforms(entries: CustomFilterEntry[]) {
        for (const c of this.inner.customDiagnostics) {
            const cur = entries.find(f => f.id === c.entry.id);
            if (cur) c.entry = cur;
        }
    }
    process(w: number, h: number, opts: ProcessOptions) {
        const t = performance.now();
        this.inner.process(w, h, opts);
        this.times.process += performance.now() - t;
        this.times.frames++;
    }
    get hasFrame() { return !!this.inner.lastOutput && !!this.inner.lastSource; }
    get outputSize() { const o = this.inner.lastOutput; return { w: o?.w ?? 0, h: o?.h ?? 0 }; }
    async readOutput8(into?: Uint8Array) {
        const t = performance.now();
        const r = this.inner.readRGBA8(this.inner.lastOutput!, into);
        this.times.readback += performance.now() - t;
        return r;
    }
    async readOutput16(into?: Uint16Array) {
        const t = performance.now();
        const r = this.inner.readRGBA16(this.inner.lastOutput!, into);
        this.times.readback += performance.now() - t;
        return r;
    }
    async readScope(which: 'output' | 'source' | 'timeline', w: number, h: number) {
        const tex = which === 'output' ? this.inner.lastOutput : which === 'timeline' ? this.inner.timelineOutput : this.inner.lastSource;
        if (!tex) return new Uint8Array(w * h * 4);
        return this.inner.readScopeSample(tex, w, h);
    }
    present(cw: number, ch: number, mode: 0 | 1 | 2, split: number) { this.inner.present(cw, ch, mode, split); }
    dispose() { this.inner.dispose(); }

    // ------------------------------------------------------------------ timeline (NLE) rendering
    get timelineReady() { return this.inner.timelineReady; }
    beginTimeline(mode: EngineSlot) { this.inner.beginTimeline(mode); }
    endTimeline() { this.inner.endTimeline(); }
    setSource(slot: number, src: FrameSource) { const t = performance.now(); this.inner.setSource(slot, src); this.times.upload += performance.now() - t; }
    renderLayer(slot: number, opts: LayerRenderOptions) { const t = performance.now(); this.inner.renderLayer(slot, opts); this.times.process += performance.now() - t; }
    composite(opts: CompositeOptions) {
        const t = performance.now();
        this.inner.composite(opts);
        this.times.process += performance.now() - t;
        this.times.frames++;
    }
    uploadLut(rgba: Float32Array, size: number) { return this.inner.uploadLut(rgba, size); }
    uploadLut3D(rgb: Float32Array, size: number) { return this.inner.uploadLut3D(rgb, size); }
    readTimeline8(into?: Uint8Array): Uint8Array {
        const o = this.inner.timelineOutput;
        if (!o) throw new Error('No composited frame to read (call composite first).');
        const t = performance.now();
        const r = this.inner.readRGBA8(o, into);
        this.times.readback += performance.now() - t;
        return r;
    }
    readTimeline16(into?: Uint16Array): Uint16Array {
        const o = this.inner.timelineOutput;
        if (!o) throw new Error('No composited frame to read (call composite first).');
        const t = performance.now();
        const r = this.inner.readRGBA16(o, into);
        this.times.readback += performance.now() - t;
        return r;
    }
    asRenderEngine(): RenderEngine {
        if (!this.renderSurface) {
            const self = this;
            this.renderSurface = {
                beginTimeline: mode => self.beginTimeline(mode),
                endTimeline: () => self.endTimeline(),
                get timelineReady() { return self.timelineReady; },
                setSource: (slot, src) => self.setSource(slot, src),
                renderLayer: (slot, o) => self.renderLayer(slot, o),
                composite: o => self.composite(o),
                uploadLut: (rgba, size) => self.uploadLut(rgba, size),
                uploadLut3D: (rgb, size) => self.uploadLut3D(rgb, size),
                readOutput8: into => self.readTimeline8(into),
                readOutput16: into => self.readTimeline16(into)
            };
        }
        return this.renderSurface;
    }
}
