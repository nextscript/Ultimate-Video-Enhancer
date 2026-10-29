import { useEffect, useMemo, useRef, useState } from 'react';
import { applyMat, matIdentity4x5 } from '../../shared/uve/matrix';
import { lutKeyOf } from '../../shared/uve/lut';
import { DEFAULT_SETTINGS, PROFILE_NAMES, type UveSettings } from '../../shared/uve/settings';
import { defaultUserProfile, newProfileId, sanitizeProfileFilename } from '../../shared/uve/profiles';
import type { UserProfile } from '../../shared/types';
import { api } from '../api';
import {
    applyProfile, enqueueBatch, exportLuts, exportProfiles, importLutFiles, importProfiles, persistProfiles, profileFromCurrent, selectLut, setSetting
} from '../state/actions';
import { getState, setState, toast, updateEffect, useStore } from '../state/store';
import { Button, Icon, Progress, Section, Select, Toggle, fmtBytes, fmtTime } from './controls';
import { Scopes } from './Scopes';
import { RveSettings } from './RveSettings';
import { player } from './Player';

// ------------------------------------------------------------------ Presets
const LOOKS: { id: string; label: string; desc: string; apply: (s: UveSettings) => Partial<UveSettings> }[] = [
    { id: 'base', label: 'Base Tone', desc: 'Base optimization (Ctrl+Alt+B)', apply: s => ({ enabled: !s.enabled }) },
    { id: 'moody', label: 'Dark & Moody', desc: 'Ctrl+Alt+D', apply: s => ({ darkMoody: !s.darkMoody }) },
    { id: 'teal', label: 'Teal & Orange', desc: 'Ctrl+Alt+O', apply: s => ({ tealOrange: !s.tealOrange }) },
    { id: 'vib', label: 'Vibrant', desc: 'Ctrl+Alt+V', apply: s => ({ vibrantSat: !s.vibrantSat }) },
    { id: 'hdr', label: 'HDR', desc: 'Ctrl+Alt+P', apply: s => ({ hdr: s.hdr ? 0 : 0.3 }) }
];

function lookActive(id: string, s: UveSettings) {
    return id === 'base' ? s.enabled : id === 'moody' ? s.darkMoody : id === 'teal' ? s.tealOrange : id === 'vib' ? s.vibrantSat : s.hdr !== 0;
}

