import { useEffect, useMemo, useRef, useState } from 'react';
import { autoMatrixAt } from '../../shared/uve/auto';
import { createEngine, wantedKind } from '../engine/createEngine';
import { type FrameEngine, hasCustomGlsl } from '../engine/types';
import { graphFromEffect } from '../engine/pipeline';
import { loadVideo, requestProxy } from '../state/actions';
import { getState, setState, useStore } from '../state/store';
import { api } from '../api';
import { Icon, Progress } from './controls';

/** Imperative player API used by the transport, timeline and keyboard shortcuts. */
export const player = {
    video: null as HTMLVideoElement | null,
    requestRender: () => { /* set by Player */ },
    /** Current decoded source frame (before effects), resampled to w×h RGBA – used for in-app LUT previews. */
    grabFrame: async (_w: number, _h: number): Promise<{ data: Uint8Array; w: number; h: number } | null> => null,
    play() { void player.video?.play(); },
    pause() { player.video?.pause(); },
    toggle() { const v = player.video; if (!v) return; if (v.paused) void v.play(); else v.pause(); },
    seek(t: number) {
        const v = player.video, m = getState().media;
        if (!v || !m) return;
        v.currentTime = Math.max(0, Math.min(m.duration || v.duration || 0, t));
        setState({ time: v.currentTime });
    },
    /** Seeks as soon as the current video has metadata (used when restoring the last session). */
    seekWhenReady(t: number) {
        const v = player.video;
        if (!v) return;
        const url = getState().mediaUrl;
        if (url && v.currentSrc === url && v.readyState >= 1) { player.seek(t); return; }
        const on = () => { if (getState().mediaUrl === url) player.seek(t); };
        v.addEventListener('loadedmetadata', on, { once: true });
    },
    step(frames: number) {
        const v = player.video, m = getState().media;
        if (!v || !m?.video) return;
        v.pause();
        const fps = m.video.fps || 25;
        // land in the middle of the target frame to avoid rounding onto the neighbour
        const cur = Math.round(v.currentTime * fps);
        player.seek((cur + frames + 0.5) / fps);
    }
};

const QUALITY_SCALE = { full: 1, half: 0.5, quarter: 0.25 } as const;

