// Timeline (program monitor) playback: plays the whole timeline from `buildPlan(tl)` with one hidden
// media element per active clip, renders every frame through the timeline render facade
// (`engineFacade(engine, 'preview')` → setSource / renderLayer / composite) and mixes the audio with
// `AudioMixer`. Every law comes from shared code: plan windows + z order (`buildPlan`), media time
// (`srcTimeAt` + the CFR hold law), clip look (`clipLookMatrix`), transitions (`transitionUniforms`
// of `transitionProgress`) and the audio gain law (`clipAudioGain`, mirror of `buildAudioGraph`).
import type { EffectState, Timeline } from '../../shared/types';
import { buildPlan, frameOf, sampleCurve, secOf, srcTimeAt, timelineDuration, type PlanClip, type TimelinePlan } from '../../shared/timeline';
import { autoMatrixAt } from '../../shared/uve/auto';
import {
    clipLookMatrix, engineFacade, trackGainLinOf, type RenderEngine, type TimelineRenderEngine
} from '../../shared/uve/engine-facade';
import type { UveGraph } from '../../shared/uve/graph';
import type { Mat4x5 } from '../../shared/uve/matrix';
import type { FrameEngine } from '../engine/types';
import { graphFromEffect } from '../engine/pipeline';
import { getState, setState } from '../state/store';
import { AudioMixer } from './audioMixer';
import { frameComposite, heldSourceTime } from './timelineMath';

/**
 * The timeline render surface of a preview engine (`FrameEngine.asRenderEngine()`), or null while an
 * engine build does not provide it yet (the monitor then falls back to the single-layer path).
 */
export function renderEngineOf(engine: FrameEngine | null): RenderEngine | null {
    if (!engine || typeof (engine as Partial<FrameEngine>).asRenderEngine !== 'function') return null;
    try { return engine.asRenderEngine(); } catch { return null; }
}

export interface PlaybackHost {
    engine: () => FrameEngine | null;
    canvas: () => HTMLCanvasElement | null;
    quality: () => number;
    parity: () => boolean;
    /** true when no picture layer is active at the playhead (gap: the monitor shows black) */
    onGap: (gap: boolean) => void;
    onError: (msg: string | null) => void;
    /** after each drawn frame: the video element of the top picture layer (the "Original" side of the wipe) */
    onFrame?: (top: HTMLVideoElement) => void;
}

const PREROLL_SEC = 1;
const graphCache = new WeakMap<EffectState, UveGraph>();
const graphOf = (e: EffectState): UveGraph => {
    let g = graphCache.get(e);
    if (!g) { g = graphFromEffect(e); graphCache.set(e, g); }
    return g;
};

export class TimelinePlayback {
    private host: PlaybackHost;
    private els = new Map<string, HTMLVideoElement>();
    private holder: HTMLDivElement;
    private mixer = new AudioMixer();
    private planFor: Timeline | null = null;
    private plan: TimelinePlan | null = null;
    private facade: TimelineRenderEngine | null = null;
    private facadeOf: RenderEngine | null = null;
    private fallbackGraph: UveGraph | null = null;
    private matrices: Mat4x5[] = [0, 1, 2, 3].map(() => new Array(20).fill(0));
    private raf = 0;
    private renderRaf = 0;
    private busy = false;
    private lastScope = 0;
    private scopeBusy = false;
    private scopeCanvas: HTMLCanvasElement | null = null;
    private lastUi = 0;
    private gap = false;
    playing = false;
    rate = 1;
    /** timeline position in seconds */
    t = 0;
    private clock = { wall: 0, t: 0 };

    constructor(host: PlaybackHost) {
        this.host = host;
        this.holder = document.createElement('div');
        this.holder.style.cssText = 'position:absolute;width:2px;height:2px;opacity:0;pointer-events:none;overflow:hidden;left:0;top:0';
        document.body.appendChild(this.holder);
    }

    get duration(): number { return timelineDuration(getState().timeline); }

    private currentPlan(): TimelinePlan {
        const tl = getState().timeline;
        if (tl !== this.planFor || !this.plan) { this.plan = buildPlan(tl); this.planFor = tl; this.cleanup(); }
        return this.plan;
    }

    private frameAt(t: number): number { return Math.floor(t * getState().timeline.frameRate + 1e-6); }

    setMonitor(volume: number, muted: boolean) { this.mixer.setMonitor(volume, muted); }

