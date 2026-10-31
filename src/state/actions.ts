import { zipSync, strToU8 } from 'fflate';
import { api } from '../api';
import { APP_VERSION, DEFAULT_EXPORT, UVE_SOURCE, type EffectState, type JobSpec, type MediaInfo, type Timeline, type TimelineClip, type TrackKind, type UserProfile, type UveProject } from '../../shared/types';
import { runAutoSceneMatch, samplesFromAnalysis, type AnalysisSample } from '../../shared/uve/auto';
import { type CustomFilterEntry, exportFilterCodesJson, normalizeEntry, parseFilterCodesJson } from '../../shared/uve/customFilters';
import { expertDiff, expertFromDiff } from '../../shared/uve/expert';
import { type LutProfile, cubeTextToMatrix4x5, lutKeyOf, lutProfileToJson, parseLutProfileObject, tiledRgbaToMatrix4x5 } from '../../shared/uve/lut';
import { mergeProfiles, newProfileId, parseUserProfileObject, sanitizeProfileFilename } from '../../shared/uve/profiles';
import { DEFAULT_SETTINGS, type ToneProfile, type UveSettings, sanitizeSettings, settingsToGvfSettings } from '../../shared/uve/settings';
import { applyTimeline, getState, initialEffect, resetHistory, setState, toast, updateEffect, type PoolSource, type RangeSel } from './store';
import { parseFps } from '../../shared/fps';
import {
    addSource, addTrack, clipById, clipEnd, defaultTimeline, deleteClip, duplicateClip, frameRateFromMedia, insertClip,
    migrateProjectV1toV2, moveClip, pairTracks, patchClip, sanitizeTimeline, snap, sourceById, splitClip, timelineDuration, trackById, validateTimeline,
    type OpResult
} from '../../shared/timeline';

