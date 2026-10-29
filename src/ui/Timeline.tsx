import { useEffect, useRef, useState } from 'react';
import { getState, setState, useStore } from '../state/store';
import { Icon, Select, fmtTime } from './controls';
import { player } from './Player';
import { sc } from './keys';

export function Transport() {
    const media = useStore(s => s.media);
    const playing = useStore(s => s.playing);
    const time = useStore(s => s.time);
    const compare = useStore(s => s.compare);
    const bypass = useStore(s => s.bypass);
    const quality = useStore(s => s.prefs?.previewQuality ?? 'full');
    const [muted, setMuted] = useState(false);
    const [vol, setVol] = useState(1);
    const [rate, setRate] = useState(1);
    useEffect(() => { if (player.video) { player.video.muted = muted; player.video.volume = vol; player.video.playbackRate = rate; } }, [muted, vol, rate, media]);
    const dis = !media;
    const fps = media?.video?.fps || 0;
    const frame = fps ? Math.floor(time * fps + 1e-6) : 0;
    return (
        <div className="transport">
            <button className="icon-btn" disabled={dis} onClick={() => player.seek(0)} title="Go to start (Home)" aria-label="Go to start"><Icon name="start" /></button>
            <button className="icon-btn" disabled={dis} onClick={() => player.step(-1)} title="Previous frame (←)" aria-label="Previous frame"><Icon name="prev" /></button>
            <button className="icon-btn play" disabled={dis} onClick={() => player.toggle()} title="Play / Pause (Space)" aria-label={playing ? 'Pause' : 'Play'}><Icon name={playing ? 'pause' : 'play'} size={22} /></button>
            <button className="icon-btn" disabled={dis} onClick={() => player.step(1)} title="Next frame (→)" aria-label="Next frame"><Icon name="next" /></button>
            <button className="icon-btn" disabled={dis} onClick={() => player.seek((media?.duration || 0) - 0.001)} title="Go to end (End)" aria-label="Go to end"><Icon name="end" /></button>
            <span className="timecode" title={`Frame ${frame}`}>{fmtTime(time, true)} <span className="dim">/ {fmtTime(media?.duration || 0, true)}</span></span>
            <span className="spacer" />
            <button className={`icon-btn${compare && !bypass ? ' active' : ''}`} disabled={dis} onClick={() => setState({ compare: !getState().compare })} title={`Before/After wipe (${sc('compare')})`} aria-label="Before/After wipe"><Icon name="split" /></button>
            <button className={`icon-btn${bypass ? ' active warn' : ''}`} disabled={dis} onClick={() => setState({ bypass: !getState().bypass })} title={`Bypass all effects in the preview (${sc('bypass')})`} aria-label="Bypass"><Icon name="bypass" /></button>
            <Select value={quality} title="Preview quality (export is always full quality)" options={[{ value: 'full', label: 'Full' }, { value: 'half', label: '1/2' }, { value: 'quarter', label: '1/4' }]}
                onChange={async v => { const { api } = await import('../api'); setState({ prefs: await api.setPrefs({ previewQuality: v }) }); }} />
            <Select value={rate} title="Playback speed" options={[0.25, 0.5, 1, 1.5, 2].map(r => ({ value: r, label: `${r}×` }))} onChange={setRate} />
            <button className="icon-btn" onClick={() => setMuted(!muted)} title={muted ? 'Unmute' : 'Mute'} aria-label={muted ? 'Unmute' : 'Mute'}><Icon name={muted ? 'mute' : 'volume'} /></button>
            <input className="volume" type="range" min={0} max={1} step={0.01} value={muted ? 0 : vol} onChange={e => { setVol(Number(e.target.value)); setMuted(false); }} aria-label="Volume" style={{ ['--pct' as string]: `${(muted ? 0 : vol) * 100}%` }} />
            <button className="icon-btn" disabled={dis} onClick={() => document.querySelector('.player')?.requestFullscreen()} title="Fullscreen" aria-label="Fullscreen"><Icon name="fullscreen" /></button>
        </div>
    );
}