    play() {
        if (this.playing) return;
        if (this.t >= this.duration - 1e-6) this.t = 0;
        this.playing = true;
        this.clock = { wall: performance.now(), t: this.t };
        setState({ playing: true });
        const tick = () => {
            if (!this.playing) return;
            this.t = this.clock.t + (performance.now() - this.clock.wall) / 1000 * this.rate;
            const end = this.duration;
            if (this.t >= end) { this.t = end; this.pause(); return; }
            this.sync(true);
            this.render();
            const now = performance.now();
            if (now - this.lastUi > 50) { this.lastUi = now; setState({ time: this.t }); }
            this.raf = requestAnimationFrame(tick);
        };
        this.raf = requestAnimationFrame(tick);
    }

    pause() {
        this.playing = false;
        cancelAnimationFrame(this.raf);
        for (const el of this.els.values()) { el.pause(); this.mixer.silence(el); }
        setState({ playing: false, time: this.t });
        this.sync(false);
        this.requestRender();
    }

    toggle() { if (this.playing) this.pause(); else this.play(); }

    setRate(rate: number) {
        this.rate = rate;
        if (this.playing) this.clock = { wall: performance.now(), t: this.t };
    }

    seek(t: number) {
        const F = getState().timeline.frameRate;
        // land in the middle of the frame to avoid rounding onto the neighbour
        this.t = Math.max(0, Math.min(this.duration, t));
        if (this.playing) this.clock = { wall: performance.now(), t: this.t };
        else this.t = Math.min(this.duration, (Math.floor(this.t * F + 1e-6) + (this.t > 0 ? 0.5 : 0)) / F);
        setState({ time: this.t });
        this.sync(this.playing);
        this.requestRender();
    }

    step(frames: number) {
        if (this.playing) this.pause();
        const F = getState().timeline.frameRate;
        this.seek((this.frameAt(this.t) + frames + 0.5) / F);
    }

    /** Media element of a clip, created on demand from the pool url of its source. */
    private elFor(pc: PlanClip): HTMLVideoElement | null {
        const url = getState().pool.find(p => p.sourceId === pc.source.id)?.url;
        if (!url) return null;
        let el = this.els.get(pc.clip.id);
        if (!el) {
            el = document.createElement('video');
            el.preload = 'auto';
            el.playsInline = true;
            // silent until sync() routes it through the mixer: an element that never gets a mixer strip
            // (e.g. the picture clip of a video whose sound lives on an audio track) would otherwise play
            // its sound straight to the speakers
            el.muted = true;
            el.crossOrigin = 'anonymous';
            el.addEventListener('seeked', () => { if (!this.playing) this.requestRender(); });
            el.addEventListener('loadeddata', () => { if (!this.playing) this.requestRender(); });
            this.holder.appendChild(el);
            this.els.set(pc.clip.id, el);
        }
        if (el.dataset.url !== url) { el.dataset.url = url; el.src = url; el.load(); }
        return el;
    }

    /** Drops elements of clips that no longer exist. */
    private cleanup() {
        const ids = new Set(getState().timeline.clips.map(c => c.id));
        for (const [id, el] of this.els) {
            if (ids.has(id)) continue;
            el.pause();
            this.mixer.forget(el);
            el.removeAttribute('src');
            el.load();
            el.remove();
            this.els.delete(id);
        }
    }

