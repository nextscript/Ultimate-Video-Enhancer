import { useEffect, useRef, useState } from 'react';
import { api } from '../api';
import { type RveStatusUi, getState, setState, toast, useStore } from '../state/store';
import { Button, Progress, Section, fmtBytes } from './controls';

export const rveApi = {
    status: (deep = true) => window.uve.invoke('rve:status', deep) as Promise<RveStatusUi>,
    install: (reinstallBackend = false) => window.uve.invoke('rve:install', reinstallBackend) as Promise<RveStatusUi>,
    cancelInstall: () => window.uve.invoke('rve:cancelInstall') as Promise<void>,
    uninstall: (keepModels = true) => window.uve.invoke('rve:uninstall', keepModels) as Promise<void>,
    downloadModel: (id: string) => window.uve.invoke('rve:downloadModel', id) as Promise<RveStatusUi>,
    deleteModel: (id: string) => window.uve.invoke('rve:deleteModel', id) as Promise<RveStatusUi>,
    importModel: () => window.uve.invoke('rve:importModel') as Promise<RveStatusUi>,
    openSource: () => window.uve.invoke('rve:openSource') as Promise<void>
};

/** Loads the RVE status into the store (used by the export panel and the settings section). */
export async function refreshRve(deep = true) {
    try { setState({ rve: await rveApi.status(deep) }); } catch { /* not available */ }
}

