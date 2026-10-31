import { useEffect, useState } from 'react';
import { APP_VERSION, DEFAULT_EXPORT, type JobState, type LutLibraryState, type UpdateItem } from '../../shared/types';
import { type CustomFilterEntry, normalizeEntry } from '../../shared/uve/customFilters';
import { api } from '../api';
import {
    cycleToneProfile, effectFromUnknown, enqueueTimelineJob, errMsg, loadVideo, newProject, openProject, saveProject, toggleHdr, toggleSetting
} from '../state/actions';
import { type View, getState, redo, setState, subscribe, toast, undo, useStore } from '../state/store';
import { comboFromEvent, sameCombo } from '../../shared/shortcuts';
import { Button, Icon, Modal, Progress, Toggle } from './controls';
import { runTimelineShortcut, sc } from './keys';
import { MediaPool } from './MediaPool';
import { ExportPanel, MediaInfoPanel } from './EditorPanels';
import { FilterStudio } from './FilterStudio';
import { Inspector } from './Inspector';
import { Player, player } from './Player';
import { Timeline, Transport } from './Timeline';
import { LutView, PresetsView, QueueView, SettingsView, installToolsNow } from './Views';

const NO_RECENTS: string[] = [];

/** Open dialog → each picked file goes into the media pool and onto the end of the timeline. */
async function openMediaOntoTimeline() {
    const paths = await api.openVideos(true);
    for (const p of paths) await loadVideo(p, { place: true });
    if (paths.length) setState({ view: 'editor' });
}

// scopes are shown inside the player (Enhancement Settings → Basic → Scopes), not as a page
const NAV: { id: View; label: string; icon: string }[] = [
    { id: 'editor', label: 'Editor', icon: 'editor' },
    { id: 'presets', label: 'Presets', icon: 'presets' },
    { id: 'luts', label: 'LUT Library', icon: 'luts' },
    { id: 'filters', label: 'Custom Filters', icon: 'filters' },
    { id: 'queue', label: 'Render Queue', icon: 'queue' }
];

function TitleBar() {
    const dirty = useStore(s => s.dirty);
    const projectPath = useStore(s => s.projectPath);
    const media = useStore(s => s.media);
    const canUndo = useStore(s => s.canUndo);
    const canRedo = useStore(s => s.canRedo);
    const recents = useStore(s => s.prefs?.recentProjects) ?? NO_RECENTS;
    const [menu, setMenu] = useState(false);
    const name = projectPath ? projectPath.split(/[\\/]/).pop() : media ? media.name : 'Untitled';
    return (
        <header className="titlebar">
            <div className="brand"><span className="logo">UVE</span><span className="appname">UVE Manager</span><span className="edition">Desktop Edition</span><span className="ver">v{APP_VERSION}</span></div>
            <div className="toolbar">
                <Button icon="open" onClick={() => void openMediaOntoTimeline()} title={`Open video or audio files and add them to the end of the timeline (${sc('open')})`}>Open Video</Button>
                <div className="menu-wrap">
                    <Button icon="folder" onClick={() => setMenu(!menu)} title="Project">Project</Button>
                    {menu && (
                        <div className="menu" onMouseLeave={() => setMenu(false)}>
                            <button onClick={() => { setMenu(false); newProject(); }}>New project</button>
                            <button onClick={() => { setMenu(false); void openProject(); }}>Open project…</button>
                            <button onClick={() => { setMenu(false); void saveProject(false); }}>Save project</button>
                            <button onClick={() => { setMenu(false); void saveProject(true); }}>Save project as…</button>
                            {recents.length > 0 && <div className="menu-sep">Recent</div>}
                            {recents.slice(0, 8).map(r => <button key={r} title={r} onClick={() => { setMenu(false); void openProject(r); }}>{r.split(/[\\/]/).pop()}</button>)}
                        </div>
                    )}
                </div>
                <Button icon="save" onClick={() => saveProject(false)} title={`Save project (${sc('save')})`}>Save</Button>
                <button className="icon-btn" disabled={!canUndo} onClick={undo} title={`Undo (${sc('undo')})`} aria-label="Undo"><Icon name="undo" /></button>
                <button className="icon-btn" disabled={!canRedo} onClick={redo} title={`Redo (${sc('redo')})`} aria-label="Redo"><Icon name="redo" /></button>
                <span className="doc-name" title={projectPath || ''}>{name}{dirty ? ' •' : ''}</span>
                <span className="drag-space" />
            </div>
        </header>
    );
}

