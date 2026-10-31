import { useRef, useState } from 'react';
import type { Timeline, TimelineClip, TimelineTrack } from '../../shared/types';
import { setTransition } from '../../shared/timeline';
import { editTimeline, selectClip, selectTrack } from '../state/actions';
import { getState } from '../state/store';
import { addDefaultTransition } from './keys';
import {
    commitFade, commitMove, commitTransitionDur, commitTrim, prevClipOf, snapMoveStart, snapTolFrames, transitionSpan, trimEdges
} from './timelineMath';

type Drag =
    | { kind: 'move'; x0: number; y0: number; dx: number; trackId: string | null; moved: boolean }
    | { kind: 'trimL' | 'trimR'; x0: number; dx: number }
    | { kind: 'fadeIn' | 'fadeOut'; x0: number; dx: number }
    | { kind: 'trans'; x0: number; y0: number; dx: number; off: boolean };

const TRANSITION_LABEL: Record<string, string> = {
    dissolve: 'Dissolve', fadeBlack: 'Fade via black', fadeWhite: 'Fade via white', wipeLeft: 'Wipe left', wipeRight: 'Wipe right',
    wipeUp: 'Wipe up', wipeDown: 'Wipe down', slideLeft: 'Slide left', slideRight: 'Slide right', smoothLeft: 'Smooth left',
    smoothRight: 'Smooth right', circleOpen: 'Circle open', circleClose: 'Circle close'
};
export const transitionLabel = (k: string) => TRANSITION_LABEL[k] ?? k;

/** Track lane under a screen point (cross-track drags). */
function laneAt(x: number, y: number): string | null {
    const el = document.elementFromPoint(x, y) as HTMLElement | null;
    return el?.closest<HTMLElement>('[data-track-id]')?.dataset.trackId ?? null;
}

/**
 * One clip on a track lane: body drag = move (within / across tracks), edges = trim (Alt on the right edge =
 * ripple trim), top corners = fade in/out, left-edge diamond = transition on this clip's LEFT cut (click adds a
 * dissolve, drag sets its length, drag off the lane removes it).
 */