export function Player() {
    const wrapRef = useRef<HTMLDivElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const videoRef = useRef<HTMLVideoElement>(null);
    const engineRef = useRef<FrameEngine | null>(null);
    const enginePref = useStore(s => s.prefs?.gpuEngine ?? 'auto');
    const glslActive = useStore(s => hasCustomGlsl(s.effect.customFilters));
    // a canvas can only own one context type: the canvas element is re-created when the backend changes
    const [canvasGen, setCanvasGen] = useState(0);
    const [glError, setGlError] = useState<string | null>(null);
    const [box, setBox] = useState({ w: 0, h: 0 });
    const [dragOver, setDragOver] = useState(false);
    const media = useStore(s => s.media);
    const mediaUrl = useStore(s => s.mediaUrl);
    const mediaError = useStore(s => s.mediaError);
    const proxyPct = useStore(s => s.proxyPct);
    const effect = useStore(s => s.effect);
    const bypass = useStore(s => s.bypass);
    const compare = useStore(s => s.compare);
    const wipe = useStore(s => s.wipe);
    const quality = useStore(s => s.prefs?.previewQuality ?? 'full');
    const parity = useStore(s => !!s.prefs?.parityMode);
    const autoKeys = useStore(s => s.analysis.keys);
    const scopesWanted = useStore(s => s.view === 'scopes' || s.scopesHud);
    const renderStats = useStore(s => s.renderStats);

    const graph = useMemo(() => graphFromEffect(effect), [effect]);

    // live values for the render callback without re-subscribing
    const live = useRef({ bypass, compare, wipe, quality, parity, autoKeys, autoOn: effect.settings.autoOn, scopesWanted, prims: 0 });
    live.current = { bypass, compare, wipe, quality, parity, autoKeys, autoOn: effect.settings.autoOn, scopesWanted, prims: graph.prims.length };

    // engine setup (v1.0.1 facade): validated WebGPU, or the v1.0.0 WebGL2 engine as fallback / for custom GLSL
    useEffect(() => {
        const want = wantedKind(enginePref, getState().effect.customFilters).kind;
        const cur = engineRef.current;
        if (cur && cur.kind === want && !(cur as { lost?: boolean }).lost) return;
        if (cur && canvasRef.current?.dataset.engine) { cur.dispose(); engineRef.current = null; setCanvasGen(g => g + 1); return; }
        let disposed = false;
        const c = canvasRef.current!;
        void createEngine(c, enginePref, getState().effect.customFilters).then(choice => {
            if (disposed) { choice.engine.dispose(); return; }
            engineRef.current = choice.engine;
            c.dataset.engine = choice.engine.kind;
            setGlError(null);
            setState({ engineInfo: { kind: choice.engine.kind, adapter: choice.engine.adapter, reason: choice.reason, validation: choice.validation ?? null } });
            const e = choice.engine;
            e.setGraph(graphFromEffect(getState().effect));
            const res = e.setCustomFilters(getState().effect.customFilters);
            const errs: Record<string, string> = {};
            res.forEach(r => { if (r.error && e.kind === 'webgl2') errs[r.entry.id] = r.error; });
            setState({ customErrors: errs });
            if (e.kind === 'webgpu') {
                void (e as unknown as { device: GPUDevice }).device.lost.then(info => {
                    if (disposed || engineRef.current !== e) return;
                    setGlError(`GPU device lost (${info.message || info.reason}) – the preview restarts.`);
                    engineRef.current = null;
                    setCanvasGen(g => g + 1);
                });
            }
            player.requestRender();
        }).catch(err => setGlError(String(err instanceof Error ? err.message : err)));
        const lost = (ev: Event) => { ev.preventDefault(); setGlError('GPU context lost – the preview restarts automatically.'); engineRef.current = null; setCanvasGen(g => g + 1); };
        c.addEventListener('webglcontextlost', lost);
        return () => { disposed = true; c.removeEventListener('webglcontextlost', lost); };
    }, [enginePref, glslActive, canvasGen]);

    useEffect(() => () => { engineRef.current?.dispose(); engineRef.current = null; }, []);

    useEffect(() => {
        player.grabFrame = async (w, h) => {
            const eng = engineRef.current;
            if (!eng?.hasFrame) return null;
            return { data: await eng.readScope('source', w, h), w, h };
        };
        // automated UI test probe: mean RGB of the original and processed preview frame
        (window as unknown as { __uveProbe: () => unknown }).__uveProbe = async () => {
            const eng = engineRef.current;
            if (!eng?.hasFrame) return null;
            const mean = (px: Uint8Array) => { const m = [0, 0, 0]; for (let i = 0; i < px.length; i += 4) { m[0] += px[i]; m[1] += px[i + 1]; m[2] += px[i + 2]; } return m.map(v => +(v / (px.length / 4)).toFixed(1)); };
            return { engine: eng.kind, source: mean(await eng.readScope('source', 64, 36)), output: mean(await eng.readScope('output', 64, 36)), graph: getState().graphWarnings };
        };
    }, []);

    // graph + custom passes
    useEffect(() => {
        const eng = engineRef.current;
        setState({ graphWarnings: graph.warnings });
        if (!eng) return;
        eng.setGraph(graph);
        player.requestRender();
    }, [graph]);

    const customKey = JSON.stringify(effect.customFilters.filter(f => f.type === 'webgl').map(f => [f.id, f.code, f.enabled, f.blendMode]));
    useEffect(() => {
        const eng = engineRef.current;
        if (!eng) return;
        const res = eng.setCustomFilters(getState().effect.customFilters);
        const errs: Record<string, string> = {};
        res.forEach(r => { if (r.error && eng.kind === 'webgl2') errs[r.entry.id] = r.error; });
        setState({ customErrors: errs });
        player.requestRender();
    }, [customKey]);

    // uniforms changes do not require recompilation, only a re-render
    useEffect(() => {
        const eng = engineRef.current;
        if (!eng) return;
        eng.updateCustomUniforms(effect.customFilters);
        player.requestRender();
    }, [effect.customFilters]);

    useEffect(() => { player.requestRender(); }, [bypass, compare, wipe, quality, parity, autoKeys, box]);

    // canvas box (fit video aspect into the viewport)
    useEffect(() => {
        const el = wrapRef.current!;
        const ro = new ResizeObserver(() => setBox({ w: el.clientWidth, h: el.clientHeight }));
        ro.observe(el);
        return () => ro.disconnect();
    }, []);

    // render loop
    useEffect(() => {
        const v = videoRef.current!;
        player.video = v;
        let raf = 0, vfc = 0, lastScope = 0, lastTime = 0, dropped = 0, scopeBusy = false;
        const render = () => {
            const eng = engineRef.current;
            const c = canvasRef.current;
            if (!eng || !c || v.readyState < 2 || !v.videoWidth) return;
            const L = live.current;
            const sc = QUALITY_SCALE[L.quality];
            const pw = Math.max(2, Math.round(v.videoWidth * sc)), ph = Math.max(2, Math.round(v.videoHeight * sc));
            const t0 = performance.now();
            try {
                eng.uploadVideo(v);
                eng.process(pw, ph, { autoMatrix: L.autoOn ? autoMatrixAt(L.autoKeys, v.currentTime) : null, time: v.currentTime, pxScale: sc, quantize8: L.parity });
                const dpr = window.devicePixelRatio || 1;
                const cw = Math.max(2, Math.round(c.clientWidth * dpr)), ch = Math.max(2, Math.round(c.clientHeight * dpr));
                if (c.width !== cw || c.height !== ch) { c.width = cw; c.height = ch; }
                eng.present(cw, ch, L.bypass ? 1 : L.compare ? 2 : 0, L.wipe);
                const now = performance.now();
                if (L.scopesWanted && now - lastScope > 200 && !scopeBusy) {
                    lastScope = now;
                    scopeBusy = true;
                    const sw = 320, sh = Math.max(2, Math.round(320 * ph / pw));
                    void Promise.all([eng.readScope('output', sw, sh), eng.readScope('source', sw, sh)])
                        .then(([enhanced, original]) => setState({ scopes: { w: sw, h: sh, enhanced, original } }))
                        .finally(() => { scopeBusy = false; });
                }
                const ms = performance.now() - t0;
                if (!v.paused && ms > 1000 / (getState().media?.video?.fps || 30)) dropped++;
                if (now - lastTime > 250) {
                    lastTime = now;
                    setState({ time: v.currentTime, renderStats: { ms, prims: L.prims, w: pw, h: ph, dropped } });
                }
            } catch (e) {
                setGlError(String(e instanceof Error ? e.message : e));
            }
        };
        const onFrame = () => { render(); if (!v.paused) vfc = v.requestVideoFrameCallback(onFrame); };
        player.requestRender = () => { cancelAnimationFrame(raf); raf = requestAnimationFrame(render); };
        const onPlay = () => { setState({ playing: true }); vfc = v.requestVideoFrameCallback(onFrame); };
        const onPause = () => { setState({ playing: false, time: v.currentTime }); player.requestRender(); };
        const onSeeked = () => { setState({ time: v.currentTime }); player.requestRender(); };
        const onLoaded = () => { player.requestRender(); };
        const onError = () => {
            const err = v.error;
            if (!getState().mediaUrl) return;
            if (err && (err.code === MediaError.MEDIA_ERR_SRC_NOT_SUPPORTED || err.code === MediaError.MEDIA_ERR_DECODE)) void requestProxy();
            else setState({ mediaError: `Playback error: ${err?.message || 'unknown'}` });
        };
        v.addEventListener('play', onPlay);
        v.addEventListener('pause', onPause);
        v.addEventListener('seeked', onSeeked);
        v.addEventListener('loadeddata', onLoaded);
        v.addEventListener('error', onError);
        return () => {
            cancelAnimationFrame(raf);
            if (vfc) v.cancelVideoFrameCallback(vfc);
            v.removeEventListener('play', onPlay);
            v.removeEventListener('pause', onPause);
            v.removeEventListener('seeked', onSeeked);
            v.removeEventListener('loadeddata', onLoaded);
            v.removeEventListener('error', onError);
        };
    }, []);

    // the <video> element only renders the first frame after a seek; "no decodable video track" also lands here
    useEffect(() => {
        const v = videoRef.current!;
        if (mediaUrl) { v.src = mediaUrl; v.load(); } else { v.removeAttribute('src'); v.load(); }
    }, [mediaUrl]);

    // fit canvas into the box
    const vw = media?.video?.displayWidth || 16, vh = media?.video?.displayHeight || 9;
    const scale = Math.min(box.w / vw, box.h / vh) || 0;
    const cw = Math.floor(vw * scale), ch = Math.floor(vh * scale);

    const draggingWipe = useRef(false);
    const setWipeFromEvent = (e: React.PointerEvent) => {
        const r = canvasRef.current!.getBoundingClientRect();
        setState({ wipe: Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) });
    };

    const onDrop = async (e: React.DragEvent) => {
        e.preventDefault();
        setDragOver(false);
        const f = e.dataTransfer.files[0];
        if (!f) return;
        const p = api.pathForFile(f);
        if (p) {
            if (/\.uveproj$/i.test(p)) { const { openProject } = await import('../state/actions'); void openProject(p); }
            else void loadVideo(p);
        }
    };

    return (
        <div className={`player${dragOver ? ' drag' : ''}`} ref={wrapRef}
            onDragOver={e => { e.preventDefault(); setDragOver(true); }} onDragLeave={() => setDragOver(false)} onDrop={onDrop}>
            <video ref={videoRef} className="decoder" playsInline preload="auto" crossOrigin="anonymous" />
            <canvas key={canvasGen} ref={canvasRef} className="player-canvas" style={{ width: cw, height: ch, visibility: media && mediaUrl ? 'visible' : 'hidden' }}
                onPointerDown={e => { if (!compare || bypass) return; draggingWipe.current = true; (e.target as Element).setPointerCapture(e.pointerId); setWipeFromEvent(e); }}
                onPointerMove={e => { if (draggingWipe.current) setWipeFromEvent(e); }}
                onPointerUp={() => { draggingWipe.current = false; }}
                onDoubleClick={() => player.toggle()} />
            {media && mediaUrl && compare && !bypass && (
                <div className="wipe-overlay" style={{ width: cw, height: ch }}>
                    <span className="badge badge-orig">Original</span>
                    <span className="badge badge-enh">Enhanced</span>
                    <div className="wipe-handle" style={{ left: `${wipe * 100}%` }}><span><Icon name="split" size={16} /></span></div>
                </div>
            )}
            {media && mediaUrl && !compare && <span className="badge badge-enh solo">{bypass ? 'Original (bypass)' : 'Enhanced'}</span>}
            {media?.video && (
                <span className="res-badge" title={`Preview processing ${renderStats.w}×${renderStats.h}, ${renderStats.prims} filter primitives, ${renderStats.ms.toFixed(1)} ms/frame`}>
                    {media.video.height >= 2160 ? '4K' : media.video.height >= 1440 ? 'QHD' : media.video.height >= 1080 ? 'FHD' : media.video.height >= 720 ? 'HD' : 'SD'}
                    {quality !== 'full' ? ` · ${quality}` : ''}
                </span>
            )}
            {!media && !mediaError && (
                <div className="dropzone">
                    <Icon name="upload" size={42} />
                    <h2>Drop a video here</h2>
                    <p>MP4, MKV, MOV, WebM, AVI, MPEG-TS, WMV and more (decoded by FFmpeg)</p>
                    <button className="btn btn-primary" onClick={async () => { const p = await api.openVideos(); if (p[0]) void loadVideo(p[0]); }}>Browse files</button>
                </div>
            )}
            {mediaError && <div className="dropzone error"><Icon name="info" size={36} /><h2>Cannot play this file</h2><p>{mediaError}</p></div>}
            {proxyPct !== null && <div className="proxy-note"><span>Creating preview proxy…</span><Progress value={proxyPct} /></div>}
            {glError && <div className="gl-error">{glError}</div>}
        </div>
    );
}
