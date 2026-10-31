import { useSyncExternalStore } from 'react';
import type {
    AppPrefs, Capabilities, EffectState, ExportSettings, JobState, LutLibraryState, MediaInfo, Timeline, TimelineClip, ToolsStatus,
    UpdateCheckResult, UserProfile
} from '../../shared/types';
import { DEFAULT_EXPORT } from '../../shared/types';
import { defaultTimeline, type OpResult } from '../../shared/timeline';
import type { AutoKey } from '../../shared/uve/auto';
import type { CustomFilterEntry } from '../../shared/uve/customFilters';
import { expertDefaults } from '../../shared/uve/expert';
import { DEFAULT_SETTINGS } from '../../shared/uve/settings';

export type View = 'editor' | 'presets' | 'luts' | 'filters' | 'scopes' | 'queue' | 'settings';
export type InspectorTab = 'Basic' | 'Color' | 'HDR' | 'Detail' | 'LUTs' | 'Custom' | 'Auto' | 'Expert';

export interface Toast { id: number; text: string; tone: 'info' | 'ok' | 'warn' | 'error' }

export interface ScopeData { w: number; h: number; enhanced: Uint8Array; original: Uint8Array }

/** A time range on some tracks, marked by dragging over the lanes. */
export interface RangeSel { t0: number; t1: number; trackIds: string[] }

/** Copied clips; `t0` is the timeline time the copy starts at, so pasting keeps their offsets and tracks. */
export interface ClipboardClips { t0: number; clips: TimelineClip[] }

export interface PoolSource {
    sourceId: string;
    path: string;
    media: MediaInfo;
    /** direct preview url (or proxy url when Chromium cannot decode the file) */
    url: string;
    /** true when `url` is a generated proxy; exports always use `path` */
    proxied: boolean;
    /** file is currently unavailable */
    missing: boolean;
    thumbs: { t: number; url: string }[];
}

export interface AppInfo {
    version: string; userData: string; settingsFile: string; packaged: boolean; electron: string; chrome: string; lutPackage: string;
    uveSource: { name: string; version: string; repo: string; commit: string; sha256: string };
    tools: ToolsStatus;
}

export interface RveModelInfo { id: string; name: string; file: string; scale: number; arch: string; kind: string; custom?: boolean; installed: boolean; size: number; engines: number }
export interface RveStatusUi {
    dir: string; python: boolean; backend: boolean; backendVersion: string; packages: boolean; torch: string; tensorrt: string; torchTensorrt: string;
    cuda: boolean; gpu: string; ready: boolean; models: RveModelInfo[]; message: string; busy: boolean;
}

export interface AppState {
    view: View;
    tab: InspectorTab;
    info: AppInfo | null;
    prefs: AppPrefs | null;
    caps: Capabilities | null;
    tools: { busy: boolean; message: string; pct: number };
    media: MediaInfo | null;
    mediaUrl: string | null;
    mediaError: string | null;
    proxyPct: number | null;
    thumbs: { t: number; url: string }[];
    time: number;
    playing: boolean;
    effect: EffectState;
    bypass: boolean;
    compare: boolean;
    wipe: number;
    inPoint: number | null;
    outPoint: number | null;
    /** Project content: the multi-track timeline; saved as schema v2. */
    timeline: Timeline;
    /** Media pool: one entry per timeline source, with its preview url and thumbnails. */
    pool: PoolSource[];
    selectedClipId: string | null;
    selectedTrackId: string | null;
    /** Timeline ruler scale in pixels per second. */
    timelineZoom: number;
    snapping: boolean;
    /** Which picture the player shows. The editor always shows the timeline; 'source' is only kept for internal paths. */
    monitor: 'source' | 'timeline';
    /** Lane height per track id in px (UI only); missing = the default height of the track kind. */
    trackHeights: Record<string, number>;
    /** Time range marked by dragging over the tracks (left button held); null = none. */
    rangeSel: RangeSel | null;
    /** Clips copied or cut in the timeline, pasted with Ctrl+V or the context menu. */
    clipboard: ClipboardClips | null;
    exportSettings: ExportSettings;
    projectPath: string | null;
    dirty: boolean;
    profiles: UserProfile[];
    activeProfileId: string | null;
    filterLibrary: CustomFilterEntry[];
    luts: LutLibraryState;
    analysis: { status: 'none' | 'running' | 'ready' | 'error'; pct: number; keys: AutoKey[] | null; error?: string; samplesKey?: string };
    jobs: JobState[];
    updates: UpdateCheckResult | null;
    updateModal: boolean;
    updateBusy: { id: string; message: string; pct: number } | null;
    toasts: Toast[];
    graphWarnings: string[];
    customErrors: Record<string, string>;
    scopes: ScopeData | null;
    scopesHud: boolean;
    showIcons: boolean;
    renderStats: { ms: number; prims: number; w: number; h: number; dropped: number };
    canUndo: boolean;
    canRedo: boolean;
    /** REAL-Video-Enhancer (AI upscaling) installation status */
    rve: RveStatusUi | null;
    /** v1.0.1: filter engine used by the preview (and why) */
    engineInfo: { kind: 'webgpu' | 'webgl2'; adapter: string; reason: string; validation: { ok: boolean; maxDiff: number; meanDiff: number; prims: number; ms: number; error?: string } | null } | null;
}

