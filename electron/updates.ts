// Startup update check for UVE Manager itself, the portable tools (FFmpeg) and the UVE LUT package.
// The renderer shows a modal ("Update now" / "Later – keep current version"); nothing is
// downloaded or replaced without that confirmation.
import { spawn } from 'node:child_process';
import { existsSync, promises as fsp } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { app, shell } from 'electron';
import { APP_VERSION, LUT_PACKAGE, type UpdateCheckResult, type UpdateItem } from '../shared/types';
import { downloadRepoLuts, remoteLutInfo, repoLutStore } from './luts';
import { installRve, latestBackendRelease, rveDir } from './rve';
import { readJson } from './util';
import { getPrefs } from './store';
import { installFfmpeg, latestFfmpegRelease, toolsStatus } from './tools';
import { compareVersions, download, fetchJson } from './util';

interface GhRelease { tag_name: string; name: string; body: string; html_url: string; draft: boolean; prerelease: boolean; assets: { name: string; browser_download_url: string; size: number }[] }

let last: UpdateCheckResult | null = null;
let appRelease: GhRelease | null = null;

export async function checkUpdates(): Promise<UpdateCheckResult> {
    const prefs = await getPrefs();
    const items: UpdateItem[] = [];
    const errors: string[] = [];

    // 1. UVE Manager
    if (prefs.updateRepo && /^[\w.-]+\/[\w.-]+$/.test(prefs.updateRepo)) {
        try {
            const rel = await fetchJson<GhRelease>(`https://api.github.com/repos/${prefs.updateRepo}/releases/latest`);
            const latest = rel.tag_name.replace(/^v/i, '');
            if (!rel.draft && compareVersions(latest, APP_VERSION) > 0 && prefs.skippedVersions.app !== latest) {
                appRelease = rel;
                const asset = pickInstaller(rel);
                items.push({ id: 'app', label: 'UVE Manager', current: APP_VERSION, latest, notes: (rel.body || '').slice(0, 4000), downloadUrl: asset?.browser_download_url || rel.html_url, size: asset?.size });
            }
        } catch (e) {
            errors.push(`UVE Manager: ${msg(e)}`);
        }
    }

    // 2. Portable FFmpeg (Windows builds only; Linux/macOS use the system FFmpeg)
    if (process.platform === 'win32') try {
        const st = await toolsStatus();
        const rel = await latestFfmpegRelease();
        if (!st.installed) {
            items.push({ id: 'ffmpeg', label: 'FFmpeg (portable tools)', current: 'not installed', latest: rel.build.slice(0, 10), downloadUrl: rel.url, size: rel.size });
        } else if (st.build && st.build !== 'latest' && new Date(rel.build).getTime() > new Date(st.build).getTime() + 60_000
            && prefs.skippedVersions.ffmpeg !== rel.build) {
            items.push({ id: 'ffmpeg', label: 'FFmpeg (portable tools)', current: `${st.version} (${st.build.slice(0, 10)})`, latest: rel.build.slice(0, 10), downloadUrl: rel.url, size: rel.size });
        }
    } catch (e) {
        errors.push(`FFmpeg: ${msg(e)}`);
    }

    // 3. LUT profile package
    try {
        const store = await repoLutStore();
        const remote = await remoteLutInfo();
        const changed = store.blobSha && remote.blobSha && store.blobSha !== remote.blobSha;
        const newer = remote.newestFile !== LUT_PACKAGE.file;
        const marker = `${remote.blobSha}|${remote.newestFile}`;
        if ((!store.profiles.length || changed || newer) && prefs.skippedVersions.luts !== marker) {
            items.push({
                id: 'luts', label: 'UVE LUT profiles', current: store.profiles.length ? `${store.file} (${store.profiles.length} LUTs)` : 'not installed',
                latest: changed ? `${LUT_PACKAGE.file} (updated)` : remote.newestFile,
                notes: newer ? `A newer package (${remote.newestFile}) exists in the UVE repository. UVE Manager 1.0.0 installs ${LUT_PACKAGE.file}.` : undefined
            });
        }
    } catch (e) {
        errors.push(`LUT profiles: ${msg(e)}`);
    }

    // 4. REAL-Video-Enhancer backend (only when the AI upscaling tool is installed)
    try {
        const info = await readJson<{ backendVersion?: string }>(path.join(await rveDir(), 'rve-manager.json'), {});
        if (info.backendVersion) {
            const rel = await latestBackendRelease();
            if (rel && compareVersions(rel.version, info.backendVersion) > 0 && prefs.skippedVersions.rve !== rel.version) {
                items.push({ id: 'rve', label: 'REAL-Video-Enhancer backend (AI upscaling)', current: info.backendVersion, latest: rel.version, size: rel.size });
            }
        }
    } catch (e) {
        errors.push(`REAL-Video-Enhancer: ${msg(e)}`);
    }

    last = { checkedAt: Date.now(), items, errors };
    return last;
}

