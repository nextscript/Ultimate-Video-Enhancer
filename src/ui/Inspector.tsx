import { useMemo, useState } from 'react';
import { describeChain } from '../../shared/uve/graph';
import { BLEND_MODES, isMpvHookShader, isTypeSupported, parseUniformDefs } from '../../shared/uve/customFilters';
import { EXPERT_DEFAULTS, EXPERT_UI, type ExpertBlockName, exportExpert, expertDefaults, importExpert } from '../../shared/uve/expert';
import { lutKeyOf } from '../../shared/uve/lut';
import { CB_NAMES, DEFAULT_SETTINGS, NUM_SPECS, PROFILE_NAMES, type UveSettings } from '../../shared/uve/settings';
import { api } from '../api';
import { applyProfile, persistProfiles, profileFromCurrent, recomputeAuto, runAnalysis, selectLut, setSetting, toggleHdr } from '../state/actions';
import { type InspectorTab, getState, setState, toast, updateEffect, useStore } from '../state/store';
import { Button, Icon, Section, Select, SliderRow, Toggle } from './controls';
import { kbd } from './keys';

const TABS: InspectorTab[] = ['Basic', 'Color', 'HDR', 'Detail', 'LUTs', 'Custom', 'Auto', 'Expert'];

function Num({ k, disabled }: { k: keyof UveSettings; disabled?: boolean }) {
    const spec = NUM_SPECS[k as string];
    const v = useStore(s => s.effect.settings[k]) as number;
    return <SliderRow label={spec.label} value={v} min={spec.min} max={spec.max} step={spec.step} def={DEFAULT_SETTINGS[k] as number}
        desc={`${spec.desc}\nSource key: ${spec.sourceKey} · range ${spec.min}…${spec.max} · default ${DEFAULT_SETTINGS[k]}`}
        disabled={disabled} onChange={x => { setSetting(k, x as never); if (k === 'autoStrength') recomputeAuto(); }} />;
}

function Flag({ k, label, desc }: { k: 'enabled' | 'darkMoody' | 'tealOrange' | 'vibrantSat' | 'autoOn' | 'autoLockWB'; label: string; desc: string }) {
    const v = useStore(s => s.effect.settings[k]);
    return <Toggle checked={v} label={label} title={desc} onChange={x => {
        setSetting(k, x, false);
        if (k === 'autoLockWB') recomputeAuto();
        if (k === 'autoOn' && x && getState().analysis.status !== 'ready') void runAnalysis();
    }} />;
}

function PresetBar() {
    const profiles = useStore(s => s.profiles);
    const activeId = useStore(s => s.activeProfileId);
    const [naming, setNaming] = useState<string | null>(null);
    return (
        <div className="preset-bar">
            <label className="field-label">Preset</label>
            <Select value={activeId ?? ''} options={[{ value: '', label: profiles.length ? '— choose a profile —' : 'No profiles yet' }, ...profiles.map(p => ({ value: p.id, label: p.name }))]}
                onChange={id => { const p = profiles.find(x => x.id === id); if (p) applyProfile(p); }} />
            {naming === null ? (
                <div className="row gap">
                    <Button icon="save" onClick={() => {
                        const cur = profiles.find(p => p.id === activeId);
                        if (cur) {
                            const upd = { ...profileFromCurrent(cur.name, cur.id), createdAt: cur.createdAt };
                            setState({ profiles: profiles.map(p => p.id === cur.id ? upd : p) });
                            void persistProfiles();
                            toast(`Profile "${cur.name}" saved`, 'ok');
                        } else setNaming('My Profile');
                    }}>Save Preset</Button>
                    <Button icon="plus" onClick={() => setNaming('New Profile')}>Save As…</Button>
                    <Button onClick={() => setState({ view: 'presets' })}>Manage</Button>
                </div>
            ) : (
                <div className="row gap">
                    <input className="text" autoFocus value={naming} onChange={e => setNaming(e.target.value)} aria-label="Profile name"
                        onKeyDown={e => { if (e.key === 'Escape') setNaming(null); if (e.key === 'Enter') (e.currentTarget.nextSibling as HTMLButtonElement)?.click(); }} />
                    <Button kind="primary" onClick={() => {
                        if (!naming.trim()) return;
                        const p = profileFromCurrent(naming);
                        setState({ profiles: [...getState().profiles, p], activeProfileId: p.id });
                        void persistProfiles();
                        setNaming(null);
                        toast(`Profile "${p.name}" created`, 'ok');
                    }}>Create</Button>
                    <Button kind="ghost" onClick={() => setNaming(null)}>Cancel</Button>
                </div>
            )}
        </div>
    );
}