export const initialEffect = (): EffectState => ({ settings: { ...DEFAULT_SETTINGS }, expert: expertDefaults(), customFilters: [], lut: null });

let state: AppState = {
    view: 'editor', tab: 'Basic', info: null, prefs: null, caps: null, tools: { busy: false, message: '', pct: 0 },
    media: null, mediaUrl: null, mediaError: null, proxyPct: null, thumbs: [], time: 0, playing: false,
    effect: initialEffect(), bypass: false, compare: true, wipe: 0.5, inPoint: null, outPoint: null,
    timeline: defaultTimeline(), pool: [], selectedClipId: null, selectedTrackId: null, timelineZoom: 90, snapping: true, monitor: 'timeline', trackHeights: {}, rangeSel: null, clipboard: null,
    exportSettings: { ...DEFAULT_EXPORT }, projectPath: null, dirty: false,
    profiles: [], activeProfileId: null, filterLibrary: [],
    luts: { profiles: [], groups: [], repo: { status: 'idle', message: '' } },
    analysis: { status: 'none', pct: 0, keys: null }, jobs: [], updates: null, updateModal: false, updateBusy: null,
    toasts: [], graphWarnings: [], customErrors: {}, scopes: null, scopesHud: false, showIcons: true,
    renderStats: { ms: 0, prims: 0, w: 0, h: 0, dropped: 0 }, canUndo: false, canRedo: false, rve: null, engineInfo: null
};

const listeners = new Set<() => void>();
export const getState = () => state;
export function setState(patch: Partial<AppState> | ((s: AppState) => Partial<AppState>)) {
    const p = typeof patch === 'function' ? patch(state) : patch;
    state = { ...state, ...p };
    listeners.forEach(l => l());
}
export function subscribe(l: () => void) { listeners.add(l); return () => { listeners.delete(l); }; }
export function useStore<T>(sel: (s: AppState) => T): T {
    return useSyncExternalStore(subscribe, () => sel(state));
}

// ---------------------------------------------------------------- undo / redo of effect + timeline edits
type Snapshot = { kind: 'effect'; value: EffectState } | { kind: 'timeline'; value: Timeline };
const undoStack: Snapshot[] = [];
const redoStack: Snapshot[] = [];
let lastKey = '';
let lastTime = 0;

/** Slider/drag edits with the same key inside 800 ms collapse into one undo step. */
function pushHistory(snapshot: Snapshot, key: string) {
    const now = Date.now();
    if (!(key && key === lastKey && now - lastTime < 800)) {
        undoStack.push(snapshot);
        if (undoStack.length > 200) undoStack.shift();
        redoStack.length = 0;
    }
    lastKey = key;
    lastTime = now;
}

const currentSnapshotOf = (kind: Snapshot['kind']): Snapshot =>
    kind === 'effect' ? { kind: 'effect', value: state.effect } : { kind: 'timeline', value: state.timeline };

export function updateEffect(fn: (e: EffectState) => EffectState, key = '') {
    pushHistory({ kind: 'effect', value: state.effect }, key);
    setState({ effect: fn(state.effect), dirty: true, canUndo: undoStack.length > 0, canRedo: false });
}

/** Runs a timeline op; a rejected op changes nothing and is returned for the caller to report. */
export function applyTimeline(op: (tl: Timeline) => OpResult, key = ''): OpResult {
    const res = op(state.timeline);
    if (!res.ok) return res;
    pushHistory({ kind: 'timeline', value: state.timeline }, key);
    setState({ timeline: res.tl, dirty: true, canUndo: undoStack.length > 0, canRedo: false });
    return res;
}

function restoreSnapshot(s: Snapshot) {
    if (s.kind === 'effect') setState({ effect: s.value, dirty: true, canUndo: undoStack.length > 0, canRedo: redoStack.length > 0 });
    else setState({ timeline: s.value, dirty: true, canUndo: undoStack.length > 0, canRedo: redoStack.length > 0 });
}

export function undo() {
    const prev = undoStack.pop();
    if (!prev) return;
    redoStack.push(currentSnapshotOf(prev.kind));
    lastKey = '';
    restoreSnapshot(prev);
}

export function redo() {
    const next = redoStack.pop();
    if (!next) return;
    undoStack.push(currentSnapshotOf(next.kind));
    lastKey = '';
    restoreSnapshot(next);
    setState({ canRedo: redoStack.length > 0 });
}

export function resetHistory() {
    undoStack.length = 0;
    redoStack.length = 0;
    lastKey = '';
    setState({ canUndo: false, canRedo: false });
}

let toastId = 1;
export function toast(text: string, tone: Toast['tone'] = 'info', ms = 4500) {
    const id = toastId++;
    setState(s => ({ toasts: [...s.toasts, { id, text, tone }] }));
    setTimeout(() => setState(s => ({ toasts: s.toasts.filter(t => t.id !== id) })), ms);
}
