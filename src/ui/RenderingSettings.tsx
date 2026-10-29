import { HW_FAMILY_LABEL, type HwFamily } from '../../shared/hw';
import { api } from '../api';
import { setState, useStore } from '../state/store';
import { Section, Select } from './controls';

/** v1.0.1: GPU filter engine, hardware decode preference and the probed capabilities. */
export function RenderingSettings() {
    const prefs = useStore(s => s.prefs);
    const caps = useStore(s => s.caps);
    const info = useStore(s => s.engineInfo);
    if (!prefs) return null;
    const setP = async (p: Parameters<typeof api.setPrefs>[0]) => setState({ prefs: await api.setPrefs(p) });
    const hw = Object.entries(caps?.hwEncoders || {});
    const fam = (name: string) => HW_FAMILY_LABEL[name.split('_')[1] as HwFamily] || name;
    const v = info?.validation;
    return (
        <Section title="Rendering & hardware acceleration" collapsible={false}>
            <Select label="GPU filter engine" value={prefs.gpuEngine ?? 'auto'} onChange={x => setP({ gpuEngine: x })}
                title="Automatic uses WebGPU (D3D12 / Vulkan / Metal) after validating it against the v1.0.0 engine, otherwise WebGL2."
                options={[{ value: 'auto', label: 'Automatic (WebGPU, validated)' }, { value: 'webgpu', label: 'WebGPU' }, { value: 'webgl2', label: 'WebGL2 (v1.0.0 engine)' }]} />
            <Select label="Hardware decoding" value={prefs.hwDecode ?? 'auto'} onChange={x => setP({ hwDecode: x })}
                title="Hardware decoders are verified on each input file before use; software decoding is always the fallback."
                options={[{ value: 'auto', label: 'Automatic' }, { value: 'hardware', label: 'Prefer Hardware' }, { value: 'software', label: 'Software' }]} />
            <p className="hint">The hardware encoder is chosen per export in Output Settings (Automatic / Prefer Hardware / Software). Every hardware path is verified with a scratch encode or decode before it is used; nothing changes the codec, resolution or frame rate.</p>
            <dl className="kv">
                <div><dt>Preview engine</dt><dd>{info ? `${info.kind} · ${info.adapter}` : 'starting…'}</dd></div>
                <div><dt>Selection</dt><dd>{info?.reason || '—'}</dd></div>
                <div><dt>WebGPU validation</dt><dd>{v ? (v.ok ? `passed – ${v.prims} primitives vs. v1.0.0 engine, mean diff ${v.meanDiff.toFixed(3)}/255, ${(((v as { outlierRate?: number }).outlierRate ?? 0) * 100).toFixed(3)} % of values > 3/255 (max ${v.maxDiff}) – ${v.ms.toFixed(0)} ms` : `not passed – ${v.error || `max diff ${v.maxDiff}/255`}`) : info?.kind === 'webgl2' ? 'not run (WebGL2 selected)' : '—'}</dd></div>
                <div><dt>Platform / GPU</dt><dd>{caps ? `${caps.platform} · ${caps.gpuName} (${caps.gpuVendor})` : '…'}</dd></div>
                <div><dt>Hardware encoders</dt><dd>{caps ? (hw.length ? hw.map(([k, ok]) => `${k} (${fam(k)}): ${ok ? 'verified' : 'failed'}`).join(' · ') : 'none in this FFmpeg build') : '…'}</dd></div>
                <div><dt>Hardware decoders</dt><dd>{caps ? (caps.hwaccels.join(', ') || 'none') + ' – verified per file at export' : '…'}</dd></div>
            </dl>
        </Section>
    );
}