function LeftRail() {
    const view = useStore(s => s.view);
    const jobs = useStore(s => s.jobs);
    const running = jobs.filter(j => ['queued', 'analyzing', 'rendering', 'upscaling'].includes(j.status)).length;
    return (
        <nav className="rail" aria-label="Main navigation">
            {NAV.map(n => (
                <button key={n.id} className={view === n.id ? 'active' : ''} onClick={() => setState({ view: n.id })} aria-current={view === n.id}>
                    <Icon name={n.icon} size={20} /><span>{n.label}</span>
                    {n.id === 'queue' && running > 0 && <span className="pill">{running}</span>}
                </button>
            ))}
            <span className="grow" />
            <button className={view === 'settings' ? 'active' : ''} onClick={() => setState({ view: 'settings' })}><Icon name="settings" size={20} /><span>Settings</span></button>
        </nav>
    );
}

function StatusBar() {
    const caps = useStore(s => s.caps);
    const jobs = useStore(s => s.jobs);
    const stats = useStore(s => s.renderStats);
    const media = useStore(s => s.media);
    const engine = useStore(s => s.engineInfo);
    const hwFams = [...new Set(Object.entries(caps?.hwEncoders || {}).filter(([, ok]) => ok).map(([k]) => k.split('_')[1].toUpperCase()))].join('/');
    const active = jobs.find(j => j.status === 'rendering' || j.status === 'analyzing' || j.status === 'upscaling');
    return (
        <footer className="statusbar">
            <span className={`status-dot ${active ? 'downloading' : 'ok'}`} />
            <span>{active ? `${active.status === 'analyzing' ? 'Analysing' : active.status === 'upscaling' ? 'AI upscaling' : 'Rendering'} ${active.name} – ${Math.round(active.progress * 100)}% · ${active.fps.toFixed(1)} fps` : 'Ready'}</span>
            <span className="grow" />
            {media && <span className="dim">Preview {stats.w}×{stats.h} · {stats.ms.toFixed(1)} ms · {stats.prims} primitives</span>}
            <span className={`status-dot ${engine ? 'ok' : 'idle'}`} />
            <span title={engine?.reason}>Engine: {engine ? engine.kind === 'webgpu' ? 'WebGPU' : 'WebGL2' : '…'}</span>
            <span className={`status-dot ${Object.values(caps?.hwEncoders || {}).some(Boolean) ? 'ok' : 'idle'}`} />
            <span>GPU: {caps?.gpuName || '…'}{hwFams ? ` (${hwFams})` : ''}</span>
            <span className={`status-dot ${caps?.ok ? 'ok' : 'error'}`} />
            <span>FFmpeg: {caps?.ok ? caps.ffmpegVersion.replace(/-.*$/, '').slice(0, 24) : caps ? 'missing' : '…'}</span>
        </footer>
    );
}

