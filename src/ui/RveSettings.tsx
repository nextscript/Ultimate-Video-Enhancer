import { useEffect, useRef, useState } from 'react';
import { api } from '../api';
import { type RveBackendUi, type RveStatusUi, type TorchFlavorUi, getState, setState, toast, useStore } from '../state/store';
import { Button, Progress, Section, Select, fmtBytes } from './controls';

export const rveApi = {
    status: (deep = true) => window.uve.invoke('rve:status', deep) as Promise<RveStatusUi>,
    install: (o: { reinstallBackend?: boolean; engine?: RveBackendUi; flavor?: TorchFlavorUi } = {}) => window.uve.invoke('rve:install', o) as Promise<RveStatusUi>,
    setEngine: (engine: RveBackendUi) => window.uve.invoke('rve:setEngine', engine) as Promise<RveStatusUi>,
    cancelInstall: () => window.uve.invoke('rve:cancelInstall') as Promise<void>,
    uninstall: (keepModels = true) => window.uve.invoke('rve:uninstall', keepModels) as Promise<void>,
    downloadModel: (id: string) => window.uve.invoke('rve:downloadModel', id) as Promise<RveStatusUi>,
    deleteModel: (id: string) => window.uve.invoke('rve:deleteModel', id) as Promise<RveStatusUi>,
    importModel: () => window.uve.invoke('rve:importModel') as Promise<RveStatusUi>,
    openSource: () => window.uve.invoke('rve:openSource') as Promise<void>
};

export const RVE_BACKEND_LABEL: Record<RveBackendUi, string> = { tensorrt: 'TensorRT', ncnn: 'NCNN (Vulkan)', pytorch: 'PyTorch' };
const BACKEND_INFO: Record<RveBackendUi, { gpus: string; size: string; text: string }> = {
    tensorrt: { gpus: 'NVIDIA RTX 20 series or newer', size: '8–16 GB', text: 'Fastest on NVIDIA. Portable Python 3.12, PyTorch 2.9 + CUDA 13, TensorRT 10.12. The first job per model and resolution builds a TensorRT engine (several minutes); later jobs reuse it.' },
    ncnn: { gpus: 'AMD, Intel, NVIDIA and Apple (Vulkan / MoltenVK)', size: 'about 400 MB', text: 'Runs on every GPU with a Vulkan driver – Windows, Linux and macOS (Intel and Apple Silicon). Portable Python 3.12 with ncnn and upscale-ncnn-py; models are NCNN conversions.' },
    pytorch: { gpus: 'NVIDIA (CUDA), AMD (ROCm, Linux), Intel Arc (XPU), Apple Silicon (MPS)', size: '3–10 GB', text: 'Uses the PyTorch models directly on the GPU (no engine build). Slower than TensorRT on NVIDIA, but needed for ROCm, Intel XPU or Apple MPS.' }
};
const FLAVOR_LABEL: Record<TorchFlavorUi, string> = { cuda: 'CUDA 13 – NVIDIA', rocm: 'ROCm 6.4 – AMD (Linux)', xpu: 'XPU – Intel Arc', mps: 'MPS – Apple Silicon' };

/** Backend suggested for this GPU: TensorRT on NVIDIA (Windows / Linux), otherwise NCNN (any Vulkan GPU). */
function recommended(st: RveStatusUi | null, gpuName: string): RveBackendUi {
    if (/nvidia|geforce|rtx|quadro/i.test(gpuName) && st?.supported.includes('tensorrt')) return 'tensorrt';
    return 'ncnn';
}
function recommendedFlavor(st: RveStatusUi | null, gpuName: string): TorchFlavorUi | undefined {
    const f = st?.flavors || [];
    const pick = (x: TorchFlavorUi) => (f.includes(x) ? x : undefined);
    return pick('mps') || (/nvidia|geforce|rtx/i.test(gpuName) ? pick('cuda') : /amd|radeon/i.test(gpuName) ? pick('rocm') : /intel|arc/i.test(gpuName) ? pick('xpu') : undefined) || f[0];
}