export function PresetsView() {
    const profiles = useStore(s => s.profiles);
    const activeId = useStore(s => s.activeProfileId);
    const settings = useStore(s => s.effect.settings);
    const [sel, setSel] = useState<string[]>([]);
    const [rename, setRename] = useState<{ id: string; name: string } | null>(null);
    const save = (list: UserProfile[]) => { setState({ profiles: list }); void persistProfiles(); };
    return (
        <div className="view">
            <h1>Presets</h1>
            <Section title="Built-in looks (Ultimate Video Enhancer)" collapsible={false}>
                <div className="look-grid">
                    {LOOKS.map(l => (
                        <button key={l.id} className={`look-card${lookActive(l.id, settings) ? ' on' : ''}`} onClick={() => updateEffect(e => ({ ...e, settings: { ...e.settings, ...l.apply(e.settings) } }), '')}>
                            <b>{l.label}</b><span className="dim small">{l.desc}</span><span className="state">{lookActive(l.id, settings) ? 'ON' : 'OFF'}</span>
                        </button>
                    ))}
                </div>
                <div className="look-grid">
                    {(Object.keys(PROFILE_NAMES) as UveSettings['profile'][]).map(p => (
                        <button key={p} className={`look-card${settings.profile === p ? ' on' : ''}`} onClick={() => setSetting('profile', p, false)}>
                            <b>{PROFILE_NAMES[p]}</b><span className="dim small">Tone profile</span>
                        </button>
                    ))}
                </div>
                <p className="hint">Looks and profiles use the exact matrices, curves and tones of the userscript (v1.17.5). They can be combined like in the browser.</p>
            </Section>
            <Section title="User Profile Manager" collapsible={false} right={
                <div className="row gap">
                    <Button small icon="plus" onClick={() => { const p = profileFromCurrent(`Profile ${profiles.length + 1}`); save([...profiles, p]); setState({ activeProfileId: p.id }); }}>From current</Button>
                    <Button small icon="upload" onClick={() => importProfiles()}>Import JSON/ZIP</Button>
                    <Button small icon="download" disabled={!sel.length} onClick={() => exportProfiles(sel, sel.length > 1 ? 'zip' : 'json')}>Export</Button>
                    <Button small disabled={sel.length !== 1} onClick={() => exportProfiles(sel, 'gvf-settings')} title="Single profile as userscript gvf-settings JSON">Export gvf-settings</Button>
                    <Button small disabled={!profiles.length} onClick={() => exportProfiles(profiles.map(p => p.id), 'zip')}>Export all (ZIP)</Button>
                </div>
            }>
                {!profiles.length && <p className="hint">No user profiles yet. Create one from the current settings or import UVE profile JSON/ZIP files (e.g. My_Profile.json from the repository).</p>}
                <table className="table">
                    <thead><tr><th /><th>Name</th><th>Profile</th><th>Looks</th><th>LUT</th><th>Updated</th><th /></tr></thead>
                    <tbody>
                        {profiles.map(p => (
                            <tr key={p.id} className={p.id === activeId ? 'active' : ''}>
                                <td><input type="checkbox" checked={sel.includes(p.id)} onChange={e => setSel(e.target.checked ? [...sel, p.id] : sel.filter(x => x !== p.id))} aria-label={`Select ${p.name}`} /></td>
                                <td>{rename?.id === p.id
                                    ? <input className="text" autoFocus value={rename.name} onChange={e => setRename({ id: p.id, name: e.target.value })}
                                        onBlur={() => { save(profiles.map(x => x.id === p.id ? { ...x, name: sanitizeProfileFilename(rename.name), updatedAt: Date.now() } : x)); setRename(null); }}
                                        onKeyDown={e => { if (e.key === 'Enter') (e.target as HTMLInputElement).blur(); if (e.key === 'Escape') setRename(null); }} />
                                    : <b>{p.name}</b>}</td>
                                <td>{PROFILE_NAMES[p.settings.profile]}</td>
                                <td className="small">{[p.settings.enabled && 'Base', p.settings.darkMoody && 'Moody', p.settings.tealOrange && 'Teal', p.settings.vibrantSat && 'Vibrant', p.settings.hdr && 'HDR', p.settings.autoOn && 'Auto'].filter(Boolean).join(', ') || '—'}</td>
                                <td className="small">{p.settings.lutProfile === 'none' ? '—' : p.settings.lutProfile.replace('||', ' / ')}</td>
                                <td className="small dim">{new Date(p.updatedAt).toLocaleString()}</td>
                                <td className="row gap">
                                    <Button small kind="primary" onClick={() => applyProfile(p)}>Apply</Button>
                                    <Button small onClick={() => setRename({ id: p.id, name: p.name })}>Rename</Button>
                                    <Button small icon="copy" title="Duplicate" onClick={() => save([...profiles, { ...JSON.parse(JSON.stringify(p)), id: newProfileId(), name: `${p.name} copy`, createdAt: Date.now(), updatedAt: Date.now() }])} />
                                    <Button small icon="trash" kind="danger" title="Delete" onClick={() => { if (confirm(`Delete profile "${p.name}"?`)) { save(profiles.filter(x => x.id !== p.id)); if (activeId === p.id) setState({ activeProfileId: null }); } }} />
                                </td>
                            </tr>
                        ))}
                    </tbody>
                </table>
                <div className="row gap">
                    <Button small onClick={() => { const d = defaultUserProfile(); d.id = newProfileId(); d.name = 'UVE Default'; save([...profiles, d]); }}>Add UVE default profile</Button>
                    <Button small onClick={() => updateEffect(e => ({ ...e, settings: { ...DEFAULT_SETTINGS } }), '')}>Reset current to UVE defaults</Button>
                </div>
            </Section>
        </div>
    );
}

// ------------------------------------------------------------------ LUT Library
function LutSwatch({ m }: { m: number[] }) {
    const ref = useRef<HTMLCanvasElement>(null);
    useEffect(() => {
        const c = ref.current!;
        const ctx = c.getContext('2d')!;
        const W = 96, H = 24;
        const img = ctx.createImageData(W, H);
        for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
            const hue = x / W, light = 1 - y / H;
            const k = (n: number) => (n + hue * 6) % 6;
            const f = (n: number) => light - light * 0.6 * Math.max(0, Math.min(k(n), 4 - k(n), 1));
            const [r, g, b] = applyMat(m, f(5), f(3), f(1));
            const i = (y * W + x) * 4;
            img.data[i] = r * 255; img.data[i + 1] = g * 255; img.data[i + 2] = b * 255; img.data[i + 3] = 255;
        }
        ctx.putImageData(img, 0, 0);
    }, [m]);
    return <canvas ref={ref} width={96} height={24} className="swatch" />;
}