function BasicTab() {
    const s = useStore(st => st.effect.settings);
    const lutName = useStore(st => st.effect.lut?.name ?? null);
    const warnings = useStore(st => st.graphWarnings);
    return (
        <>
            <Section title="Base & Looks">
                <Flag k="enabled" label={`Base Tone (${kbd('Ctrl+Alt+B')})`} desc="Base optimization: brightness 1.02, contrast 1.05, saturation 1.15" />
                <Flag k="darkMoody" label={`Dark & Moody (${kbd('Ctrl+Alt+D')})`} desc="Per-channel gamma curve + saturation 0.90" />
                <Flag k="tealOrange" label={`Teal & Orange (${kbd('Ctrl+Alt+O')})`} desc="Teal and orange matrices + saturation 1.08" />
                <Flag k="vibrantSat" label={`Vibrant (${kbd('Ctrl+Alt+V')})`} desc="Saturation 1.35" />
                <Toggle checked={s.hdr !== 0} label={`HDR look (${kbd('Ctrl+Alt+P')})`} title="Toggles HDR Boost with the last used strength" onChange={() => toggleHdr()} />
            </Section>
            <Section title="Profile">
                <Select label="Tone profile" value={s.profile} options={(Object.keys(PROFILE_NAMES) as UveSettings['profile'][]).map(p => ({ value: p, label: PROFILE_NAMES[p] }))}
                    onChange={v => setSetting('profile', v, false)} title={`Movie / Anime / Gaming / EyeCare profile matrices and tones; User enables the grading controls (${kbd('Ctrl+Alt+C')} cycles)`} />
                <Select label="Color blindness" value={s.cbFilter} options={(Object.keys(CB_NAMES) as UveSettings['cbFilter'][]).map(p => ({ value: p, label: CB_NAMES[p] }))}
                    onChange={v => setSetting('cbFilter', v, false)} />
            </Section>
            <Section title="Filter chain (UVE order)">
                <ol className="chain">
                    {describeChain(s, lutName).map(c => <li key={c.id} className={c.on ? 'on' : ''}><span className="dot" />{c.label}</li>)}
                    {useStore(st => st.effect.customFilters).filter(f => f.enabled).map(f => <li key={f.id} className="on"><span className="dot" />Custom: {f.label}</li>)}
                </ol>
                <p className="hint">The order is fixed by the Ultimate Video Enhancer chain; custom filters run last. Preview and export use the same graph.</p>
                {warnings.map((w, i) => <p key={i} className="warn-text">{w}</p>)}
            </Section>
        </>
    );
}

function ColorTab() {
    const profile = useStore(s => s.effect.settings.profile);
    const user = profile === 'user';
    return (
        <>
            <Section title="Levels (base)">
                <Num k="bl" />
                <Num k="wl" />
            </Section>
            <Section title="Grading (User profile)" right={!user && <Button small onClick={() => setSetting('profile', 'user', false)}>Switch to User</Button>}>
                {!user && <p className="hint">As in UVE, the grading controls are only applied when the tone profile is <b>User</b>.</p>}
                {(['u_contrast', 'u_black', 'u_white', 'u_highlights', 'u_shadows', 'u_gamma', 'u_sat', 'u_vib', 'u_hue'] as const).map(k => <Num key={k} k={k} disabled={!user} />)}
            </Section>
            <Section title="RGB gain (User profile)" defaultOpen={false}>
                {(['u_r_gain', 'u_g_gain', 'u_b_gain'] as const).map(k => <Num key={k} k={k} disabled={!user} />)}
            </Section>
        </>
    );
}