function UpdateModal() {
    const updates = useStore(s => s.updates);
    const busy = useStore(s => s.updateBusy);
    const [sel, setSel] = useState<string[]>(() => updates?.items.map(i => i.id) ?? []);
    const [dontAsk, setDontAsk] = useState(false);
    const [results, setResults] = useState<string[]>([]);
    if (!updates?.items.length) return null;
    const later = async () => {
        if (dontAsk) for (const i of updates.items) await api.skipUpdate(i.id);
        setState({ updateModal: false });
    };
    const now = async () => {
        const out: string[] = [];
        // tools and LUTs first, the app installer last (it closes the app)
        const order: UpdateItem['id'][] = ['ffmpeg', 'luts', 'rve', 'app'];
        for (const id of order) {
            if (!sel.includes(id) || !updates.items.some(i => i.id === id)) continue;
            setState({ updateBusy: { id, message: 'Starting…', pct: 0 } });
            const r = await api.applyUpdate(id);
            out.push(`${updates.items.find(i => i.id === id)?.label}: ${r.message}`);
            setResults([...out]);
            if (r.restart) return;
        }
        setState({ updateBusy: null });
        const [caps, info, luts] = await Promise.all([api.caps(true), api.info(), api.luts()]);
        setState({ caps, info, luts, updates: { ...updates, items: updates.items.filter(i => !sel.includes(i.id)) } });
    };
    return (
        <Modal title="Updates available" onClose={busy ? undefined : later} footer={
            results.length && !busy ? <Button kind="primary" onClick={() => setState({ updateModal: false })}>Close</Button> : <>
                <Toggle checked={dontAsk} onChange={setDontAsk} label="Don't ask again for these versions" disabled={!!busy} />
                <span className="grow" />
                <Button onClick={later} disabled={!!busy}>Later – keep current version</Button>
                <Button kind="primary" icon="download" onClick={now} disabled={!!busy || !sel.length}>Update now</Button>
            </>
        }>
            <p>New versions were found. Do you want to update now or keep the installed versions for now?</p>
            {updates.items.map(i => (
                <div key={i.id} className="update-item">
                    <label className="row gap">
                        <input type="checkbox" checked={sel.includes(i.id)} disabled={!!busy} onChange={e => setSel(e.target.checked ? [...sel, i.id] : sel.filter(x => x !== i.id))} />
                        <b>{i.label}</b>
                        <span className="dim">{i.current} → <b className="accent">{i.latest}</b></span>
                        {i.size ? <span className="dim small">({(i.size / 1048576).toFixed(0)} MB)</span> : null}
                    </label>
                    {i.notes && <pre className="notes">{i.notes}</pre>}
                </div>
            ))}
            {busy && <Progress value={busy.pct} label={busy.message} />}
            {results.map((r, k) => <p key={k} className="small">{r}</p>)}
        </Modal>
    );
}

function ToolsModal({ onClose }: { onClose: () => void }) {
    const tools = useStore(s => s.tools);
    const caps = useStore(s => s.caps);
    if (caps?.ok && !tools.busy) return null;
    return (
        <Modal title="Portable tools required" onClose={tools.busy ? undefined : onClose} footer={<>
            <span className="grow" />
            <Button onClick={onClose} disabled={tools.busy}>Later</Button>
            <Button kind="primary" icon="download" disabled={tools.busy} onClick={() => installToolsNow()}>Download portable FFmpeg</Button>
        </>}>
            <p>UVE Manager decodes, analyses and encodes video with FFmpeg. No FFmpeg was found in the portable <code>tools</code> folder.</p>
            <p className="hint">The portable build (Windows x64, GPL, with NVENC, x264, x265, SVT-AV1, zimg; ~150 MB download) is placed in <code>{getState().info?.tools.dir}</code>. Nothing is installed system-wide.</p>
            {tools.busy && <Progress value={tools.pct} label={tools.message} />}
        </Modal>
    );
}

function Toasts() {
    const toasts = useStore(s => s.toasts);
    return <div className="toasts" role="status" aria-live="polite">{toasts.map(t => <div key={t.id} className={`toast ${t.tone}`} onClick={() => setState(s => ({ toasts: s.toasts.filter(x => x.id !== t.id) }))}>{t.text}</div>)}</div>;
}

