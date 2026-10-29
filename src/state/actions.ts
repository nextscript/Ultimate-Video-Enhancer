import { zipSync, strToU8 } from 'fflate';
import { api } from '../api';
import { APP_VERSION, DEFAULT_EXPORT, UVE_SOURCE, type EffectState, type JobSpec, type UserProfile, type UveProject } from '../../shared/types';
import { runAutoSceneMatch, samplesFromAnalysis, type AnalysisSample } from '../../shared/uve/auto';
import { type CustomFilterEntry, exportFilterCodesJson, normalizeEntry, parseFilterCodesJson } from '../../shared/uve/customFilters';
import { expertDiff, expertFromDiff } from '../../shared/uve/expert';
import { type LutProfile, cubeTextToMatrix4x5, lutKeyOf, lutProfileToJson, parseLutProfileObject, tiledRgbaToMatrix4x5 } from '../../shared/uve/lut';
import { mergeProfiles, newProfileId, parseUserProfileObject, sanitizeProfileFilename } from '../../shared/uve/profiles';
import { DEFAULT_SETTINGS, type ToneProfile, type UveSettings, sanitizeSettings, settingsToGvfSettings } from '../../shared/uve/settings';
import { getState, initialEffect, resetHistory, setState, toast, updateEffect } from './store';

