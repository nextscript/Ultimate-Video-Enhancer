// v1.0.7: Output Settings → AI Upscale → Video2X (preset, scale, GPU, status, disk estimate, exact-pipeline preview).
// v1.0.8: Processor selector (Anime4K / Real-ESRGAN / Real-CUGAN) with model, scale and noise for the neural models.
import { useEffect, useState } from 'react';
import { timelineDuration } from '../../shared/timeline';
import { VIDEO2X_SCALES, video2xOptions, video2xProcessor } from '../../shared/upscale-provider';
import { estimateTempBytes } from '../../shared/video2x-cli';
import {
    DEFAULT_MODEL, NEURAL_MODELS, NOISE_LABEL, PROCESSOR_LABEL, VIDEO2X_PROCESSORS, type Video2xProcessor, comboKey, isNeural, neuralComboProblem
} from '../../shared/video2x-processors';
import type { ExportSettings } from '../../shared/types';
import { api } from '../api';
import { enqueueTimelineJob } from '../state/actions';
import { getState, setState, toast, useStore } from '../state/store';
import { Button, Select, Toggle, fmtBytes } from './controls';
import { errText, refreshVideo2x, video2xApi } from './Video2xSettings';

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
    const [testBusy, setTestBusy] = useState(false);
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

    // v1.0.8 neural processors (Anime4K keeps its preset in `presetId` while another processor is selected)
    const proc = video2xProcessor(o);
    const neural = isNeural(proc);
    const pcap = neural ? st?.processors?.[proc] : undefined;
    const models = neural ? (pcap?.models ?? NEURAL_MODELS[proc]) : [];
    const modelId = neural ? (o.modelId && models.some(m => m.id === o.modelId) ? o.modelId : DEFAULT_MODEL[proc]) : '';
    const model = models.find(m => m.id === modelId);
    const modelScales = model ? Object.keys(model.scales).map(Number) : VIDEO2X_SCALES;
    const noises = proc === 'realcugan' && model ? (model.scales[o.scale] ?? []) : [];
    const noise = proc === 'realcugan' ? (o.noiseLevel !== undefined && noises.includes(o.noiseLevel) ? o.noiseLevel : noises[0]) : undefined;
    const combo = neural ? neuralComboProblem(proc, modelId, o.scale, noise) : '';
    const modelOut = v ? { w: pw * o.scale, h: ph * o.scale } : null;
    const needsResize = neural && !!out && !!modelOut && ex.resolution !== 'source' && ex.resolution !== 'x2' && (out.w !== modelOut.w || out.h !== modelOut.h);
    const key = neural ? comboKey(proc, modelId, o.scale, noise, o.device) : '';
    const verified = !!pcap?.verified.includes(key);
    const failed = pcap?.failed.find(f => f.combo === key);
    const shownOut = neural && modelOut && !(needsResize && o.postResize) ? modelOut : out;

    const setProcessor = (p: Video2xProcessor) => {
        if (!isNeural(p)) { setO({ processor: undefined }); return; }
        const keep = o.modelId && NEURAL_MODELS[p].some(m => m.id === o.modelId);
        setO({ processor: p, modelId: keep ? o.modelId : DEFAULT_MODEL[p], noiseLevel: p === 'realcugan' ? o.noiseLevel : undefined });
    };
    const testCombo = async () => {
        if (!neural) return;
        setTestBusy(true);
        try {
            const r = await video2xApi.testProcessor({ processor: proc, modelId, scale: o.scale, noiseLevel: noise, device: o.device });
            toast(r.ok ? `${PROCESSOR_LABEL[proc]} works on this GPU: ${r.message}` : `Test failed: ${r.message}`, r.ok ? 'ok' : 'error', 12000);
        } catch (e) { toast(errText(e), 'error', 12000); }
        finally { setTestBusy(false); void refreshVideo2x(false); }
    };

    const preview = async () => {
        if (!tl.clips.length) { toast('The timeline is empty.', 'warn'); return; }
        setPreviewBusy(true);
        try {
            const s = getState();
            const start = Math.max(0, Math.min(s.time, Math.max(0, dur - 5)));
            const range = { start, end: Math.min(dur, start + 5) };
            const exp = { ...s.exportSettings, outputDir: '', fileName: '' };
            const key2 = { tl: s.timeline, effect: s.effect, ex: exp, range, v: 1 };
            const r = await video2xApi.previewPath({ key: key2, presetId: o.presetId, ext: exp.videoCodec === 'prores' ? 'mov' : exp.container });
            if (r.queued) { toast('This preview is already in the render queue.', 'info', 5000); setState({ view: 'queue' }); return; }
            if (r.exists) { toast('This preview was rendered before (same timeline range, effects, shaders and output settings) – showing the cached file.', 'ok', 7000); void api.showItem(r.path); return; }
            const outFile = await enqueueTimelineJob({ range, output: r.path, name: `Preview 5 s @ ${start.toFixed(1)} s – Video2X` });
            if (outFile) setState({ view: 'queue' });
        } catch (e) { toast(`Preview failed: ${e instanceof Error ? e.message : e}`, 'error', 9000); }
        finally { setPreviewBusy(false); }
    };

    const previewOk = neural ? !combo && pcap?.available !== false && (!needsResize || !!o.postResize) : (current?.available ?? false);
    return (
        <div className="v2x-export">
            <div className="export-grid">
                <Select<Video2xProcessor> label="Processor" value={proc} onChange={setProcessor}
                    title="Video2X processing mode: Anime4K v4 shaders (libplacebo, fastest) or the Real-ESRGAN / Real-CUGAN ncnn models (Vulkan)"
                    options={VIDEO2X_PROCESSORS.map(p => {
                        const c = isNeural(p) ? st?.processors?.[p] : undefined;
                        return { value: p, label: `${PROCESSOR_LABEL[p]}${c && !c.available ? ' – not available' : ''}` };
                    })} />
                <label className="select-row"><span className="select-label">Engine</span><span className="dim small">{neural ? 'Video2X / ncnn (Vulkan)' : 'Video2X / libplacebo'}{st?.version ? ` ${st.version}` : ''}</span></label>
                {!neural && <Select label="Preset" value={o.presetId} disabled={!presets.length} onChange={id => setO({ presetId: id })}
                    title="Anime4K v4 shader preset (built-in presets come from the installed Video2X build) or a custom GLSL chain"
                    options={presets.length ? presets.map(p => ({ value: p.id, label: `${p.displayName}${p.kind === 'custom' ? ` (${p.passes} pass${p.passes === 1 ? '' : 'es'})` : ''}${p.available ? '' : ' – not usable'}` }))
                        : [{ value: o.presetId, label: o.presetId.startsWith('builtin:') ? `${o.presetId.slice(8)} (Video2X not detected)` : 'Custom preset (Video2X not detected)' }]} />}
                {neural && <Select label="Model" value={modelId} onChange={m => setO({ modelId: m })}
                    title={pcap ? 'Models of this Video2X build that UVE Manager knows' : 'Models of the tested Video2X 6.4.0 – checked against your binary once Video2X is detected'}
                    options={models.map(m => ({ value: m.id, label: `${m.label}${pcap?.verified.some(k => k.startsWith(`${proc}|${m.id}|`)) ? ' – verified' : ''}` }))} />}
                {!neural && <Select<number> label="Scale" value={o.scale} onChange={v2 => setO({ scale: v2 })} disabled={ex.resolution !== 'source'}
                    title={ex.resolution !== 'source' ? 'The Resolution choice sets the output size' : 'Upscale factor of the UVE render'}
                    options={VIDEO2X_SCALES.map(s2 => ({ value: s2, label: `${s2}×` }))} />}
                {neural && <Select<number> label="Scale" value={o.scale} onChange={v2 => setO({ scale: v2 })}
                    title="Fixed scale of the model: the output is exactly this factor × the UVE render"
                    options={[...modelScales, ...(modelScales.includes(o.scale) ? [] : [o.scale])].map(s2 => ({ value: s2, label: `${s2}×${modelScales.includes(s2) ? '' : ' (not supported by this model)'}` }))} />}
                {proc === 'realcugan' && pcap?.noise !== false && noises.length > 0 && noise !== undefined && <Select<number> label="Noise level" value={noise} onChange={n => setO({ noiseLevel: n })}
                    title="Real-CUGAN denoising strength (only the levels this model and scale provide)"
                    options={noises.map(n => ({ value: n, label: NOISE_LABEL[n] }))} />}
                <Select<number> label="Processing" value={o.device} onChange={d => setO({ device: d })}
                    title="Vulkan GPU used by Video2X (Automatic = the first device Video2X lists)"
                    options={[{ value: -1, label: 'GPU: Auto' }, ...(st?.vulkan.devices || []).map(d => ({ value: d.index, label: `GPU ${d.index}: ${d.name}` }))]} />
            </div>
            <p className="hint">
                {neural
                    ? `Model: ${model?.label || modelId}${proc === 'realcugan' && noise !== undefined ? ` · ${NOISE_LABEL[noise]}` : ''}`
                    : <>Shader chain: {current ? (current.kind === 'built-in' ? `1 built-in shader (${current.builtinShader})` : `custom chain, ${current.passes} pass(es)`)
                        : st?.exe ? 'preset not available in this Video2X build' : o.presetId.startsWith('builtin:') ? `built-in ${o.presetId.slice(8)} (checked once Video2X is detected)` : 'custom chain (checked once Video2X is detected)'}</>}
                {shownOut && ` · ${pw} × ${ph} → ${shownOut.w} × ${shownOut.h}`}
                {out && frames > 0 && ` · about ${fmtBytes(temp)} temporary disk space`}
                {' · Output FPS: '}{ex.fps ? ex.fps : 'timeline frame rate'}
            </p>
            {!neural && current && !current.available && <p className="warn-text">{current.problem}</p>}
            {neural && pcap && !pcap.available && <p className="warn-text">{PROCESSOR_LABEL[proc]} is not available: {pcap.problem}</p>}
            {neural && combo && <p className="warn-text">{combo}</p>}
            {neural && modelOut && (modelOut.w % 2 || modelOut.h % 2) ? <p className="warn-text">The {o.scale}× output would be {modelOut.w} × {modelOut.h} – odd sizes cannot be encoded. Choose 2× or 4×.</p> : null}
            {needsResize && modelOut && out && (
                <div className="row gap wrap">
                    <p className="warn-text">The model outputs {modelOut.w} × {modelOut.h}, but Resolution asks for {out.w} × {out.h}. Without a post-resize the job is refused (no silent resize).</p>
                    <Toggle checked={!!o.postResize} label="Resize model output to the Resolution choice (Lanczos)" onChange={c => setO({ postResize: c || undefined })} />
                </div>
            )}
            {neural && !combo && <p className={failed && !verified ? 'warn-text' : 'hint'}>
                {verified ? `Verified on this GPU (${modelId}, ${o.scale}×).` : failed ? `Last test failed: ${failed.message}` : 'Not tested yet – a 10-frame test job runs automatically before the first render.'}
            </p>}
            <p className={st?.state === 'installed' ? 'hint' : 'warn-text'}>
                Status: {st ? (st.state === 'unsupported' ? st.platformReason : st.state === 'not-installed' ? 'Video2X is not installed – install it in Settings → AI Upscaling → Video2X.' : `${st.summary}${st.diagnostic ? ` – ${st.diagnostic}` : ''}`) : 'checking…'}
            </p>
            <div className="row gap wrap">
                <Button small onClick={() => setState({ view: 'settings' })}>{usable ? (neural ? 'Configure Video2X' : 'Configure / Manage shaders') : 'Install / Verify / Configure'}</Button>
                {neural && <Button small disabled={!usable || testBusy || !!combo || pcap?.available === false} onClick={testCombo}
                    title="Runs a 10-frame test job with exactly this model, scale, noise level and GPU">Test this model</Button>}
                <Button small disabled={!usable || previewBusy || !tl.clips.length || !previewOk} onClick={preview}
                    title={`Renders 5 seconds from the playhead (${time.toFixed(1)} s) through the exact pipeline: UVE effects → Video2X → your encoder. Cached by timeline range, effects, ${neural ? 'model' : 'shaders'} and output settings.`}>Render 5-second preview</Button>
            </div>
            <p className="hint">Full-resolution preview requires a test render: the live player shows the UVE look without {neural ? PROCESSOR_LABEL[proc] : 'Anime4K'}.</p>
        </div>
    );
}