const errMsg = (e: unknown) => (e instanceof Error ? e.message.replace(/^Error invoking remote method '[^']+': (Error: )?/, '') : String(e));
export { errMsg };

// ------------------------------------------------------------------ settings helpers
export function setSetting<K extends keyof UveSettings>(key: K, value: UveSettings[K], coalesce = true) {
    updateEffect(e => ({ ...e, settings: { ...e.settings, [key]: value } }), coalesce ? `s:${String(key)}` : '');
}

export function toggleSetting(key: 'enabled' | 'darkMoody' | 'tealOrange' | 'vibrantSat' | 'autoOn' | 'autoLockWB') {
    const v = !getState().effect.settings[key];
    setSetting(key, v, false);
    const names: Record<string, string> = { enabled: 'Base Tone', darkMoody: 'Dark & Moody', tealOrange: 'Teal & Orange', vibrantSat: 'Vibrant', autoOn: 'Auto-Scene-Match', autoLockWB: 'Auto White Balance' };
    toast(`${names[key]} ${v ? 'on' : 'off'}`, 'info', 1500);
}

/** HDR toggle like Ctrl+Alt+P: remembers the last strength (GVF_HDR_SETTINGS.useLastValue). */
let hdrLast = 0.3;
export function toggleHdr() {
    const cur = getState().effect.settings.hdr;
    if (cur !== 0) { hdrLast = cur; setSetting('hdr', 0, false); toast('HDR off', 'info', 1500); }
    else { setSetting('hdr', hdrLast || 0.3, false); toast(`HDR on (${(hdrLast || 0.3).toFixed(2)})`, 'info', 1500); }
}

const PROFILE_CYCLE: ToneProfile[] = ['off', 'film', 'anime', 'gaming', 'eyecare', 'user'];
export function cycleToneProfile() {
    const cur = getState().effect.settings.profile;
    const next = PROFILE_CYCLE[(PROFILE_CYCLE.indexOf(cur) + 1) % PROFILE_CYCLE.length];
    setSetting('profile', next, false);
    toast(`Profile: ${next}`, 'info', 1500);
}

// ------------------------------------------------------------------ media pool
/**
 * Makes one pool file the "current" file: the one Auto-Scene-Match analyses and the media info panel shows.
 * The editor always plays the timeline, so the playhead is left where it is.
 */
function setSourceMonitor(entry: PoolSource) {
    if (!entry.media.video) return;
    setState({
        media: entry.media, mediaUrl: entry.url, thumbs: entry.thumbs,
        mediaError: entry.missing ? `Media not found: ${entry.path}` : null,
        inPoint: null, outPoint: null, proxyPct: null,
        analysis: { status: 'none', pct: 0, keys: null }
    });
}

/** Adds or refreshes one pool entry; a preview proxy made for the same file is kept. */
async function ensurePoolEntry(sourceId: string, media: MediaInfo): Promise<PoolSource> {
    const old = getState().pool.find(p => p.sourceId === sourceId || p.path === media.path);
    const keepProxy = !!old && old.proxied && old.path === media.path;
    const entry: PoolSource = {
        sourceId, path: media.path, media, thumbs: old?.thumbs ?? [],
        url: keepProxy ? old.url : await api.mediaUrl(media.path), proxied: keepProxy, missing: false
    };
    setState(st => ({ pool: [...st.pool.filter(p => p.sourceId !== sourceId && p.path !== media.path), entry] }));
    if (!entry.thumbs.length && entry.url) {
        void api.thumbs(media, 24)
            .then(thumbs => setState(st => ({ pool: st.pool.map(p => p.sourceId === sourceId ? { ...p, thumbs } : p) })))
            .catch(() => undefined);
    }
    return entry;
}

/** Re-probes every timeline source and rebuilds the pool; stale entries are dropped. */
export async function rebuildPool(tl: Timeline) {
    for (const s of tl.sources) {
        try {
            await ensurePoolEntry(s.id, await api.probe(s.path));
        } catch {
            setState(st => ({
                pool: [...st.pool.filter(p => p.sourceId !== s.id),
                    { sourceId: s.id, path: s.path, media: s.media, url: '', proxied: false, missing: true, thumbs: [] }]
            }));
        }
    }
    setState(st => ({ pool: st.pool.filter(p => tl.sources.some(s => s.id === p.sourceId || s.path === p.path)) }));
    const primary = getState().pool.find(e => !e.missing && e.media.video);
    if (primary) setSourceMonitor(primary);
}

/**
 * Imports a video or audio file into the media pool. With `place`, it is also appended to the end of the
 * timeline (picture on a video track, sound on an audio track). Returns the pool source id.
 */
export async function loadVideo(path: string, opts: { place?: boolean } = {}): Promise<string | null> {
    try {
        setState({ mediaError: null, proxyPct: null });
        const media = await api.probe(path);
        if (!media.video && !media.audio.length) throw new Error(media.warnings[0] || 'No video or audio stream found.');
        if (media.encrypted) throw new Error('This file appears to be DRM-protected. UVE Manager does not process protected content.');
        applyTimeline(tl => addSource(tl, path, media), '');
        const source = getState().timeline.sources.find(s => s.path === path);
        if (!source) return null;
        const entry = await ensurePoolEntry(source.id, media);
        if (media.video) setSourceMonitor(entry);
        media.warnings.forEach(w => toast(w, 'warn', 7000));
        if (media.video) {
            void api.setTitle(`UVE Manager ${APP_VERSION} – ${media.name}`);
            if (getState().effect.settings.autoOn) void runAnalysis();
        }
        if (opts.place) placeMedia(source.id, { start: timelineDuration(getState().timeline) });
        return source.id;
    } catch (e) {
        setState({ mediaError: errMsg(e) });
        toast(`Cannot open media: ${errMsg(e)}`, 'error', 8000);
        return null;
    }
}

/** Called by the player when Chromium cannot decode the file: builds a preview proxy. */
export async function requestProxy() {
    const m = getState().media;
    if (!m || getState().proxyPct !== null) return;
    setState({ proxyPct: 0 });
    toast('This format cannot be previewed directly – creating a preview proxy (export always uses the original file).', 'info', 7000);
    try {
        const url = await api.proxy(m);
        if (getState().media?.path !== m.path) return;
        setState(st => ({ mediaUrl: url, proxyPct: null, pool: st.pool.map(p => p.path === m.path ? { ...p, url, proxied: true } : p) }));
    } catch (e) {
        setState({ proxyPct: null, mediaError: `Preview not possible: ${errMsg(e)}` });
    }
}

let samplesCache: { key: string; samples: AnalysisSample[] } | null = null;

export async function runAnalysis() {
    const m = getState().media;
    if (!m) return;
    if (getState().analysis.status === 'running') return;
    setState({ analysis: { ...getState().analysis, status: 'running', pct: 0 } });
    try {
        const r = await api.analyze(m);
        if (getState().media?.path !== m.path) return;
        samplesCache = { key: r.key, samples: samplesFromAnalysis(r) };
        recomputeAuto();
    } catch (e) {
        setState({ analysis: { status: 'error', pct: 0, keys: null, error: errMsg(e) } });
    }
}

/** Re-runs the auto state machine (fast) after strength / WB-lock changes. */
export function recomputeAuto() {
    if (!samplesCache) return;
    const s = getState().effect.settings;
    const keys = runAutoSceneMatch(samplesCache.samples, { strength: s.autoStrength, lockWB: s.autoLockWB });
    setState({ analysis: { status: 'ready', pct: 1, keys, samplesKey: samplesCache.key } });
}

// ------------------------------------------------------------------ project
/** Projects are saved as schema v2 (timeline). v1 files stay readable and are migrated on open. */
export function buildProject(): UveProject {
    const s = getState();
    return {
        schema: 'uve-project', version: 2, app: `UVE Manager ${APP_VERSION}`, uveSource: UVE_SOURCE, savedAt: new Date().toISOString(),
        effect: s.effect, timeline: s.timeline, export: s.exportSettings,
        ui: { wipe: s.wipe, compare: s.compare, timelineZoom: s.timelineZoom }
    };
}

export function effectFromUnknown(e: unknown): EffectState {
    const o = (e && typeof e === 'object' ? e : {}) as Record<string, unknown>;
    const lut = o.lut ? parseLutProfileObject(o.lut, 'user') : null;
    return {
        settings: sanitizeSettings(o.settings),
        expert: expertFromDiff(o.expert),
        customFilters: Array.isArray(o.customFilters) ? o.customFilters.map(normalizeEntry).filter((x): x is CustomFilterEntry => !!x) : [],
        lut
    };
}

export async function saveProject(saveAs = false) {
    try {
        const p = await api.saveProject(getState().projectPath, buildProject(), saveAs);
        if (p) { setState({ projectPath: p, dirty: false }); toast(`Project saved: ${p}`, 'ok'); }
    } catch (e) { toast(`Save failed: ${errMsg(e)}`, 'error'); }
}

const baseName = (p: string) => (p.replace(/\.[^.]+$/, '').match(/[\\/][^\\/]*$/) ?? ['/'])[0].slice(1) || 'Timeline';

/** Frame rate requested in the export settings, '' or missing = none. */
function frameRateFromSetting(fps: string | undefined): number | null {
    const p = parseFps(fps);
    return p ? p.num / p.den : null;
}

async function probeOrToast(path: string): Promise<MediaInfo | null> {
    try {
        return await api.probe(path);
    } catch (e) {
        toast(`Cannot read media: ${path} (${errMsg(e)})`, 'error', 8000);
        return null;
    }
}

/** Timeline of an opened project; media that moved is located with the user. v1 projects are migrated. */
async function timelineOfProject(pr: UveProject, missing: string[]): Promise<Timeline> {
    const settingRate = frameRateFromSetting(pr.export?.fps);
    if (pr.version === 1) {
        const declared = pr.media?.path ?? '';
        if (!declared) return defaultTimeline();
        let path = declared;
        if (missing.includes(declared)) {
            toast(`The project video was not found: ${declared}. Please locate it.`, 'warn', 9000);
            path = (await api.locateMedia()) ?? '';
        }
        const media = path ? await probeOrToast(path) : null;
        if (!media) {
            toast('The project stays empty until its video file is available.', 'warn', 9000);
            return defaultTimeline();
        }
        return migrateProjectV1toV2(pr, media, settingRate ?? frameRateFromMedia(media)).timeline;
    }
    const tl = sanitizeTimeline(pr.timeline, { frameRate: settingRate ?? undefined, parseEffect: effectFromUnknown });
    for (const src of tl.sources) {
        let path = src.path;
        if (missing.includes(path)) {
            toast(`Media not found: ${path}. Please locate it.`, 'warn', 9000);
            path = (await api.locateMedia()) ?? '';
        }
        const media = path ? await probeOrToast(path) : null;
        if (!media) continue;
        tl.sources = tl.sources.map(s => s.id === src.id ? { ...s, path, media } : s);
    }
    return tl;
}

export async function openProject(path?: string) {
    try {
        const r = await api.openProject(path);
        if (!r) return;
        const pr = r.project;
        const effect = effectFromUnknown(pr.effect);
        const timeline = await timelineOfProject(pr, r.missing);
        const zoom = pr.version === 2 ? pr.ui?.timelineZoom : undefined;
        setState({
            effect, exportSettings: { ...DEFAULT_EXPORT, ...(pr.export ?? {}) }, timeline,
            projectPath: r.path, dirty: r.missing.length > 0,
            wipe: pr.ui?.wipe ?? 0.5, compare: pr.ui?.compare ?? true,
            timelineZoom: typeof zoom === 'number' && zoom >= 0.5 ? zoom : 90,
            selectedClipId: null, selectedTrackId: timeline.tracks[0]?.id ?? null,
            pool: [], time: 0, playing: false
        });
        resetHistory();
        checkLutAvailability(effect);
        await rebuildPool(timeline);
        const issues = validateTimeline(timeline);
        if (issues.length) toast(`This project has ${issues.length} problem(s): ${issues[0].message}`, 'warn', 9000);
        toast(pr.version === 1 ? `Project opened and converted to a timeline project: ${r.path}` : `Project opened: ${r.path}`, 'ok');
    } catch (e) { toast(`Cannot open project: ${errMsg(e)}`, 'error'); }
}

export function newProject() {
    setState({
        effect: initialEffect(), projectPath: null, dirty: false, media: null, mediaUrl: null, thumbs: [], mediaError: null,
        inPoint: null, outPoint: null, timeline: defaultTimeline(), pool: [], selectedClipId: null, selectedTrackId: null,
        analysis: { status: 'none', pct: 0, keys: null }
    });
    resetHistory();
}

// ------------------------------------------------------------------ timeline editing
/** Runs a timeline op with undo history; a rejected op is reported and changes nothing. */
export function editTimeline(op: (tl: Timeline) => OpResult, key = ''): boolean {
    const res = applyTimeline(op, key);
    if (!res.ok) toast(res.error, 'warn', 6000);
    return res.ok;
}

export const selectClip = (id: string | null) => setState(id ? { selectedClipId: id, rangeSel: null } : { selectedClipId: null });
export const selectTrack = (id: string | null) => setState({ selectedTrackId: id });
export const setTimelineZoom = (pxPerSecond: number) => setState({ timelineZoom: Math.max(0.5, Math.min(600, pxPerSecond)) });
export const setSnapping = (on: boolean) => setState({ snapping: on });
/** Switches the player between the source monitor and the timeline program monitor. */
export const setMonitor = (monitor: 'source' | 'timeline') => setState({ monitor, playing: false });

/** Makes one pool source the current file (analysis, media info). The player keeps showing the timeline. */
export function showSourceInMonitor(sourceId: string) {
    const entry = getState().pool.find(p => p.sourceId === sourceId);
    if (!entry) return;
    setSourceMonitor(entry);
}

/** A new track of `kind` at the bottom, then the tracks reordered into V1, A1, V2, A2, … pairs. */
function addPairedTrack(tl: Timeline, kind: TrackKind): OpResult {
    const r = addTrack(tl, kind, tl.tracks.length);
    return r.ok ? pairTracks(r.tl) : r;
}

/** Adds a track below the existing ones, keeps the tracks in V1, A1, V2, A2, … pairs and selects the new track. */
export function insertTrack(kind: TrackKind) {
    const before = new Set(getState().timeline.tracks.map(t => t.id));
    const res = applyTimeline(tl => addPairedTrack(tl, kind), '');
    if (!res.ok) { toast(res.error, 'warn', 6000); return; }
    setState({ selectedTrackId: res.tl.tracks.find(t => !before.has(t.id))?.id ?? getState().selectedTrackId });
}


/**
 * Places a pool source on the timeline. A video with sound puts its picture on a video track (with the clip's
 * own audio muted) and its sound as a separate clip on an audio track at the same position, so the two can be
 * edited and synced independently. Audio-only files go to an audio track. One undo step.
 */
export function placeMedia(sourceId: string, opts: { trackId?: string; start?: number; mode?: 'ripple' | 'replace' } = {}): boolean {
    const st = getState();
    const source = sourceById(st.timeline, sourceId);
    if (!source) { toast('Unknown media source.', 'warn', 4000); return false; }
    const F = st.timeline.frameRate;
    const media = source.media;
    // whole frames only: rounding the media length up would run past the end of the media
    const dur = Math.floor(media.duration * F + 1e-6) / F;
    if (dur <= 0) { toast('This file is shorter than one frame.', 'warn', 4000); return false; }
    const start = snap(Math.max(0, opts.start ?? st.time), F);
    const picked = opts.trackId ? trackById(st.timeline, opts.trackId) : undefined;
    const wantVideo = !!media.video && (!picked || picked.kind === 'video');
    const wantAudio = media.audio.length > 0;
    let placedId: string | null = null;
    const res = applyTimeline(tl => {
        let cur = tl;
        const free = (kind: TrackKind) => cur.tracks.find(t => t.kind === kind && !t.locked)?.id;
        const put = (trackId: string): OpResult => {
            const before = new Set(cur.clips.map(c => c.id));
            const r = insertClip(cur, { trackId, sourceId, start, dur, srcIn: 0, mode: opts.mode });
            if (r.ok) { cur = r.tl; placedId ??= cur.clips.find(c => !before.has(c.id))?.id ?? null; }
            return r;
        };
        if (wantVideo) {
            const vId = picked?.kind === 'video' ? picked.id : free('video');
            if (!vId) return { ok: false, error: 'There is no unlocked video track.' };
            const r = put(vId);
            if (!r.ok) return r;
            if (wantAudio) {
                const p = patchClip(cur, placedId!, { audioMuted: true });
                if (!p.ok) return p;
                cur = p.tl;
            }
        }
        if (wantAudio) {
            let aId = picked?.kind === 'audio' ? picked.id : free('audio');
            if (!aId) {
                const r = addPairedTrack(cur, 'audio');
                if (!r.ok) return r;
                cur = r.tl;
                aId = free('audio');
            }
            const r = put(aId!);
            if (!r.ok) return r;
        }
        return { ok: true, tl: cur };
    }, '');
    if (!res.ok) { toast(res.error, 'warn', 6000); return false; }
    setState({ selectedClipId: placedId });
    return true;
}

/** Splits one clip at a timeline time (snapped to the frame grid). */
export function razorAt(clipId: string, t: number): boolean {
    const st = getState();
    const at = snap(t, st.timeline.frameRate);
    const before = new Set(st.timeline.clips.map(c => c.id));
    const res = applyTimeline(tl => splitClip(tl, clipId, at), '');
    if (!res.ok) { toast(res.error, 'warn', 6000); return false; }
    setState({ selectedClipId: res.tl.clips.find(c => !before.has(c.id))?.id ?? null });
    return true;
}

/**
 * Razor at the playhead: splits the selected clip, or else every clip under the playhead on all unlocked
 * tracks (so a picture and its separate sound stay cut at the same frame). One undo step.
 */
export function razorAtPlayhead(): boolean {
    const st = getState();
    const t = snap(st.time, st.timeline.frameRate);
    const selected = st.selectedClipId ? clipById(st.timeline, st.selectedClipId) : undefined;
    if (selected && t > selected.start && t < clipEnd(selected)) return razorAt(selected.id, t);
    const targets = st.timeline.clips.filter(c => c.start < t && clipEnd(c) > t && !trackById(st.timeline, c.trackId)?.locked).map(c => c.id);
    if (!targets.length) { toast('Razor: no clip under the playhead.', 'info', 2500); return false; }
    const res = applyTimeline(tl => {
        let cur = tl;
        for (const id of targets) {
            const r = splitClip(cur, id, t);
            if (!r.ok) return r;
            cur = r.tl;
        }
        return { ok: true, tl: cur };
    }, '');
    if (!res.ok) { toast(res.error, 'warn', 6000); return false; }
    setState({ selectedClipId: null });
    return true;
}

// ------------------------------------------------------------------ range selection (drag over the tracks)
/** Sets the marked range (frame-aligned, t0 < t1, at least one frame) or clears it; a range replaces the clip selection. */
export function setRangeSel(r: RangeSel | null) {
    if (!r) { setState({ rangeSel: null }); return; }
    const F = getState().timeline.frameRate;
    const t0 = snap(Math.max(0, Math.min(r.t0, r.t1)), F), t1 = snap(Math.max(r.t0, r.t1), F);
    setState({ rangeSel: t1 - t0 >= 1 / F - 1e-9 && r.trackIds.length ? { t0, t1, trackIds: r.trackIds } : null, selectedClipId: null });
}

/**
 * Splits every clip on the range's tracks that crosses its start or end. Returns the split timeline and the
 * ids of the clips lying inside the range (these are exactly the marked pieces).
 */
function cutAtRange(tl: Timeline, r: RangeSel): { ok: true; tl: Timeline; inside: string[] } | { ok: false; error: string } {
    let cur = tl;
    for (const at of [r.t0, r.t1]) {
        const crossing = cur.clips.filter(c => r.trackIds.includes(c.trackId) && c.start < at - 1e-9 && clipEnd(c) > at + 1e-9).map(c => c.id);
        for (const id of crossing) {
            const res = splitClip(cur, id, at);
            if (!res.ok) return res;
            cur = res.tl;
        }
    }
    const inside = cur.clips.filter(c => r.trackIds.includes(c.trackId) && c.start >= r.t0 - 1e-9 && clipEnd(c) <= r.t1 + 1e-9).map(c => c.id);
    return { ok: true, tl: cur, inside };
}

/** Moves every clip on `trackIds` that starts at or after `from` by `delta` seconds (in an order that never collides). */
function shiftClips(tl: Timeline, trackIds: string[], from: number, delta: number): OpResult {
    let cur = tl;
    const later = cur.clips.filter(c => trackIds.includes(c.trackId) && c.start >= from - 1e-9)
        .sort((a, b) => delta > 0 ? b.start - a.start : a.start - b.start);
    for (const c of later) {
        const r = moveClip(cur, c.id, { start: c.start + delta });
        if (!r.ok) return r;
        cur = r.tl;
    }
    return { ok: true, tl: cur };
}

/** Cuts the clips at the edges of the marked range (all marked tracks). */
export function splitAtRange(): boolean {
    const r = getState().rangeSel;
    if (!r) return false;
    return editTimeline(tl => cutAtRange(tl, r), '');
}

/**
 * Deletes the marked range on the marked tracks: clips crossing its edges are cut there, the pieces inside are
 * removed. `ripple` also moves everything after the range left by its length (on the marked tracks only).
 */
export function deleteRange(mode: 'ripple' | 'lift'): boolean {
    const r = getState().rangeSel;
    if (!r) return false;
    const ok = editTimeline(tl => {
        const cut = cutAtRange(tl, r);
        if (!cut.ok) return cut;
        let cur = cut.tl;
        for (const id of cut.inside) {
            const d = deleteClip(cur, id, 'lift');
            if (!d.ok) return d;
            cur = d.tl;
        }
        return mode === 'ripple' ? shiftClips(cur, r.trackIds, r.t1, -(r.t1 - r.t0)) : { ok: true, tl: cur };
    }, '');
    if (ok) setState({ rangeSel: mode === 'ripple' ? null : r, selectedClipId: null });
    return ok;
}

/**
 * Copies to the timeline clipboard: the marked range (the parts of the clips inside it, on every marked track),
 * else the given / selected clip. `cut` also lifts what was copied.
 */
export function copyClip(cut = false, clipId = getState().selectedClipId): boolean {
    const st = getState();
    const r = st.rangeSel;
    if (r && !(clipId && clipId !== st.selectedClipId)) {
        const res = cutAtRange(st.timeline, r);
        if (!res.ok) { toast(res.error, 'warn', 6000); return false; }
        const clips = res.inside.map(id => structuredClone(clipById(res.tl, id)!));
        if (!clips.length) { toast('The marked range contains no clips.', 'info', 2500); return false; }
        setState({ clipboard: { t0: r.t0, clips } });
        if (cut) return deleteRange('lift');
        toast(`${clips.length} clip part${clips.length > 1 ? 's' : ''} copied.`, 'info', 1500);
        return true;
    }
    if (!clipId) return false;
    const clip = clipById(st.timeline, clipId);
    if (!clip) return false;
    setState({ clipboard: { t0: clip.start, clips: [structuredClone(clip)] } });
    if (!cut) { toast('Clip copied.', 'info', 1500); return true; }
    const ok = editTimeline(tl => deleteClip(tl, clipId, 'lift'), '');
    if (ok) setState({ selectedClipId: null });
    return ok;
}

/**
 * Pastes the clipboard at `start` (default: playhead), keeping the clips' offsets to each other. A copy from one
 * track goes to `trackId` (default: the selected track when its kind fits, else its own track, else the first
 * track of its kind); a copy from several tracks goes back to its own tracks. Into free space it is placed as
 * it is; over existing clips room is made first (clips there are cut at `start` and the rest moves right).
 * Keeps each clip's look, levels, fades, speed and automation; transitions are not pasted. One undo step.
 */
export function pasteClip(opts: { trackId?: string; start?: number } = {}): boolean {
    const st = getState();
    const board = st.clipboard;
    if (!board?.clips.length) { toast('Nothing to paste – copy a clip or mark a range first.', 'info', 2500); return false; }
    const tl = st.timeline;
    const kindOf = (c: TimelineClip): TrackKind => trackById(tl, c.trackId)?.kind ?? (sourceById(tl, c.sourceId)?.media.video ? 'video' : 'audio');
    const fits = (id: string | null | undefined, kind: TrackKind) => { const t = id ? trackById(tl, id) : undefined; return t && t.kind === kind && !t.locked ? t.id : undefined; };
    const single = new Set(board.clips.map(c => c.trackId)).size === 1;
    const dest = new Map<string, string>();
    for (const c of board.clips) {
        if (!sourceById(tl, c.sourceId)) { toast('The media of a copied clip is no longer in the project.', 'warn', 5000); return false; }
        if (dest.has(c.trackId)) continue;
        const kind = kindOf(c);
        const id = (single ? fits(opts.trackId, kind) ?? fits(st.selectedTrackId, kind) : undefined) ?? fits(c.trackId, kind) ?? tl.tracks.find(t => t.kind === kind && !t.locked)?.id;
        if (!id) { toast(`There is no unlocked ${kind} track to paste into.`, 'warn', 4000); return false; }
        dest.set(c.trackId, id);
    }
    const start = snap(Math.max(0, opts.start ?? st.time), tl.frameRate);
    const span = Math.max(...board.clips.map(c => c.start - board.t0 + c.dur));
    const targets = [...new Set(dest.values())];
    const occupied = tl.clips.some(c => targets.includes(c.trackId) && c.start < start + span - 1e-9 && clipEnd(c) > start + 1e-9);
    const added: string[] = [];
    const res = applyTimeline(t0 => {
        let cur = t0;
        if (occupied) {
            const cut = cutAtRange(cur, { t0: start, t1: start, trackIds: targets });
            if (!cut.ok) return cut;
            const moved = shiftClips(cut.tl, targets, start, span);
            if (!moved.ok) return moved;
            cur = moved.tl;
        }
        for (const clip of board.clips) {
            const before = new Set(cur.clips.map(c => c.id));
            const r = insertClip(cur, { trackId: dest.get(clip.trackId)!, sourceId: clip.sourceId, start: start + clip.start - board.t0, dur: clip.dur, srcIn: clip.srcIn, mode: 'replace' });
            if (!r.ok) return r;
            const newId = r.tl.clips.find(c => !before.has(c.id))?.id;
            if (!newId) return r;
            const { id: _id, trackId: _t, sourceId: _s, start: _st, dur: _d, srcIn: _i, transition: _tr, ...look } = clip;
            const p = patchClip(r.tl, newId, structuredClone(look));
            if (!p.ok) return p;
            cur = p.tl;
            added.push(newId);
        }
        return { ok: true, tl: cur };
    }, '');
    if (!res.ok) { toast(res.error, 'warn', 6000); return false; }
    if (added.length === 1) setState({ selectedClipId: added[0], selectedTrackId: targets[0], rangeSel: null });
    else setState({ selectedClipId: null, rangeSel: { t0: start, t1: start + span, trackIds: targets } });
    return true;
}

/** Ripple delete (later clips close the gap) or lift/extract (the gap stays): the marked range, else the selected clip. */
export function removeSelectedClip(mode: 'ripple' | 'lift'): boolean {
    if (getState().rangeSel) return deleteRange(mode);
    const id = getState().selectedClipId;
    if (!id) return false;
    const ok = editTimeline(tl => deleteClip(tl, id, mode), '');
    if (ok) setState({ selectedClipId: null });
    return ok;
}

/** Copy of the selected clip directly behind it. */
export function duplicateSelectedClip(): boolean {
    const st = getState();
    const id = st.selectedClipId;
    if (!id) return false;
    const clip = clipById(st.timeline, id);
    if (!clip) return false;
    const before = new Set(st.timeline.clips.map(c => c.id));
    const res = applyTimeline(tl => duplicateClip(tl, id, clipEnd(clip)), '');
    if (!res.ok) { toast(res.error, 'warn', 6000); return false; }
    setState({ selectedClipId: res.tl.clips.find(c => !before.has(c.id))?.id ?? null });
    return true;
}

// ------------------------------------------------------------------ LUTs
export function checkLutAvailability(effect: EffectState) {
    const key = effect.settings.lutProfile;
    if (!key || key === 'none') return;
    const lib = getState().luts.profiles.find(p => lutKeyOf(p) === key);
    if (!lib && !effect.lut) toast(`LUT "${key.replace('||', ' / ')}" is missing. Import it in the LUT Library – until then it is NOT applied.`, 'warn', 10000);
}

export function selectLut(key: string) {
    const p = getState().luts.profiles.find(x => lutKeyOf(x) === key) || null;
    updateEffect(e => ({ ...e, settings: { ...e.settings, lutProfile: p ? key : 'none' }, lut: p ? { ...p } : null }), '');
}

async function decodePng(data: Uint8Array): Promise<{ rgba: Uint8ClampedArray; w: number; h: number }> {
    const bmp = await createImageBitmap(new Blob([data as BlobPart], { type: 'image/png' }), { colorSpaceConversion: 'none', premultiplyAlpha: 'none' });
    const c = new OffscreenCanvas(bmp.width, bmp.height);
    const ctx = c.getContext('2d', { willReadFrequently: true, colorSpace: 'srgb' })!;
    ctx.drawImage(bmp, 0, 0);
    return { rgba: ctx.getImageData(0, 0, bmp.width, bmp.height).data, w: bmp.width, h: bmp.height };
}

/** Imports .cube / PNG / .json / .zip LUTs into the library (as UVE 4x5 matrix profiles). */
export async function importLutFiles(group: string, opts: { linearizeIn?: boolean; delinearizeOut?: boolean; flipY?: boolean } = {}) {
    const files = await api.openFiles('Import LUTs', [{ name: 'LUT files', extensions: ['cube', 'png', 'json', 'zip'] }], true, 100 * 1024 * 1024);
    if (!files.length) return;
    const out: LutProfile[] = [];
    const errors: string[] = [];
    const now = Date.now();
    const handle = async (name: string, data: Uint8Array) => {
        const ext = name.toLowerCase().split('.').pop();
        const base = name.replace(/^.*[\\/]/, '').replace(/\.[^.]+$/, '');
        try {
            if (ext === 'cube') {
                const r = cubeTextToMatrix4x5(new TextDecoder().decode(data), opts);
                out.push({ name: base, group, matrix4x5: r.matrix4x5, createdAt: now, updatedAt: now, source: 'user', origin: name });
            } else if (ext === 'png') {
                const img = await decodePng(data);
                out.push({ name: base, group, matrix4x5: tiledRgbaToMatrix4x5(img.rgba, img.w, img.h, opts), createdAt: now, updatedAt: now, source: 'user', origin: name });
            } else if (ext === 'json') {
                const obj = JSON.parse(new TextDecoder().decode(data));
                const list = Array.isArray(obj) ? obj : Array.isArray(obj?.profiles) ? obj.profiles : [obj];
                if (obj?.schema === 'gvf-lut-groups') return;
                let any = false;
                for (const it of list) { const p = parseLutProfileObject(it, 'user'); if (p) { out.push(p); any = true; } }
                if (!any) errors.push(`${name}: no gvf-lut-profile found`);
            } else if (ext === 'zip') {
                for (const e of await api.unzip(data)) await handle(e.name, e.data);
            }
        } catch (e) { errors.push(`${name}: ${errMsg(e)}`); }
    };
    for (const f of files) await handle(f.name, f.data);
    if (out.length) setState({ luts: await api.saveLuts(out) });
    toast(`${out.length} LUT(s) imported${errors.length ? `, ${errors.length} failed: ${errors.slice(0, 3).join('; ')}` : ''}`, errors.length ? 'warn' : 'ok', 8000);
}

export async function exportLuts(keys: string[]) {
    const list = getState().luts.profiles.filter(p => keys.includes(lutKeyOf(p)));
    if (!list.length) return;
    if (list.length === 1) {
        const p = list[0];
        await api.saveFile('Export LUT', `${sanitizeProfileFilename(p.group ? p.group + '__' + p.name : p.name)}.json`, [{ name: 'JSON', extensions: ['json'] }], JSON.stringify(lutProfileToJson(p), null, 2));
        return;
    }
    const files: Record<string, Uint8Array> = {};
    const groups = [...new Set(list.map(p => p.group))];
    files['_lut_groups.json'] = strToU8(JSON.stringify({ schema: 'gvf-lut-groups', ver: 1, groups, exportedAt: Date.now() }, null, 2));
    list.forEach(p => { files[`${sanitizeProfileFilename(`${p.group}__${p.name}`)}.json`] = strToU8(JSON.stringify(lutProfileToJson(p), null, 2)); });
    await api.saveFile('Export LUTs', 'UVE_LUT_Profiles.zip', [{ name: 'ZIP', extensions: ['zip'] }], zipSync(files));
}

// ------------------------------------------------------------------ user profiles
export async function persistProfiles() {
    const s = getState();
    await api.saveProfiles({ profiles: s.profiles, activeId: s.activeProfileId });
}

export function profileFromCurrent(name: string, id = newProfileId()): UserProfile {
    const e = getState().effect;
    const now = Date.now();
    return {
        id, name: sanitizeProfileFilename(name), createdAt: now, updatedAt: now, settings: { ...e.settings },
        desktop: { expert: expertDiff(e.expert), customFilters: e.customFilters, lut: e.lut }
    };
}

export function applyProfile(p: UserProfile) {
    updateEffect(e => ({
        settings: sanitizeSettings(p.settings, DEFAULT_SETTINGS),
        expert: p.desktop?.expert ? expertFromDiff(p.desktop.expert) : e.expert,
        customFilters: p.desktop?.customFilters ? p.desktop.customFilters.map(normalizeEntry).filter((x): x is CustomFilterEntry => !!x) : e.customFilters,
        lut: p.desktop?.lut ?? (getState().luts.profiles.find(l => lutKeyOf(l) === p.settings.lutProfile) || null)
    }), '');
    setState({ activeProfileId: p.id });
    void persistProfiles();
    checkLutAvailability(getState().effect);
    if (getState().effect.settings.autoOn) { if (getState().analysis.status === 'ready') recomputeAuto(); else void runAnalysis(); }
}

export async function importProfiles() {
    const files = await api.openFiles('Import profiles', [{ name: 'Profiles', extensions: ['json', 'zip'] }], true, 20 * 1024 * 1024);
    const incoming: UserProfile[] = [];
    const errors: string[] = [];
    const handle = async (name: string, data: Uint8Array) => {
        if (name.toLowerCase().endsWith('.zip')) { for (const e of await api.unzip(data)) await handle(e.name, e.data); return; }
        if (!name.toLowerCase().endsWith('.json')) return;
        try {
            const obj = JSON.parse(new TextDecoder().decode(data));
            const list = Array.isArray(obj) ? obj : [obj];
            for (const it of list) {
                const p = parseUserProfileObject(it, name.replace(/^.*[\\/]/, '').replace(/\.json$/i, ''));
                if (p) incoming.push(p); else errors.push(`${name}: not a UVE profile`);
            }
        } catch (e) { errors.push(`${name}: ${errMsg(e)}`); }
    };
    for (const f of files) await handle(f.name, f.data);
    if (!incoming.length) { if (files.length) toast(`No profiles imported. ${errors.slice(0, 3).join('; ')}`, 'warn'); return; }
    const r = mergeProfiles(getState().profiles, incoming);
    setState({ profiles: r.list });
    await persistProfiles();
    toast(`Profiles: ${r.added} added, ${r.replaced} replaced${errors.length ? `, ${errors.length} skipped` : ''}`, 'ok');
}

export async function exportProfiles(ids: string[], format: 'zip' | 'json' | 'gvf-settings') {
    const list = getState().profiles.filter(p => ids.includes(p.id));
    if (!list.length) return;
    if (format === 'gvf-settings') {
        await api.saveFile('Export settings (userscript format)', `${sanitizeProfileFilename(list[0].name)}.json`, [{ name: 'JSON', extensions: ['json'] }], JSON.stringify(settingsToGvfSettings(list[0].settings), null, 2));
        return;
    }
    if (format === 'json' && list.length === 1) {
        await api.saveFile('Export profile', `${sanitizeProfileFilename(list[0].name)}.json`, [{ name: 'JSON', extensions: ['json'] }], JSON.stringify(list[0], null, 2));
        return;
    }
    const files: Record<string, Uint8Array> = {};
    list.forEach(p => { files[`${sanitizeProfileFilename(p.name)}.json`] = strToU8(JSON.stringify(p, null, 2)); });
    const d = new Date();
    await api.saveFile('Export profiles', `UVE_Profiles_${d.getFullYear()}${String(d.getMonth() + 1).padStart(2, '0')}${String(d.getDate()).padStart(2, '0')}.zip`, [{ name: 'ZIP', extensions: ['zip'] }], zipSync(files));
}

// ------------------------------------------------------------------ custom filters
export async function importFilterCodes() {
    const files = await api.openFiles('Import custom filters', [{ name: 'Filter codes', extensions: ['json'] }], true, 10 * 1024 * 1024);
    let added = 0;
    for (const f of files) {
        try {
            const list = parseFilterCodesJson(new TextDecoder().decode(f.data));
            updateEffect(e => {
                const cur = e.customFilters.slice();
                for (const n of list) {
                    const i = cur.findIndex(c => c.label === n.label);
                    if (i >= 0) cur[i] = { ...n, id: cur[i].id }; else cur.push(cur.some(c => c.id === n.id) ? { ...n, id: n.id + '_' + Date.now() } : n);
                    added++;
                }
                return { ...e, customFilters: cur };
            });
        } catch (e) { toast(`${f.name}: ${errMsg(e)}`, 'error'); }
    }
    if (added) toast(`${added} custom filter(s) imported`, 'ok');
}

export async function exportFilterCodes(entries: CustomFilterEntry[]) {
    if (!entries.length) return;
    await api.saveFile('Export custom filters', 'gvf_filter_codes.json', [{ name: 'JSON', extensions: ['json'] }], JSON.stringify(exportFilterCodesJson(entries), null, 2));
}

// ------------------------------------------------------------------ rendering
export function profileLabel(): string {
    const s = getState();
    return s.profiles.find(p => p.id === s.activeProfileId)?.name || 'Custom';
}

export async function enqueueCurrent() {
    const s = getState();
    if (!s.media) { toast('Open a video first.', 'warn'); return; }
    if (!s.caps?.ok) { toast(s.caps?.error || 'FFmpeg is not available.', 'error'); return; }
    try {
        const range = s.exportSettings.useInOut && (s.inPoint !== null || s.outPoint !== null)
            ? { start: s.inPoint ?? 0, end: s.outPoint ?? s.media.duration } : { start: 0, end: s.media.duration };
        if (range.end - range.start <= 0) throw new Error('The In/Out range is empty.');
        const output = await api.outputPath(s.media, s.exportSettings, profileLabel());
        // global bypass is a preview-only comparison aid; the job always renders the edited look
        const effect: EffectState = JSON.parse(JSON.stringify(s.effect));
        const spec: JobSpec = {
            id: 'job_' + Date.now() + '_' + Math.random().toString(36).slice(2, 6), name: s.media.name, input: s.media.path, output,
            media: s.media, effect, autoKeys: effect.settings.autoOn && s.analysis.status === 'ready' ? s.analysis.keys : null,
            exportSettings: { ...s.exportSettings }, range, parityMode: !!s.prefs?.parityMode
        };
        await api.enqueue(spec);
        toast(`Added to render queue → ${output}`, 'ok', 6000);
    } catch (e) { toast(`Cannot start rendering: ${errMsg(e)}`, 'error', 8000); }
}

/** Renders the whole project timeline as one job (the single-file quick path stays `enqueueCurrent`). */
export async function enqueueTimelineJob() {
    const s = getState();
    const tl = s.timeline;
    if (!tl.clips.length) { toast('The timeline is empty.', 'warn', 4000); return; }
    if (!s.caps?.ok) { toast(s.caps?.error || 'FFmpeg is not available.', 'error'); return; }
    const issues = validateTimeline(tl);
    if (issues.length) { toast(`Cannot render the timeline: ${issues[0].message}`, 'error', 9000); return; }
    try {
        const primary = tl.sources.find(src => src.media.video)?.media;
        if (!primary) throw new Error('The timeline has no picture to render: no source has a video stream.');
        const output = await api.outputPath(primary, s.exportSettings, profileLabel());
        // global bypass is a preview-only comparison aid; the job always renders the edited look
        const effect: EffectState = JSON.parse(JSON.stringify(s.effect));
        await api.enqueue({
            id: 'job_' + Date.now() + '_' + Math.random().toString(36).slice(2, 6),
            name: baseName(s.projectPath ?? primary.path),
            input: primary.path, output, media: primary, effect,
            autoKeys: effect.settings.autoOn && s.analysis.status === 'ready' ? s.analysis.keys : null,
            // timeline audio is synthesized from clip/track levels, so it is always re-encoded
            exportSettings: { ...s.exportSettings, useInOut: false, audio: s.exportSettings.audio === 'copy' ? 'aac' : s.exportSettings.audio },
            range: { start: 0, end: timelineDuration(tl) }, parityMode: !!s.prefs?.parityMode, timeline: structuredClone(tl)
        });
        toast(`Timeline added to the render queue → ${output}`, 'ok', 6000);
    } catch (e) { toast(`Cannot start rendering: ${errMsg(e)}`, 'error', 8000); }
}

export async function enqueueBatch(paths: string[], profile: UserProfile | null) {
    const s = getState();
    let ok = 0;
    for (const p of paths) {
        try {
            const media = await api.probe(p);
            if (!media.video) throw new Error('no video stream');
            const effect: EffectState = profile ? {
                settings: sanitizeSettings(profile.settings),
                expert: profile.desktop?.expert ? expertFromDiff(profile.desktop.expert) : s.effect.expert,
                customFilters: profile.desktop?.customFilters ?? s.effect.customFilters,
                lut: profile.desktop?.lut ?? (s.luts.profiles.find(l => lutKeyOf(l) === profile.settings.lutProfile) || null)
            } : JSON.parse(JSON.stringify(s.effect));
            const output = await api.outputPath(media, s.exportSettings, profile?.name || profileLabel());
            await api.enqueue({
                id: 'job_' + Date.now() + '_' + Math.random().toString(36).slice(2, 6), name: media.name, input: p, output, media, effect,
                autoKeys: null, exportSettings: { ...s.exportSettings, useInOut: false }, range: { start: 0, end: media.duration }, parityMode: !!s.prefs?.parityMode
            });
            ok++;
        } catch (e) { toast(`${p}: ${errMsg(e)}`, 'error', 8000); }
    }
    if (ok) toast(`${ok} file(s) added to the render queue`, 'ok');
}