type Frame = { data: Uint8Array; w: number; h: number };

/** Draws the frame with a 4×5 LUT matrix applied (identity = original). */
function drawLutFrame(c: HTMLCanvasElement, frame: Frame, m: number[] | null) {
    c.width = frame.w;
    c.height = frame.h;
    const ctx = c.getContext('2d')!;
    const img = ctx.createImageData(frame.w, frame.h);
    const d = frame.data, o = img.data;
    for (let i = 0; i < d.length; i += 4) {
        if (m) {
            const [r, g, b] = applyMat(m, d[i] / 255, d[i + 1] / 255, d[i + 2] / 255);
            o[i] = r * 255; o[i + 1] = g * 255; o[i + 2] = b * 255;
        } else { o[i] = d[i]; o[i + 1] = d[i + 1]; o[i + 2] = d[i + 2]; }
        o[i + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
}

function LutThumb({ m, frame, onOpen, label }: { m: number[]; frame: Frame | null; onOpen: () => void; label: string }) {
    const ref = useRef<HTMLCanvasElement>(null);
    useEffect(() => { if (frame && ref.current) drawLutFrame(ref.current, frame, m); }, [m, frame]);
    if (!frame) return <div className="swatches"><LutSwatch m={matIdentity4x5()} /><LutSwatch m={m} /></div>;
    return (
        <button className="lut-thumb" onClick={onOpen} title={`Enlarge preview: ${label}`} aria-label={`Enlarge preview of ${label}`}>
            <canvas ref={ref} />
        </button>
    );
}

/** Enlarged LUT preview on the current video frame with a draggable before/after wipe. */
function LutZoom({ lut, frame, onClose, onNav, active }: {
    lut: { name: string; group: string; matrix4x5: number[]; key: string }; frame: Frame; onClose: () => void; onNav: (d: number) => void; active: boolean;
}) {
    const before = useRef<HTMLCanvasElement>(null);
    const after = useRef<HTMLCanvasElement>(null);
    const [wipe, setWipe] = useState(0.5);
    const drag = useRef(false);
    useEffect(() => {
        if (before.current) drawLutFrame(before.current, frame, null);
        if (after.current) drawLutFrame(after.current, frame, lut.matrix4x5);
    }, [frame, lut]);
    useEffect(() => {
        const k = (e: KeyboardEvent) => {
            if (e.key === 'Escape') onClose();
            if (e.key === 'ArrowLeft') { e.preventDefault(); onNav(-1); }
            if (e.key === 'ArrowRight') { e.preventDefault(); onNav(1); }
        };
        window.addEventListener('keydown', k, true);
        return () => window.removeEventListener('keydown', k, true);
    }, [onClose, onNav]);
    const fromEvent = (e: React.PointerEvent) => {
        const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
        setWipe(Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)));
    };
    return (
        <div className="modal-backdrop lut-zoom-backdrop" role="dialog" aria-modal="true" aria-label={`LUT preview ${lut.name}`} onClick={onClose}>
            <div className="lut-zoom" onClick={e => e.stopPropagation()}>
                <header className="row gap">
                    <b className="grow">{lut.name} <span className="dim small">· {lut.group || '—'}</span></b>
                    <Button small icon="prev" onClick={() => onNav(-1)} title="Previous LUT (←)" />
                    <Button small icon="next" onClick={() => onNav(1)} title="Next LUT (→)" />
                    <Button small kind={active ? 'primary' : 'default'} onClick={() => selectLut(active ? 'none' : lut.key)}>{active ? 'Active – remove' : 'Apply'}</Button>
                    <button className="icon-btn" onClick={onClose} aria-label="Close"><Icon name="x" /></button>
                </header>
                <div className="lut-zoom-stage" style={{ aspectRatio: `${frame.w} / ${frame.h}` }}
                    onPointerDown={e => { drag.current = true; (e.currentTarget as Element).setPointerCapture(e.pointerId); fromEvent(e); }}
                    onPointerMove={e => { if (drag.current) fromEvent(e); }}
                    onPointerUp={() => { drag.current = false; }}>
                    <canvas ref={after} />
                    <canvas ref={before} style={{ clipPath: `inset(0 ${(1 - wipe) * 100}% 0 0)` }} />
                    <span className="badge badge-orig">Original</span>
                    <span className="badge badge-enh">LUT</span>
                    <div className="wipe-handle" style={{ left: `${wipe * 100}%` }}><span><Icon name="split" size={16} /></span></div>
                </div>
                <p className="hint">Preview of the LUT alone on the current video frame (drag to compare). In the editor the LUT is combined with the rest of the UVE chain.</p>
            </div>
        </div>
    );
}

