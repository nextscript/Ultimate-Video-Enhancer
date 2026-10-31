import { useEffect, useRef, useState } from 'react';
import { TRANSITION_KINDS, type TrackKind, type TransitionKind } from '../../shared/types';
import { addMarker, clipById, clipsOn, removeMarker, removeTrack, renameMarker, setTransition, timelineDuration, trackById } from '../../shared/timeline';
import {
    copyClip, deleteRange, duplicateSelectedClip, editTimeline, enqueueTimelineJob, insertTrack, pasteClip, razorAt, razorAtPlayhead, removeSelectedClip,
    selectClip, selectTrack, setRangeSel, setSnapping, setTimelineZoom, splitAtRange
} from '../state/actions';
import { getState, setState, useStore } from '../state/store';
import { snapEdge, snapTolFrames } from './timelineMath';
import { ClipBox, transitionLabel } from './ClipBox';
import { Icon, Select, fmtTime } from './controls';
import { addToTimeline, POOL_DRAG_TYPE } from './MediaPool';
import { player } from './Player';
import { TrackHeader } from './TrackHeader';
import { addDefaultTransition, rippleTrimSelectedToPlayhead, sc, zoomToFit } from './keys';

export function Transport() {
    const playing = useStore(s => s.playing);
    const time = useStore(s => s.time);
    const quality = useStore(s => s.prefs?.previewQuality ?? 'full');
    const tl = useStore(s => s.timeline);
    const [muted, setMuted] = useState(false);
    const [vol, setVol] = useState(1);
    const [rate, setRate] = useState(1);
    useEffect(() => {
        player.timeline?.setMonitor(vol, muted);
        player.timeline?.setRate(rate);
    }, [muted, vol, rate]);
    const dis = !tl.clips.length;
    const dur = timelineDuration(tl);
    const fps = tl.frameRate;
    const frame = Math.floor(time * fps + 1e-6);
    return (
        <div className="transport">
            <button className="icon-btn" disabled={dis} onClick={() => player.seek(0)} title={`Go to start (${sc('start')})`} aria-label="Go to start"><Icon name="start" /></button>
            <button className="icon-btn" disabled={dis} onClick={() => player.step(-1)} title={`Previous frame (${sc('frameBack')})`} aria-label="Previous frame"><Icon name="prev" /></button>
            <button className="icon-btn play" disabled={dis} onClick={() => player.toggle()} title={`Play / Pause (${sc('playPause')})`} aria-label={playing ? 'Pause' : 'Play'}><Icon name={playing ? 'pause' : 'play'} size={22} /></button>
            <button className="icon-btn" disabled={dis} onClick={() => player.step(1)} title={`Next frame (${sc('frameFwd')})`} aria-label="Next frame"><Icon name="next" /></button>
            <button className="icon-btn" disabled={dis} onClick={() => player.seek(dur - 0.001)} title={`Go to end (${sc('end')})`} aria-label="Go to end"><Icon name="end" /></button>
            <span className="timecode" title={`Frame ${frame}`}>{fmtTime(time, true)} <span className="dim">/ {fmtTime(dur, true)}</span></span>
            <span className="spacer" />
            <Select value={quality} title="Preview quality (export is always full quality)" options={[{ value: 'full', label: 'Full' }, { value: 'half', label: '1/2' }, { value: 'quarter', label: '1/4' }]}
                onChange={async v => { const { api } = await import('../api'); setState({ prefs: await api.setPrefs({ previewQuality: v }) }); }} />
            <Select value={rate} title="Playback speed" options={[0.25, 0.5, 1, 1.5, 2].map(r => ({ value: r, label: `${r}×` }))} onChange={setRate} />
            <button className="icon-btn" onClick={() => setMuted(!muted)} title={muted ? 'Unmute' : 'Mute'} aria-label={muted ? 'Unmute' : 'Mute'}><Icon name={muted ? 'mute' : 'volume'} /></button>
            <input className="volume" type="range" min={0} max={1} step={0.01} value={muted ? 0 : vol} onChange={e => { setVol(Number(e.target.value)); setMuted(false); }} aria-label="Volume" style={{ ['--pct' as string]: `${(muted ? 0 : vol) * 100}%` }} />
            <button className="icon-btn" disabled={dis} onClick={() => document.querySelector('.player')?.requestFullscreen()} title="Fullscreen" aria-label="Fullscreen"><Icon name="fullscreen" /></button>
        </div>
    );
}

