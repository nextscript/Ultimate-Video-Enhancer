import { useEffect, useMemo, useRef, useState } from 'react';
import {
    BLEND_MODES, type CustomFilterEntry, type CustomFilterType, isMpvHookShader, isTypeSupported, newFilterId, parseCustomSvgPrimitives, parseUniformDefs
} from '../../shared/uve/customFilters';
import { EffectEngine } from '../engine/EffectEngine';
import { api } from '../api';
import { exportFilterCodes, importFilterCodes } from '../state/actions';
import { getState, setState, toast, updateEffect, useStore } from '../state/store';
import { Button, Icon, Section, Select, Toggle } from './controls';
import { IS_MAC, kbd } from './keys';

const TEMPLATES: Record<'svg' | 'webgl', string> = {
    webgl: `// @uniform float STRENGTH 0.5 0.0 1.0 "Strength"
// @select float MODE 0 "Mode" 0:Warm,1:Cool
// u_video = previous image, u_video_raw = original frame, v_uv = 0..1, u_res = size, u_time = media time
void main() {
    vec4 c = texture(u_video, v_uv);
    vec3 tint = MODE < 0.5 ? vec3(1.06, 1.0, 0.92) : vec3(0.92, 1.0, 1.08);
    fragColor = vec4(mix(c.rgb, c.rgb * tint, STRENGTH), c.a);
}
`,
    svg: `<feColorMatrix type="saturate" values="1.2"/>
<feComponentTransfer>
  <feFuncR type="gamma" amplitude="1" exponent="0.95" offset="0"/>
  <feFuncG type="gamma" amplitude="1" exponent="0.95" offset="0"/>
  <feFuncB type="gamma" amplitude="1" exponent="1.0" offset="0"/>
</feComponentTransfer>
`
};

let validator: EffectEngine | null = null;
function validateGlsl(code: string): string | null {
    if (!validator) {
        const c = document.createElement('canvas');
        const gl = c.getContext('webgl2');
        if (!gl) return 'WebGL2 not available';
        validator = new EffectEngine(gl);
    }
    return validator.validateGlsl(code);
}