export function LutView() {
    const luts = useStore(s => s.luts);
    const media = useStore(s => s.media);
    const [thumb, setThumb] = useState<Frame | null>(null);
    const [big, setBig] = useState<Frame | null>(null);
    const [zoomKey, setZoomKey] = useState<string | null>(null);
    const vw = media?.video?.displayWidth || 16, vh = media?.video?.displayHeight || 9;
    const grab = () => {
        if (!media) { setThumb(null); setBig(null); return; }
        const tw = 192, th = Math.max(2, Math.round(tw * vh / vw));
        const bw = Math.min(1600, vw), bh = Math.max(2, Math.round(bw * vh / vw));
        setThumb(player.grabFrame(tw, th));
        setBig(player.grabFrame(bw, bh));
    };
    useEffect(grab, [media]); // eslint-disable-line react-hooks/exhaustive-deps
    const active = useStore(s => s.effect.settings.lutProfile);
    const autoDl = useStore(s => s.prefs?.autoDownloadLuts ?? true);
    const [group, setGroup] = useState<string>('*');
    const [q, setQ] = useState('');
    const [sel, setSel] = useState<string[]>([]);
    const [importGroup, setImportGroup] = useState('Imported');
    const [fit, setFit] = useState({ linearizeIn: false, delinearizeOut: false, flipY: false });
    const list = luts.profiles.filter(p => (group === '*' || p.group === group) && (!q || `${p.group} ${p.name}`.toLowerCase().includes(q.toLowerCase())));
    return (
        <div className="view">
            <h1>LUT Library</h1>
            <Section title="UVE LUT package" collapsible={false}>
                <div className="row gap wrap">
                    <span className={`status-dot ${luts.repo.status}`} />
                    <span className="grow">{luts.repo.message || 'LUTsProfiles_v3.0.zip from github.com/nextscript/Ultimate-Video-Enhancer'}</span>
                    <Button small icon="download" disabled={luts.repo.status === 'downloading'} onClick={async () => setState({ luts: await api.downloadLuts(true) })}>{luts.repo.count ? 'Re-download' : 'Download now'}</Button>
                    <Toggle checked={autoDl} label="Download automatically" onChange={async v => setState({ prefs: await api.setPrefs({ autoDownloadLuts: v }) })} />
                </div>
                {luts.repo.status === 'downloading' && <Progress value={0.5} label="Downloading…" />}
            </Section>
            <div className="lib-layout">
                <nav className="groups" aria-label="LUT groups">
                    <button className={group === '*' ? 'sel' : ''} onClick={() => setGroup('*')}>All <span className="dim">{luts.profiles.length}</span></button>
                    {luts.groups.map(g => <button key={g} className={group === g ? 'sel' : ''} onClick={() => setGroup(g)}>{g || '(no group)'} <span className="dim">{luts.profiles.filter(p => p.group === g).length}</span></button>)}
                </nav>
                <div className="lib-main">
                    <div className="row gap wrap">
                        <div className="search grow"><Icon name="search" size={14} /><input placeholder="Search LUTs…" value={q} onChange={e => setQ(e.target.value)} aria-label="Search LUTs" /></div>
                        <Button small icon="download" disabled={!sel.length} onClick={() => exportLuts(sel)}>Export {sel.length > 1 ? 'ZIP' : 'JSON'}</Button>
                        <Button small icon="trash" kind="danger" disabled={!sel.length} onClick={async () => { if (confirm(`Remove ${sel.length} LUT(s)? Repository LUTs can be restored.`)) { setState({ luts: await api.deleteLuts(sel) }); setSel([]); } }}>Remove</Button>
                        <Button small onClick={async () => setState({ luts: await api.restoreLuts() })}>Restore removed</Button>
                        <Button small icon="reset" disabled={!media} onClick={grab} title="Use the current frame of the editor for the previews">Refresh preview frame</Button>
                    </div>
                    {!media && <p className="hint">Open a video in the editor to preview every LUT on your own footage.</p>}
                    <div className="lut-grid">
                        {list.map(p => {
                            const k = lutKeyOf(p);
                            return (
                                <div key={k} className={`lut-card${active === k ? ' active' : ''}`}>
                                    <input type="checkbox" checked={sel.includes(k)} onChange={e => setSel(e.target.checked ? [...sel, k] : sel.filter(x => x !== k))} aria-label={`Select ${p.name}`} />
                                    <LutThumb m={p.matrix4x5} frame={thumb} label={p.name} onOpen={() => { grab(); setZoomKey(k); }} />
                                    <div className="grow"><b>{p.name}</b><div className="dim small">{p.group || '—'} · {p.source === 'repo' ? 'UVE package' : 'user'}</div></div>
                                    <Button small kind={active === k ? 'primary' : 'default'} onClick={() => selectLut(active === k ? 'none' : k)}>{active === k ? 'Active' : 'Apply'}</Button>
                                </div>
                            );
                        })}
                        {!list.length && <p className="hint">No LUTs{luts.repo.status === 'offline' ? ' – the LUT package could not be downloaded (offline?).' : '.'}</p>}
                    </div>
                </div>
            </div>
            <Section title="Import LUTs (.cube, tiled PNG, gvf-lut-profile JSON, ZIP)" collapsible={false}>
                <div className="row gap wrap">
                    <label className="select-row"><span className="select-label">Group</span><input className="text" value={importGroup} onChange={e => setImportGroup(e.target.value)} /></label>
                    <Toggle checked={fit.linearizeIn} label="Linearize input" onChange={v => setFit({ ...fit, linearizeIn: v })} title="Sample the LUT input grid in linear light (userscript option)" />
                    <Toggle checked={fit.delinearizeOut} label="Delinearize output" onChange={v => setFit({ ...fit, delinearizeOut: v })} />
                    <Toggle checked={fit.flipY} label="Flip PNG Y" onChange={v => setFit({ ...fit, flipY: v })} />
                    <Button kind="primary" icon="upload" onClick={() => importLutFiles(importGroup.trim(), fit)}>Import files…</Button>
                </div>
                <p className="hint">Like the userscript, every LUT is converted into a 4×5 color matrix by a least-squares fit over an 11³ sample grid. Strongly non-linear 3D LUTs are therefore approximated.</p>
            </Section>
            {zoomKey && big && (() => {
                const idx = list.findIndex(p => lutKeyOf(p) === zoomKey);
                const p = list[idx];
                if (!p) return null;
                return <LutZoom lut={{ ...p, key: zoomKey }} frame={big} active={active === zoomKey} onClose={() => setZoomKey(null)}
                    onNav={d => { const n = list[(idx + d + list.length) % list.length]; if (n) setZoomKey(lutKeyOf(n)); }} />;
            })()}
        </div>
    );
}