const RULER_H = 24;
const DEFAULT_H: Record<TrackKind, number> = { video: 58, audio: 44 };
const MIN_H = 22, MAX_H = 220;
const clampH = (h: number) => Math.round(Math.max(MIN_H, Math.min(MAX_H, h)));
const TICK_STEPS = [1 / 25, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 1800, 3600];

function MarkerFlag({ id, t, name, zoom }: { id: number; t: number; name: string; zoom: number }) {
    const [edit, setEdit] = useState(false);
    const [text, setText] = useState(name);
    return (
        <span className="marker" style={{ left: t * zoom }} title={`${name || 'Marker'} · ${fmtTime(t, true)}\nClick: go to · Double-click: rename · Right-click: remove`}
            onPointerDown={e => { e.stopPropagation(); player.seek(t); }}
            onDoubleClick={e => { e.stopPropagation(); setText(name); setEdit(true); }}
            onContextMenu={e => { e.preventDefault(); e.stopPropagation(); editTimeline(tl => removeMarker(tl, id)); }}>
            {edit
                ? <input className="marker-edit" autoFocus value={text} onChange={e => setText(e.target.value)} onPointerDown={e => e.stopPropagation()}
                    onBlur={() => { setEdit(false); if (text !== name) editTimeline(tl => renameMarker(tl, id, text)); }}
                    onKeyDown={e => { e.stopPropagation(); if (e.key === 'Enter') (e.target as HTMLInputElement).blur(); if (e.key === 'Escape') { setText(name); setEdit(false); } }} />
                : <span className="marker-name">{name || id}</span>}
        </span>
    );
}

/** Where the context menu was opened: the track, the clip under the pointer (if any) and the timeline time there. */
interface MenuAt { x: number; y: number; trackId: string; clipId: string | null; t: number }
type MenuItem = { label: string; key?: string; disabled?: boolean; run: () => void } | 'sep';