    /** Puts every clip element on its media time for the current frame (playing or parked). */
    private sync(playing: boolean) {
        const st = getState();
        const tl = st.timeline;
        const plan = this.currentPlan();
        const F = plan.frameRate;
        const f = this.frameAt(this.t);
        const preF = f + Math.round(PREROLL_SEC * F);
        for (const pc of plan.clips) {
            const s0 = pc.startF - pc.headF, s1 = pc.endF + pc.tailF;
            const active = f >= s0 && f < s1 && (pc.visible || pc.audible);
            const preroll = !active && s0 > f && s0 <= preF && (pc.visible || pc.audible);
            const el = active || preroll ? this.elFor(pc) : this.els.get(pc.clip.id) ?? null;
            if (!el) continue;
            if (!active && !preroll) { if (!el.paused) el.pause(); this.mixer.silence(el); continue; }
            const srcFps = pc.source.media.video?.fps || F;
            const at = active ? f : s0;
            const target = heldSourceTime(pc, at, F, 1 / srcFps);
            const held = Math.abs(srcTimeAt(pc, at, F) - target) > 1 / srcFps;
            const run = playing && active && !pc.clip.reverse && !held;
            if (el.readyState < 1) continue;
            if (run) {
                el.playbackRate = Math.max(0.0625, Math.min(16, pc.clip.speed * this.rate));
                if (el.paused) { el.currentTime = target; void el.play().catch(() => undefined); }
                else if (Math.abs(el.currentTime - target) > 0.15) el.currentTime = target;
            } else {
                if (!el.paused) el.pause();
                // reversed clips (no negative playbackRate in Chromium) and parked clips seek per frame
                if (!el.seeking && Math.abs(el.currentTime - target) > 0.5 / srcFps) el.currentTime = target;
            }
            // audio: reversed clips are silent in the preview (export plays them through the reverse pre-pass);
            // only clips that are heard reach the speakers, and only through their mixer strip
            const heard = run && pc.audible && pc.source.media.audio.length > 0;
            el.muted = !heard;
            if (heard) this.mixer.update(el, tl, pc, f);
            else this.mixer.silence(el);
        }
    }

    requestRender() {
        cancelAnimationFrame(this.renderRaf);
        this.renderRaf = requestAnimationFrame(() => this.render());
    }

    private setGap(gap: boolean) {
        if (gap !== this.gap) { this.gap = gap; this.host.onGap(gap); }
    }

    /** Output size of the timeline picture: the first video source (like the export), at preview quality. */
    private outputSize(): { W: number; H: number } | null {
        const v = getState().timeline.sources.find(s => s.media.video)?.media.video;
        if (!v) return null;
        const sc = this.host.quality();
        const even = (x: number) => Math.max(2, Math.round(x * sc / 2) * 2);
        return { W: even(v.displayWidth || v.width), H: even(v.displayHeight || v.height) };
    }

    private render() {
        if (this.busy) return;
        const fe = this.host.engine();
        const c = this.host.canvas();
        const size = this.outputSize();
        if (!fe || !c || !size) return;
        const st = getState();
        const tl = st.timeline;
        const plan = this.currentPlan();
        const F = plan.frameRate;
        const f = this.frameAt(this.t);
        const { W, H } = size;
        const comp = frameComposite(plan, f, W, H);
        this.setGap(comp.layers.length === 0);
        if (!comp.layers.length) return;
        const els = comp.layers.map(l => this.elFor(l.plan));
        // hold the previous picture until every layer of this frame has decoded data
        if (els.some(el => !el || el.readyState < 2 || !el.videoWidth)) return;
        const sc = this.host.quality();
        const re = renderEngineOf(fe);
        try {
            if (!re) { this.renderFallback(fe, c, comp.layers[comp.layers.length - 1].plan, els[els.length - 1]!, W, H); this.host.onFrame?.(els[els.length - 1]!); return; }
            if (this.facadeOf !== re || !this.facade) { this.facade?.destroy(); this.facade = engineFacade(re, 'preview'); this.facadeOf = re; }
            const facade = this.facade;
            if (c.width !== W || c.height !== H) { c.width = W; c.height = H; }
            this.busy = true;
            void (async () => {
                if (!facade.ready()) await facade.init();
                for (let i = 0; i < comp.layers.length; i++) {
                    const l = comp.layers[i], el = els[i]!, pc = l.plan, clip = pc.clip;
                    const trackLin = trackGainLinOf(pc.track.gainDb, tl.masterGainDb);
                    const t = secOf(f - pc.startF, F);
                    const srcT = el.currentTime;
                    await facade.setSource(l.slot, { kind: 'video', video: el });
                    await facade.renderLayer(l.slot, {
                        sourceW: el.videoWidth, sourceH: el.videoHeight,
                        opacity: clip.opacity * (sampleCurve(clip.opacityCurve, t) ?? 1),
                        levelLin: clip.level * (sampleCurve(clip.levelCurve, t) ?? 1) * trackLin,
                        veil: 0, veilColor: 0,
                        autoMatrix: this.autoFor(pc, srcT),
                        clipMatrix: clipLookMatrix(clip, pc, f, F, trackLin, this.matrices[l.slot]),
                        effectGraph: graphOf(clip.effect ?? st.effect),
                        lutHandle: null, hwLutHandle: null, lut3dSize: null,
                        time: srcT, pxScale: sc, quantize8: this.host.parity()
                    });
                }
                await facade.composite({
                    width: W, height: H,
                    entries: comp.layers.map(l => l.entry),
                    veil: comp.veil, veilColor: comp.veilColor
                });
                this.grabScopes(fe, els[els.length - 1]!, W, H);
                this.host.onFrame?.(els[els.length - 1]!);
                this.host.onError(null);
            })().catch(e => this.host.onError(String(e instanceof Error ? e.message : e))).finally(() => { this.busy = false; });
        } catch (e) {
            this.busy = false;
            this.host.onError(String(e instanceof Error ? e.message : e));
        }
    }

