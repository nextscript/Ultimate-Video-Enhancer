// v1.0.7: Settings → AI Upscaling → Video2X → Shaders. Custom mpv/libplacebo GLSL presets as ordered chains.
import { useState } from 'react';
import type { Video2xPresetInfo } from '../../shared/video2x-types';
import { getState, setState, toast, useStore } from '../state/store';
import { Button, Select, fmtBytes } from './controls';
import { errText, refreshVideo2x, video2xApi } from './Video2xSettings';

const STATE_TEXT = { ok: 'ok', missing: 'MISSING', modified: 'MODIFIED', invalid: 'INVALID' } as const;

export function Video2xShaderLibrary() {
    const st = useStore(s => s.video2x);
    const [mode, setMode] = useState<'copy' | 'reference'>('copy');
    const [sel, setSel] = useState<string>('');
    const [busy, setBusy] = useState(false);
    // Electron has no window.prompt(): names are typed into an inline field
    const [naming, setNaming] = useState<{ op: 'create' | 'rename'; value: string } | null>(null);
    const custom = (st?.presets || []).filter(p => p.kind === 'custom');
    const builtin = (st?.presets || []).filter(p => p.kind === 'built-in');
    const cur: Video2xPresetInfo | undefined = custom.find(p => p.id === sel) || custom[0];

    const act = async (fn: () => Promise<unknown>, after?: (r: unknown) => void) => {
        setBusy(true);
        try { const r = await fn(); after?.(r); await refreshVideo2x(false); }
        catch (e) { toast(errText(e), 'error', 12000); }
        finally { setBusy(false); }
    };
    const warnExternal = () => confirm('Shader code runs on your GPU. Only import shader packs from sources you trust.\n\nThe files are checked for mpv/libplacebo directives now; whether they compile is shown by "Test with Video2X".');
    const edit = (e: Record<string, unknown>) => act(() => video2xApi.editPreset(e));
    const test = (id: string) => act(() => video2xApi.testPreset(`custom:${id}`), r => {
        const t = r as { ok: boolean; message: string };
        toast(t.ok ? `Shader preset works with Video2X: ${t.message}` : `Shader test failed: ${t.message}`, t.ok ? 'ok' : 'error', 14000);
    });
    const useInExport = (id: string) => {
        const ex = getState().exportSettings;
        setState({ exportSettings: { ...ex, upscaleProvider: 'video2x', video2x: { scale: ex.video2x?.scale ?? 2, device: ex.video2x?.device ?? -1, presetId: id } }, dirty: true });
        toast('Preset selected in Output Settings (AI Upscale → Video2X)', 'ok');
    };

    return (
        <>
            <h4 className="subhead">Shaders</h4>
            <p className="hint">
                Built-in presets ({builtin.length ? builtin.map(p => p.displayName).join(' · ') : 'shown after Video2X has been detected'}) come from the installed Video2X build.
                Custom presets are ordered chains of mpv-compatible <code>.glsl</code> user shaders; the enabled passes are combined in this order and
                executed by libplacebo inside Video2X. The live editor preview does not show them – use “Render 5-second preview” in Output Settings.
            </p>
            <div className="row gap wrap">
                <Select<'copy' | 'reference'> label="Import as" value={mode} onChange={setMode} options={[
                    { value: 'copy', label: 'Copy into the UVE shader library' }, { value: 'reference', label: 'Reference the original file (read-only)' }]} />
                <Button small icon="upload" disabled={busy} onClick={() => { if (warnExternal()) void act(() => video2xApi.importShaders({ mode }), id => { if (id) setSel(String(id)); }); }}>Import .glsl</Button>
                <Button small icon="upload" disabled={busy} onClick={() => { if (warnExternal()) void act(() => video2xApi.importFolder({ mode }), id => { if (id) setSel(String(id)); }); }}>Import folder</Button>
                <Button small disabled={busy} onClick={() => setNaming({ op: 'create', value: 'My shader chain' })}>Create preset</Button>
                <Button small disabled={busy} onClick={() => video2xApi.openShaderFolder()}>Open shader folder</Button>
                <Button small disabled={busy} onClick={() => { const ex = getState().exportSettings; setState({ exportSettings: { ...ex, video2x: { scale: ex.video2x?.scale ?? 2, device: ex.video2x?.device ?? -1, presetId: 'builtin:anime4k-v4-a' } }, dirty: true }); toast('Output Settings use the built-in Fast – Anime4K v4 preset again', 'ok'); }}>Reset to built-in preset</Button>
            </div>
            {naming && (
                <div className="row gap wrap">
                    <label className="select-row grow"><span className="select-label">{naming.op === 'create' ? 'New preset name' : 'Rename preset'}</span>
                        <input className="text" autoFocus value={naming.value} onChange={e => setNaming({ ...naming, value: e.target.value })}
                            onKeyDown={e => { if (e.key === 'Escape') setNaming(null); if (e.key === 'Enter') (e.currentTarget.parentElement?.nextElementSibling as HTMLButtonElement | null)?.click(); }} />
                    </label>
                    <Button small kind="primary" disabled={busy || !naming.value.trim()} onClick={() => {
                        const n = naming.value.trim(); setNaming(null);
                        if (naming.op === 'create') void act(() => video2xApi.editPreset({ op: 'create', name: n }), id => setSel(String(id)));
                        else if (cur) void edit({ op: 'rename', id: cur.id, name: n });
                    }}>OK</Button>
                    <Button small onClick={() => setNaming(null)}>Cancel</Button>
                </div>
            )}
            {custom.length > 0 && (
                <div className="row gap wrap">
                    <Select label="Custom preset" value={cur?.id || ''} onChange={setSel} options={custom.map(p => ({ value: p.id, label: `${p.displayName}${p.available ? '' : ' – not usable'}` }))} />
                    {cur && <>
                        <Button small disabled={busy} onClick={() => setNaming({ op: 'rename', value: cur.displayName })}>Rename</Button>
                        <Button small disabled={busy} onClick={() => act(() => video2xApi.editPreset({ op: 'duplicate', id: cur.id }), id => setSel(String(id)))}>Duplicate</Button>
                        <Button small icon="upload" disabled={busy} onClick={() => { if (warnExternal()) void act(() => video2xApi.importShaders({ presetId: cur.id, mode })); }}>Add .glsl</Button>
                        <Button small kind="primary" disabled={busy || !cur.available || st?.state === 'unsupported' || !st?.exe} onClick={() => test(cur.id)}>Test with Video2X</Button>
                        <Button small disabled={!cur.available} onClick={() => useInExport(`custom:${cur.id}`)}>Use for export</Button>
                        <Button small kind="danger" icon="trash" disabled={busy} onClick={() => { if (confirm(`Remove preset "${cur.displayName}"? Copied shader files only it uses are deleted; referenced original files are never touched.`)) void edit({ op: 'remove', id: cur.id }); }}>Remove</Button>
                    </>}
                </div>
            )}
            {cur && (
                <>
                    {cur.problem && <p className="warn-text">{cur.problem}</p>}
                    <p className="hint">
                        {cur.passes} hook pass(es) · last test: {cur.tested ? `${cur.tested.ok ? 'passed' : 'FAILED'} ${new Date(cur.tested.at).toLocaleString()} – ${cur.tested.message}` : 'not tested yet (runs automatically before the first render)'}
                    </p>
                    <table className="table compact">
                        <tbody>
                            {cur.shaderFiles.map((f, i) => (
                                <tr key={`${f.sha256}-${i}`} className={cur.files[i]?.state !== 'ok' ? 'conflict' : ''}>
                                    <td className="small">{i + 1}.</td>
                                    <td><b>{f.name}</b><div className="dim small">{f.mode === 'copy' ? 'copy' : 'reference'} · {fmtBytes(f.size)} · sha256 {f.sha256.slice(0, 12)}… · {f.mode === 'reference' ? f.path : f.origin || ''}</div>
                                        {cur.files[i]?.message && <div className={cur.files[i].state === 'ok' ? 'dim small' : 'warn-text'}>{STATE_TEXT[cur.files[i].state]}: {cur.files[i].message}</div>}
                                    </td>
                                    <td className="row gap">
                                        <Button small disabled={busy || i === 0} title="Move up" onClick={() => edit({ op: 'move', id: cur.id, index: i, dir: -1 })}>▲</Button>
                                        <Button small disabled={busy || i === cur.shaderFiles.length - 1} title="Move down" onClick={() => edit({ op: 'move', id: cur.id, index: i, dir: 1 })}>▼</Button>
                                        <Button small disabled={busy} onClick={() => edit({ op: 'toggle', id: cur.id, index: i })}>{f.enabled ? 'Disable' : 'Enable'}</Button>
                                        {cur.files[i]?.state === 'modified' && <Button small disabled={busy} title="Accept the changed file (re-checks it and stores the new hash)" onClick={() => edit({ op: 'acceptChange', id: cur.id, index: i })}>Accept change</Button>}
                                        <Button small kind="danger" icon="trash" disabled={busy} title="Remove from preset" onClick={() => edit({ op: 'removeFile', id: cur.id, index: i })} />
                                    </td>
                                </tr>
                            ))}
                            {!cur.shaderFiles.length && <tr><td className="hint">No shader files yet – use “Add .glsl”.</td></tr>}
                        </tbody>
                    </table>
                    <div className="row gap wrap">
                        <label className="select-row"><span className="select-label">Source</span><input className="text" defaultValue={cur.source || ''} key={`s${cur.id}`} onBlur={e => e.target.value !== (cur.source || '') && edit({ op: 'meta', id: cur.id, source: e.target.value })} /></label>
                        <label className="select-row"><span className="select-label">Author</span><input className="text" defaultValue={cur.author || ''} key={`a${cur.id}`} onBlur={e => e.target.value !== (cur.author || '') && edit({ op: 'meta', id: cur.id, author: e.target.value })} /></label>
                        <label className="select-row"><span className="select-label">License</span><input className="text" defaultValue={cur.license || ''} key={`l${cur.id}`} placeholder="e.g. MIT" onBlur={e => e.target.value !== (cur.license || '') && edit({ op: 'meta', id: cur.id, license: e.target.value })} /></label>
                    </div>
                    <p className="hint">Keep the original license text of a shader pack next to its files when you share them.</p>
                </>
            )}
        </>
    );
}
