import { constants, existsSync, promises as fsp } from 'node:fs';
import path from 'node:path';
import { app } from 'electron';
import { APP_VERSION, type AppPrefs, DEFAULT_EXPORT, DEFAULT_UPDATE_REPO } from '../shared/types';
import { DEFAULT_SHORTCUTS, defaultShortcuts, mergeShortcuts } from '../shared/shortcuts';
import { isWritableDir, readJson, writeJson } from './util';

export { DEFAULT_SHORTCUTS };

export const DEFAULT_PREFS: AppPrefs = {
    ffmpegDir: '',
    gpuEngine: 'auto',
    hwDecode: 'auto',
    autoUpdate: true,
    updateRepo: DEFAULT_UPDATE_REPO,
    skippedVersions: {},
    autoDownloadLuts: true,
    previewQuality: 'full',
    parityMode: false,
    lastOutputDir: '',
    recentProjects: [],
    recentFiles: [],
    shortcuts: defaultShortcuts(process.platform),
    defaultExport: { ...DEFAULT_EXPORT }
};

export interface WindowState { x?: number; y?: number; width: number; height: number; maximized: boolean; fullscreen: boolean }

/** settings.json: everything the user chose, so nothing has to be selected again after a restart. */
export interface SettingsFile {
    schema: 'uve-manager-settings';
    version: 1;
    app: string;
    savedAt: string;
    prefs: AppPrefs;
    window: WindowState;
    /** Last editor session (effect settings, export settings, open video, view/tab, wipe, …) written by the renderer */
    editor: Record<string, unknown> | null;
}

export const userDir = () => app.getPath('userData');
export const settingsFile = () => path.join(userDir(), 'settings.json');
export const cacheDir = () => path.join(userDir(), 'cache');

/** Folder that contains the application: next to the portable/installed exe, or the project root in dev. */
export function appRootDir(): string {
    if (!app.isPackaged) return path.resolve(__dirname, '..');
    if (process.env.PORTABLE_EXECUTABLE_DIR) return process.env.PORTABLE_EXECUTABLE_DIR;
    return path.dirname(app.getPath('exe'));
}

/** Portable copy (portable.txt next to the exe, or the self-extracting portable exe) – everything lives next to it. */
export function isPortableCopy(): boolean {
    if (!app.isPackaged) return false;
    return !!process.env.PORTABLE_EXECUTABLE_DIR || existsSync(path.join(path.dirname(app.getPath('exe')), 'portable.txt'));
}

/**
 * v1.0.5: tools folder of an installed copy. It lies outside the program folder, because an installer update
 * removes that folder – FFmpeg and REAL-Video-Enhancer (several GB) would have to be downloaded again.
 * Windows: %LOCALAPPDATA%\UVE Manager\tools (local, not roaming); elsewhere <userData>/tools.
 */
export function installedToolsDir(): string {
    const local = process.platform === 'win32' ? process.env.LOCALAPPDATA : undefined;
    return local ? path.join(local, 'UVE Manager', 'tools') : path.join(userDir(), 'tools');
}

let toolsDirCache: Promise<string> | null = null;
/**
 * Tools folder (FFmpeg, REAL-Video-Enhancer), resolved once. Development and portable copies: tools/ next to
 * the app (userData/tools when that is not writable). Installed copies: `installedToolsDir()`; tools that an
 * older version kept in the program folder or in userData are moved there once.
 */
export function toolsDir(): Promise<string> {
    if (!toolsDirCache) {
        if (!app.isPackaged || isPortableCopy()) {
            const preferred = path.join(appRootDir(), 'tools');
            toolsDirCache = isWritableDir(preferred).then(ok => ok ? preferred : path.join(userDir(), 'tools'));
        } else {
            toolsDirCache = adoptLegacyTools(installedToolsDir(), [path.join(appRootDir(), 'tools'), path.join(userDir(), 'tools')]);
        }
    }
    return toolsDirCache;
}

/** Tool folders an older version may have downloaded into a legacy tools folder. */
const ADOPTED_TOOLS = ['rve', 'ffmpeg'];

/**
 * Moves each tool folder (rve, ffmpeg) that is missing in `target` from the first legacy folder that has it.
 * A folder that cannot be moved (read-only program folder, another drive) stays where it is; `target` is
 * always the folder used, so downloads never go to a read-only place. FFmpeg shipped inside the program
 * folder is still found by the tools lookup (electron/tools.ts).
 */