export function ClipBox({ tl, clip, track, zoom, height, selected, name }: {
    tl: Timeline; clip: TimelineClip; track: TimelineTrack; zoom: number; height: number; selected: boolean; name: string;
}) {
    const [drag, setDrag] = useState<Drag | null>(null);
    const dragRef = useRef<Drag | null>(null);
    // Alt during a right-edge drag = ripple trim; tracked so the live preview matches the commit
    const altHeld = useRef(false);
    const set = (d: Drag | null) => { dragRef.current = d; setDrag(d); };
    const F = tl.frameRate;
    const tol = snapTolFrames(tl, zoom);
    const snapOn = () => getState().snapping;

    const begin = (e: React.PointerEvent, d: Drag) => {
        if (e.button !== 0) return;
        // Shift+drag anywhere on a clip marks a range instead (handled by the lane), like a drag on empty track space
        if (e.shiftKey) return;
        e.stopPropagation();
        selectClip(clip.id);
        selectTrack(track.id);
        if (track.locked) return;
        (e.currentTarget as Element).setPointerCapture(e.pointerId);
        set(d);
    };
    const onMove = (e: React.PointerEvent) => {
        const d = dragRef.current;
        if (!d) return;
        altHeld.current = e.altKey;
        const dx = e.clientX - d.x0;
        if (d.kind === 'move') {
            const moved = d.moved || Math.abs(dx) > 3 || Math.abs(e.clientY - d.y0) > 3;
            set({ ...d, dx, moved, trackId: laneAt(e.clientX, e.clientY) });
        } else if (d.kind === 'trans') {
            set({ ...d, dx, off: Math.abs(e.clientY - d.y0) > height * 0.75 });
        } else set({ ...d, dx });
    };
    const onUp = (e: React.PointerEvent) => {
        const d = dragRef.current;
        set(null);
        if (!d) return;
        const dt = d.dx / zoom;
        switch (d.kind) {
            case 'move': {
                if (!d.moved) return;
                const target = d.trackId ?? track.id;
                const kind = tl.tracks.find(t => t.id === target)?.kind;
                if (kind && kind !== track.kind) { editTimeline(() => ({ ok: false, error: `A clip on ${track.name} cannot move to a ${kind} track.` })); return; }
                editTimeline(t => commitMove(t, clip.id, clip.start + dt, target, snapOn(), tol));
                return;
            }
            case 'trimL': editTimeline(t => commitTrim(t, clip.id, 'left', dt, false, snapOn(), tol)); return;
            case 'trimR': editTimeline(t => commitTrim(t, clip.id, 'right', dt, e.altKey, snapOn(), tol)); return;
            case 'fadeIn': editTimeline(t => commitFade(t, clip.id, 'in', fadeInOf(clip, track) + dt)); return;
            case 'fadeOut': editTimeline(t => commitFade(t, clip.id, 'out', fadeOutOf(clip, track) - dt)); return;
            case 'trans': {
                if (d.off && clip.transition) { editTimeline(t => setTransition(t, clip.id, null)); return; }
                if (!clip.transition) { if (Math.abs(d.dx) < 3) addDefaultTransition(clip.id); else editTimeline(t => setTransition(t, clip.id, 'dissolve', Math.max(1 / F, 2 * Math.abs(dt)))); return; }
                const span = transitionSpan(tl, clip)!;
                editTimeline(t => commitTransitionDur(t, clip.id, span.t1 + dt));
                return;
            }
        }
    };

    // live geometry while dragging: snapped and clamped exactly like the commit (never before 00:00)
    let left = clip.start * zoom, width = Math.max(2, clip.dur * zoom), dy = 0;
    if (drag?.kind === 'move' && drag.moved) left = snapMoveStart(tl, clip, clip.start + drag.dx / zoom, snapOn(), tol) * zoom;
    if (drag?.kind === 'trimL' || drag?.kind === 'trimR') {
        const r = trimEdges(tl, clip, drag.kind === 'trimL' ? 'left' : 'right', drag.dx / zoom, drag.kind === 'trimR' && altHeld.current, snapOn(), tol);
        left = r.start * zoom;
        width = Math.max(2, (r.end - r.start) * zoom);
    }
    if (drag?.kind === 'move' && drag.moved && drag.trackId && drag.trackId !== track.id) {
        const from = document.querySelector<HTMLElement>(`[data-track-id="${track.id}"]`), to = document.querySelector<HTMLElement>(`[data-track-id="${drag.trackId}"]`);
        if (from && to) dy = to.getBoundingClientRect().top - from.getBoundingClientRect().top;
    }
    const fi = fadeInOf(clip, track) + (drag?.kind === 'fadeIn' ? drag.dx / zoom : 0);
    const fo = fadeOutOf(clip, track) - (drag?.kind === 'fadeOut' ? drag.dx / zoom : 0);
    const clampF = (v: number) => Math.max(0, Math.min(clip.dur, v)) * zoom;
    const span = transitionSpan(tl, clip);
    const hasLeftNeighbour = !!prevClipOf(tl, clip) && Math.abs((prevClipOf(tl, clip)!.start + prevClipOf(tl, clip)!.dur) - clip.start) < 1e-6;
    const transW = span ? Math.max(4, (span.t1 - span.t0) * zoom + (drag?.kind === 'trans' ? drag.dx * 2 : 0)) : 0;
    const transL = span ? (span.t0 - clip.start) * zoom - (drag?.kind === 'trans' ? drag.dx : 0) : 0;
    const badges = [clip.speed !== 1 ? `${+clip.speed.toFixed(3)}×` : null, clip.reverse ? 'REV' : null, clip.hidden ? 'hidden' : null, clip.audioMuted ? 'muted' : null, clip.effect ? 'look' : null].filter(Boolean);

    return (
        <div className={`clip ${track.kind}${selected ? ' selected' : ''}${drag ? ' dragging' : ''}${track.locked ? ' locked' : ''}`}
            style={{ left, width, height: height - 6, transform: dy ? `translateY(${dy}px)` : undefined }}
            data-clip-id={clip.id}
            title={`${name}\n${clip.start.toFixed(3)} s – ${(clip.start + clip.dur).toFixed(3)} s · in ${clip.srcIn.toFixed(3)} s`}
            onPointerDown={e => begin(e, { kind: 'move', x0: e.clientX, y0: e.clientY, dx: 0, trackId: null, moved: false })}
            onPointerMove={onMove} onPointerUp={onUp} onPointerCancel={() => set(null)}>
            <svg className="clip-fades" width="100%" height="100%" preserveAspectRatio="none" aria-hidden>
                {fi > 0 && <polygon points={`0,0 ${clampF(fi)},0 0,${height}`} />}
                {fo > 0 && <polygon points={`${width},0 ${width - clampF(fo)},0 ${width},${height}`} />}
            </svg>
            <span className="clip-name">{name}</span>
            {badges.length > 0 && <span className="clip-badges">{badges.map(b => <span key={b!}>{b}</span>)}</span>}
            <span className="clip-edge left" title="Trim the start" onPointerDown={e => begin(e, { kind: 'trimL', x0: e.clientX, dx: 0 })} onPointerMove={onMove} onPointerUp={onUp} />
            <span className="clip-edge right" title="Trim the end (Alt: ripple trim)" onPointerDown={e => begin(e, { kind: 'trimR', x0: e.clientX, dx: 0 })} onPointerMove={onMove} onPointerUp={onUp} />
            <span className="clip-fade in" style={{ left: clampF(fi) }} title="Fade in" onPointerDown={e => begin(e, { kind: 'fadeIn', x0: e.clientX, dx: 0 })} onPointerMove={onMove} onPointerUp={onUp} />
            <span className="clip-fade out" style={{ left: width - clampF(fo) }} title="Fade out" onPointerDown={e => begin(e, { kind: 'fadeOut', x0: e.clientX, dx: 0 })} onPointerMove={onMove} onPointerUp={onUp} />
            {span && (
                <span className={`clip-trans${drag?.kind === 'trans' && drag.off ? ' off' : ''}`} style={{ left: transL, width: transW }}
                    title={`${transitionLabel(clip.transition!.kind)} · ${clip.transition!.dur.toFixed(2)} s – drag the handle to change the length, drag it off the track to remove`}>
                    <span className="clip-trans-label">{transitionLabel(clip.transition!.kind)}</span>
                    <span className="clip-trans-handle" onPointerDown={e => begin(e, { kind: 'trans', x0: e.clientX, y0: e.clientY, dx: 0, off: false })} onPointerMove={onMove} onPointerUp={onUp} />
                </span>
            )}
            {!span && hasLeftNeighbour && (
                <span className="clip-trans-add" title="Transition from the previous clip: click for a dissolve, drag to set its length"
                    onPointerDown={e => begin(e, { kind: 'trans', x0: e.clientX, y0: e.clientY, dx: 0, off: false })} onPointerMove={onMove} onPointerUp={onUp}>◆</span>
            )}
        </div>
    );
}

function fadeInOf(clip: TimelineClip, track: TimelineTrack): number { return track.kind === 'video' ? clip.fadeIn : clip.audioFadeIn; }
function fadeOutOf(clip: TimelineClip, track: TimelineTrack): number { return track.kind === 'video' ? clip.fadeOut : clip.audioFadeOut; }