const errMsg = (e: unknown) => (e instanceof Error ? e.message.replace(/^Error invoking remote method '[^']+': (Error: )?/, '') : String(e));
export { errMsg };

// ------------------------------------------------------------------ settings helpers
export function setSetting<K extends keyof UveSettings>(key: K, value: UveSettings[K], coalesce = true) {
    updateEffect(e => ({ ...e, settings: { ...e.settings, [key]: value } }), coalesce ? `s:${String(key)}` : '');
}

export function toggleSetting(key: 'enabled' | 'darkMoody' | 'tealOrange' | 'vibrantSat' | 'autoOn' | 'autoLockWB') {
    const v = !getState().effect.settings[key];
    setSetting(key, v, false);
    const names: Record<string, string> = { enabled: 'Base Tone', darkMoody: 'Dark & Moody', tealOrange: 'Teal & Orange', vibrantSat: 'Vibrant', autoOn: 'Auto-Scene-Match', autoLockWB: 'Auto White Balance' };
    toast(`${names[key]} ${v ? 'on' : 'off'}`, 'info', 1500);
}

/** HDR toggle like Ctrl+Alt+P: remembers the last strength (GVF_HDR_SETTINGS.useLastValue). */
let hdrLast = 0.3;
export function toggleHdr() {
    const cur = getState().effect.settings.hdr;
    if (cur !== 0) { hdrLast = cur; setSetting('hdr', 0, false); toast('HDR off', 'info', 1500); }
    else { setSetting('hdr', hdrLast || 0.3, false); toast(`HDR on (${(hdrLast || 0.3).toFixed(2)})`, 'info', 1500); }
}

const PROFILE_CYCLE: ToneProfile[] = ['off', 'film', 'anime', 'gaming', 'eyecare', 'user'];
export function cycleToneProfile() {
    const cur = getState().effect.settings.profile;
    const next = PROFILE_CYCLE[(PROFILE_CYCLE.indexOf(cur) + 1) % PROFILE_CYCLE.length];
    setSetting('profile', next, false);
    toast(`Profile: ${next}`, 'info', 1500);
}

// ------------------------------------------------------------------ media
export async function loadVideo(path: string, opts: { keepEffect?: boolean } = {}) {
    try {
        setState({ mediaError: null, proxyPct: null });
        const media = await api.probe(path);
        if (!media.video) throw new Error(media.warnings[0] || 'No video stream found.');
        if (media.encrypted) throw new Error('This file appears to be DRM-protected. UVE Manager does not process protected content.');
        const url = await api.mediaUrl(path);
        setState({
            media, mediaUrl: url, thumbs: [], time: 0, playing: false, inPoint: null, outPoint: null,
            analysis: { status: 'none', pct: 0, keys: null }, dirty: opts.keepEffect ? getState().dirty : getState().dirty
        });
        media.warnings.forEach(w => toast(w, 'warn', 7000));
        void api.thumbs(media, 24).then(thumbs => { if (getState().media?.path === path) setState({ thumbs }); }).catch(() => undefined);
        void api.setTitle(`UVE Manager ${APP_VERSION} – ${media.name}`);
        if (getState().effect.settings.autoOn) void runAnalysis();
    } catch (e) {
        setState({ mediaError: errMsg(e) });
        toast(`Cannot open video: ${errMsg(e)}`, 'error', 8000);
    }
}

/** Called by the player when Chromium cannot decode the file: builds a preview proxy. */
export async function requestProxy() {
    const m = getState().media;
    if (!m || getState().proxyPct !== null) return;
    setState({ proxyPct: 0 });
    toast('This format cannot be previewed directly – creating a preview proxy (export always uses the original file).', 'info', 7000);
    try {
        const url = await api.proxy(m);
        if (getState().media?.path === m.path) setState({ mediaUrl: url, proxyPct: null });
    } catch (e) {
        setState({ proxyPct: null, mediaError: `Preview not possible: ${errMsg(e)}` });
    }
}

let samplesCache: { key: string; samples: AnalysisSample[] } | null = null;

export async function runAnalysis() {
    const m = getState().media;
    if (!m) return;
    if (getState().analysis.status === 'running') return;
    setState({ analysis: { ...getState().analysis, status: 'running', pct: 0 } });
    try {
        const r = await api.analyze(m);
        if (getState().media?.path !== m.path) return;
        samplesCache = { key: r.key, samples: samplesFromAnalysis(r) };
        recomputeAuto();
    } catch (e) {
        setState({ analysis: { status: 'error', pct: 0, keys: null, error: errMsg(e) } });
    }
}

/** Re-runs the auto state machine (fast) after strength / WB-lock changes. */
export function recomputeAuto() {
    if (!samplesCache) return;
    const s = getState().effect.settings;
    const keys = runAutoSceneMatch(samplesCache.samples, { strength: s.autoStrength, lockWB: s.autoLockWB });
    setState({ analysis: { status: 'ready', pct: 1, keys, samplesKey: samplesCache.key } });
}

// ------------------------------------------------------------------ project
export function buildProject(): UveProject {
    const s = getState();
    return {
        schema: 'uve-project', version: 1, app: `UVE Manager ${APP_VERSION}`, uveSource: UVE_SOURCE, savedAt: new Date().toISOString(),
        media: s.media ? { path: s.media.path, size: s.media.size, mtimeMs: s.media.mtimeMs, videoStream: s.media.video?.index ?? 0, audioStream: s.exportSettings.audioTrack } : null,
        effect: s.effect, inPoint: s.inPoint, outPoint: s.outPoint, export: s.exportSettings, ui: { wipe: s.wipe, compare: s.compare }
    };
}

export function effectFromUnknown(e: unknown): EffectState {
    const o = (e && typeof e === 'object' ? e : {}) as Record<string, unknown>;
    const lut = o.lut ? parseLutProfileObject(o.lut, 'user') : null;
    return {
        settings: sanitizeSettings(o.settings),
        expert: expertFromDiff(o.expert),
        customFilters: Array.isArray(o.customFilters) ? o.customFilters.map(normalizeEntry).filter((x): x is CustomFilterEntry => !!x) : [],
        lut
    };
}

export async function saveProject(saveAs = false) {
    try {
        const p = await api.saveProject(getState().projectPath, buildProject(), saveAs);
        if (p) { setState({ projectPath: p, dirty: false }); toast(`Project saved: ${p}`, 'ok'); }
    } catch (e) { toast(`Save failed: ${errMsg(e)}`, 'error'); }
}

export async function openProject(path?: string) {
    try {
        const r = await api.openProject(path);
        if (!r) return;
        const pr = r.project;
        const effect = effectFromUnknown(pr.effect);
        setState({
            effect, projectPath: r.path, dirty: false, exportSettings: { ...DEFAULT_EXPORT, ...(pr.export || {}) },
            inPoint: pr.inPoint ?? null, outPoint: pr.outPoint ?? null, wipe: pr.ui?.wipe ?? 0.5, compare: pr.ui?.compare ?? true
        });
        resetHistory();
        checkLutAvailability(effect);
        if (pr.media?.path) {
            let mediaPath: string | null = pr.media.path;
            if (!r.mediaOk) {
                toast(`The project video was not found: ${pr.media.path}. Please locate it.`, 'warn', 9000);
                mediaPath = await api.locateMedia();
            }
            if (mediaPath) {
                const keepIn = pr.inPoint ?? null, keepOut = pr.outPoint ?? null;
                await loadVideo(mediaPath, { keepEffect: true });
                setState({ inPoint: keepIn, outPoint: keepOut, dirty: !r.mediaOk });
            }
        }
        toast(`Project opened: ${r.path}`, 'ok');
    } catch (e) { toast(`Cannot open project: ${errMsg(e)}`, 'error'); }
}

export function newProject() {
    setState({ effect: initialEffect(), projectPath: null, dirty: false, media: null, mediaUrl: null, thumbs: [], inPoint: null, outPoint: null, analysis: { status: 'none', pct: 0, keys: null } });
    resetHistory();
}

// ------------------------------------------------------------------ LUTs
export function checkLutAvailability(effect: EffectState) {
    const key = effect.settings.lutProfile;
    if (!key || key === 'none') return;
    const lib = getState().luts.profiles.find(p => lutKeyOf(p) === key);
    if (!lib && !effect.lut) toast(`LUT "${key.replace('||', ' / ')}" is missing. Import it in the LUT Library – until then it is NOT applied.`, 'warn', 10000);
}

export function selectLut(key: string) {
    const p = getState().luts.profiles.find(x => lutKeyOf(x) === key) || null;
    updateEffect(e => ({ ...e, settings: { ...e.settings, lutProfile: p ? key : 'none' }, lut: p ? { ...p } : null }), '');
}

async function decodePng(data: Uint8Array): Promise<{ rgba: Uint8ClampedArray; w: number; h: number }> {
    const bmp = await createImageBitmap(new Blob([data as BlobPart], { type: 'image/png' }), { colorSpaceConversion: 'none', premultiplyAlpha: 'none' });
    const c = new OffscreenCanvas(bmp.width, bmp.height);
    const ctx = c.getContext('2d', { willReadFrequently: true, colorSpace: 'srgb' })!;
    ctx.drawImage(bmp, 0, 0);
    return { rgba: ctx.getImageData(0, 0, bmp.width, bmp.height).data, w: bmp.width, h: bmp.height };
}

/** Imports .cube / PNG / .json / .zip LUTs into the library (as UVE 4x5 matrix profiles). */
export async function importLutFiles(group: string, opts: { linearizeIn?: boolean; delinearizeOut?: boolean; flipY?: boolean } = {}) {
    const files = await api.openFiles('Import LUTs', [{ name: 'LUT files', extensions: ['cube', 'png', 'json', 'zip'] }], true, 100 * 1024 * 1024);
    if (!files.length) return;
    const out: LutProfile[] = [];
    const errors: string[] = [];
    const now = Date.now();
    const handle = async (name: string, data: Uint8Array) => {
        const ext = name.toLowerCase().split('.').pop();
        const base = name.replace(/^.*[\\/]/, '').replace(/\.[^.]+$/, '');
        try {
            if (ext === 'cube') {
                const r = cubeTextToMatrix4x5(new TextDecoder().decode(data), opts);
                out.push({ name: base, group, matrix4x5: r.matrix4x5, createdAt: now, updatedAt: now, source: 'user', origin: name });
            } else if (ext === 'png') {
                const img = await decodePng(data);
                out.push({ name: base, group, matrix4x5: tiledRgbaToMatrix4x5(img.rgba, img.w, img.h, opts), createdAt: now, updatedAt: now, source: 'user', origin: name });
            } else if (ext === 'json') {
                const obj = JSON.parse(new TextDecoder().decode(data));
                const list = Array.isArray(obj) ? obj : Array.isArray(obj?.profiles) ? obj.profiles : [obj];
                if (obj?.schema === 'gvf-lut-groups') return;
                let any = false;
                for (const it of list) { const p = parseLutProfileObject(it, 'user'); if (p) { out.push(p); any = true; } }
                if (!any) errors.push(`${name}: no gvf-lut-profile found`);
            } else if (ext === 'zip') {
                for (const e of await api.unzip(data)) await handle(e.name, e.data);
            }
        } catch (e) { errors.push(`${name}: ${errMsg(e)}`); }
    };
    for (const f of files) await handle(f.name, f.data);
    if (out.length) setState({ luts: await api.saveLuts(out) });
    toast(`${out.length} LUT(s) imported${errors.length ? `, ${errors.length} failed: ${errors.slice(0, 3).join('; ')}` : ''}`, errors.length ? 'warn' : 'ok', 8000);
}

export async function exportLuts(keys: string[]) {
    const list = getState().luts.profiles.filter(p => keys.includes(lutKeyOf(p)));
    if (!list.length) return;
    if (list.length === 1) {
        const p = list[0];
        await api.saveFile('Export LUT', `${sanitizeProfileFilename(p.group ? p.group + '__' + p.name : p.name)}.json`, [{ name: 'JSON', extensions: ['json'] }], JSON.stringify(lutProfileToJson(p), null, 2));
        return;
    }
    const files: Record<string, Uint8Array> = {};
    const groups = [...new Set(list.map(p => p.group))];
    files['_lut_groups.json'] = strToU8(JSON.stringify({ schema: 'gvf-lut-groups', ver: 1, groups, exportedAt: Date.now() }, null, 2));
    list.forEach(p => { files[`${sanitizeProfileFilename(`${p.group}__${p.name}`)}.json`] = strToU8(JSON.stringify(lutProfileToJson(p), null, 2)); });
    await api.saveFile('Export LUTs', 'UVE_LUT_Profiles.zip', [{ name: 'ZIP', extensions: ['zip'] }], zipSync(files));
}

// ------------------------------------------------------------------ user profiles
export async function persistProfiles() {
    const s = getState();
    await api.saveProfiles({ profiles: s.profiles, activeId: s.activeProfileId });
}

export function profileFromCurrent(name: string, id = newProfileId()): UserProfile {
    const e = getState().effect;
    const now = Date.now();
    return {
        id, name: sanitizeProfileFilename(name), createdAt: now, updatedAt: now, settings: { ...e.settings },
        desktop: { expert: expertDiff(e.expert), customFilters: e.customFilters, lut: e.lut }
    };
}

export function applyProfile(p: UserProfile) {
    updateEffect(e => ({
        settings: sanitizeSettings(p.settings, DEFAULT_SETTINGS),
        expert: p.desktop?.expert ? expertFromDiff(p.desktop.expert) : e.expert,
        customFilters: p.desktop?.customFilters ? p.desktop.customFilters.map(normalizeEntry).filter((x): x is CustomFilterEntry => !!x) : e.customFilters,
        lut: p.desktop?.lut ?? (getState().luts.profiles.find(l => lutKeyOf(l) === p.settings.lutProfile) || null)
    }), '');
    setState({ activeProfileId: p.id });
    void persistProfiles();
    checkLutAvailability(getState().effect);
    if (getState().effect.settings.autoOn) { if (getState().analysis.status === 'ready') recomputeAuto(); else void runAnalysis(); }
}

export async function importProfiles() {
    const files = await api.openFiles('Import profiles', [{ name: 'Profiles', extensions: ['json', 'zip'] }], true, 20 * 1024 * 1024);
    const incoming: UserProfile[] = [];
    const errors: string[] = [];
    const handle = async (name: string, data: Uint8Array) => {
        if (name.toLowerCase().endsWith('.zip')) { for (const e of await api.unzip(data)) await handle(e.name, e.data); return; }
        if (!name.toLowerCase().endsWith('.json')) return;
        try {
            const obj = JSON.parse(new TextDecoder().decode(data));
            const list = Array.isArray(obj) ? obj : [obj];
            for (const it of list) {
                const p = parseUserProfileObject(it, name.replace(/^.*[\\/]/, '').replace(/\.json$/i, ''));
                if (p) incoming.push(p); else errors.push(`${name}: not a UVE profile`);
            }
        } catch (e) { errors.push(`${name}: ${errMsg(e)}`); }
    };
    for (const f of files) await handle(f.name, f.data);
    if (!incoming.length) { if (files.length) toast(`No profiles imported. ${errors.slice(0, 3).join('; ')}`, 'warn'); return; }
    const r = mergeProfiles(getState().profiles, incoming);
    setState({ profiles: r.list });
    await persistProfiles();
    toast(`Profiles: ${r.added} added, ${r.replaced} replaced${errors.length ? `, ${errors.length} skipped` : ''}`, 'ok');
}

export async function exportProfiles(ids: string[], format: 'zip' | 'json' | 'gvf-settings') {
    const list = getState().profiles.filter(p => ids.includes(p.id));
    if (!list.length) return;
    if (format === 'gvf-settings') {
        await api.saveFile('Export settings (userscript format)', `${sanitizeProfileFilename(list[0].name)}.json`, [{ name: 'JSON', extensions: ['json'] }], JSON.stringify(settingsToGvfSettings(list[0].settings), null, 2));
        return;
    }
    if (format === 'json' && list.length === 1) {
        await api.saveFile('Export profile', `${sanitizeProfileFilename(list[0].name)}.json`, [{ name: 'JSON', extensions: ['json'] }], JSON.stringify(list[0], null, 2));
        return;
    }
    const files: Record<string, Uint8Array> = {};
    list.forEach(p => { files[`${sanitizeProfileFilename(p.name)}.json`] = strToU8(JSON.stringify(p, null, 2)); });
    const d = new Date();
    await api.saveFile('Export profiles', `UVE_Profiles_${d.getFullYear()}${String(d.getMonth() + 1).padStart(2, '0')}${String(d.getDate()).padStart(2, '0')}.zip`, [{ name: 'ZIP', extensions: ['zip'] }], zipSync(files));
}

// ------------------------------------------------------------------ custom filters
export async function importFilterCodes() {
    const files = await api.openFiles('Import custom filters', [{ name: 'Filter codes', extensions: ['json'] }], true, 10 * 1024 * 1024);
    let added = 0;
    for (const f of files) {
        try {
            const list = parseFilterCodesJson(new TextDecoder().decode(f.data));
            updateEffect(e => {
                const cur = e.customFilters.slice();
                for (const n of list) {
                    const i = cur.findIndex(c => c.label === n.label);
                    if (i >= 0) cur[i] = { ...n, id: cur[i].id }; else cur.push(cur.some(c => c.id === n.id) ? { ...n, id: n.id + '_' + Date.now() } : n);
                    added++;
                }
                return { ...e, customFilters: cur };
            });
        } catch (e) { toast(`${f.name}: ${errMsg(e)}`, 'error'); }
    }
    if (added) toast(`${added} custom filter(s) imported`, 'ok');
}

export async function exportFilterCodes(entries: CustomFilterEntry[]) {
    if (!entries.length) return;
    await api.saveFile('Export custom filters', 'gvf_filter_codes.json', [{ name: 'JSON', extensions: ['json'] }], JSON.stringify(exportFilterCodesJson(entries), null, 2));
}

// ------------------------------------------------------------------ rendering
export function profileLabel(): string {
    const s = getState();
    return s.profiles.find(p => p.id === s.activeProfileId)?.name || 'Custom';
}

export async function enqueueCurrent() {
    const s = getState();
    if (!s.media) { toast('Open a video first.', 'warn'); return; }
    if (!s.caps?.ok) { toast(s.caps?.error || 'FFmpeg is not available.', 'error'); return; }
    try {
        const range = s.exportSettings.useInOut && (s.inPoint !== null || s.outPoint !== null)
            ? { start: s.inPoint ?? 0, end: s.outPoint ?? s.media.duration } : { start: 0, end: s.media.duration };
        if (range.end - range.start <= 0) throw new Error('The In/Out range is empty.');
        const output = await api.outputPath(s.media, s.exportSettings, profileLabel());
        // global bypass is a preview-only comparison aid; the job always renders the edited look
        const effect: EffectState = JSON.parse(JSON.stringify(s.effect));
        const spec: JobSpec = {
            id: 'job_' + Date.now() + '_' + Math.random().toString(36).slice(2, 6), name: s.media.name, input: s.media.path, output,
            media: s.media, effect, autoKeys: effect.settings.autoOn && s.analysis.status === 'ready' ? s.analysis.keys : null,
            exportSettings: { ...s.exportSettings }, range, parityMode: !!s.prefs?.parityMode
        };
        await api.enqueue(spec);
        toast(`Added to render queue → ${output}`, 'ok', 6000);
    } catch (e) { toast(`Cannot start rendering: ${errMsg(e)}`, 'error', 8000); }
}

export async function enqueueBatch(paths: string[], profile: UserProfile | null) {
    const s = getState();
    let ok = 0;
    for (const p of paths) {
        try {
            const media = await api.probe(p);
            if (!media.video) throw new Error('no video stream');
            const effect: EffectState = profile ? {
                settings: sanitizeSettings(profile.settings),
                expert: profile.desktop?.expert ? expertFromDiff(profile.desktop.expert) : s.effect.expert,
                customFilters: profile.desktop?.customFilters ?? s.effect.customFilters,
                lut: profile.desktop?.lut ?? (s.luts.profiles.find(l => lutKeyOf(l) === profile.settings.lutProfile) || null)
            } : JSON.parse(JSON.stringify(s.effect));
            const output = await api.outputPath(media, s.exportSettings, profile?.name || profileLabel());
            await api.enqueue({
                id: 'job_' + Date.now() + '_' + Math.random().toString(36).slice(2, 6), name: media.name, input: p, output, media, effect,
                autoKeys: null, exportSettings: { ...s.exportSettings, useInOut: false }, range: { start: 0, end: media.duration }, parityMode: !!s.prefs?.parityMode
            });
            ok++;
        } catch (e) { toast(`${p}: ${errMsg(e)}`, 'error', 8000); }
    }
    if (ok) toast(`${ok} file(s) added to the render queue`, 'ok');
}