async function adoptLegacyTools(target: string, legacy: string[]): Promise<string> {
    for (const name of ADOPTED_TOOLS) {
        const dest = path.join(target, name);
        if (existsSync(dest)) continue;
        for (const old of legacy) {
            const src = path.join(old, name);
            if (path.resolve(old) === path.resolve(target) || !existsSync(src)) continue;
            try {
                await fsp.mkdir(target, { recursive: true });
                await fsp.rename(src, dest);
                break;
            } catch { /* keep it where it is */ }
        }
    }
    return target;
}

const DEFAULT_WINDOW: WindowState = { width: 1600, height: 1000, maximized: false, fullscreen: false };

let settings: SettingsFile | null = null;

function normalizePrefs(raw: Partial<AppPrefs> | undefined): AppPrefs {
    const r = raw || {};
    return {
        ...DEFAULT_PREFS, ...r,
        // the pre-1.0.2 default repository never had releases
        ...(r.updateRepo === 'nextscript/UVE-Manager' ? { updateRepo: DEFAULT_UPDATE_REPO } : {}),
        shortcuts: mergeShortcuts(process.platform, r.shortcuts),
        defaultExport: { ...DEFAULT_EXPORT, ...(r.defaultExport || {}) },
        skippedVersions: { ...(r.skippedVersions || {}) }
    };
}

/** Loads settings.json; on first start migrates the older prefs.json / session.json files. */
export async function getSettings(): Promise<SettingsFile> {
    if (settings) return settings;
    const raw = await readJson<Partial<SettingsFile> | null>(settingsFile(), null);
    if (raw && raw.schema === 'uve-manager-settings' && raw.app && raw.app !== APP_VERSION) {
        // additive migration: keep an untouched copy of the previous version's settings (rollback)
        const backup = path.join(userDir(), `settings.backup-v${String(raw.app).replace(/[^\w.-]/g, '_')}.json`);
        await fsp.copyFile(settingsFile(), backup, constants.COPYFILE_EXCL).catch(() => undefined);
    }
    if (raw && raw.schema === 'uve-manager-settings') {
        settings = {
            schema: 'uve-manager-settings', version: 1, app: APP_VERSION, savedAt: String(raw.savedAt || ''),
            prefs: normalizePrefs(raw.prefs),
            window: { ...DEFAULT_WINDOW, ...(raw.window || {}) },
            editor: raw.editor && typeof raw.editor === 'object' ? raw.editor : null
        };
        return settings;
    }
    const oldPrefs = await readJson<Partial<AppPrefs> | undefined>(path.join(userDir(), 'prefs.json'), undefined);
    const oldSession = await readJson<Record<string, unknown> | null>(path.join(userDir(), 'session.json'), null);
    settings = {
        schema: 'uve-manager-settings', version: 1, app: APP_VERSION, savedAt: '',
        prefs: normalizePrefs(oldPrefs), window: { ...DEFAULT_WINDOW }, editor: oldSession
    };
    await saveSettings();
    if (oldPrefs) await fsp.rm(path.join(userDir(), 'prefs.json'), { force: true }).catch(() => undefined);
    if (oldSession) await fsp.rm(path.join(userDir(), 'session.json'), { force: true }).catch(() => undefined);
    return settings;
}

let writing: Promise<void> = Promise.resolve();
/** Serialized write of settings.json (atomic temp file + rename). */
export function saveSettings(): Promise<void> {
    writing = writing.then(async () => {
        if (!settings) return;
        settings.savedAt = new Date().toISOString();
        settings.app = APP_VERSION;
        await writeJson(settingsFile(), settings);
    }).catch(() => undefined);
    return writing;
}

export async function getPrefs(): Promise<AppPrefs> {
    return (await getSettings()).prefs;
}

export async function setPrefs(patch: Partial<AppPrefs>): Promise<AppPrefs> {
    const s = await getSettings();
    s.prefs = { ...s.prefs, ...patch };
    await saveSettings();
    return s.prefs;
}

export async function getWindowState(): Promise<WindowState> {
    return (await getSettings()).window;
}

export async function setWindowState(w: WindowState): Promise<void> {
    const s = await getSettings();
    s.window = w;
    await saveSettings();
}

export async function getEditorState(): Promise<Record<string, unknown> | null> {
    return (await getSettings()).editor;
}

export async function setEditorState(e: Record<string, unknown> | null): Promise<void> {
    const s = await getSettings();
    s.editor = e;
    await saveSettings();
}

export async function pushRecent(kind: 'recentProjects' | 'recentFiles', p: string) {
    const cur = await getPrefs();
    const list = [p, ...cur[kind].filter(x => x.toLowerCase() !== p.toLowerCase())].slice(0, 12);
    await setPrefs({ [kind]: list } as Partial<AppPrefs>);
}
