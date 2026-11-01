// v1.0.7: Settings → AI Upscaling → Video2X (install / update / remove, custom executable, verification).
import { useEffect, useState } from 'react';
import { api } from '../api';
import type { Video2xStatus } from '../../shared/video2x-types';
import { setState, toast, useStore } from '../state/store';
import { Button, Progress, Section, fmtBytes } from './controls';
import { Video2xShaderLibrary } from './Video2xShaderLibrary';

const inv = <T,>(ch: string, ...a: unknown[]) => window.uve.invoke(ch, ...a) as Promise<T>;
export const video2xApi = {
    status: (deep = true) => inv<Video2xStatus>('video2x:status', deep),
    install: () => inv<Video2xStatus>('video2x:install'),
    cancelInstall: () => inv<void>('video2x:cancelInstall'),
    remove: () => inv<Video2xStatus>('video2x:remove'),
    verify: (device = -1) => inv<Video2xStatus>('video2x:verify', device),
    reset: () => inv<Video2xStatus>('video2x:reset'),
    selectExe: () => inv<Video2xStatus>('video2x:selectExe'),
    clearExe: () => inv<Video2xStatus>('video2x:clearExe'),
    openLogs: () => inv<string>('video2x:openLogs'),
    openSource: () => inv<void>('video2x:openSource'),
    openShaderFolder: () => inv<string>('video2x:openShaderFolder'),
    importShaders: (o: { presetId?: string; mode: 'copy' | 'reference' }) => inv<string | null>('video2x:importShaders', o),
    importFolder: (o: { mode: 'copy' | 'reference' }) => inv<string | null>('video2x:importFolder', o),
    editPreset: (e: Record<string, unknown>) => inv<string>('video2x:editPreset', e),
    testPreset: (id: string, device = -1) => inv<{ ok: boolean; message: string }>('video2x:testPreset', id, device),
    previewPath: (o: { key: unknown; presetId: string; ext: string }) => inv<{ path: string; exists: boolean; queued: boolean }>('video2x:previewPath', o)
};

export async function refreshVideo2x(deep = true) {
    try { setState({ video2x: await video2xApi.status(deep) }); } catch { /* not available */ }
}

export const errText = (e: unknown) => `${e instanceof Error ? e.message.replace(/^Error invoking remote method '[^']+': (Error: )?/, '') : e}`;

const STATE_LABEL: Record<Video2xStatus['state'], string> = {
    installed: 'Installed', 'not-installed': 'Not installed', unsupported: 'Unsupported', 'verification-failed': 'Verification failed', unverified: 'Installed – not verified'
};