function HdrTab() {
    return (
        <Section title="HDR Boost" collapsible={false}>
            <Num k="hdr" />
            <p className="hint">Positive values add local contrast (clarity), an S-curve tone map, a linear slope and saturation – the UVE "HDR" look.
                Negative values give a soft, low-contrast look. The output stays SDR BT.709; this does not create HDR10/HLG metadata or extra dynamic range.</p>
        </Section>
    );
}

function DetailTab() {
    const user = useStore(s => s.effect.settings.profile === 'user');
    return (
        <>
            <Section title="Sharpness & noise (base)">
                <Num k="sl" />
                <Num k="sr" />
                <Num k="dn" />
                <Num k="edge" />
            </Section>
            <Section title="Grading detail (User profile)">
                <Num k="u_sharp" disabled={!user} />
                <Num k="u_grain" disabled={!user} />
            </Section>
        </>
    );
}

function LutTab() {
    const luts = useStore(s => s.luts);
    const key = useStore(s => s.effect.settings.lutProfile);
    const lut = useStore(s => s.effect.lut);
    const [q, setQ] = useState('');
    const list = luts.profiles.filter(p => !q || `${p.group} ${p.name}`.toLowerCase().includes(q.toLowerCase()));
    const missing = key !== 'none' && !luts.profiles.some(p => lutKeyOf(p) === key);
    return (
        <Section title="LUT / Color Grading" collapsible={false} right={<Button small onClick={() => setState({ view: 'luts' })}>Library</Button>}>
            <div className="search"><Icon name="search" size={14} /><input placeholder="Search LUTs…" value={q} onChange={e => setQ(e.target.value)} aria-label="Search LUTs" /></div>
            {key !== 'none' && <div className="lut-current">Active: <b>{key.replace('||', ' / ')}</b></div>}
            <div className="lut-list" role="listbox" aria-label="LUT profiles">
                <button role="option" aria-selected={key === 'none'} className={`lut-item${key === 'none' ? ' sel' : ''}`} onClick={() => selectLut('none')}>None (no LUT)</button>
                {list.map((p, i) => {
                    const k = lutKeyOf(p);
                    const newGroup = i === 0 || list[i - 1].group !== p.group;
                    return (
                        <div key={k} className="lut-row">
                            {newGroup && <div className="lut-group">{p.group || '(no group)'}</div>}
                            <button role="option" aria-selected={k === key} className={`lut-item${k === key ? ' sel' : ''}`} onClick={() => selectLut(k)}
                                title={`${p.group ? p.group + ' / ' : ''}${p.name} – ${p.source === 'repo' ? 'from LUTsProfiles_v3.0.zip' : 'user LUT'}`}>
                                {p.name}
                            </button>
                        </div>
                    );
                })}
                {!list.length && <p className="hint">{luts.profiles.length ? 'No LUT matches the search.' : 'No LUT profiles yet – they are downloaded automatically or can be imported in the LUT Library.'}</p>}
            </div>
            {missing && <p className="warn-text">The selected LUT "{key.replace('||', ' / ')}" is not in the library{lut ? ' – the embedded copy from the project is used.' : ' and is NOT applied. Import it in the LUT Library.'}</p>}
            {luts.repo.status === 'downloading' && <p className="hint">{luts.repo.message}</p>}
            <p className="hint">UVE applies LUTs as a 4×5 color matrix (affine fit of the LUT) after HDR/color-blindness and before grading – exactly like the userscript.</p>
        </Section>
    );
}