function useShortcuts() {
    useEffect(() => {
        const onKey = (e: KeyboardEvent) => {
            const t = e.target as HTMLElement;
            if (t && (t.tagName === 'INPUT' && !['range', 'checkbox'].includes((t as HTMLInputElement).type) || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable)) return;
            if (document.querySelector('.modal-backdrop')) return;
            const map = getState().prefs?.shortcuts;
            if (!map) return;
            const combo = comboFromEvent(e);
            const action = Object.entries(map).find(([, v]) => sameCombo(v, combo))?.[0];
            if (!action) return;
            e.preventDefault();
            const s = getState();
            switch (action) {
                case 'base': toggleSetting('enabled'); break;
                case 'moody': toggleSetting('darkMoody'); break;
                case 'teal': toggleSetting('tealOrange'); break;
                case 'vibrant': toggleSetting('vibrantSat'); break;
                case 'hdr': toggleHdr(); break;
                case 'cycleProfile': cycleToneProfile(); break;
                case 'hud': setState({ showIcons: !s.showIcons }); break;
                case 'gpu': {
                    const q = s.prefs?.previewQuality === 'full' ? 'half' : 'full';
                    void api.setPrefs({ previewQuality: q }).then(p => setState({ prefs: p }));
                    toast(`Preview quality: ${q}`, 'info', 1500);
                    break;
                }
                case 'expert': setState({ view: 'editor', tab: 'Expert' }); break;
                case 'manager': setState({ view: s.view === 'presets' ? 'luts' : s.view === 'luts' ? 'filters' : 'presets' }); break;
                case 'scopes': setState({ scopesHud: !s.scopesHud }); break;
                case 'auto': toggleSetting('autoOn'); if (!s.effect.settings.autoOn && s.analysis.status !== 'ready') void import('../state/actions').then(a => a.runAnalysis()); break;
                case 'compare': setState({ compare: !s.compare, bypass: false }); break;
                case 'playPause': player.toggle(); break;
                case 'frameBack': player.step(-1); break;
                case 'frameFwd': player.step(1); break;
                case 'jumpBack': player.seek(Math.max(0, s.time - 1)); break;
                case 'jumpFwd': player.seek(Math.min(player.duration(), s.time + 1)); break;
                case 'start': player.seek(0); break;
                case 'end': player.seek(player.duration() - 0.001); break;
                case 'undo': undo(); break;
                case 'redo': case 'redoAlt': redo(); break;
                case 'save': void saveProject(false); break;
                case 'open': void openMediaOntoTimeline(); break;
                case 'render': void enqueueTimelineJob(); break;
                case 'escape': setState({ scopesHud: false, compare: false, rangeSel: null }); break;
                default: runTimelineShortcut(action); break;
            }
        };
        window.addEventListener('keydown', onKey);
        return () => window.removeEventListener('keydown', onKey);
    }, []);
    // Ctrl+Alt+W (⌃⌥W on macOS) toggles Auto White Balance (userscript WB lock)
    useEffect(() => {
        const k = (e: KeyboardEvent) => { if (sameCombo(comboFromEvent(e), 'Ctrl+Alt+W')) { e.preventDefault(); toggleSetting('autoLockWB'); void import('../state/actions').then(a => a.recomputeAuto()); } };
        window.addEventListener('keydown', k);
        return () => window.removeEventListener('keydown', k);
    }, []);
}

/** Editor state stored in settings.json (section "editor") so nothing has to be chosen again after a restart. */
interface EditorSnapshot {
    effect?: unknown;
    exportSettings?: unknown;
    view?: View;
    tab?: string;
    compare?: boolean;
    wipe?: number;
    showIcons?: boolean;
    mediaPath?: string | null;
    inPoint?: number | null;
    outPoint?: number | null;
    time?: number;
    projectPath?: string | null;
}

const VIEWS: View[] = ['editor', 'presets', 'luts', 'filters', 'queue', 'settings'];
const TABS = ['Basic', 'Color', 'HDR', 'Detail', 'LUTs', 'Custom', 'Auto', 'Expert'];
let editorRestored = false;

function editorSnapshot(): EditorSnapshot {
    const s = getState();
    return {
        effect: s.effect, exportSettings: s.exportSettings, view: s.view, tab: s.tab, compare: s.compare, wipe: s.wipe,
        showIcons: s.showIcons, mediaPath: s.media?.path ?? null, inPoint: s.inPoint, outPoint: s.outPoint,
        time: Math.round(s.time * 1000) / 1000, projectPath: s.projectPath
    };
}

async function restoreEditor(e: EditorSnapshot | null) {
    try {
        if (!e) return;
        const patch: Partial<ReturnType<typeof getState>> = {};
        if (e.effect) patch.effect = effectFromUnknown(e.effect);
        if (e.exportSettings && typeof e.exportSettings === 'object') patch.exportSettings = { ...DEFAULT_EXPORT, ...(e.exportSettings as object) };
        if (e.view && VIEWS.includes(e.view)) patch.view = e.view;
        if (e.tab && TABS.includes(e.tab)) patch.tab = e.tab as typeof patch.tab;
        if (typeof e.compare === 'boolean') patch.compare = e.compare;
        if (typeof e.wipe === 'number' && e.wipe >= 0 && e.wipe <= 1) patch.wipe = e.wipe;
        if (typeof e.showIcons === 'boolean') patch.showIcons = e.showIcons;
        if (typeof e.projectPath === 'string') patch.projectPath = e.projectPath;
        // the app starts with an empty timeline: the last video is not reopened
        setState(patch);
    } finally {
        editorRestored = true;
    }
}

