// Legacy backend: wraps the unchanged v1.0.0 EffectEngine (WebGL2) behind the v1.0.1 engine facade.
import type { CustomFilterEntry } from '../../shared/uve/customFilters';
import type { UveGraph } from '../../shared/uve/graph';
import { EffectEngine } from './EffectEngine';
import type { CustomDiagnostic, FrameEngine, ProcessOptions, StageTimes } from './types';

export class Webgl2Engine implements FrameEngine {
    readonly kind = 'webgl2' as const;
    readonly adapter: string;
    readonly times: StageTimes = { upload: 0, process: 0, readback: 0, frames: 0 };
    readonly inner: EffectEngine;

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
    async readScope(which: 'output' | 'source', w: number, h: number) {
        const tex = which === 'output' ? this.inner.lastOutput : this.inner.lastSource;
        if (!tex) return new Uint8Array(w * h * 4);
        return this.inner.readScopeSample(tex, w, h);
    }
    present(cw: number, ch: number, mode: 0 | 1 | 2, split: number) { this.inner.present(cw, ch, mode, split); }
    dispose() { this.inner.dispose(); }
}