    /**
     * Scope data while the scopes overlay is open (at most 5× per second): "Enhanced" is the composited
     * timeline frame, "Original" the untouched picture of the top layer.
     */
    private grabScopes(fe: FrameEngine, top: HTMLVideoElement, W: number, H: number) {
        const st = getState();
        if (!(st.scopesHud || st.view === 'scopes') || this.scopeBusy) return;
        const now = performance.now();
        if (now - this.lastScope < 200) return;
        this.lastScope = now;
        this.scopeBusy = true;
        const sw = 320, sh = Math.max(2, Math.round(320 * H / W));
        const original = this.pixelsOf(top, sw, sh);
        void fe.readScope('timeline', sw, sh)
            .then(enhanced => setState({ scopes: { w: sw, h: sh, enhanced, original: original ?? enhanced } }))
            .catch(() => undefined)
            .finally(() => { this.scopeBusy = false; });
    }

    private pixelsOf(v: HTMLVideoElement, w: number, h: number): Uint8Array | null {
        const c = this.scopeCanvas ??= document.createElement('canvas');
        if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
        const ctx = c.getContext('2d', { willReadFrequently: true });
        if (!ctx) return null;
        try {
            ctx.drawImage(v, 0, 0, w, h);
            return new Uint8Array(ctx.getImageData(0, 0, w, h).data.buffer);
        } catch { return null; }
    }

    /** Auto-Scene-Match keys belong to the analysed source-monitor file only. */
    private autoFor(pc: PlanClip, srcT: number): Mat4x5 | null {
        const st = getState();
        const effect = pc.clip.effect ?? st.effect;
        if (!effect.settings.autoOn || !st.analysis.keys || st.media?.path !== pc.source.path) return null;
        return autoMatrixAt(st.analysis.keys, srcT);
    }

    /**
     * Engine without the timeline surface: shows the top-most layer through the single-clip path so the
     * timeline is still watchable (no transitions / layer stacking until the engine provides `composite`).
     */
    private renderFallback(fe: FrameEngine, c: HTMLCanvasElement, pc: PlanClip, el: HTMLVideoElement, W: number, H: number) {
        const st = getState();
        const graph = graphOf(pc.clip.effect ?? st.effect);
        if (graph !== this.fallbackGraph) { fe.setGraph(graph); this.fallbackGraph = graph; }
        const sc = this.host.quality();
        fe.uploadVideo(el);
        fe.process(W, H, { autoMatrix: this.autoFor(pc, el.currentTime), time: el.currentTime, pxScale: sc, quantize8: this.host.parity() });
        const dpr = window.devicePixelRatio || 1;
        const cw = Math.max(2, Math.round(c.clientWidth * dpr)), ch = Math.max(2, Math.round(c.clientHeight * dpr));
        if (c.width !== cw || c.height !== ch) { c.width = cw; c.height = ch; }
        fe.present(cw, ch, 0, 0);
    }

    /** Leaves the timeline monitor: stops playback and releases the layer slots. */
    deactivate() {
        if (this.playing) this.pause();
        for (const el of this.els.values()) { el.pause(); this.mixer.silence(el); }
        this.facade?.destroy();
        this.facade = null;
        this.facadeOf = null;
        this.fallbackGraph = null;
    }

    /** Forces the next fallback frame to re-send its graph (the source monitor changed the engine graph). */
    invalidateGraph() { this.fallbackGraph = null; }

    dispose() {
        this.deactivate();
        for (const el of this.els.values()) { this.mixer.forget(el); el.removeAttribute('src'); el.load(); }
        this.els.clear();
        this.mixer.dispose();
        this.holder.remove();
    }
}

/** Frame number of a timeline position (for the transport's frame read-out). */
export const timelineFrameOf = (tl: Timeline, t: number) => frameOf(t, tl.frameRate);