/** Folder of an extracted portable copy (portable.txt next to the exe), or null for installed copies. */
function portableDir(): string | null {
    if (process.env.PORTABLE_EXECUTABLE_FILE) return path.dirname(process.env.PORTABLE_EXECUTABLE_FILE);
    const dir = path.dirname(app.getPath('exe'));
    return app.isPackaged && existsSync(path.join(dir, 'portable.txt')) ? dir : null;
}

function pickInstaller(rel: GhRelease) {
    const portable = !!portableDir();
    const exes = rel.assets.filter(a => /\.exe$/i.test(a.name));
    return (portable ? exes.find(a => /portable/i.test(a.name)) : exes.find(a => /setup|nsis|installer/i.test(a.name) && !/portable/i.test(a.name)))
        || exes[0];
}

const msg = (e: unknown) => (e instanceof Error ? e.message : String(e));

export async function skipUpdate(id: UpdateItem['id']) {
    const prefs = await getPrefs();
    const item = last?.items.find(i => i.id === id);
    if (!item) return;
    const { setPrefs } = await import('./store');
    let marker = item.latest;
    if (id === 'luts') { try { const r = await remoteLutInfo(); marker = `${r.blobSha}|${r.newestFile}`; } catch { /* keep */ } }
    if (id === 'ffmpeg') { try { marker = (await latestFfmpegRelease()).build; } catch { /* keep */ } }
    await setPrefs({ skippedVersions: { ...prefs.skippedVersions, [id]: marker } });
}

/** Applies one update. For the app itself the installer is started and the app quits. */
export async function applyUpdate(id: UpdateItem['id'], progress: (m: string, pct: number) => void): Promise<{ ok: boolean; message: string; restart?: boolean }> {
    try {
        if (id === 'ffmpeg') {
            const st = await installFfmpeg(progress);
            return { ok: true, message: `FFmpeg ${st.version} installed in ${st.dir}` };
        }
        if (id === 'rve') {
            await installRve(progress, () => undefined, { reinstallBackend: true });
            return { ok: true, message: 'REAL-Video-Enhancer backend updated.' };
        }
        if (id === 'luts') {
            progress('Downloading LUT profiles…', 0);
            await downloadRepoLuts(true);
            return { ok: true, message: 'LUT profiles updated.' };
        }
        const rel = appRelease;
        if (!rel) return { ok: false, message: 'No update information – run the check again.' };
        const asset = pickInstaller(rel);
        if (!asset) { await shell.openExternal(rel.html_url); return { ok: true, message: 'The release page was opened in your browser.' }; }
        const data = await download(asset.browser_download_url, {
            maxBytes: 800 * 1024 * 1024,
            onProgress: (r, t) => progress(`Downloading ${asset.name}… ${(r / 1048576).toFixed(0)} MB`, t ? r / t : 0)
        });
        const pdir = portableDir();
        if (pdir) {
            const target = path.join(pdir, asset.name);
            await fsp.writeFile(target, data);
            shell.showItemInFolder(target);
            return { ok: true, message: `The new portable version was saved to ${target}. Close UVE Manager, start that file and choose this folder (${pdir}) – your settings in "data" and the tools are kept.` };
        }
        if (process.platform !== 'win32') { await shell.openExternal(rel.html_url); return { ok: true, message: 'The release page was opened in your browser – download the package for your system there.' }; }
        const file = path.join(os.tmpdir(), `uve-manager-update-${Date.now()}-${asset.name.replace(/[^\w.-]/g, '_')}`);
        await fsp.writeFile(file, data);
        spawn(file, [], { detached: true, stdio: 'ignore', windowsHide: false }).unref();
        setTimeout(() => app.quit(), 800);
        return { ok: true, message: 'The installer was started. UVE Manager closes now.', restart: true };
    } catch (e) {
        return { ok: false, message: msg(e) };
    }
}