/** Loads the RVE status into the store (used by the export panel and the settings section). */
export async function refreshRve(deep = true) {
    try { setState({ rve: await rveApi.status(deep) }); } catch { /* not available */ }
}

const errText = (e: unknown) => `${e instanceof Error ? e.message.replace(/^Error invoking remote method '[^']+': (Error: )?/, '') : e}`;

export function RveSettings() {
    const st = useStore(s => s.rve);
    const gpuName = useStore(s => s.caps?.gpuName || '');
    const [prog, setProg] = useState<{ message: string; pct: number } | null>(null);
    const [log, setLog] = useState<string[]>([]);
    const [showLog, setShowLog] = useState(false);
    const [busy, setBusy] = useState(false);
    const [choice, setChoice] = useState<RveBackendUi | null>(null);
    const [flavor, setFlavor] = useState<TorchFlavorUi | null>(null);
    const logRef = useRef<HTMLPreElement>(null);
    useEffect(() => {
        void refreshRve(true);
        const a = api.on('rve:progress', (p: { message: string; pct: number }) => setProg(p));
        const b = api.on('rve:log', (l: string) => setLog(x => [...x.slice(-400), l]));
        return () => { a(); b(); };
    }, []);
    useEffect(() => { if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight; }, [log]);

    const rec = recommended(st, gpuName);
    // before anything is installed the recommended backend is preselected, afterwards the active one
    const sel: RveBackendUi = choice || (st?.python && st.packages ? st.engine : rec);
    const fl: TorchFlavorUi | undefined = flavor || (st?.torchFlavor || undefined) || recommendedFlavor(st, gpuName);
    const supported = st?.supported || [];
    const info = BACKEND_INFO[sel];
    const selInstalled = !!st?.installed?.[sel] && (sel !== 'pytorch' || !fl || st.torchFlavor === fl);
    const isActive = st?.engine === sel;

    const install = async (reinstallBackend = false) => {
        const what = sel === 'pytorch' && fl ? `PyTorch (${FLAVOR_LABEL[fl]})` : RVE_BACKEND_LABEL[sel];
        if (!reinstallBackend && !confirm(`Install REAL-Video-Enhancer with the ${what} backend into the portable tools folder?\n\nThis downloads a portable Python, the RVE backend and the ${what} packages – ${info.size} on disk. It can take a long time.`)) return;
        setBusy(true); setLog([]); setShowLog(true);
        try {
            const r = await rveApi.install({ reinstallBackend, engine: sel, flavor: sel === 'pytorch' ? fl : undefined });
            setState({ rve: r });
            setChoice(null); setFlavor(null);
            toast(`REAL-Video-Enhancer (${RVE_BACKEND_LABEL[r.engine]}) is ready for AI upscaling${r.gpu ? ` on ${r.gpu}` : ''}`, 'ok', 8000);
        } catch (e) {
            toast(`RVE installation failed: ${errText(e)}`, 'error', 12000);
            void refreshRve(true);
        } finally { setBusy(false); setProg(null); }
    };

    const action = async (fn: () => Promise<RveStatusUi>) => {
        setBusy(true);
        try { setState({ rve: await fn() }); }
        catch (e) { toast(errText(e), 'error', 9000); }
        finally { setBusy(false); setProg(null); }
    };

    const activate = async () => {
        await action(() => rveApi.setEngine(sel));
        setChoice(null);
        // a model chosen for another backend may not exist here
        const ex = getState().exportSettings;
        const now = getState().rve;
        if (ex.aiUpscale && now && !now.models.some(m => m.id === ex.aiUpscale)) setState({ exportSettings: { ...ex, aiUpscale: '' } });
        void refreshRve(true);
    };

    const pkgText = (s: RveStatusUi) => {
        if (s.engine === 'ncnn') return s.ncnn ? `ncnn ${s.ncnn}` : 'missing';
        if (s.engine === 'tensorrt') return s.installed?.tensorrt ? `torch ${s.torch} · tensorrt ${s.tensorrt} · torch-tensorrt ${s.torchTensorrt}` : 'missing';
        return s.torch ? `torch ${s.torch}${s.torchDevice ? ` (${s.torchDevice})` : ''}` : 'missing';
    };

    return (
        <Section title="AI Upscaling – REAL-Video-Enhancer" collapsible={false}>
            <p className="hint">
                AI upscaling uses <b>REAL-Video-Enhancer</b> by TNTwise as a separate portable tool in <code>{st?.dir || 'tools/rve'}</code>, installed exactly like the RVE app does it.
                UVE Manager first renders the UVE look into a lossless intermediate, then RVE upscales it with the selected model and encodes the final file.
                Choose the backend that fits your graphics card – NVIDIA, AMD, Intel and Apple GPUs are supported.
            </p>
            {st && !supported.length && <p className="warn-text">{st.unsupportedReason || 'REAL-Video-Enhancer is not available on this system.'}</p>}
            {supported.length > 0 && (
                <div className="row gap wrap">
                    <Select<RveBackendUi> label="Backend" value={sel} disabled={busy} onChange={v => setChoice(v)}
                        options={supported.map(b => ({ value: b, label: `${RVE_BACKEND_LABEL[b]}${b === rec ? ' (recommended)' : ''}${st?.engine === b && st.packages ? ' – active' : st?.installed?.[b] ? ' – installed' : ''}` }))} />
                    {sel === 'pytorch' && (st?.flavors.length || 0) > 0 && fl && (
                        <Select<TorchFlavorUi> label="PyTorch build" value={fl} disabled={busy} onChange={v => setFlavor(v)}
                            options={(st?.flavors || []).map(f => ({ value: f, label: `${FLAVOR_LABEL[f]}${st?.torchFlavor === f && st.installed?.pytorch ? ' – installed' : ''}` }))} />
                    )}
                </div>
            )}
            {supported.length > 0 && <p className="hint"><b>{RVE_BACKEND_LABEL[sel]}</b> – GPUs: {info.gpus}. Download: {info.size}. {info.text}</p>}
            {sel === 'tensorrt' && !/nvidia|geforce|rtx|quadro/i.test(gpuName) && <p className="warn-text">TensorRT requires an NVIDIA GPU (RTX 20 series or newer). For AMD, Intel or Apple GPUs choose NCNN (Vulkan) or PyTorch.</p>}
            {sel === 'pytorch' && fl === 'rocm' && <p className="hint">ROCm needs a supported AMD GPU (RDNA 2 or newer recommended) and the ROCm runtime of your distribution.</p>}
            {sel === 'pytorch' && fl === 'xpu' && <p className="hint">Intel XPU needs an Intel Arc / Core Ultra GPU and the current Intel graphics driver.</p>}
            <dl className="kv">
                <div><dt>Status</dt><dd className={st?.ready ? 'accent' : ''}>{st ? (st.ready ? `Ready – ${RVE_BACKEND_LABEL[st.engine]}${st.gpu ? ` on ${st.gpu}` : ''}` : st.message) : 'checking…'}</dd></div>
                <div><dt>Portable Python</dt><dd>{st?.python ? 'installed' : 'missing'}</dd></div>
                <div><dt>RVE backend</dt><dd>{st?.backend ? `installed${st.backendVersion ? ` (${st.backendVersion})` : ''}` : 'missing'}</dd></div>
                <div><dt>Active backend</dt><dd>{st?.python ? `${RVE_BACKEND_LABEL[st.engine]}${st.engine === 'pytorch' && st.torchFlavor ? ` (${st.torchFlavor.toUpperCase()})` : ''}` : '—'}</dd></div>
                <div><dt>Packages</dt><dd>{st?.python ? pkgText(st) : '—'}</dd></div>
                <div><dt>GPU</dt><dd>{st?.python && st.packages ? (st.gpu || 'NOT available') : '—'}</dd></div>
            </dl>
            {prog && <Progress value={prog.pct} label={prog.message} />}
            <div className="row gap wrap">
                {supported.length > 0 && !selInstalled && <Button kind="primary" icon="download" disabled={busy} onClick={() => install(false)}>
                    {st?.python ? `Install ${RVE_BACKEND_LABEL[sel]}${sel === 'pytorch' && fl ? ` (${fl.toUpperCase()})` : ''} packages` : `Install REAL-Video-Enhancer (${RVE_BACKEND_LABEL[sel]})`}
                </Button>}
                {selInstalled && !isActive && <Button kind="primary" disabled={busy} onClick={activate}>Use {RVE_BACKEND_LABEL[sel]} for rendering</Button>}
                {selInstalled && isActive && !st?.ready && <Button kind="primary" icon="download" disabled={busy} onClick={() => install(false)}>Complete / repair installation</Button>}
                {st?.ready && isActive && <Button icon="download" disabled={busy} onClick={() => install(true)}>Reinstall backend</Button>}
                {busy && <Button kind="danger" onClick={() => rveApi.cancelInstall()}>Cancel</Button>}
                <Button disabled={busy} onClick={() => refreshRve(true)}>Re-check</Button>
                <Button disabled={busy || !st?.python} kind="danger" onClick={async () => {
                    if (!confirm('Remove the REAL-Video-Enhancer runtime (Python, packages of all backends, RVE backend)? Downloaded models are kept.')) return;
                    await rveApi.uninstall(true); void refreshRve(true);
                }}>Uninstall runtime</Button>
                <Button small onClick={() => setShowLog(!showLog)}>{showLog ? 'Hide log' : 'Show log'}</Button>
            </div>
            {showLog && <pre className="log" ref={logRef}>{log.join('\n') || 'No output yet.'}</pre>}

            <h4 className="subhead">Upscale models ({RVE_BACKEND_LABEL[st?.engine || sel]})</h4>
            <table className="table compact">
                <tbody>
                    {(st?.models || []).map(m => (
                        <tr key={m.id}>
                            <td><b>{m.name}</b><div className="dim small">{m.file} · {m.arch}{m.engines ? ` · ${m.engines} TensorRT engine(s) cached` : ''}</div></td>
                            <td className="small">{m.installed ? fmtBytes(m.size) : 'not downloaded'}</td>
                            <td className="row gap">
                                {!m.installed && !m.custom && <Button small icon="download" disabled={busy} onClick={() => action(() => rveApi.downloadModel(m.id))}>Download</Button>}
                                {m.installed && <Button small icon="trash" kind="danger" disabled={busy} title={st?.engine === 'tensorrt' ? 'Delete model and cached engines' : 'Delete model'} onClick={() => action(() => rveApi.deleteModel(m.id))} />}
                            </td>
                        </tr>
                    ))}
                </tbody>
            </table>
            <div className="row gap wrap">
                <Button small icon="upload" disabled={busy} onClick={() => action(() => rveApi.importModel())}>{st?.engine === 'ncnn' ? 'Import custom NCNN model (.param + .bin)' : 'Import custom .pth / .safetensors'}</Button>
                <span className="hint">Models are downloaded automatically when a render job needs them.{st?.engine === 'tensorrt' ? ' The first job per model and resolution builds a TensorRT engine (several minutes); later jobs reuse it.' : ''}</span>
            </div>
            <p className="hint">
                REAL-Video-Enhancer is licensed under the GNU AGPL-3.0 and is not part of UVE Manager; it is downloaded from its official GitHub releases and run as a separate program.
                Models are from their respective authors (OpenModelDB / Upscale-Hub / Tencent ARC).{' '}
                <button className="link-btn inline" onClick={() => rveApi.openSource()}>Source code: github.com/TNTwise/REAL-Video-Enhancer</button>
            </p>
        </Section>
    );
}
