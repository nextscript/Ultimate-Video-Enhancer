import type { PoolSource } from '../state/store';
import { getState, useStore } from '../state/store';
import { loadVideo, placeMedia, showSourceInMonitor } from '../state/actions';
import { api } from '../api';
import { Icon, fmtTime } from './controls';

/** dataTransfer type of a pool item dragged onto a timeline track */
export const POOL_DRAG_TYPE = 'application/x-uve-source';

/** Track a pool source lands on: the selected track when its kind fits, else the first track of the fitting kind. */
function targetTrackFor(entry: PoolSource): string | undefined {
    const st = getState();
    const kind = entry.media.video ? 'video' : 'audio';
    const sel = st.timeline.tracks.find(t => t.id === st.selectedTrackId);
    if (sel && sel.kind === kind && !sel.locked) return sel.id;
    return st.timeline.tracks.find(t => t.kind === kind && !t.locked)?.id;
}

/** Puts a pool file on the timeline (default: at the playhead); a video's sound lands on an audio track. */
export function addToTimeline(entry: PoolSource, trackId?: string, start?: number): boolean {
    return placeMedia(entry.sourceId, { trackId: trackId ?? targetTrackFor(entry), start: start ?? getState().time });
}

export function MediaPool() {
    const pool = useStore(s => s.pool);
    const sources = useStore(s => s.timeline.sources);
    const clips = useStore(s => s.timeline.clips);
    const currentPath = useStore(s => s.media?.path);
    const list = pool.filter(p => sources.some(s => s.id === p.sourceId));
    return (
        <div className="panel media-pool">
            <h3 className="row gap"><Icon name="folder" /> Media Pool <span className="grow" />
                <button className="icon-btn" title="Import video or audio files into the pool" aria-label="Import media" onClick={async () => {
                    const paths = await api.openVideos(true);
                    for (const p of paths) await loadVideo(p);
                }}><Icon name="plus" /></button>
            </h3>
            {!list.length && <p className="hint">Imported video and audio files appear here. Drag one onto a track or press + to add it at the playhead – a video's sound goes onto an audio track.</p>}
            <div className="pool-list">
                {list.map(e => {
                    const used = clips.filter(c => c.sourceId === e.sourceId).length;
                    return (
                        <div key={e.sourceId} className={`pool-item${currentPath === e.path ? ' active' : ''}${e.missing ? ' missing' : ''}`} draggable={!e.missing}
                            title={`${e.path}\nClick: make it the current file (analysis, media info) · Drag onto a track to add`}
                            onDragStart={ev => { ev.dataTransfer.setData(POOL_DRAG_TYPE, e.sourceId); ev.dataTransfer.effectAllowed = 'copy'; }}
                            onClick={() => showSourceInMonitor(e.sourceId)}>
                            {e.thumbs[0] ? <img src={e.thumbs[0].url} alt="" draggable={false} /> : <span className="pool-thumb"><Icon name={e.media.video ? 'editor' : 'volume'} /></span>}
                            <span className="pool-name">{e.media.name || e.path.split(/[\\/]/).pop()}</span>
                            <span className="dim small">{fmtTime(e.media.duration)}{used ? ` · ${used}×` : ''}{e.proxied ? ' · proxy' : ''}{e.missing ? ' · missing' : ''}</span>
                            <button className="icon-btn" disabled={e.missing} title="Add to the timeline at the playhead" aria-label="Add to timeline"
                                onClick={ev => { ev.stopPropagation(); addToTimeline(e); }}><Icon name="plus" size={14} /></button>
                        </div>
                    );
                })}
            </div>
        </div>
    );
}
