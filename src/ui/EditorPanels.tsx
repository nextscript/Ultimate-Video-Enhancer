import type { Container, ExportSettings, VideoCodec } from '../../shared/types';
import { FPS_MAX, FPS_MIN, FPS_PRESETS, parseFps } from '../../shared/fps';
import { HW_FAMILY_LABEL, type HwFamily } from '../../shared/hw';
import { api } from '../api';
import { enqueueTimelineJob, quickRender } from '../state/actions';
import { getState, setState, useStore } from '../state/store';
import { RVE_BACKEND_LABEL, refreshRve } from './RveSettings';
import { useEffect } from 'react';
import { PROVIDER_LABEL, type UpscaleProvider, providerPatch, resolveUpscaleProvider } from '../../shared/upscale-provider';
import { Video2xExportSection, video2xTargetSize } from './Video2xPreview';
import { Button, Icon, Select, Toggle, fmtBytes, fmtTime } from './controls';

export function MediaInfoPanel() {
    const m = useStore(s => s.media);
    const v = m?.video;
    const rows: [string, string][] = m && v ? [
        ['File Name', m.name],
        ['Container', `${m.container.split(',')[0].toUpperCase()}`],
        ['Video', `${v.codec.toUpperCase()}${v.profile ? ` (${v.profile})` : ''}`],
        ['Resolution', `${v.width} × ${v.height}${v.rotation ? ` · rotated ${v.rotation}°` : ''}${v.sar !== '1:1' && v.sar !== '0:1' ? ` · SAR ${v.sar}` : ''}`],
        ['Frame Rate', `${v.fps.toFixed(3)} fps${v.vfr ? ' (VFR)' : ''}`],
        ['Duration', fmtTime(m.duration, true)],
        ['Pixel Format', `${v.pixFmt} · ${v.bitDepth}-bit`],
        ['Color', `${v.colorSpace || 'unspecified'} / ${v.colorTransfer || '—'} / ${v.colorRange || 'tv'}${v.hdr ? ' · HDR' : ''}`],
        ['File Size', fmtBytes(m.size)],
        ['Audio', m.audio.length ? m.audio.map(a => `${a.codec.toUpperCase()} ${a.sampleRate} Hz ${a.layout || a.channels + 'ch'}${a.language ? ` [${a.language}]` : ''}`).join(' · ') : 'none'],
        ['Subtitles', m.subtitles.length ? m.subtitles.map(s => `${s.codec}${s.language ? ` [${s.language}]` : ''}`).join(', ') : 'none']
    ] : [];
    return (
        <div className="panel media-info">
            <h3><Icon name="editor" /> Video Information</h3>
            {m ? (
                <dl>{rows.map(([k, val]) => <div key={k}><dt>{k}:</dt><dd title={val}>{val}</dd></div>)}</dl>
            ) : <p className="hint">No video loaded.</p>}
        </div>
    );
}

const CODECS: { value: VideoCodec; label: string }[] = [
    { value: 'h264', label: 'H.264 / AVC' }, { value: 'hevc', label: 'H.265 / HEVC' }, { value: 'av1', label: 'AV1' },
    { value: 'vp9', label: 'VP9' }, { value: 'prores', label: 'ProRes 422 HQ' }
];