function CustomTab() {
    const filters = useStore(s => s.effect.customFilters);
    const errors = useStore(s => s.customErrors);
    const move = (i: number, d: number) => updateEffect(e => {
        const l = e.customFilters.slice();
        const j = i + d;
        if (j < 0 || j >= l.length) return e;
        [l[i], l[j]] = [l[j], l[i]];
        return { ...e, customFilters: l };
    }, '');
    const patch = (id: string, p: Partial<(typeof filters)[0]>, key = '') => updateEffect(e => ({ ...e, customFilters: e.customFilters.map(f => f.id === id ? { ...f, ...p } : f) }), key);
    return (
        <Section title="Custom filter stack" collapsible={false} right={<Button small onClick={() => setState({ view: 'filters' })}>Filter Studio</Button>}>
            {!filters.length && <p className="hint">No custom filters. Add SVG or GLSL filters in the Filter Studio or import a gvf_filter_codes JSON.</p>}
            {filters.map((f, i) => {
                const unsupported = !isTypeSupported(f.type) || (f.type === 'webgl' && isMpvHookShader(f.code));
                const defs = f.type === 'webgl' ? parseUniformDefs(f.code).filter(d => !d.name.startsWith('u_def_')) : [];
                return (
                    <div key={f.id} className={`custom-card${f.enabled ? '' : ' off'}`}>
                        <div className="row gap">
                            <Toggle checked={f.enabled} onChange={v => patch(f.id, { enabled: v })} />
                            <b className="grow">{f.label}</b>
                            <span className="tag">{f.type.toUpperCase()}</span>
                            <button className="icon-btn" onClick={() => move(i, -1)} disabled={i === 0} aria-label="Move up"><Icon name="up" size={14} /></button>
                            <button className="icon-btn" onClick={() => move(i, 1)} disabled={i === filters.length - 1} aria-label="Move down"><Icon name="down" size={14} /></button>
                        </div>
                        {unsupported && <p className="warn-text">Not supported offline ({f.type === 'webgl' ? 'mpv hook shader' : f.type}) – skipped in preview and export.</p>}
                        {errors[f.id] && <p className="warn-text">Compile error (bypassed): {errors[f.id].split('\n')[0]}</p>}
                        {f.enabled && !unsupported && (
                            <>
                                <Select label="Blend" value={f.blendMode || 'normal'} options={BLEND_MODES.map(b => ({ value: b, label: b }))} onChange={v => patch(f.id, { blendMode: v })} />
                                {defs.map(d => d.kind === 'select'
                                    ? <Select key={d.name} label={d.label} value={Number(f.uniforms[d.name] ?? d.def)} options={(d.options || []).map(o => ({ value: o.value, label: o.label }))}
                                        onChange={v => patch(f.id, { uniforms: { ...f.uniforms, [d.name]: v } })} />
                                    : <SliderRow key={d.name} label={d.label} value={Number(f.uniforms[d.name] ?? d.def)} min={d.min ?? 0} max={d.max ?? 1} step={Math.max(((d.max ?? 1) - (d.min ?? 0)) / 200, 0.0001)} def={d.def} digits={3}
                                        onChange={v => patch(f.id, { uniforms: { ...f.uniforms, [d.name]: v } }, `u:${f.id}:${d.name}`)} />)}
                            </>
                        )}
                    </div>
                );
            })}
        </Section>
    );
}