function TimelineMenu({ at, onClose }: { at: MenuAt; onClose: () => void }) {
    const ref = useRef<HTMLDivElement>(null);
    const tl = getState().timeline;
    const clip = at.clipId ? clipById(tl, at.clipId) : undefined;
    const track = tl.tracks.find(t => t.id === at.trackId);
    const time = getState().time;
    const hasClip = !!clip;
    const canSplitHere = !!clip && at.t > clip.start + 1e-6 && at.t < clip.start + clip.dur - 1e-6;
    const r = getState().rangeSel;
    const inRange = !!r && r.trackIds.includes(at.trackId) && at.t >= r.t0 - 1e-9 && at.t <= r.t1 + 1e-9;
    const rangeItems: MenuItem[] = r && inRange ? [
        { label: 'Delete marked range (lift)', key: sc('lift'), run: () => deleteRange('lift') },
        { label: 'Ripple delete marked range', key: sc('ripple'), run: () => deleteRange('ripple') },
        { label: 'Copy marked range', key: sc('copy'), run: () => copyClip(false) },
        { label: 'Cut marked range', key: sc('cut'), run: () => copyClip(true) },
        { label: 'Split at range edges', run: () => splitAtRange() },
        { label: 'Clear marking', key: sc('escape'), run: () => setRangeSel(null) },
        'sep',
        { label: 'Paste here', key: sc('paste'), disabled: !getState().clipboard, run: () => pasteClip({ trackId: at.trackId, start: at.t }) },
        { label: 'Split at playhead', key: sc('razor'), run: () => razorAtPlayhead() },
        { label: 'Add marker here', key: sc('marker'), run: () => editTimeline(t => addMarker(t, at.t)) }
    ] : [];
    const items: MenuItem[] = rangeItems.length ? rangeItems : [
        { label: 'Split here', disabled: !canSplitHere, run: () => { if (clip) razorAt(clip.id, at.t); } },
        { label: 'Split at playhead', key: sc('razor'), run: () => razorAtPlayhead() },
        { label: 'Ripple trim to playhead', key: sc('rippleTrimEnd'), disabled: !hasClip || time <= (clip?.start ?? 0), run: () => rippleTrimSelectedToPlayhead() },
        'sep',
        { label: 'Copy', key: sc('copy'), disabled: !hasClip, run: () => copyClip(false, clip?.id) },
        { label: 'Cut', key: sc('cut'), disabled: !hasClip, run: () => copyClip(true, clip?.id) },
        { label: 'Paste here', key: sc('paste'), disabled: !getState().clipboard, run: () => pasteClip({ trackId: at.trackId, start: at.t }) },
        { label: 'Duplicate', key: sc('duplicate'), disabled: !hasClip, run: () => duplicateSelectedClip() },
        'sep',
        { label: 'Delete (lift)', key: sc('lift'), disabled: !hasClip, run: () => removeSelectedClip('lift') },
        { label: 'Ripple delete', key: sc('ripple'), disabled: !hasClip, run: () => removeSelectedClip('ripple') },
        'sep',
        { label: 'Add dissolve on the left cut', key: sc('transition'), disabled: !hasClip, run: () => { if (clip) addDefaultTransition(clip.id); } },
        { label: 'Remove transition', disabled: !clip?.transition, run: () => { if (clip) editTimeline(t => setTransition(t, clip.id, null)); } },
        { label: 'Add marker here', key: sc('marker'), run: () => editTimeline(t => addMarker(t, at.t)) },
        'sep',
        { label: 'Add video track', key: sc('addVideoTrack'), run: () => insertTrack('video', 0) },
        { label: 'Add audio track', key: sc('addAudioTrack'), run: () => insertTrack('audio') },
        { label: `Remove track${track ? ` “${track.name}”` : ''}`, disabled: !track || clipsOn(tl, at.trackId).length > 0, run: () => editTimeline(t => removeTrack(t, at.trackId)) }
    ];
    useEffect(() => {
        const away = (e: PointerEvent) => { if (!ref.current?.contains(e.target as Node)) onClose(); };
        const key = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
        window.addEventListener('pointerdown', away, true);
        window.addEventListener('keydown', key, true);
        window.addEventListener('blur', onClose);
        return () => { window.removeEventListener('pointerdown', away, true); window.removeEventListener('keydown', key, true); window.removeEventListener('blur', onClose); };
    }, [onClose]);
    // keep the menu inside the window
    const h = items.reduce((n, i) => n + (i === 'sep' ? 9 : 28), 8);
    const left = Math.min(at.x, window.innerWidth - 250), top = Math.max(4, Math.min(at.y, window.innerHeight - h - 4));
    return (
        <div className="menu ctx-menu" ref={ref} role="menu" style={{ left, top }} onContextMenu={e => e.preventDefault()}>
            {items.map((i, k) => i === 'sep'
                ? <div key={k} className="ctx-sep" />
                : <button key={k} role="menuitem" disabled={i.disabled} onClick={() => { onClose(); i.run(); }}>
                    <span>{i.label}</span>{i.key && <span className="dim small">{i.key}</span>}
                </button>)}
        </div>
    );
}