export function ExportPanel() {
    const ex = useStore(s => s.exportSettings);
    const media = useStore(s => s.media);
    const caps = useStore(s => s.caps);
    const hasClips = useStore(s => s.timeline.clips.length > 0);
    const rve = useStore(s => s.rve);
    useEffect(() => { if (!rve) void refreshRve(false); }, [rve]);
    const rveInstalled = !!rve?.python && !!rve?.backend;
    const provider = resolveUpscaleProvider(ex);
    const aiModel = provider === 'rve' ? rve?.models.find(m => m.id === ex.aiUpscale) : undefined;
    const v2xPlatform = useStore(s => s.video2x?.platformSupported ?? true);
    const set = (p: Partial<ExportSettings>) => setState({ exportSettings: { ...getState().exportSettings, ...p }, dirty: true });
    // v1.0.1: any verified hardware encoder family (NVENC / AMF / QSV / VideoToolbox / VA-API) for the codec
    const hwFor = Object.entries(caps?.hwEncoders || {}).find(([k, ok]) => ok && k.startsWith(`${ex.videoCodec}_`))?.[0] || '';
    const gpuAvail = !!hwFor && !(ex.videoCodec === 'h264' && ex.tenBit);
    const hwLabel = hwFor ? (HW_FAMILY_LABEL[hwFor.split('_')[1] as HwFamily] || hwFor) : '';
    const encLabel = ex.encoder === 'cpu' || !gpuAvail ? 'CPU' : 'HW';
    const v = media?.video;
    const srcLabel = v ? `Source (${v.rotation % 180 ? v.height : v.width} × ${v.rotation % 180 ? v.width : v.height})` : 'Source';
    return (
        <div className="panel export-panel">
            <h3><Icon name="render" /> Output Settings</h3>
            <div className="export-grid">
                <Select<UpscaleProvider> label="AI Upscale" value={provider}
                    title={'AI upscale provider: REAL-Video-Enhancer (neural network models; TensorRT, NCNN/Vulkan or PyTorch) or Video2X (Anime4K v4 shaders in libplacebo, fast, Vulkan). Install them in Settings → AI Upscaling.'}
                    onChange={p => set(providerPatch(ex, p))}
                    options={[{ value: 'off', label: PROVIDER_LABEL.off }, { value: 'rve', label: `${PROVIDER_LABEL.rve}${rveInstalled ? '' : ' (not installed)'}` },
                        { value: 'video2x', label: `${PROVIDER_LABEL.video2x}${v2xPlatform ? '' : ' (not available on this system)'}` }]} />
                {provider !== 'video2x' && <Select label="RVE model" value={ex.aiUpscale} disabled={provider === 'off' && !rveInstalled} title={`AI upscaling with REAL-Video-Enhancer${rve?.python ? ` (${RVE_BACKEND_LABEL[rve.engine]})` : '' } – TensorRT (NVIDIA), NCNN/Vulkan (AMD, Intel, NVIDIA, Apple) or PyTorch. Install it in Settings → AI Upscaling.`}
                    onChange={id => set({ aiUpscale: id, upscaleProvider: id ? 'rve' : 'off', ...(id && ex.resolution === 'x2' ? { resolution: 'source' as const } : {}) })}
                    options={[{ value: '', label: rveInstalled ? 'None' : 'None (REAL-Video-Enhancer not installed)' },
                        ...(rveInstalled ? (rve?.models || []).map(m => ({ value: m.id, label: `${m.name}${m.installed ? '' : ' – downloads on render'}` })) : [])]} />}
                <Select label="Resolution" value={ex.resolution} onChange={r => set({ resolution: r })} options={[
                    { value: 'source', label: aiModel && v ? `AI output (${(v.rotation % 180 ? v.height : v.width) * aiModel.scale} × ${(v.rotation % 180 ? v.width : v.height) * aiModel.scale})`
                        : provider === 'video2x' && v ? (() => { const t = video2xTargetSize(ex, v.rotation % 180 ? v.height : v.width, v.rotation % 180 ? v.width : v.height); return `Video2X output (${t.w} × ${t.h})`; })() : srcLabel }, { value: '2160', label: '2160p (4K UHD)' }, { value: '1440', label: '1440p' }, { value: '1080', label: '1080p (FHD)' },
                    { value: '720', label: '720p' }, { value: '480', label: '480p' }, ...(provider !== 'off' ? [] : [{ value: 'x2' as const, label: '2× Upscale (Lanczos, not AI)' }])]} />
                <label className="select-row" title="Output frame rate. Empty = keep the source frame rate. Accepts e.g. 30, 29.97, 59.94 or 24000/1001. Frames are duplicated or dropped (no interpolation).">
                    <span className="select-label">Frame rate (fps)</span>
                    <input className={`text num fps${ex.fps && !parseFps(ex.fps) ? ' invalid' : ''}`} list="uve-fps-presets" value={ex.fps}
                        placeholder={v ? `Source (${(v.fpsNum / (v.fpsDen || 1)).toFixed(3).replace(/\.?0+$/, '')})` : 'Source'}
                        onChange={e => set({ fps: e.target.value.replace(/[^\d.,/]/g, '') })} />
                    <datalist id="uve-fps-presets">{FPS_PRESETS.map(f => <option key={f} value={f} />)}</datalist>
                    <span className="dim small">{ex.fps ? (parseFps(ex.fps) ? 'fps' : `invalid (${FPS_MIN}–${FPS_MAX})`) : 'source'}</span>
                </label>
                <Select label="Container" value={ex.container} onChange={c => set({ container: c as Container, ...(c === 'webm' && !['vp9', 'av1'].includes(ex.videoCodec) ? { videoCodec: 'vp9' as VideoCodec } : {}) })}
                    options={[{ value: 'mp4', label: 'MP4' }, { value: 'mkv', label: 'MKV' }, { value: 'mov', label: 'MOV' }, { value: 'webm', label: 'WebM' }]} />
                <Select label="Video Codec" value={ex.videoCodec} onChange={c => set({ videoCodec: c, ...(c === 'prores' ? { container: 'mov' as Container } : {}) })}
                    options={CODECS.filter(c => ex.container !== 'webm' || c.value === 'vp9' || c.value === 'av1')} />
                <Select label="Encoder" value={ex.encoder} onChange={e => set({ encoder: e })} options={[
                    { value: 'auto', label: `Automatic (${gpuAvail ? hwLabel : 'software'})` }, { value: 'gpu', label: `Prefer Hardware${gpuAvail ? ` – ${hwLabel}` : ' (none verified)'}` }, { value: 'cpu', label: 'Software (CPU)' }]} />
                <label className="select-row" title={encLabel === 'HW' ? 'Constant quality for the hardware encoder (same 0–51 scale)' : 'Constant rate factor (CRF)'}>
                    <span className="select-label">Quality ({encLabel === 'HW' ? 'CQ' : 'CRF'})</span>
                    <input type="number" className="text num" min={0} max={51} value={ex.quality} disabled={ex.videoCodec === 'prores'} onChange={e => set({ quality: Math.max(0, Math.min(63, Number(e.target.value) || 0)) })} />
                    <span className="dim small">{ex.quality <= 16 ? 'very high' : ex.quality <= 20 ? 'high' : ex.quality <= 26 ? 'medium' : 'low'}</span>
                </label>
                <Select label="Speed" value={ex.preset} onChange={p => set({ preset: p })} options={[{ value: 'fast', label: 'Fast' }, { value: 'medium', label: 'Balanced' }, { value: 'slow', label: 'Best quality' }]} />
                <Select label="Audio" value={ex.audio} onChange={a => set({ audio: a })} options={[
                    { value: 'copy', label: 'Copy original audio' }, { value: 'aac', label: `AAC ${ex.audioBitrate} kbit/s` }, { value: 'opus', label: `Opus ${ex.audioBitrate} kbit/s` }, { value: 'none', label: 'No audio' }]} />
                {media && media.audio.length > 1 && <Select label="Audio track" value={ex.audioTrack} onChange={t => set({ audioTrack: t })}
                    options={[{ value: -1, label: 'First track' }, ...media.audio.map(a => ({ value: a.index, label: `#${a.index} ${a.codec} ${a.language || ''} ${a.title || ''}` }))]} />}
                <Select label="Subtitles" value={ex.subtitles} onChange={s => set({ subtitles: s })} options={[{ value: 'none', label: 'None' }, { value: 'copy', label: 'Copy (if compatible)' }]} />
                <label className="select-row">
                    <span className="select-label">Output Folder</span>
                    <input className="text" value={ex.outputDir} placeholder={media ? '…\\UVE_Enhanced (next to the source)' : 'Next to the source'} onChange={e => set({ outputDir: e.target.value })} />
                    <button className="icon-btn" title="Choose folder" aria-label="Choose output folder" onClick={async () => { const d = await api.pickFolder('Output folder'); if (d) { set({ outputDir: d }); void api.setPrefs({ lastOutputDir: d }); } }}><Icon name="folder" /></button>
                </label>
                <label className="select-row" title="{name} {profile} {date} {time}">
                    <span className="select-label">File name</span>
                    <input className="text" value={ex.fileName} onChange={e => set({ fileName: e.target.value })} />
                </label>
            </div>
            <div className="row gap wrap">
                <Toggle checked={ex.tenBit} label="10-bit output" onChange={t => set({ tenBit: t })} disabled={ex.videoCodec === 'prores'} title="Reads the processed frames with 16-bit precision and encodes 10-bit (HEVC/AV1/VP9; H.264 only on CPU)" />            </div>
            {provider === 'video2x' && <Video2xExportSection set={set} />}
            {provider === 'rve' && !rveInstalled && <p className="warn-text">AI upscaling needs REAL-Video-Enhancer – install it in Settings → AI Upscaling.</p>}
            {provider === 'rve' && rveInstalled && <p className="hint">Two-stage render: UVE look → lossless intermediate → REAL-Video-Enhancer {rve ? RVE_BACKEND_LABEL[rve.engine] : ''} {aiModel?.scale}× upscale → final encode.{rve?.engine === 'tensorrt' ? ' The first job per model/resolution builds a TensorRT engine.' : ''}</p>}
            <div className="row gap">
                <button className="btn btn-primary btn-render" disabled={!hasClips || !caps?.ok} onClick={() => enqueueTimelineJob()} title="Render the whole timeline (all tracks, transitions and audio) as one file"><Icon name="render" /> Render Video</button>
                <Button disabled={!media?.video || !caps?.ok} onClick={() => void quickRender()} title={'Sofort-Render: the original file 1:1 with your current look (filters, profile, LUT, Auto-Scene-Match) - same resolution, frame rate, codec and bit depth, every audio and subtitle track copied. No AI upscaling, timeline edits are ignored.'}>Sofort-Render (Raw + Filter)</Button>
                <Button onClick={() => setState({ view: 'queue' })}>Render Queue</Button>
            </div>
            {!caps?.ok && <p className="warn-text">{caps?.error || 'Checking FFmpeg…'}</p>}
        </div>
    );
}