function AutoTab() {
    const a = useStore(s => s.analysis);
    const on = useStore(s => s.effect.settings.autoOn);
    const media = useStore(s => s.media);
    return (
        <>
            <Section title={`Auto-Scene-Match (${kbd('Ctrl+Alt+A')})`} collapsible={false}>
                <Flag k="autoOn" label="Auto-Scene-Match" desc="Adapts brightness, contrast, saturation and white balance to each scene" />
                <Num k="autoStrength" disabled={!on} />
                <Flag k="autoLockWB" label={`Auto White Balance (WB lock, ${kbd('Ctrl+Alt+W')})`} desc="Removes red/blue casts via a hue correction (±10°)" />
                <div className="row gap">
                    <span className="grow small dim">
                        {!media ? 'Open a video to analyse it.' : a.status === 'running' ? `Analysing… ${Math.round(a.pct * 100)}%` : a.status === 'ready' ? `Analysis ready – ${(a.keys?.length ?? 1) - 1} correction updates` : a.status === 'error' ? `Analysis failed: ${a.error}` : 'Not analysed yet'}
                    </span>
                    <Button small disabled={!media || a.status === 'running'} onClick={() => runAnalysis()}>Analyse</Button>
                </div>
            </Section>
            <Section title="How it works" defaultOpen={false}>
                <p className="hint">The clip is analysed once (96×54, 10 fps media-time grid). The original UVE state machine – EMA statistics, cut detection, motion gating,
                    adaptive 2–10 fps scheduler and smoothing – is replayed on media time, so seeking to any timestamp and the final export see exactly the same correction.
                    Dark-scene protection is part of the algorithm: relative saturation keeps dark scenes from being over-saturated and contrast is never reduced below 1.0.
                    UVE 1.17.5 has no separate "Dark Scene Lift" control.</p>
            </Section>
        </>
    );
}

function ExpertTab() {
    const expert = useStore(s => s.effect.expert);
    const [q, setQ] = useState('');
    const setField = (name: ExpertBlockName, k: string, v: number | boolean) =>
        updateEffect(e => ({ ...e, expert: { ...e.expert, [name]: { ...e.expert[name], [k]: v } } }), `x:${name}:${k}`);
    const resetBlock = (name: ExpertBlockName) => updateEffect(e => ({ ...e, expert: { ...e.expert, [name]: JSON.parse(JSON.stringify(EXPERT_DEFAULTS[name])) } }), '');
    const blocks = EXPERT_UI.map(b => ({ ...b, fields: b.fields.filter(f => !q || `${b.title} ${f.label} ${f.k}`.toLowerCase().includes(q.toLowerCase())) })).filter(b => b.fields.length);
    return (
        <>
            <div className="row gap wrap">
                <div className="search grow"><Icon name="search" size={14} /><input placeholder="Search expert settings…" value={q} onChange={e => setQ(e.target.value)} aria-label="Search expert settings" /></div>
                <Button small icon="reset" onClick={() => updateEffect(e => ({ ...e, expert: expertDefaults() }), '')}>Reset all</Button>
            </div>
            <div className="row gap wrap">
                <Button small icon="download" onClick={() => api.saveFile('Export expert settings', 'gvf_expert_settings.json', [{ name: 'JSON', extensions: ['json'] }], JSON.stringify(exportExpert(getState().effect.expert), null, 2))}>Export</Button>
                <Button small icon="upload" onClick={async () => {
                    const f = await api.openFiles('Import expert settings', [{ name: 'JSON', extensions: ['json'] }]);
                    if (!f[0]) return;
                    try {
                        const r = importExpert(JSON.parse(new TextDecoder().decode(f[0].data)));
                        if (!r) throw new Error('not an expert settings export');
                        updateEffect(e => ({ ...e, expert: r }), '');
                        toast('Expert settings imported', 'ok');
                    } catch (err) { toast(`Import failed: ${err instanceof Error ? err.message : err}`, 'error'); }
                }}>Import</Button>
                <Button small icon="copy" onClick={async () => { await navigator.clipboard.writeText(JSON.stringify(exportExpert(getState().effect.expert), null, 2)); toast('Copied to clipboard', 'ok', 1500); }}>Copy</Button>
                <Button small onClick={async () => {
                    try {
                        const r = importExpert(JSON.parse(await navigator.clipboard.readText()));
                        if (!r) throw new Error('clipboard does not contain expert settings');
                        updateEffect(e => ({ ...e, expert: r }), '');
                        toast('Expert settings pasted', 'ok');
                    } catch (err) { toast(`Paste failed: ${err instanceof Error ? err.message : err}`, 'error'); }
                }}>Paste</Button>
            </div>
            {blocks.map(b => (
                <Section key={b.name} title={b.title} defaultOpen={!!q} right={<button className="icon-btn" title="Reset group" aria-label={`Reset ${b.title}`} onClick={() => resetBlock(b.name)}><Icon name="reset" size={13} /></button>}>
                    <p className="hint">{b.desc}</p>
                    {b.fields.map(f => {
                        const cur = (expert[b.name] as unknown as Record<string, number | boolean>)[f.k];
                        const def = (EXPERT_DEFAULTS[b.name] as unknown as Record<string, number | boolean>)[f.k];
                        return f.type === 'bool'
                            ? <div key={f.k} className="row gap" title={`${f.desc}\nKey: ${b.name}.${f.k} · default ${def}`}>
                                <Toggle checked={!!cur} label={f.label} onChange={v => setField(b.name, f.k, v)} />
                                <span className="grow" />
                                <button className="icon-btn" disabled={cur === def} onClick={() => setField(b.name, f.k, def)} aria-label={`Reset ${f.label}`}><Icon name="reset" size={13} /></button>
                            </div>
                            : <SliderRow key={f.k} label={f.label} value={Number(cur)} min={f.min!} max={f.max!} step={f.step!} def={Number(def)}
                                desc={`${f.desc}\nKey: ${b.name}.${f.k} · range ${f.min}…${f.max} · step ${f.step} · default ${def}`} onChange={v => setField(b.name, f.k, v)} />;
                    })}
                </Section>
            ))}
        </>
    );
}