export function FilterStudio() {
    const filters = useStore(s => s.effect.customFilters);
    const library = useStore(s => s.filterLibrary);
    const [selId, setSelId] = useState<string | null>(filters[0]?.id ?? null);
    const sel = filters.find(f => f.id === selId) || null;
    const [draft, setDraft] = useState(sel?.code ?? '');
    const [diag, setDiag] = useState<string | null>(null);
    const lineRef = useRef<HTMLDivElement>(null);

    useEffect(() => { setDraft(sel?.code ?? ''); setDiag(null); }, [selId]); // eslint-disable-line react-hooks/exhaustive-deps

    const check = useMemo(() => (code: string, type: CustomFilterType): string | null => {
        if (type === 'webgl') return validateGlsl(code);
        if (type === 'svg') {
            const r = parseCustomSvgPrimitives(code, 'SourceGraphic', 'chk_');
            if (!r.prims.length) return 'No supported SVG filter primitive found.';
            return r.errors.length ? r.errors.join('\n') : null;
        }
        return `${type} filters run JavaScript in the browser and are not supported by the UVE Manager renderer (no code execution in the desktop app).`;
    }, []);

    const patch = (id: string, p: Partial<CustomFilterEntry>, key = '') => updateEffect(e => ({ ...e, customFilters: e.customFilters.map(f => f.id === id ? { ...f, ...p } : f) }), key);
    const add = (type: 'svg' | 'webgl') => {
        const e: CustomFilterEntry = { id: newFilterId(), label: type === 'webgl' ? 'New GLSL filter' : 'New SVG filter', code: TEMPLATES[type], type, enabled: true, blendMode: 'normal', tags: [], category: '', uniforms: {} };
        updateEffect(s => ({ ...s, customFilters: [...s.customFilters, e] }), '');
        setSelId(e.id);
    };
    const apply = () => {
        if (!sel) return;
        const err = check(draft, sel.type);
        setDiag(err);
        if (err && sel.type === 'webgl') { toast('Shader has errors – it stays bypassed until it compiles.', 'warn'); }
        patch(sel.id, { code: draft });
        if (!err) toast('Filter updated', 'ok', 1500);
    };
    const lines = draft.split('\n').length;
    const defs = sel?.type === 'webgl' ? parseUniformDefs(sel.code).filter(d => !d.name.startsWith('u_def_')) : [];

    return (
        <div className="view studio">
            <h1>Custom Filter Studio</h1>
            <div className="lib-layout">
                <nav className="groups filter-list" aria-label="Custom filters">
                    <div className="row gap wrap">
                        <Button small icon="plus" onClick={() => add('webgl')}>GLSL</Button>
                        <Button small icon="plus" onClick={() => add('svg')}>SVG</Button>
                        <Button small icon="upload" onClick={() => importFilterCodes()}>Import</Button>
                        <Button small icon="download" disabled={!filters.length} onClick={() => exportFilterCodes(filters)}>Export</Button>
                    </div>
                    {filters.map((f, i) => (
                        <div key={f.id} className={`filter-item${f.id === selId ? ' sel' : ''}${f.enabled ? '' : ' off'}`} onClick={() => setSelId(f.id)}>
                            <Toggle checked={f.enabled} onChange={v => patch(f.id, { enabled: v })} />
                            <span className="grow">{f.label}</span>
                            <span className={`tag${isTypeSupported(f.type) && !(f.type === 'webgl' && isMpvHookShader(f.code)) ? '' : ' bad'}`}>{f.type}</span>
                            <button className="icon-btn" aria-label="Move up" disabled={i === 0} onClick={e => { e.stopPropagation(); updateEffect(s => { const l = s.customFilters.slice(); [l[i - 1], l[i]] = [l[i], l[i - 1]]; return { ...s, customFilters: l }; }, ''); }}><Icon name="up" size={13} /></button>
                            <button className="icon-btn" aria-label="Move down" disabled={i === filters.length - 1} onClick={e => { e.stopPropagation(); updateEffect(s => { const l = s.customFilters.slice(); [l[i + 1], l[i]] = [l[i], l[i + 1]]; return { ...s, customFilters: l }; }, ''); }}><Icon name="down" size={13} /></button>
                        </div>
                    ))}
                    {!filters.length && <p className="hint">No filters in this project. Add one or import the repository's gvf_filter_codes_example.json.</p>}
                    <Section title="Saved library" defaultOpen={false}>
                        <div className="row gap wrap">
                            <Button small disabled={!filters.length} onClick={async () => {
                                const map = new Map(getState().filterLibrary.map(f => [f.label, f]));
                                filters.forEach(f => map.set(f.label, f));
                                const lib = [...map.values()];
                                setState({ filterLibrary: lib });
                                await api.saveFilters(lib);
                                toast('Filters saved to the library', 'ok');
                            }}>Save stack to library</Button>
                        </div>
                        {library.map(f => (
                            <div key={f.id} className="filter-item">
                                <span className="grow">{f.label}</span><span className="tag">{f.type}</span>
                                <Button small onClick={() => updateEffect(s => ({ ...s, customFilters: [...s.customFilters, { ...f, id: newFilterId() }] }), '')}>Add</Button>
                                <Button small icon="trash" kind="danger" onClick={async () => { const lib = getState().filterLibrary.filter(x => x.id !== f.id); setState({ filterLibrary: lib }); await api.saveFilters(lib); }} />
                            </div>
                        ))}
                    </Section>
                </nav>
                <div className="lib-main">
                    {!sel ? <p className="hint">Select or add a filter.</p> : (
                        <>
                            <div className="row gap wrap">
                                <input className="text grow" value={sel.label} onChange={e => patch(sel.id, { label: e.target.value }, `lbl:${sel.id}`)} aria-label="Filter name" />
                                <Select label="Blend" value={sel.blendMode || 'normal'} options={BLEND_MODES.map(b => ({ value: b, label: b }))} onChange={v => patch(sel.id, { blendMode: v })} />
                                <Button icon="copy" onClick={() => { const c = { ...sel, id: newFilterId(), label: sel.label + ' copy' }; updateEffect(s => ({ ...s, customFilters: [...s.customFilters, c] }), ''); setSelId(c.id); }}>Duplicate</Button>
                                <Button icon="trash" kind="danger" onClick={() => { updateEffect(s => ({ ...s, customFilters: s.customFilters.filter(f => f.id !== sel.id) }), ''); setSelId(null); }}>Delete</Button>
                            </div>
                            {!isTypeSupported(sel.type) && <p className="warn-text">Type "{sel.type}" executes JavaScript inside the browser page. UVE Manager does not run arbitrary scripts; this entry is kept for round-trip export but skipped when rendering.</p>}
                            {sel.description && <p className="hint">{sel.description}</p>}
                            <div className="code-editor">
                                <div className="gutter" ref={lineRef} aria-hidden="true">{Array.from({ length: lines }, (_, i) => <div key={i}>{i + 1}</div>)}</div>
                                <textarea spellCheck={false} value={draft} onChange={e => setDraft(e.target.value)} aria-label="Filter code"
                                    onScroll={e => { if (lineRef.current) lineRef.current.scrollTop = (e.target as HTMLTextAreaElement).scrollTop; }}
                                    onKeyDown={e => {
                                        if (e.key === 'Tab') { e.preventDefault(); const t = e.currentTarget; const p = t.selectionStart; setDraft(draft.slice(0, p) + '    ' + draft.slice(t.selectionEnd)); requestAnimationFrame(() => { t.selectionStart = t.selectionEnd = p + 4; }); }
                                        if (e.key.toLowerCase() === 's' && (IS_MAC ? e.metaKey : e.ctrlKey)) { e.preventDefault(); apply(); }
                                    }} />
                            </div>
                            <div className="row gap">
                                <Button kind="primary" icon="check" onClick={apply}>Apply ({kbd(IS_MAC ? 'Meta+S' : 'Ctrl+S')})</Button>
                                <Button onClick={() => setDiag(check(draft, sel.type) || 'OK – no errors.')}>Validate</Button>
                                <Button kind="ghost" onClick={() => setDraft(sel.code)} disabled={draft === sel.code}>Revert</Button>
                                <span className="grow" />
                                <Button small onClick={() => api.openExternal('https://svg.ts3x.cc/')}>Open svg.ts3x.cc (online)</Button>
                            </div>
                            {diag && <pre className={`diag${diag.startsWith('OK') ? ' ok' : ''}`}>{diag}</pre>}
                            {defs.length > 0 && (
                                <Section title="Controls (@uniform / @select)" collapsible={false}>
                                    <p className="hint">Tune the values in the Inspector → Custom tab while watching the preview.</p>
                                    <ul className="small">{defs.map(d => <li key={d.name}><code>{d.name}</code> – {d.label} ({d.kind === 'select' ? d.options?.map(o => o.label).join(' / ') : `${d.min}…${d.max}, default ${d.def}`})</li>)}</ul>
                                </Section>
                            )}
                            <Section title="GLSL conventions" defaultOpen={false}>
                                <p className="hint">Same conventions as the userscript: <code>u_video</code> (previous image), <code>u_video_raw</code>, <code>u_res</code>, <code>u_time</code> (media time),
                                    <code>u_avg_lum/u_avg_r/g/b/u_contrast</code> (frame statistics), <code>v_uv</code>, output <code>fragColor</code>. Shadertoy <code>mainImage</code>, <code>texture2D</code>,
                                    <code>gl_FragColor</code>, <code>iChannel0</code> and <code>iTime</code> are translated automatically. Loops are limited to 4096 iterations.</p>
                            </Section>
                        </>
                    )}
                </div>
            </div>
        </div>
    );
}
