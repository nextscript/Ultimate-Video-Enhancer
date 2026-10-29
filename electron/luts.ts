// LUT library: user LUTs + the LUT profile package of the UVE repository
// (LUTsProfiles_v3.0.zip), downloaded automatically on first start.
import path from 'node:path';
import { LUT_PACKAGE, type LutLibraryState, UVE_SOURCE } from '../shared/types';
import { type LutProfile, lutKeyOf, parseLutProfileObject } from '../shared/uve/lut';
import { getPrefs, userDir } from './store';
import { compareVersions, download, fetchJson, readJson, safeUnzip, sha256, writeJson } from './util';

interface RepoStore { file: string; blobSha?: string; sha256?: string; downloadedAt?: number; profiles: LutProfile[]; groups: string[] }
interface UserStore { profiles: LutProfile[]; groups: string[]; hidden: string[] }

const lutDir = () => path.join(userDir(), 'luts');
const repoFile = () => path.join(lutDir(), 'repo.json');
const userFile = () => path.join(lutDir(), 'user.json');

let repoStatus: LutLibraryState['repo'] = { status: 'idle', message: '' };
let listeners: ((s: LutLibraryState) => void)[] = [];
export function onLutChange(cb: (s: LutLibraryState) => void) { listeners.push(cb); }

async function readRepo() { return readJson<RepoStore>(repoFile(), { file: '', profiles: [], groups: [] }); }
async function readUser() { return readJson<UserStore>(userFile(), { profiles: [], groups: [], hidden: [] }); }

export async function lutLibrary(): Promise<LutLibraryState> {
    const [repo, user] = await Promise.all([readRepo(), readUser()]);
    const hidden = new Set(user.hidden);
    const map = new Map<string, LutProfile>();
    repo.profiles.forEach(p => { if (!hidden.has(lutKeyOf(p))) map.set(lutKeyOf(p), { ...p, source: 'repo' }); });
    user.profiles.forEach(p => map.set(lutKeyOf(p), { ...p, source: 'user' }));
    const profiles = [...map.values()].sort((a, b) => (a.group || '').localeCompare(b.group || '') || a.name.localeCompare(b.name));
    const groups = [...new Set([...repo.groups, ...user.groups, ...profiles.map(p => p.group)].filter(g => g !== undefined))].sort((a, b) => a.localeCompare(b));
    return {
        profiles, groups,
        repo: { ...repoStatus, sha256: repo.sha256, downloadedAt: repo.downloadedAt, count: repo.profiles.length }
    };
}

async function emit() {
    const s = await lutLibrary();
    listeners.forEach(cb => cb(s));
}

/** Parses the LUT package ZIP (gvf-lut-profile JSON files + _lut_groups.json). */
export function parseLutPackage(zip: Uint8Array): { profiles: LutProfile[]; groups: string[]; skipped: number } {
    const entries = safeUnzip(zip, { maxEntries: 5000, maxEntryBytes: 2 * 1024 * 1024, maxTotalBytes: 200 * 1024 * 1024, filter: n => n.toLowerCase().endsWith('.json') });
    const profiles: LutProfile[] = [];
    let groups: string[] = [];
    let skipped = 0;
    for (const e of entries) {
        let obj: unknown;
        try { obj = JSON.parse(Buffer.from(e.data).toString('utf8')); } catch { skipped++; continue; }
        const o = obj as Record<string, unknown>;
        if (o && o.schema === 'gvf-lut-groups' && Array.isArray(o.groups)) { groups = o.groups.map(String).slice(0, 500); continue; }
        const list = Array.isArray(obj) ? obj : (o && Array.isArray(o.profiles) ? o.profiles as unknown[] : [obj]);
        for (const item of list) {
            const p = parseLutProfileObject(item, 'repo');
            if (p) profiles.push(p); else skipped++;
        }
    }
    return { profiles, groups, skipped };
}

let downloading: Promise<void> | null = null;