export function Video2xSettings() {
    const st = useStore(s => s.video2x);
    const [prog, setProg] = useState<{ message: string; pct: number } | null>(null);
    const [busy, setBusy] = useState(false);
    useEffect(() => {
        void refreshVideo2x(true);
        return api.on('video2x:progress', (p: { message: string; pct: number }) => setProg(p));
    }, []);

    const run = async (fn: () => Promise<Video2xStatus>, ok?: (s: Video2xStatus) => string) => {
        setBusy(true);
        try { const s = await fn(); setState({ video2x: s }); if (ok) toast(ok(s), s.state === 'installed' ? 'ok' : 'warn', 8000); }
        catch (e) { toast(errText(e), 'error', 12000); void refreshVideo2x(true); }
        finally { setBusy(false); setProg(null); }
    };
    const install = () => {
        const i = st?.install;
        if (!i) return;
        const ok = confirm(`Install Video2X ${i.version} into the tools folder?\n\nVendor: ${i.vendor}\nLicense: ${i.license} – Video2X is a separate program, not part of UVE Manager.\nFile: ${i.asset} (${fmtBytes(i.size)})\nSource: ${i.url}\nGitHub release ${i.releaseId}, asset ${i.assetId}\nSHA-256: ${i.sha256}\n\n${i.integrity}`);
        if (ok) void run(() => video2xApi.install(), s => s.state === 'installed' ? `Video2X ${s.version} installed and verified` : `Video2X installed – ${s.diagnostic || STATE_LABEL[s.state]}`);
    };

    const dis = busy || !!st?.busy;
    return (
        <Section title="AI Upscaling – Video2X (Anime4K)" collapsible={false}>
            <p className="hint">
                <b>Video2X</b> by K4YT3X runs the <b>Anime4K v4</b> GLSL shaders (bloc97) in libplacebo on any Vulkan GPU – a fast alternative to
                the neural-network models of REAL-Video-Enhancer, mainly for animation. Anime4K is a set of real-time shaders, not a large
                AI model: it is much faster but restores less detail. UVE Manager renders the edited timeline into a lossless intermediate,
                Video2X upscales it, then your encoder settings produce the final file (render time = UVE render + Video2X + encode).
            </p>
            {st && !st.platformSupported && <p className="warn-text">{st.platformReason}</p>}
            <dl className="kv">
                <div><dt>Status</dt><dd className={st?.state === 'installed' ? 'accent' : ''}>{st ? `${STATE_LABEL[st.state]}${st.summary && st.state !== 'not-installed' && st.state !== 'unsupported' ? ` – ${st.summary}` : ''}` : 'checking…'}</dd></div>
                <div><dt>Installed version</dt><dd>{st?.version || '—'}{st?.version && st.version !== st.pinnedVersion ? ` (tested: ${st.pinnedVersion})` : ''}</dd></div>
                <div><dt>Executable path</dt><dd>{st?.exe ? `${st.exe} (${st.source === 'managed' ? 'installed by UVE Manager' : st.source === 'custom' ? 'selected by you' : 'test override'})` : '—'}</dd></div>
                <div><dt>Vulkan status</dt><dd>{st ? `${{ verified: 'Verified by a test job', 'devices-found': 'Device(s) found, not verified', none: 'No Vulkan device', unknown: 'Unknown', error: 'Error' }[st.vulkan.status]}${st.vulkan.devices.length ? ` – ${st.vulkan.devices.map(d => `${d.index}. ${d.name}${d.type ? ` (${d.type})` : ''}`).join(', ')}` : ''}` : '—'}</dd></div>
                <div><dt>Shader support</dt><dd>{st?.shaderSupport || '—'}</dd></div>
                <div><dt>Last verification</dt><dd>{st?.verification ? `${st.verification.ok ? 'passed' : 'FAILED'} ${new Date(st.verification.at).toLocaleString()} – ${st.verification.message}` : 'not run'}</dd></div>
                {st?.managed && <div><dt>Managed install</dt><dd>{st.managed.dir} · from {st.managed.sourceUrl} · SHA-256 {st.managed.sha256.slice(0, 16)}…</dd></div>}
            </dl>
            {st?.diagnostic && <p className="warn-text">Diagnostic: {st.diagnostic}</p>}
            {st?.warnings.map(w => <p key={w} className="warn-text">{w}</p>)}
            {(prog || st?.busy) && <Progress value={prog?.pct ?? 0.5} label={prog?.message || st?.message || 'Working…'} />}
            <div className="row gap wrap">
                {st?.install && !st.managed && <Button kind="primary" icon="download" disabled={dis} onClick={install} title={`${st.install.asset} · ${fmtBytes(st.install.size)}`}>Install Video2X {st.install.version}</Button>}
                {st?.install && st.managed && st.updateAvailable && <Button kind="primary" icon="download" disabled={dis} onClick={install}>Update Video2X to {st.install.version}</Button>}
                {busy && prog && <Button kind="danger" onClick={() => video2xApi.cancelInstall()}>Cancel</Button>}
                <Button disabled={dis} onClick={() => run(() => video2xApi.selectExe())}>Select existing executable…</Button>
                {st?.customExe && <Button disabled={dis} onClick={() => run(() => video2xApi.clearExe())}>Use managed installation</Button>}
                <Button disabled={dis || !st?.exe} onClick={() => run(() => video2xApi.verify(), s => s.verification?.ok ? 'Video2X verified: test job passed' : `Verification failed: ${s.diagnostic || s.verification?.message || ''}`)}>Verify installation</Button>
                {st?.managed && <Button kind="danger" disabled={dis} onClick={() => { if (confirm(`Remove the Video2X installation in ${st.managed!.dir}? (A selected executable elsewhere is not touched.)`)) void run(() => video2xApi.remove()); }}>Remove managed installation</Button>}
                <Button small disabled={dis} onClick={() => refreshVideo2x(true)}>Re-check</Button>
                <Button small onClick={() => video2xApi.openLogs()}>Open logs</Button>
                <Button small disabled={dis} onClick={() => { if (confirm('Reset Video2X settings to default? This forgets a selected executable and all verification results; installed files and shaders are kept.')) void run(() => video2xApi.reset()); }}>Reset to default</Button>
            </div>
            <Video2xShaderLibrary />
            <p className="hint">
                Video2X is licensed under the GNU AGPL-3.0 and is not part of UVE Manager: it is downloaded on request from its official GitHub release
                (pinned version, SHA-256 checked) and run as a separate program. The Anime4K shaders are MIT-licensed (github.com/bloc97/Anime4K).{' '}
                <button className="link-btn inline" onClick={() => video2xApi.openSource()}>Source code: github.com/k4yt3x/video2x</button>
            </p>
        </Section>
    );
}