function SearchResults({ q }: { q: string }) {
    const keys = useMemo(() => Object.keys(NUM_SPECS).filter(k => `${NUM_SPECS[k].label} ${NUM_SPECS[k].desc} ${NUM_SPECS[k].sourceKey}`.toLowerCase().includes(q.toLowerCase())), [q]);
    return (
        <Section title={`Search: "${q}"`} collapsible={false}>
            {keys.length ? keys.map(k => <Num key={k} k={k as keyof UveSettings} />) : <p className="hint">No matching control. Expert settings have their own search in the Expert tab.</p>}
        </Section>
    );
}

export function Inspector() {
    const tab = useStore(s => s.tab);
    const [q, setQ] = useState('');
    return (
        <aside className="inspector" aria-label="Enhancement settings">
            <header className="inspector-head">
                <h2>Enhancement Settings</h2>
                <button className="link-btn" title="Reset all effect settings to the UVE defaults" onClick={() => updateEffect(e => ({ ...e, settings: { ...DEFAULT_SETTINGS }, lut: null }), '')}><Icon name="reset" size={14} /> Reset</button>
            </header>
            <PresetBar />
            <div className="search"><Icon name="search" size={14} /><input placeholder="Find a control…" value={q} onChange={e => setQ(e.target.value)} aria-label="Find a control" /></div>
            <nav className="tabs" role="tablist">
                {TABS.map(t => <button key={t} role="tab" aria-selected={tab === t} className={tab === t ? 'active' : ''} onClick={() => { setQ(''); setState({ tab: t }); }}>{t}</button>)}
            </nav>
            <div className="inspector-body">
                {q ? <SearchResults q={q} /> : (
                    <>
                        {tab === 'Basic' && <BasicTab />}
                        {tab === 'Color' && <ColorTab />}
                        {tab === 'HDR' && <HdrTab />}
                        {tab === 'Detail' && <DetailTab />}
                        {tab === 'LUTs' && <LutTab />}
                        {tab === 'Custom' && <CustomTab />}
                        {tab === 'Auto' && <AutoTab />}
                        {tab === 'Expert' && <ExpertTab />}
                    </>
                )}
            </div>
        </aside>
    );
}