export function RveSettings() {
    const st = useStore(s => s.rve);
    const [prog, setProg] = useState<{ message: string; pct: number } | null>(null);
    const [log, setLog] = useState<string[]>([]);
    const [showLog, setShowLog] = useState(false);
    const [busy, setBusy] = useState(false);
    const logRef = useRef<HTMLPreElement>(null);
    useEffect(() => {
        void refreshRve(true);
        const a = api.on('rve:progress', (p: { message: string; pct: number }) => setProg(p));
        const b = api.on('rve:log', (l: string) => setLog(x => [...x.slice(-400), l]));
        return () => { a(); b(); };
    }, []);
    useEffect(() => { if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight; }, [log]);
    const nvidia = /nvidia/i.test(getState().caps?.gpuName || '');

    const install = async (reinstallBackend = false) => {
        if (!reinstallBackend && !confirm('Install REAL-Video-Enhancer with the TensorRT backend into the portable tools folder?\n\nThis downloads a portable Python, the RVE backend and PyTorch (CUDA 13) + TensorRT – about 8–16 GB on disk. It can take a long time.')) return;
        setBusy(true); setLog([]); setShowLog(true);
        try {
            setState({ rve: await rveApi.install(reinstallBackend) });
            toast('REAL-Video-Enhancer (TensorRT) is ready for AI upscaling', 'ok', 8000);
        } catch (e) {
            toast(`RVE installation failed: ${e instanceof Error ? e.message.replace(/^Error invoking remote method '[^']+': (Error: )?/, '') : e}`, 'error', 12000);
            void refreshRve(true);
        } finally { setBusy(false); setProg(null); }
    };

    const modelAction = async (fn: () => Promise<RveStatusUi>) => {
        setBusy(true);
        try { setState({ rve: await fn() }); }
        catch (e) { toast(`${e instanceof Error ? e.message.replace(/^Error invoking remote method '[^']+': (Error: )?/, '') : e}`, 'error', 9000); }
        finally { setBusy(false); setProg(null); }
    };

    return (
        <Section title="AI Upscaling – REAL-Video-Enhancer (TensorRT)" collapsible={false}>
            <p className="hint">
                AI upscaling uses <b>REAL-Video-Enhancer</b> by TNTwise as a separate portable tool in <code>{st?.dir || 'tools\\rve'}</code>
                (portable Python 3.12, PyTorch {`2.9 + CUDA 13`}, TensorRT 10.12 and the RVE backend, installed exactly like the RVE app does).
                UVE Manager first renders the UVE look into a lossless intermediate, then RVE upscales it with the selected model and encodes the final file.
            </p>
            {!nvidia && <p className="warn-text">The TensorRT backend requires an NVIDIA GPU (RTX 20 series or newer recommended).</p>}
            <dl className="kv">
                <div><dt>Status</dt><dd className={st?.ready ? 'accent' : ''}>{st ? (st.ready ? `Ready – ${st.gpu}` : st.message) : 'checking…'}</dd></div>
                <div><dt>Portable Python</dt><dd>{st?.python ? 'installed' : 'missing'}</dd></div>
                <div><dt>RVE backend</dt><dd>{st?.backend ? `installed${st.backendVersion ? ` (${st.backendVersion})` : ''}` : 'missing'}</dd></div>
                <div><dt>PyTorch / TensorRT</dt><dd>{st?.packages ? `torch ${st.torch} · tensorrt ${st.tensorrt} · torch-tensorrt ${st.torchTensorrt}` : 'missing'}</dd></div>
                <div><dt>CUDA</dt><dd>{st?.python && st?.packages ? (st.cuda ? 'available' : 'NOT available') : '—'}</dd></div>
            </dl>
            {prog && <Progress value={prog.pct} label={prog.message} />}
            <div className="row gap wrap">
                {!st?.ready && <Button kind="primary" icon="download" disabled={busy || !nvidia} onClick={() => install(false)}>{st?.python ? 'Complete / repair installation' : 'Install REAL-Video-Enhancer (TensorRT)'}</Button>}
                {st?.ready && <Button icon="download" disabled={busy} onClick={() => install(true)}>Reinstall backend</Button>}
                {busy && <Button kind="danger" onClick={() => rveApi.cancelInstall()}>Cancel</Button>}
                <Button disabled={busy} onClick={() => refreshRve(true)}>Re-check</Button>
                <Button disabled={busy || !st?.python} kind="danger" onClick={async () => {
                    if (!confirm('Remove the REAL-Video-Enhancer runtime (Python, packages, backend)? Downloaded models are kept.')) return;
                    await rveApi.uninstall(true); void refreshRve(true);
                }}>Uninstall runtime</Button>
                <Button small onClick={() => setShowLog(!showLog)}>{showLog ? 'Hide log' : 'Show log'}</Button>
            </div>
            {showLog && <pre className="log" ref={logRef}>{log.join('\n') || 'No output yet.'}</pre>}

            <h4 className="subhead">Upscale models (TensorRT)</h4>
            <table className="table compact">
                <tbody>
                    {(st?.models || []).map(m => (
                        <tr key={m.id}>
                            <td><b>{m.name}</b><div className="dim small">{m.file} · {m.arch}{m.engines ? ` · ${m.engines} TensorRT engine(s) cached` : ''}</div></td>
                            <td className="small">{m.installed ? fmtBytes(m.size) : 'not downloaded'}</td>
                            <td className="row gap">
                                {!m.installed && !m.custom && <Button small icon="download" disabled={busy} onClick={() => modelAction(() => rveApi.downloadModel(m.id))}>Download</Button>}
                                {m.installed && <Button small icon="trash" kind="danger" disabled={busy} title="Delete model and cached engines" onClick={() => modelAction(() => rveApi.deleteModel(m.id))} />}
                            </td>
                        </tr>
                    ))}
                </tbody>
            </table>
            <div className="row gap wrap">
                <Button small icon="upload" disabled={busy} onClick={() => modelAction(() => rveApi.importModel())}>Import custom .pth / .safetensors</Button>
                <span className="hint">Models are downloaded automatically when a render job needs them. The first job per model and resolution builds a TensorRT engine (several minutes); later jobs reuse it.</span>
            </div>
            <p className="hint">
                REAL-Video-Enhancer is licensed under the GNU AGPL-3.0 and is not part of UVE Manager; it is downloaded from its official GitHub releases and run as a separate program.
                Models are from their respective authors (OpenModelDB / Upscale-Hub / Tencent ARC).{' '}
                <button className="link-btn inline" onClick={() => rveApi.openSource()}>Source code: github.com/TNTwise/REAL-Video-Enhancer</button>
            </p>
        </Section>
    );
}
