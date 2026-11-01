// v1.0.7: Output Settings → AI Upscale → Video2X (preset, scale, GPU, status, disk estimate, exact-pipeline preview).
import { useEffect, useState } from 'react';
import { timelineDuration } from '../../shared/timeline';
import { VIDEO2X_SCALES, video2xOptions } from '../../shared/upscale-provider';
import { estimateTempBytes } from '../../shared/video2x-cli';
import type { ExportSettings } from '../../shared/types';
import { api } from '../api';
import { enqueueTimelineJob } from '../state/actions';
import { getState, setState, toast, useStore } from '../state/store';
import { Button, Select, fmtBytes } from './controls';
import { refreshVideo2x, video2xApi } from './Video2xSettings';

const even = (n: number) => Math.max(2, Math.round(n / 2) * 2);

/** Final output size of a Video2X export (the Resolution choice wins over the scale factor). */
export function video2xTargetSize(ex: ExportSettings, w: number, h: number): { w: number; h: number } {
    const o = video2xOptions(ex);
    if (ex.resolution === 'source' || ex.resolution === 'x2') return { w: even(w * o.scale), h: even(h * o.scale) };
    const th = Number(ex.resolution);
    return { w: even(w * th / h), h: even(th) };
}

export function Video2xExportSection({ set }: { set: (p: Partial<ExportSettings>) => void }) {
    const st = useStore(s => s.video2x);
    const ex = useStore(s => s.exportSettings);
    const tl = useStore(s => s.timeline);
    const time = useStore(s => s.time);
    const [previewBusy, setPreviewBusy] = useState(false);
    useEffect(() => { if (!st) void refreshVideo2x(false); }, [st]);
    const o = video2xOptions(ex);
    const setO = (p: Partial<typeof o>) => set({ video2x: { ...o, ...p } });
    const presets = st?.presets || [];
    const current = presets.find(p => p.id === o.presetId);
    const usable = st?.state === 'installed' || st?.state === 'unverified';
    // picture size of the timeline (first source with video, the size the UVE render uses)
    const v = tl.sources.find(x => x.media.video)?.media.video;
    const pw = v ? (v.rotation % 180 ? v.height : v.width) : 0, ph = v ? (v.rotation % 180 ? v.width : v.height) : 0;
    const out = v ? video2xTargetSize(ex, pw, ph) : null;
    const dur = timelineDuration(tl);
    const frames = Math.round(dur * (tl.frameRate || 25));
    const temp = out ? estimateTempBytes(frames, pw, ph, out.w, out.h) : 0;

    const preview = async () => {
        if (!tl.clips.length) { toast('The timeline is empty.', 'warn'); return; }
        setPreviewBusy(true);
        try {
            const s = getState();
            const start = Math.max(0, Math.min(s.time, Math.max(0, dur - 5)));
            const range = { start, end: Math.min(dur, start + 5) };
            const exp = { ...s.exportSettings, outputDir: '', fileName: '' };
            const key = { tl: s.timeline, effect: s.effect, ex: exp, range, v: 1 };
            const r = await video2xApi.previewPath({ key, presetId: o.presetId, ext: exp.videoCodec === 'prores' ? 'mov' : exp.container });
            if (r.queued) { toast('This preview is already in the render queue.', 'info', 5000); setState({ view: 'queue' }); return; }
            if (r.exists) { toast('This preview was rendered before (same timeline range, effects, shaders and output settings) – showing the cached file.', 'ok', 7000); void api.showItem(r.path); return; }
            const outFile = await enqueueTimelineJob({ range, output: r.path, name: `Preview 5 s @ ${start.toFixed(1)} s – Video2X` });
            if (outFile) setState({ view: 'queue' });
        } catch (e) { toast(`Preview failed: ${e instanceof Error ? e.message : e}`, 'error', 9000); }
        finally { setPreviewBusy(false); }
    };

    return (
        <div className="v2x-export">
            <div className="export-grid">
                <label className="select-row"><span className="select-label">Engine</span><span className="dim small">Video2X / libplacebo{st?.version ? ` ${st.version}` : ''}</span></label>
                <Select label="Preset" value={o.presetId} disabled={!presets.length} onChange={id => setO({ presetId: id })}
                    title="Anime4K v4 shader preset (built-in presets come from the installed Video2X build) or a custom GLSL chain"
                    options={presets.length ? presets.map(p => ({ value: p.id, label: `${p.displayName}${p.kind === 'custom' ? ` (${p.passes} pass${p.passes === 1 ? '' : 'es'})` : ''}${p.available ? '' : ' – not usable'}` }))
                        : [{ value: o.presetId, label: o.presetId.startsWith('builtin:') ? `${o.presetId.slice(8)} (Video2X not detected)` : 'Custom preset (Video2X not detected)' }]} />
                <Select<number> label="Scale" value={o.scale} onChange={v2 => setO({ scale: v2 })} disabled={ex.resolution !== 'source'}
                    title={ex.resolution !== 'source' ? 'The Resolution choice sets the output size' : 'Upscale factor of the UVE render'}
                    options={VIDEO2X_SCALES.map(s2 => ({ value: s2, label: `${s2}×` }))} />
                <Select<number> label="Processing" value={o.device} onChange={d => setO({ device: d })}
                    title="Vulkan GPU used by Video2X (Automatic = the first device Video2X lists)"
                    options={[{ value: -1, label: 'GPU: Auto' }, ...(st?.vulkan.devices || []).map(d => ({ value: d.index, label: `GPU ${d.index}: ${d.name}` }))]} />
            </div>
            <p className="hint">
                Shader chain: {current ? (current.kind === 'built-in' ? `1 built-in shader (${current.builtinShader})` : `custom chain, ${current.passes} pass(es)`)
                    : st?.exe ? 'preset not available in this Video2X build' : o.presetId.startsWith('builtin:') ? `built-in ${o.presetId.slice(8)} (checked once Video2X is detected)` : 'custom chain (checked once Video2X is detected)'}
                {out && ` · ${pw} × ${ph} → ${out.w} × ${out.h}`}
                {out && frames > 0 && ` · about ${fmtBytes(temp)} temporary disk space`}
                {' · Output FPS: '}{ex.fps ? ex.fps : 'timeline frame rate'}
            </p>
            {current && !current.available && <p className="warn-text">{current.problem}</p>}
            <p className={st?.state === 'installed' ? 'hint' : 'warn-text'}>
                Status: {st ? (st.state === 'unsupported' ? st.platformReason : st.state === 'not-installed' ? 'Video2X is not installed – install it in Settings → AI Upscaling → Video2X.' : `${st.summary}${st.diagnostic ? ` – ${st.diagnostic}` : ''}`) : 'checking…'}
            </p>
            <div className="row gap wrap">
                <Button small onClick={() => setState({ view: 'settings' })}>{usable ? 'Configure / Manage shaders' : 'Install / Verify / Configure'}</Button>
                <Button small disabled={!usable || previewBusy || !tl.clips.length || !(current?.available ?? false)} onClick={preview}
                    title={`Renders 5 seconds from the playhead (${time.toFixed(1)} s) through the exact pipeline: UVE effects → Video2X → your encoder. Cached by timeline range, effects, shaders and output settings.`}>Render 5-second preview</Button>
            </div>
            <p className="hint">Full-resolution preview requires a test render: the live player shows the UVE look without Anime4K.</p>
        </div>
    );
}
