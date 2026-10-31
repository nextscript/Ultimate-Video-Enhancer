import { useEffect, useState } from 'react';
import type { TimelineTrack } from '../../shared/types';
import { moveTrack, patchTrack, removeTrack } from '../../shared/timeline';
import { editTimeline, selectTrack } from '../state/actions';
import { Icon } from './controls';

/** Track header row: name, mute / solo / hide / lock, gain (dB) and pan, reorder and remove. */
export function TrackHeader({ track, index, count, selected, height }: { track: TimelineTrack; index: number; count: number; selected: boolean; height: number }) {
    const [name, setName] = useState(track.name);
    useEffect(() => setName(track.name), [track.name]);
    const patch = (p: Partial<TimelineTrack>, key = '') => editTimeline(tl => patchTrack(tl, track.id, p), key);
    const flag = (k: 'muted' | 'solo' | 'hidden' | 'locked', label: string, title: string) => (
        <button className={`trk-flag${track[k] ? ` on ${k}` : ''}`} onClick={e => { e.stopPropagation(); patch({ [k]: !track[k] }); }} title={title} aria-pressed={track[k]} aria-label={title}>{label}</button>
    );
    return (
        <div className={`trk-head ${track.kind}${selected ? ' selected' : ''}${height < 50 ? ' compact' : ''}`} style={{ height }} onPointerDown={() => selectTrack(track.id)}>
            <div className="row trk-top">
                <span className={`trk-kind ${track.kind}`}>{track.kind === 'video' ? 'V' : 'A'}</span>
                <input className="trk-name" value={name} aria-label="Track name" onChange={e => setName(e.target.value)}
                    onBlur={() => { if (name.trim() && name !== track.name) patch({ name: name.trim() }); else setName(track.name); }}
                    onKeyDown={e => { if (e.key === 'Enter') (e.target as HTMLInputElement).blur(); e.stopPropagation(); }} />
                {flag('muted', 'M', track.kind === 'video' ? 'Mute the audio of this track' : 'Mute track')}
                {flag('solo', 'S', 'Solo: only solo tracks are heard')}
                {track.kind === 'video' && flag('hidden', 'H', 'Hide the picture of this track')}
                {flag('locked', 'L', 'Lock track (no edits)')}
            </div>
            <div className="row trk-mix">
                <label title="Track gain (dB) – audio level, and picture brightness on video tracks. Double-click resets.">
                    <span className="dim small">{track.gainDb > 0 ? '+' : ''}{track.gainDb.toFixed(1)} dB</span>
                    <input type="range" min={-30} max={12} step={0.5} value={track.gainDb} aria-label="Track gain"
                        onChange={e => patch({ gainDb: Number(e.target.value) }, `tg:${track.id}`)} onDoubleClick={() => patch({ gainDb: 0 })} />
                </label>
                <label title="Track pan (left … right). Double-click centres.">
                    <span className="dim small">{track.pan === 0 ? 'C' : track.pan < 0 ? `L${Math.round(-track.pan * 100)}` : `R${Math.round(track.pan * 100)}`}</span>
                    <input type="range" min={-1} max={1} step={0.05} value={track.pan} aria-label="Track pan"
                        onChange={e => patch({ pan: Number(e.target.value) }, `tp:${track.id}`)} onDoubleClick={() => patch({ pan: 0 })} />
                </label>
                <span className="grow" />
                <button className="icon-btn tiny" disabled={index === 0} title="Move track up" aria-label="Move track up" onClick={() => editTimeline(tl => moveTrack(tl, track.id, index - 1))}><Icon name="up" size={12} /></button>
                <button className="icon-btn tiny" disabled={index >= count - 1} title="Move track down" aria-label="Move track down" onClick={() => editTimeline(tl => moveTrack(tl, track.id, index + 1))}><Icon name="down" size={12} /></button>
                <button className="icon-btn tiny" title="Remove track (only when it is empty)" aria-label="Remove track" onClick={() => editTimeline(tl => removeTrack(tl, track.id))}><Icon name="trash" size={12} /></button>
            </div>
        </div>
    );
}