async function init(setToolsPrompt: (b: boolean) => void) {
    const [info, prefs, luts, prof, lib, session, jobs] = await Promise.all([
        api.info(), api.prefs(), api.luts(), api.profiles(), api.filters(), api.session(), api.jobs()
    ]);
    const filterLibrary = (lib as unknown[]).map(normalizeEntry).filter((x): x is CustomFilterEntry => !!x);
    setState({ info, prefs, luts, profiles: prof.profiles, activeProfileId: prof.activeId, filterLibrary, jobs, exportSettings: { ...DEFAULT_EXPORT, ...prefs.defaultExport } });
    await restoreEditor(session as EditorSnapshot | null);
    api.on('luts:changed', (l: LutLibraryState) => setState({ luts: l }));
    api.on('render:jobs', (j: JobState[]) => setState({ jobs: j }));
    api.on('tools:progress', (p: { message: string; pct: number }) => setState({ tools: { busy: true, message: p.message, pct: p.pct } }));
    api.on('updates:progress', (p: { id: string; message: string; pct: number }) => setState({ updateBusy: p }));
    api.on('analysis:progress', (p: { path: string; pct: number }) => { if (getState().media?.path === p.path) setState({ analysis: { ...getState().analysis, pct: p.pct } }); });
    api.on('media:proxyProgress', (p: { path: string; pct: number }) => { if (getState().media?.path === p.path) setState({ proxyPct: p.pct }); });

    const caps = await api.caps();
    setState({ caps });
    if (!caps.ok) setToolsPrompt(true);
    if (prefs.autoUpdate) {
        try {
            const r = await api.checkUpdates();
            // missing tools are handled by the tools dialog
            const items = caps.ok ? r.items : r.items.filter(i => i.id !== 'ffmpeg');
            setState({ updates: { ...r, items }, updateModal: items.length > 0 });
        } catch (e) { console.warn('update check failed', errMsg(e)); }
    }
}

export function App() {
    const view = useStore(s => s.view);
    const updateModal = useStore(s => s.updateModal);
    const [toolsPrompt, setToolsPrompt] = useState(false);
    useShortcuts();
    useEffect(() => { void init(setToolsPrompt); }, []);

    // persist the editor state into settings.json shortly after changes (and when the window closes)
    useEffect(() => {
        let timer = 0;
        let last = '';
        const save = () => {
            if (!editorRestored) return;
            const snap = editorSnapshot();
            const key = JSON.stringify({ ...snap, time: Math.round((snap.time || 0) * 2) / 2 });
            if (key === last) return;
            last = key;
            void api.saveSession(snap);
        };
        const unsub = subscribe(() => { clearTimeout(timer); timer = window.setTimeout(save, 800); });
        const onUnload = () => { clearTimeout(timer); save(); };
        window.addEventListener('beforeunload', onUnload);
        return () => { unsub(); clearTimeout(timer); window.removeEventListener('beforeunload', onUnload); };
    }, []);

    // whole-window drag & drop
    useEffect(() => {
        const over = (e: DragEvent) => e.preventDefault();
        const drop = (e: DragEvent) => {
            if ((e.target as HTMLElement).closest('.player')) return;
            e.preventDefault();
            const f = e.dataTransfer?.files[0];
            if (!f) return;
            const p = api.pathForFile(f);
            if (!p) return;
            setState({ view: 'editor' });
            if (/\.uveproj$/i.test(p)) void openProject(p); else void loadVideo(p, { place: true });
        };
        window.addEventListener('dragover', over);
        window.addEventListener('drop', drop);
        return () => { window.removeEventListener('dragover', over); window.removeEventListener('drop', drop); };
    }, []);

    return (
        <div className="app">
            <TitleBar />
            <div className="body">
                <LeftRail />
                <main className="main">
                    <div className={`editor${view === 'editor' ? '' : ' hidden'}`}>
                        <div className="editor-center">
                            <Player />
                            <Transport />
                            <Timeline />
                            <div className="bottom-panels">
                                <MediaPool />
                                <MediaInfoPanel />
                                <ExportPanel />
                            </div>
                        </div>
                        <Inspector />
                    </div>
                    {view === 'presets' && <PresetsView />}
                    {view === 'luts' && <LutView />}
                    {view === 'filters' && <FilterStudio />}
                    {view === 'queue' && <QueueView />}
                    {view === 'settings' && <SettingsView />}
                </main>
            </div>
            <StatusBar />
            {updateModal && <UpdateModal />}
            {toolsPrompt && <ToolsModal onClose={() => setToolsPrompt(false)} />}
            <Toasts />
        </div>
    );
}