// ------------------------------------------------------------------ Scopes
export function ScopesView() {
    return (
        <div className="view">
            <h1>Scopes</h1>
            <p className="hint">Analysis of the processed preview frame (and the original for comparison). Scopes are never burned into the export.</p>
            <Scopes large />
        </div>
    );
}

// ------------------------------------------------------------------ Render queue & batch
export function QueueView() {
    const jobs = useStore(s => s.jobs);
    const profiles = useStore(s => s.profiles);
    const [batchProfile, setBatchProfile] = useState<string>('');
    const [openLog, setOpenLog] = useState<string | null>(null);
    return (
        <div className="view">
            <h1>Render Queue</h1>
            <Section title="Batch processing" collapsible={false}>
                <div className="row gap wrap">
                    <Select label="Profile for new jobs" value={batchProfile} onChange={setBatchProfile}
                        options={[{ value: '', label: 'Current editor settings' }, ...profiles.map(p => ({ value: p.id, label: p.name }))]} />
                    <Button kind="primary" icon="plus" onClick={async () => {
                        const paths = await api.openVideos(true);
                        if (paths.length) await enqueueBatch(paths, profiles.find(p => p.id === batchProfile) || null);
                    }}>Add files…</Button>
                    <span className="hint">Output settings come from the editor's Output Settings panel. Each job keeps its own snapshot of the settings.</span>
                </div>
            </Section>
            <div className="row gap"><span className="grow" /><Button small onClick={() => api.clearFinished()}>Clear finished</Button></div>
            {!jobs.length && <p className="hint">The queue is empty. Use "Render Video" in the editor or add files above.</p>}
            {jobs.map(j => (
                <div key={j.id} className={`job ${j.status}`}>
                    <div className="row gap">
                        <b className="grow" title={j.input}>{j.name}</b>
                        <span className={`tag ${j.status}`}>{j.status}</span>
                        {j.encoder && <span className="tag">{j.encoder}</span>}
                        {['queued', 'analyzing', 'rendering', 'upscaling'].includes(j.status) && <Button small kind="danger" onClick={() => api.cancelJob(j.id)}>Cancel</Button>}
                        {['failed', 'canceled'].includes(j.status) && <Button small onClick={() => api.retryJob(j.id)}>Retry</Button>}
                        {j.status === 'done' && <Button small icon="folder" onClick={() => api.showItem(j.output)}>Show</Button>}
                        <Button small onClick={() => setOpenLog(openLog === j.id ? null : j.id)}>Log</Button>
                        {!['analyzing', 'rendering', 'upscaling'].includes(j.status) && <Button small icon="trash" title="Remove" onClick={() => api.removeJob(j.id)} />}
                    </div>
                    <Progress value={j.progress} label={j.status === 'analyzing' ? `Analysing ${Math.round(j.progress * 100)}%` : j.status === 'upscaling' ? `AI upscaling (RVE) ${Math.round(j.progress * 100)}%` : `${Math.round(j.progress * 100)}%`} />
                    <div className="job-stats small dim">
                        <span>{j.frames}/{j.totalFrames || '?'} frames</span>
                        <span>{j.fps.toFixed(1)} fps</span>
                        <span>elapsed {fmtTime(j.elapsed)}</span>
                        <span>remaining ≈ {fmtTime(j.eta)}</span>
                        <span>{fmtBytes(j.outSize)}</span>
                        <span title={j.output}>→ {j.output}</span>
                    </div>
                    {j.error && <p className="warn-text">{j.error}</p>}
                    {openLog === j.id && <pre className="log">{j.log.join('\n')}</pre>}
                </div>
            ))}
        </div>
    );
}