/** Downloads LUTsProfiles_v3.0.zip from the UVE repository (auto on first start, or on demand). */
export function downloadRepoLuts(force = false): Promise<void> {
    if (downloading) return downloading;
    downloading = (async () => {
        try {
            const repo = await readRepo();
            if (!force && repo.profiles.length) { repoStatus = { status: 'ok', message: `${repo.profiles.length} LUT profiles from ${repo.file}` }; return; }
            repoStatus = { status: 'downloading', message: `Downloading ${LUT_PACKAGE.file}…` };
            await emit();
            let zip: Uint8Array;
            try {
                zip = await download(LUT_PACKAGE.url, { maxBytes: 100 * 1024 * 1024 });
            } catch {
                zip = await download(LUT_PACKAGE.fallbackUrl, { maxBytes: 100 * 1024 * 1024 });
            }
            const parsed = parseLutPackage(zip);
            if (!parsed.profiles.length) throw new Error('The package contains no valid gvf-lut-profile entries.');
            let blobSha: string | undefined;
            try { blobSha = (await remoteLutInfo()).blobSha; } catch { /* optional */ }
            await writeJson(repoFile(), {
                file: LUT_PACKAGE.file, blobSha, sha256: sha256(zip), downloadedAt: Date.now(), profiles: parsed.profiles, groups: parsed.groups
            } satisfies RepoStore);
            repoStatus = { status: 'ok', message: `${parsed.profiles.length} LUT profiles installed from ${LUT_PACKAGE.file}${parsed.skipped ? ` (${parsed.skipped} invalid entries skipped)` : ''}.` };
        } catch (e) {
            const msg = e instanceof Error ? e.message : String(e);
            repoStatus = { status: /fetch|network|ENOTFOUND|ERR_INTERNET|ERR_NAME/i.test(msg) ? 'offline' : 'error', message: `LUT download failed: ${msg}` };
        } finally {
            downloading = null;
            await emit();
        }
    })();
    return downloading;
}

export async function initLuts() {
    const prefs = await getPrefs();
    const repo = await readRepo();
    if (repo.profiles.length) repoStatus = { status: 'ok', message: `${repo.profiles.length} LUT profiles from ${repo.file}` };
    else if (prefs.autoDownloadLuts) void downloadRepoLuts(false);
}

/** Remote info for the update check: blob sha of v3.0 and the newest LUTsProfiles_v*.zip in the repository. */
export async function remoteLutInfo(): Promise<{ blobSha?: string; newestFile: string }> {
    const repoPath = UVE_SOURCE.repo.replace('https://github.com/', '');
    const list = await fetchJson<{ name: string; sha: string; type: string }[]>(`https://api.github.com/repos/${repoPath}/contents/`);
    const zips = list.filter(f => /^LUTsProfiles_v[\d.]+\.zip$/i.test(f.name));
    zips.sort((a, b) => compareVersions(b.name.replace(/^LUTsProfiles_v|\.zip$/gi, ''), a.name.replace(/^LUTsProfiles_v|\.zip$/gi, '')));
    return { blobSha: list.find(f => f.name === LUT_PACKAGE.file)?.sha, newestFile: zips[0]?.name || LUT_PACKAGE.file };
}

export async function repoLutStore() { return readRepo(); }

export async function saveUserLuts(incoming: LutProfile[]): Promise<LutLibraryState> {
    const user = await readUser();
    const map = new Map(user.profiles.map(p => [lutKeyOf(p), p]));
    for (const p of incoming) {
        const clean = parseLutProfileObject(p, 'user');
        if (!clean) continue;
        map.set(lutKeyOf(clean), { ...clean, updatedAt: Date.now() });
        user.hidden = user.hidden.filter(k => k !== lutKeyOf(clean));
        if (clean.group && !user.groups.includes(clean.group)) user.groups.push(clean.group);
    }
    user.profiles = [...map.values()];
    await writeJson(userFile(), user);
    await emit();
    return lutLibrary();
}

export async function deleteLuts(keys: string[]): Promise<LutLibraryState> {
    const user = await readUser();
    const repo = await readRepo();
    const repoKeys = new Set(repo.profiles.map(lutKeyOf));
    user.profiles = user.profiles.filter(p => !keys.includes(lutKeyOf(p)));
    for (const k of keys) if (repoKeys.has(k) && !user.hidden.includes(k)) user.hidden.push(k);
    await writeJson(userFile(), user);
    await emit();
    return lutLibrary();
}

export async function restoreHiddenLuts(): Promise<LutLibraryState> {
    const user = await readUser();
    user.hidden = [];
    await writeJson(userFile(), user);
    await emit();
    return lutLibrary();
}