export function Timeline() {
    const tl = useStore(s => s.timeline);
    const pool = useStore(s => s.pool);
    const time = useStore(s => s.time);
    const analysis = useStore(s => s.analysis);
    const zoom = useStore(s => s.timelineZoom);
    const snapping = useStore(s => s.snapping);
    const selClip = useStore(s => s.selectedClipId);
    const selTrack = useStore(s => s.selectedTrackId);
    const heights = useStore(s => s.trackHeights);
    const range = useStore(s => s.rangeSel);
    const caps = useStore(s => s.caps);
    const scrollRef = useRef<HTMLDivElement>(null);
    const bodyRef = useRef<HTMLDivElement>(null);
    const scrubbing = useRef(false);
    const [menu, setMenu] = useState<MenuAt | null>(null);
    const dur = timelineDuration(tl);
    const selected = selClip ? clipById(tl, selClip) : undefined;
    const rowH = (id: string, kind: TrackKind) => heights[id] ?? DEFAULT_H[kind];

    useEffect(() => {
        // keep the playhead visible while the timeline plays
        const el = scrollRef.current;
        if (!el) return;
        const x = time * zoom;
        if (x < el.scrollLeft || x > el.scrollLeft + el.clientWidth - 20) el.scrollLeft = Math.max(0, x - el.clientWidth * 0.2);
    }, [time, zoom]);

    // mouse wheel over the tracks: zoom the time scale around the pointer (out = clips get shorter, more of the
    // timeline fits); Alt+wheel: track height; Ctrl+wheel: scroll the tracks; Shift+wheel: scroll sideways
    useEffect(() => {
        const el = scrollRef.current!;
        const onWheel = (e: WheelEvent) => {
            if (e.shiftKey) return;
            e.preventDefault();
            const dy = e.deltaY || e.deltaX;
            if (e.ctrlKey || e.metaKey) { bodyRef.current!.scrollTop += dy; return; }
            if (e.altKey) {
                const st = getState();
                const f = dy < 0 ? 1.15 : 1 / 1.15;
                setState({ trackHeights: Object.fromEntries(st.timeline.tracks.map(t => [t.id, clampH((st.trackHeights[t.id] ?? DEFAULT_H[t.kind]) * f)])) });
                return;
            }
            const r = el.getBoundingClientRect();
            const z = getState().timelineZoom;
            const t = Math.max(0, (e.clientX - r.left + el.scrollLeft) / z);
            setTimelineZoom(z * (dy < 0 ? 1.2 : 1 / 1.2));
            const next = getState().timelineZoom;
            requestAnimationFrame(() => { el.scrollLeft = Math.max(0, t * next - (e.clientX - r.left)); });
        };
        el.addEventListener('wheel', onWheel, { passive: false });
        return () => el.removeEventListener('wheel', onWheel);
    }, []);

    const timeAt = (clientX: number) => {
        const r = scrollRef.current!.getBoundingClientRect();
        return Math.max(0, (clientX - r.left + scrollRef.current!.scrollLeft) / zoom);
    };
    const scrub = (e: React.PointerEvent) => player.seek(timeAt(e.clientX));

    /**
     * Left button on an empty part of a lane or on the ruler: a click moves the playhead; holding the button and
     * dragging marks a time range – from a lane on every (unlocked) track the drag passes over, from the ruler
     * (`trackId` null) on all unlocked tracks. Edges snap like clip edges.
     */
    const startMarking = (e: React.PointerEvent, trackId: string | null) => {
        if (e.button !== 0) return;
        const x0 = e.clientX, y0 = e.clientY, tStart = timeAt(e.clientX);
        let marking = false;
        const edge = (t: number) => {
            const st = getState();
            return snapEdge(st.timeline, t, st.snapping, snapTolFrames(st.timeline, st.timelineZoom));
        };
        const lanesBetween = (ya: number, yb: number) => {
            const lo = Math.min(ya, yb), hi = Math.max(ya, yb) + 1;
            return [...scrollRef.current!.querySelectorAll<HTMLElement>('.lane')]
                .filter(l => { const r = l.getBoundingClientRect(); return r.bottom > lo && r.top < hi; })
                .map(l => l.dataset.trackId!)
                .filter(id => !trackById(getState().timeline, id)?.locked);
        };
        const move = (ev: PointerEvent) => {
            if (!marking && Math.abs(ev.clientX - x0) < 4 && Math.abs(ev.clientY - y0) < 4) return;
            marking = true;
            const trackIds = trackId ? lanesBetween(y0, ev.clientY) : getState().timeline.tracks.filter(t => !t.locked).map(t => t.id);
            setRangeSel({ t0: edge(tStart), t1: edge(timeAt(ev.clientX)), trackIds });
        };
        const up = (ev: PointerEvent) => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            if (trackId) selectTrack(trackId);
            if (marking) return;
            selectClip(null);
            setRangeSel(null);
            player.seek(timeAt(ev.clientX));
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
    };
    const inRange = (c: { trackId: string; start: number; dur: number }) =>
        !!range && range.trackIds.includes(c.trackId) && c.start < range.t1 - 1e-9 && c.start + c.dur > range.t0 + 1e-9;

    /** Drag on the lower edge of a track header to change that track's height. */
    const startResize = (e: React.PointerEvent, trackId: string, kind: TrackKind) => {
        e.preventDefault();
        e.stopPropagation();
        const y0 = e.clientY, h0 = rowH(trackId, kind);
        const move = (ev: PointerEvent) => setState(st => ({ trackHeights: { ...st.trackHeights, [trackId]: clampH(h0 + ev.clientY - y0) } }));
        const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
    };

    const openMenu = (e: React.MouseEvent, trackId: string) => {
        e.preventDefault();
        const clipId = (e.target as HTMLElement).closest<HTMLElement>('[data-clip-id]')?.dataset.clipId ?? null;
        const t = timeAt(e.clientX);
        const r = getState().rangeSel;
        // right-click inside the marked range keeps the marking (its menu acts on the range)
        const keepRange = !!r && r.trackIds.includes(trackId) && t >= r.t0 - 1e-9 && t <= r.t1 + 1e-9;
        selectTrack(trackId);
        if (!keepRange) { setRangeSel(null); selectClip(clipId); }
        setMenu({ x: e.clientX, y: e.clientY, trackId, clipId, t });
    };

    const width = Math.max(((dur || 10) + 10) * zoom, 400);
    const step = TICK_STEPS.find(s => s * zoom >= 80) ?? 3600;
    const ticks: number[] = [];
    for (let t = 0; t * zoom <= width; t += step) ticks.push(t);
    const nameOf = (sourceId: string) => pool.find(p => p.sourceId === sourceId)?.media.name || tl.sources.find(s => s.id === sourceId)?.path.split(/[\\/]/).pop() || 'Media';

    return (
        <div className="timeline nle">
            <div className="timeline-tools">
                <button className="tool-btn" onClick={() => razorAtPlayhead()} title={`Razor: split the selected clip, or all tracks, at the playhead (${sc('razor')})`}>Split</button>
                <button className="tool-btn" disabled={!selected} onClick={() => rippleTrimSelectedToPlayhead()} title={`Ripple trim the selected clip's end to the playhead (${sc('rippleTrimEnd')})`}>Ripple trim</button>
                <button className="tool-btn" disabled={!selected && !range} onClick={() => removeSelectedClip('lift')} title={`Delete the selected clip or the marked range, keep the gap (${sc('lift')})`}>Lift</button>
                <button className="tool-btn" disabled={!selected && !range} onClick={() => removeSelectedClip('ripple')} title={`Delete the selected clip or the marked range and close the gap (${sc('ripple')})`}>Ripple delete</button>
                <button className="icon-btn" disabled={!selected} onClick={() => duplicateSelectedClip()} title={`Duplicate the selected clip (${sc('duplicate')})`} aria-label="Duplicate clip"><Icon name="copy" /></button>
                <Select value={selected?.transition?.kind ?? 'none'} disabled={!selected} title={`Transition on the selected clip's left cut (${sc('transition')}: dissolve)`}
                    options={[{ value: 'none', label: 'No transition' }, ...TRANSITION_KINDS.map(k => ({ value: k, label: transitionLabel(k) }))]}
                    onChange={k => { if (!selected) return; if (k === 'none') editTimeline(t => setTransition(t, selected.id, null)); else addDefaultTransition(selected.id, k as TransitionKind); }} />
                <button className="tool-btn" onClick={() => editTimeline(t => addMarker(t, getState().time))} title={`Add a marker at the playhead (${sc('marker')})`}>Marker</button>
                <span className="spacer" />
                {analysis.status === 'running' && <span className="dim small">Auto-Scene-Match analysis {Math.round(analysis.pct * 100)}%</span>}
                <button className={`tool-btn${snapping ? ' on' : ''}`} onClick={() => setSnapping(!snapping)} aria-pressed={snapping} title={`Snap to clip edges, markers and frames (${sc('snapping')})`}>Snap</button>
                <button className="icon-btn" onClick={() => setTimelineZoom(zoom / 1.5)} title={`Zoom out (${sc('zoomOut')}, mouse wheel)`} aria-label="Zoom out"><Icon name="zoomOut" /></button>
                <button className="tool-btn" disabled={!dur} onClick={zoomToFit} title={`Fit the whole timeline into view (${sc('zoomFit')})`}>{zoom >= 10 ? Math.round(zoom) : zoom.toFixed(1)} px/s</button>
                <button className="icon-btn" onClick={() => setTimelineZoom(zoom * 1.5)} title={`Zoom in (${sc('zoomIn')}, mouse wheel)`} aria-label="Zoom in"><Icon name="zoomIn" /></button>
                <button className="tool-btn primary" disabled={!tl.clips.length || !caps?.ok} onClick={() => enqueueTimelineJob()} title={`Render the whole timeline into the render queue (${sc('render')})`}>Render timeline</button>
            </div>
            <div className="nle-body" ref={bodyRef}>
                <div className="nle-heads">
                    <div className="nle-corner" style={{ height: RULER_H }}>
                        <button className="icon-btn tiny" onClick={() => insertTrack('video', 0)} title={`Add a video track on top (${sc('addVideoTrack')})`} aria-label="Add video track"><Icon name="plus" size={12} /> V</button>
                        <button className="icon-btn tiny" onClick={() => insertTrack('audio')} title={`Add an audio track at the bottom (${sc('addAudioTrack')})`} aria-label="Add audio track"><Icon name="plus" size={12} /> A</button>
                    </div>
                    {tl.tracks.map((t, i) => (
                        <div key={t.id} className="trk-wrap" onContextMenu={e => openMenu(e, t.id)}>
                            <TrackHeader track={t} index={i} count={tl.tracks.length} selected={t.id === selTrack} height={rowH(t.id, t.kind)} />
                            <span className="trk-resize" title="Drag to change the track height (Alt+wheel: all tracks; double-click: default)"
                                onPointerDown={e => startResize(e, t.id, t.kind)}
                                onDoubleClick={() => setState(st => { const h = { ...st.trackHeights }; delete h[t.id]; return { trackHeights: h }; })} />
                        </div>
                    ))}
                </div>
                <div className="nle-scroll" ref={scrollRef}>
                    <div className="nle-inner" style={{ width }}>
                        <div className="nle-ruler" style={{ height: RULER_H }}
                            title="Drag to mark a range on all tracks · click to move the playhead · drag the playhead to scrub"
                            onPointerDown={e => {
                                if (e.button !== 0) return;
                                // grabbing the playhead itself scrubs; anywhere else a drag marks a range
                                if (Math.abs(timeAt(e.clientX) - getState().time) * zoom <= 6) {
                                    scrubbing.current = true;
                                    (e.currentTarget as Element).setPointerCapture(e.pointerId);
                                    scrub(e);
                                    return;
                                }
                                startMarking(e, null);
                            }}
                            onPointerMove={e => { if (scrubbing.current) scrub(e); }}
                            onPointerUp={() => { scrubbing.current = false; }}>
                            {ticks.map(t => <span key={t} className="tick" style={{ left: t * zoom }}>{fmtTime(t, step < 1)}</span>)}
                            {tl.markers.map(m => <MarkerFlag key={m.id} id={m.id} t={m.t} name={m.name} zoom={zoom} />)}
                            {/* shown in the ruler, not the toolbar, so the lanes never shift while a range is dragged */}
                            {range && (
                                <div className="range-sel ruler" style={{ left: range.t0 * zoom, width: (range.t1 - range.t0) * zoom }}
                                    title={`Marked ${fmtTime(range.t0, true)} – ${fmtTime(range.t1, true)} on ${range.trackIds.length} track(s)\nDelete / Shift+Delete, Ctrl+C / Ctrl+X, right-click for more, Esc clears`}>
                                    <span className="range-label">{fmtTime(range.t1 - range.t0, true)}</span>
                                </div>
                            )}
                        </div>
                        {tl.tracks.map(t => {
                            const h = rowH(t.id, t.kind);
                            return (
                                <div key={t.id} className={`lane ${t.kind}${t.id === selTrack ? ' selected' : ''}${t.hidden || t.muted ? ' off' : ''}`} data-track-id={t.id} style={{ height: h }}
                                    onPointerDown={e => startMarking(e, t.id)}
                                    onContextMenu={e => openMenu(e, t.id)}
                                    onDragOver={e => { if (e.dataTransfer.types.includes(POOL_DRAG_TYPE)) { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; } }}
                                    onDrop={e => {
                                        const id = e.dataTransfer.getData(POOL_DRAG_TYPE);
                                        const entry = getState().pool.find(p => p.sourceId === id);
                                        if (!entry) return;
                                        e.preventDefault();
                                        e.stopPropagation();
                                        addToTimeline(entry, t.id, timeAt(e.clientX));
                                    }}>
                                    {clipsOn(tl, t.id).map(c => (
                                        <ClipBox key={c.id} tl={tl} clip={c} track={t} zoom={zoom} height={h} selected={c.id === selClip || inRange(c)} name={nameOf(c.sourceId)} />
                                    ))}
                                    {range?.trackIds.includes(t.id) && <div className="range-sel" style={{ left: range.t0 * zoom, width: (range.t1 - range.t0) * zoom }} />}
                                </div>
                            );
                        })}
                        {tl.markers.map(m => <span key={m.id} className="marker-line" style={{ left: m.t * zoom }} />)}
                        <div className="playhead" style={{ left: time * zoom }}><span /></div>
                        {!tl.clips.length && <div className="nle-empty" style={{ top: RULER_H }}>Open a video or audio file, or drag one from the Media Pool onto a track. A video's sound gets its own clip on the audio track.</div>}
                    </div>
                </div>
            </div>
            {menu && <TimelineMenu at={menu} onClose={() => setMenu(null)} />}
        </div>
    );
}