export function Timeline() {
    const media = useStore(s => s.media);
    const thumbs = useStore(s => s.thumbs);
    const time = useStore(s => s.time);
    const inP = useStore(s => s.inPoint);
    const outP = useStore(s => s.outPoint);
    const analysis = useStore(s => s.analysis);
    const [zoom, setZoom] = useState(1);
    const scrollRef = useRef<HTMLDivElement>(null);
    const dragging = useRef(false);
    const dur = media?.duration || 0;

    useEffect(() => {
        // keep the playhead visible while playing when zoomed in
        const el = scrollRef.current;
        if (!el || zoom === 1 || !dur) return;
        const x = (time / dur) * el.scrollWidth;
        if (x < el.scrollLeft || x > el.scrollLeft + el.clientWidth) el.scrollLeft = x - el.clientWidth * 0.2;
    }, [time, zoom, dur]);

    const seekFromEvent = (e: React.PointerEvent) => {
        const el = scrollRef.current!;
        const inner = el.firstElementChild as HTMLElement;
        const r = inner.getBoundingClientRect();
        player.seek(((e.clientX - r.left) / r.width) * dur);
    };

    const ticks: number[] = [];
    if (dur) {
        const target = 10 * zoom;
        const steps = [0.1, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 1800, 3600];
        const step = steps.find(s => dur / s <= target) || 3600;
        for (let t = 0; t <= dur; t += step) ticks.push(t);
    }
    const pct = (t: number) => `${dur ? (t / dur) * 100 : 0}%`;

    return (
        <div className="timeline">
            <div className="timeline-tools">
                <button className="icon-btn" disabled={!media} onClick={() => setState({ inPoint: getState().time })} title="Set In point (I)" aria-label="Set In point"><Icon name="inMark" /></button>
                <button className="icon-btn" disabled={!media} onClick={() => setState({ outPoint: getState().time })} title="Set Out point (O)" aria-label="Set Out point"><Icon name="outMark" /></button>
                <button className="icon-btn" disabled={inP === null && outP === null} onClick={() => setState({ inPoint: null, outPoint: null })} title="Clear In/Out" aria-label="Clear In and Out"><Icon name="x" /></button>
                <span className="dim small">{inP !== null || outP !== null ? `In ${fmtTime(inP ?? 0)} · Out ${fmtTime(outP ?? dur)}` : 'Full clip'}</span>
                <span className="spacer" />
                {analysis.status === 'running' && <span className="dim small">Auto-Scene-Match analysis {Math.round(analysis.pct * 100)}%</span>}
                <button className="icon-btn" disabled={zoom <= 1} onClick={() => setZoom(z => Math.max(1, z / 2))} title="Zoom out (-)" aria-label="Zoom out"><Icon name="zoomOut" /></button>
                <span className="dim small">{zoom}×</span>
                <button className="icon-btn" disabled={zoom >= 64} onClick={() => setZoom(z => Math.min(64, z * 2))} title="Zoom in (+)" aria-label="Zoom in"><Icon name="zoomIn" /></button>
            </div>
            <div className="timeline-scroll" ref={scrollRef}>
                <div className="timeline-inner" style={{ width: `${zoom * 100}%` }}
                    onPointerDown={e => { if (!media) return; dragging.current = true; (e.currentTarget as Element).setPointerCapture(e.pointerId); seekFromEvent(e); }}
                    onPointerMove={e => { if (dragging.current) seekFromEvent(e); }}
                    onPointerUp={() => { dragging.current = false; }}>
                    <div className="ruler">
                        {ticks.map(t => <span key={t} className="tick" style={{ left: pct(t) }}>{fmtTime(t)}</span>)}
                    </div>
                    <div className="thumbs">
                        {media ? thumbs.map((th, i) => (
                            <img key={i} src={th.url} alt="" draggable={false} style={{ left: `${(i / thumbs.length) * 100}%`, width: `${100 / thumbs.length}%` }} />
                        )) : <div className="thumbs-empty">Timeline – open a video to start</div>}
                        {(inP !== null || outP !== null) && <div className="inout" style={{ left: pct(inP ?? 0), width: `calc(${pct((outP ?? dur) - (inP ?? 0))})` }} />}
                    </div>
                    {analysis.keys && dur > 0 && (
                        <div className="autolane" title="Auto-Scene-Match corrections over time">
                            {analysis.keys.slice(1).map((k, i) => <span key={i} style={{ left: pct(k.t) }} />)}
                        </div>
                    )}
                    {media && <div className="playhead" style={{ left: pct(time) }}><span /></div>}
                </div>
            </div>
        </div>
    );
}