// ------------------------------------------------------------------ Settings
const SHORTCUT_LABELS: Record<string, string> = {
    base: 'Base Tone', moody: 'Dark & Moody', teal: 'Teal & Orange', vibrant: 'Vibrant', hdr: 'HDR look', cycleProfile: 'Cycle tone profile',
    hud: 'Toggle overlay badges', gpu: 'Toggle preview quality (GPU load)', expert: 'Expert Settings', manager: 'Profiles / LUT / filter manager', scopes: 'Scopes HUD',
    auto: 'Auto-Scene-Match', compare: 'Before/After wipe', bypass: 'Bypass effects (preview)', playPause: 'Play / Pause', frameBack: 'Previous frame',
    frameFwd: 'Next frame', start: 'Go to start', end: 'Go to end', setIn: 'Set In point', setOut: 'Set Out point', zoomIn: 'Timeline zoom in',
    zoomOut: 'Timeline zoom out', undo: 'Undo', redo: 'Redo', save: 'Save project', open: 'Open video', render: 'Render video', escape: 'Close overlay / exit compare'
};

export function SettingsView() {
    const prefs = useStore(s => s.prefs);
    const caps = useStore(s => s.caps);
    const info = useStore(s => s.info);
    const tools = useStore(s => s.tools);
    const updates = useStore(s => s.updates);
    const [capture, setCapture] = useState<string | null>(null);
    const [checking, setChecking] = useState(false);
    const conflicts = useMemo(() => {
        const seen: Record<string, string[]> = {};
        Object.entries(prefs?.shortcuts || {}).forEach(([k, v]) => { (seen[v] = seen[v] || []).push(k); });
        return Object.values(seen).filter(l => l.length > 1).flat();
    }, [prefs?.shortcuts]);
    useEffect(() => {
        if (!capture) return;
        const onKey = async (e: KeyboardEvent) => {
            e.preventDefault(); e.stopPropagation();
            if (['Control', 'Alt', 'Shift', 'Meta'].includes(e.key)) return;
            const combo = `${e.ctrlKey ? 'Ctrl+' : ''}${e.altKey ? 'Alt+' : ''}${e.shiftKey ? 'Shift+' : ''}${e.key === ' ' ? 'Space' : e.key.length === 1 ? e.key.toUpperCase() : e.key}`;
            setState({ prefs: await api.setPrefs({ shortcuts: { ...getState().prefs!.shortcuts, [capture]: combo } }) });
            setCapture(null);
        };
        window.addEventListener('keydown', onKey, true);
        return () => window.removeEventListener('keydown', onKey, true);
    }, [capture]);
    if (!prefs) return null;
    const setP = async (p: Parameters<typeof api.setPrefs>[0]) => setState({ prefs: await api.setPrefs(p) });
    return (
        <div className="view">
            <h1>Settings</h1>
            <Section title="Updates" collapsible={false}>
                <Toggle checked={prefs.autoUpdate} label="Check for updates automatically at startup" onChange={v => setP({ autoUpdate: v })}
                    title="Checks UVE Manager, the portable tools (FFmpeg) and the UVE LUT package. You are always asked before anything is installed." />
                <label className="select-row"><span className="select-label">Release source (GitHub owner/repo)</span>
                    <input className="text" value={prefs.updateRepo} onChange={e => setP({ updateRepo: e.target.value.trim() })} /></label>
                <div className="row gap">
                    <Button icon="download" disabled={checking} onClick={async () => {
                        setChecking(true);
                        try {
                            const r = await api.checkUpdates();
                            setState({ updates: r, updateModal: r.items.length > 0 });
                            if (!r.items.length) toast(r.errors.length ? `Update check incomplete: ${r.errors.join(' · ')}` : 'Everything is up to date.', r.errors.length ? 'warn' : 'ok', 7000);
                        } finally { setChecking(false); }
                    }}>{checking ? 'Checking…' : 'Check now'}</Button>
                    {updates && <span className="dim small">Last check: {new Date(updates.checkedAt).toLocaleString()}</span>}
                    {Object.keys(prefs.skippedVersions).length > 0 && <Button small onClick={() => setP({ skippedVersions: {} })}>Forget skipped versions</Button>}
                </div>
            </Section>
            <Section title="Portable tools" collapsible={false}>
                <dl className="kv">
                    <div><dt>Tools folder</dt><dd>{info?.tools.dir}</dd></div>
                    <div><dt>FFmpeg in use</dt><dd>{caps?.ok ? `${caps.ffmpegVersion} (${caps.source}) – ${caps.ffmpegPath}` : caps?.error || 'checking…'}</dd></div>
                    <div><dt>Portable build</dt><dd>{info?.tools.installed ? `${info.tools.version} · build ${info.tools.build.slice(0, 10)}` : 'not installed'}</dd></div>
                    <div><dt>GPU</dt><dd>{caps ? `${caps.gpuName}${caps.gpuDriver ? ` (driver ${caps.gpuDriver})` : ''}` : '…'}</dd></div>
                    <div><dt>Hardware encoders</dt><dd>{caps ? Object.entries(caps.nvencWorking).map(([k, v]) => `${k}: ${v ? 'working' : 'not available'}`).join(' · ') : '…'}</dd></div>
                    <div><dt>Software encoders</dt><dd>{caps ? Object.entries(caps.encoders).filter(([k, v]) => v && !k.includes('nvenc')).map(([k]) => k).join(', ') : '…'}</dd></div>
                    <div><dt>HDR tone mapping</dt><dd>{caps?.filters.zscale && caps?.filters.tonemap ? 'zscale + tonemap available' : 'not available'}</dd></div>
                </dl>
                {tools.busy && <Progress value={tools.pct} label={tools.message} />}
                <div className="row gap wrap">
                    <Button icon="download" disabled={tools.busy} onClick={() => installToolsNow()}>{info?.tools.installed ? 'Reinstall / update portable FFmpeg' : 'Install portable FFmpeg'}</Button>
                    <Button onClick={async () => setState({ caps: await api.caps(true) })}>Re-detect</Button>
                    <label className="select-row grow"><span className="select-label">Custom FFmpeg folder (optional)</span>
                        <input className="text" value={prefs.ffmpegDir} placeholder="empty = portable tools" onChange={e => setP({ ffmpegDir: e.target.value })} />
                        <button className="icon-btn" aria-label="Choose FFmpeg folder" onClick={async () => { const d = await api.pickFolder('Folder containing ffmpeg.exe and ffprobe.exe'); if (d) { await setP({ ffmpegDir: d }); setState({ caps: await api.caps(true) }); } }}><Icon name="folder" /></button>
                    </label>
                </div>
            </Section>
            <RveSettings />
            <Section title="Preview & engine" collapsible={false}>
                <Select label="Preview quality" value={prefs.previewQuality} onChange={v => setP({ previewQuality: v })}
                    options={[{ value: 'full', label: 'Full (exact)' }, { value: 'half', label: 'Half' }, { value: 'quarter', label: 'Quarter' }]} />
                <Toggle checked={prefs.parityMode} label="8-bit parity mode" onChange={v => setP({ parityMode: v })}
                    title="Quantizes every filter primitive to 8 bit like the browser's SVG filter pipeline (for parity tests). Off = higher precision float processing." />
                <div className="row gap">
                    <Button onClick={async () => { const b = await api.clearCache(); toast(`Cache cleared (${fmtBytes(b)})`, 'ok'); }}>Clear thumbnail / proxy / analysis cache</Button>
                </div>
            </Section>
            <Section title="Keyboard shortcuts" defaultOpen={false}>
                <p className="hint">Shortcuts work only inside the app and are disabled while typing in a text field. Click a shortcut and press the new key combination.</p>
                <table className="table compact">
                    <tbody>
                        {Object.entries(prefs.shortcuts).map(([k, v]) => (
                            <tr key={k} className={conflicts.includes(k) ? 'conflict' : ''}>
                                <td>{SHORTCUT_LABELS[k] || k}</td>
                                <td><button className={`kbd${capture === k ? ' capturing' : ''}`} onClick={() => setCapture(k)}>{capture === k ? 'press keys…' : v}</button></td>
                                <td>{conflicts.includes(k) && <span className="warn-text">conflict</span>}</td>
                            </tr>
                        ))}
                    </tbody>
                </table>
                <Button small onClick={async () => { const { DEFAULT_SHORTCUTS } = await import('../../shared/shortcuts'); setP({ shortcuts: { ...DEFAULT_SHORTCUTS } }); }}>Restore defaults</Button>
            </Section>
            <Section title="About & licenses" defaultOpen={false}>
                <dl className="kv">
                    <div><dt>UVE Manager</dt><dd>{info?.version} · Electron {info?.electron} · Chromium {info?.chrome}</dd></div>
                    <div><dt>Effect chain</dt><dd>{info?.uveSource.name} v{info?.uveSource.version} by Freak288 – ported from commit {info?.uveSource.commit.slice(0, 10)} (sha256 {info?.uveSource.sha256.slice(0, 12)}…)</dd></div>
                    <div><dt>LUT package</dt><dd>{info?.lutPackage} from {info?.uveSource.repo}</dd></div>
                    <div><dt>Data folder</dt><dd>{info?.userData}</dd></div>
                    <div><dt>Settings file</dt><dd>{info?.settingsFile} – all choices, the last editor session and the window position/size</dd></div>
                    <div><dt>Privacy</dt><dd>No telemetry. Network access only for the update check and the LUT/FFmpeg downloads.</dd></div>
                </dl>
                <p className="hint">FFmpeg is © the FFmpeg developers, distributed as a GPL build (BtbN FFmpeg-Builds). See docs/LICENSES.md for all third-party notices.</p>
                <div className="row gap">
                    <Button small onClick={() => api.openExternal('https://github.com/nextscript/Ultimate-Video-Enhancer')}>UVE repository</Button>
                    <Button small onClick={() => api.openExternal('https://greasyfork.org/de/scripts/561189-ultimate-video-enhancer-sharpen-hdr-color-boost')}>UVE on Greasy Fork</Button>
                </div>
            </Section>
        </div>
    );
}

export async function installToolsNow() {
    setState({ tools: { busy: true, message: 'Starting…', pct: 0 } });
    try {
        await api.installTools();
        const [caps, info] = await Promise.all([api.caps(true), api.info()]);
        setState({ caps, info, tools: { busy: false, message: '', pct: 1 } });
        toast(`Portable FFmpeg ${caps.ffmpegVersion} installed`, 'ok');
    } catch (e) {
        setState({ tools: { busy: false, message: '', pct: 0 } });
        toast(`Tool installation failed: ${e instanceof Error ? e.message : e}`, 'error', 9000);
    }
}
