import { createReadStream, existsSync, promises as fsp } from 'node:fs';
import path from 'node:path';
import { Readable } from 'node:stream';
import { BrowserWindow, Menu, app, dialog, ipcMain, net, protocol, screen, shell } from 'electron';
import { APP_VERSION, LUT_PACKAGE, UVE_SOURCE, type AppPrefs, type JobSpec, type MediaInfo, type UserProfile, type UveProject } from '../shared/types';
import type { LutProfile } from '../shared/uve/lut';
import { analyze, cancelAnalysis } from './analysis';
import { deleteLuts, downloadRepoLuts, initLuts, lutLibrary, onLutChange, restoreHiddenLuts, saveUserLuts } from './luts';
import { clearCache, previewProxy, probe, thumbnails } from './media';
import {
    cancelJob, clearFinished, enqueue, fileNameFromTemplate, initRenderQueue, isRendering, listJobs, outputExtension, removeJob, retryJob, uniqueOutput
} from './render';
import { type WindowState, getEditorState, getPrefs, getWindowState, pushRecent, setEditorState, setPrefs, setWindowState, settingsFile, userDir } from './store';
import { capabilities, installFfmpeg, invalidateCaps, toolsStatus } from './tools';
import { applyUpdate, checkUpdates, skipUpdate } from './updates';
import { RVE, cancelInstall, deleteModel, downloadModel, importCustomModel, installRve, rveStatus, uninstallRve } from './rve';
import { readJson, safeUnzip, writeJson } from './util';

// Data folder: settings.json, profiles, LUTs and caches live in <app folder>\data for the portable
// version (marked by portable.txt) and in development (<project>\data). Installed copies use %APPDATA%,
// because an installer update would replace the program folder.
const migrationCheck = process.argv.find(x => x.startsWith('--migration-check='))?.slice(18);
if (migrationCheck) {
    // test harness: run the settings migration on a COPY of an older data folder
    app.setPath('userData', path.resolve(migrationCheck));
} else if (app.isPackaged) {
    const appDir = path.dirname(app.getPath('exe'));
    if (existsSync(path.join(appDir, 'portable.txt'))) app.setPath('userData', path.join(appDir, 'data'));
} else {
    app.setPath('userData', path.resolve(__dirname, '..', 'data'));
}

protocol.registerSchemesAsPrivileged([
    { scheme: 'uve-media', privileges: { standard: true, secure: true, stream: true, supportFetchAPI: true, corsEnabled: true } },
    { scheme: 'uve-app', privileges: { standard: true, secure: true, supportFetchAPI: true } }
]);

let mainWin: BrowserWindow | null = null;
const devUrl = process.env.VITE_DEV_SERVER_URL;
const distDir = path.join(__dirname, '..', 'dist');

// Files the renderer may stream through uve-media:// (only files the user opened or dropped)
const allowedMedia = new Map<string, string>();
function allowMedia(p: string): string {
    const abs = path.resolve(p);
    for (const [id, file] of allowedMedia) if (file === abs) return id;
    const id = Math.random().toString(36).slice(2, 12) + Date.now().toString(36);
    allowedMedia.set(id, abs);
    return id;
}

const MIME: Record<string, string> = {
    '.mp4': 'video/mp4', '.m4v': 'video/mp4', '.mov': 'video/quicktime', '.webm': 'video/webm', '.mkv': 'video/x-matroska', '.ogv': 'video/ogg',
    '.mp3': 'audio/mpeg', '.wav': 'audio/wav', '.m4a': 'audio/mp4', '.aac': 'audio/aac', '.flac': 'audio/flac', '.ogg': 'audio/ogg', '.opus': 'audio/ogg'
};
const VIDEO_EXTS = ['mp4', 'm4v', 'mkv', 'mov', 'webm', 'avi', 'mpg', 'mpeg', 'ts', 'mts', 'm2ts', 'flv', 'wmv', '3gp', 'ogv', 'mxf', 'vob'];
const AUDIO_EXTS = ['mp3', 'wav', 'm4a', 'aac', 'flac', 'ogg', 'opus', 'wma', 'aiff', 'aif', 'ac3', 'eac3', 'mka'];

async function serveMedia(req: Request): Promise<Response> {
    const url = new URL(req.url);
    const file = allowedMedia.get(url.hostname || url.pathname.replace(/^\/+/, ''));
    if (!file) return new Response('Not found', { status: 404 });
    const st = await fsp.stat(file).catch(() => null);
    if (!st) return new Response('Not found', { status: 404 });
    const type = MIME[path.extname(file).toLowerCase()] || 'application/octet-stream';
    const range = req.headers.get('range');
    if (range) {
        const m = range.match(/bytes=(\d*)-(\d*)/);
        let start = m && m[1] ? Number(m[1]) : 0;
        let end = m && m[2] ? Number(m[2]) : st.size - 1;
        if (!m?.[1] && m?.[2]) { start = Math.max(0, st.size - Number(m[2])); end = st.size - 1; }
        end = Math.min(end, st.size - 1);
        if (start > end || start >= st.size) return new Response(null, { status: 416, headers: { 'Content-Range': `bytes */${st.size}` } });
        const body = Readable.toWeb(createReadStream(file, { start, end })) as ReadableStream;
        return new Response(body, { status: 206, headers: { 'Content-Type': type, 'Access-Control-Allow-Origin': '*', 'Accept-Ranges': 'bytes', 'Content-Length': String(end - start + 1), 'Content-Range': `bytes ${start}-${end}/${st.size}` } });
    }
    const body = Readable.toWeb(createReadStream(file)) as ReadableStream;
    return new Response(body, { status: 200, headers: { 'Content-Type': type, 'Access-Control-Allow-Origin': '*', 'Accept-Ranges': 'bytes', 'Content-Length': String(st.size) } });
}

async function serveApp(req: Request): Promise<Response> {
    const url = new URL(req.url);
    const rel = decodeURIComponent(url.pathname).replace(/^\/+/, '') || 'index.html';
    const file = path.resolve(distDir, rel);
    if (!file.startsWith(path.resolve(distDir))) return new Response('Forbidden', { status: 403 });
    return net.fetch('file:///' + file.replace(/\\/g, '/'));
}

const pageUrl = (page: string) => devUrl ? `${devUrl.replace(/\/$/, '')}/${page}` : `uve-app://app/${page}`;
const send = (ch: string, ...args: unknown[]) => { if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send(ch, ...args); };

/** Restored bounds are only used when the window is still visible on a connected display. */
function visibleBounds(w: WindowState): { x?: number; y?: number; width: number; height: number } {
    const width = Math.max(1100, Math.round(w.width || 1600)), height = Math.max(700, Math.round(w.height || 1000));
    if (typeof w.x !== 'number' || typeof w.y !== 'number') return { width, height };
    const r = { x: Math.round(w.x), y: Math.round(w.y), width, height };
    const onScreen = screen.getAllDisplays().some(d => {
        const a = d.workArea;
        const ix = Math.min(r.x + r.width, a.x + a.width) - Math.max(r.x, a.x);
        const iy = Math.min(r.y + r.height, a.y + a.height) - Math.max(r.y, a.y);
        return ix >= 200 && iy >= 100;
    });
    return onScreen ? r : { width, height };
}

async function createWindow() {
    const ws = await getWindowState();
    mainWin = new BrowserWindow({
        ...visibleBounds(ws), minWidth: 1100, minHeight: 700, show: false, backgroundColor: '#0b1220',
        title: `UVE Manager ${APP_VERSION}`,
        titleBarStyle: 'hidden',
        titleBarOverlay: { color: '#0b1220', symbolColor: '#c9d6ee', height: 44 },
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true, spellcheck: false
        }
    });
    // Windows may shift a window created with explicit coordinates (DPI / snap rules): apply the saved bounds again
    const restored = visibleBounds(ws);
    if (typeof restored.x === 'number') mainWin.setBounds(restored as Electron.Rectangle);
    mainWin.once('ready-to-show', () => {
        if (!mainWin) return;
        if (typeof restored.x === 'number' && !ws.maximized) mainWin.setBounds(restored as Electron.Rectangle);
        if (ws.maximized) mainWin.maximize();
        if (ws.fullscreen) mainWin.setFullScreen(true);
        mainWin.show();
    });
    // remember position, size and maximized state in settings.json
    let saveTimer: NodeJS.Timeout | null = null;
    const saveBounds = () => {
        if (!mainWin || mainWin.isDestroyed() || mainWin.isMinimized()) return;
        const b = mainWin.getNormalBounds();
        void setWindowState({ x: b.x, y: b.y, width: b.width, height: b.height, maximized: mainWin.isMaximized(), fullscreen: mainWin.isFullScreen() });
    };
    const queueSave = () => { if (saveTimer) clearTimeout(saveTimer); saveTimer = setTimeout(saveBounds, 400); };
    for (const ev of ['resize', 'move', 'maximize', 'unmaximize', 'enter-full-screen', 'leave-full-screen'] as const) mainWin.on(ev as 'resize', queueSave);
    mainWin.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
    mainWin.webContents.on('will-navigate', e => e.preventDefault());
    mainWin.on('close', e => {
        if (isRendering()) {
            const r = dialog.showMessageBoxSync(mainWin!, { type: 'warning', buttons: ['Keep rendering', 'Quit and cancel'], defaultId: 0, cancelId: 0, title: 'Rendering in progress', message: 'A render job is still running. Quit and cancel it?' });
            if (r === 0) { e.preventDefault(); return; }
        }
        if (saveTimer) clearTimeout(saveTimer);
        saveBounds();
    });
    void mainWin.loadURL(pageUrl('index.html'));
}

function h<T extends unknown[], R>(ch: string, fn: (...a: T) => R | Promise<R>) {
    ipcMain.handle(ch, async (e, ...a) => {
        if (!mainWin || e.sender.id !== mainWin.webContents.id) throw new Error('IPC sender not allowed');
        return fn(...(a as T));
    });
}

const storeFile = (n: string) => path.join(userDir(), n);

function registerIpc() {
    h('app:info', async () => ({
        version: APP_VERSION, uveSource: UVE_SOURCE, lutPackage: LUT_PACKAGE.file, userData: userDir(), settingsFile: settingsFile(),
        tools: await toolsStatus(), packaged: app.isPackaged, electron: process.versions.electron, chrome: process.versions.chrome
    }));
    h('prefs:get', () => getPrefs());
    h('prefs:set', async (patch: Partial<AppPrefs>) => {
        const allowed: (keyof AppPrefs)[] = ['gpuEngine', 'hwDecode', 'ffmpegDir', 'autoUpdate', 'updateRepo', 'autoDownloadLuts', 'previewQuality', 'parityMode', 'lastOutputDir', 'shortcuts', 'defaultExport', 'skippedVersions'];
        const clean: Partial<AppPrefs> = {};
        for (const k of allowed) if (k in patch) (clean as Record<string, unknown>)[k] = patch[k];
        if ('ffmpegDir' in clean) invalidateCaps();
        return setPrefs(clean);
    });
    h('caps:get', (force?: boolean) => capabilities(!!force));
    h('tools:status', () => toolsStatus());
    h('tools:install', async () => {
        const st = await installFfmpeg((m, p) => send('tools:progress', { message: m, pct: p }));
        invalidateCaps();
        return st;
    });
    h('updates:check', () => checkUpdates());
    // REAL-Video-Enhancer (AI upscaling, separate AGPL tool in tools\rve)
    h('rve:status', (deep?: boolean) => rveStatus(deep !== false));
    h('rve:install', (reinstallBackend?: boolean) => installRve(
        (m, p) => send('rve:progress', { message: m, pct: p }),
        l => send('rve:log', l),
        { reinstallBackend: !!reinstallBackend }));
    h('rve:cancelInstall', () => cancelInstall());
    h('rve:uninstall', (keepModels?: boolean) => uninstallRve(keepModels !== false));
    h('rve:downloadModel', async (id: string) => { await downloadModel(id, (m, p) => send('rve:progress', { message: m, pct: p })); return rveStatus(false); });
    h('rve:deleteModel', async (id: string) => { await deleteModel(id); return rveStatus(false); });
    h('rve:importModel', async () => {
        const r = await dialog.showOpenDialog(mainWin!, { title: 'Import PyTorch upscale model', filters: [{ name: 'PyTorch model', extensions: ['pth', 'safetensors'] }], properties: ['openFile'] });
        if (!r.canceled && r.filePaths[0]) await importCustomModel(r.filePaths[0]);
        return rveStatus(false);
    });
    h('rve:openSource', () => shell.openExternal(RVE.source));
    h('updates:apply', (id: 'app' | 'ffmpeg' | 'luts' | 'rve') => applyUpdate(id, (m, p) => send('updates:progress', { id, message: m, pct: p })));
    h('updates:skip', (id: 'app' | 'ffmpeg' | 'luts' | 'rve') => skipUpdate(id));

    // media
    h('dialog:openVideos', async (multi?: boolean) => {
        const r = await dialog.showOpenDialog(mainWin!, {
            title: 'Open video or audio', properties: multi ? ['openFile', 'multiSelections'] : ['openFile'],
            filters: [
                { name: 'Video and audio', extensions: [...VIDEO_EXTS, ...AUDIO_EXTS] },
                { name: 'Video', extensions: VIDEO_EXTS },
                { name: 'Audio', extensions: AUDIO_EXTS },
                { name: 'All files', extensions: ['*'] }
            ]
        });
        return r.canceled ? [] : r.filePaths;
    });
    h('media:probe', async (p: string) => {
        const info = await probe(p);
        await pushRecent('recentFiles', p);
        return info;
    });
    h('media:url', (p: string) => `uve-media://${allowMedia(p)}/${encodeURIComponent(path.basename(p))}`);
    h('media:thumbs', (m: MediaInfo, count: number) => thumbnails(m, count));
    h('media:proxy', async (m: MediaInfo) => {
        const file = await previewProxy(m, pct => send('media:proxyProgress', { path: m.path, pct }));
        return `uve-media://${allowMedia(file)}/proxy.mp4`;
    });
    h('analysis:run', (m: MediaInfo) => analyze(m, pct => send('analysis:progress', { path: m.path, pct })));
    h('analysis:cancel', (key: string) => cancelAnalysis(key));
    h('cache:clear', () => clearCache());

    // LUTs
    h('luts:get', () => lutLibrary());
    h('luts:download', async (force?: boolean) => { await downloadRepoLuts(!!force); return lutLibrary(); });
    h('luts:save', (list: LutProfile[]) => saveUserLuts(list));
    h('luts:delete', (keys: string[]) => deleteLuts(keys));
    h('luts:restore', () => restoreHiddenLuts());
    onLutChange(s => send('luts:changed', s));

    // persistent libraries
    h('profiles:get', () => readJson<{ profiles: UserProfile[]; activeId: string | null }>(storeFile('profiles.json'), { profiles: [], activeId: null }));
    h('profiles:save', (d: { profiles: UserProfile[]; activeId: string | null }) => writeJson(storeFile('profiles.json'), d));
    h('filters:get', () => readJson<unknown[]>(storeFile('filters.json'), []));
    h('filters:save', (list: unknown[]) => writeJson(storeFile('filters.json'), list));
    // last editor session (effect + export settings, open video, view, …) inside settings.json
    h('state:get', () => getEditorState());
    h('state:set', (s: unknown) => setEditorState(s && typeof s === 'object' ? s as Record<string, unknown> : null));

    // generic file IO (user-chosen paths only)
    h('files:open', async (o: { title: string; filters: { name: string; extensions: string[] }[]; multi?: boolean; maxBytes?: number }) => {
        const r = await dialog.showOpenDialog(mainWin!, { title: o.title, filters: o.filters, properties: o.multi ? ['openFile', 'multiSelections'] : ['openFile'] });
        if (r.canceled) return [];
        const max = Math.min(o.maxBytes || 50 * 1024 * 1024, 300 * 1024 * 1024);
        const out: { name: string; path: string; data: Uint8Array }[] = [];
        for (const f of r.filePaths) {
            const st = await fsp.stat(f);
            if (st.size > max) throw new Error(`${path.basename(f)} is larger than ${(max / 1048576).toFixed(0)} MB.`);
            out.push({ name: path.basename(f), path: f, data: new Uint8Array(await fsp.readFile(f)) });
        }
        return out;
    });
    h('files:save', async (o: { title: string; defaultName: string; filters: { name: string; extensions: string[] }[]; data: Uint8Array | string }) => {
        const r = await dialog.showSaveDialog(mainWin!, { title: o.title, defaultPath: o.defaultName, filters: o.filters });
        if (r.canceled || !r.filePath) return null;
        await fsp.writeFile(r.filePath, typeof o.data === 'string' ? o.data : Buffer.from(o.data));
        return r.filePath;
    });
    h('files:unzip', (data: Uint8Array) => safeUnzip(data, { maxEntries: 2000, maxEntryBytes: 20 * 1024 * 1024, maxTotalBytes: 200 * 1024 * 1024 }));
    h('dialog:pickFolder', async (title: string) => {
        const r = await dialog.showOpenDialog(mainWin!, { title, properties: ['openDirectory', 'createDirectory'] });
        return r.canceled ? null : r.filePaths[0];
    });

    // projects
    h('project:open', async (p?: string) => {
        let file = p;
        if (!file) {
            const r = await dialog.showOpenDialog(mainWin!, { title: 'Open project', filters: [{ name: 'UVE Manager Project', extensions: ['uveproj'] }], properties: ['openFile'] });
            if (r.canceled) return null;
            file = r.filePaths[0];
        }
        const st = await fsp.stat(file);
        if (st.size > 50 * 1024 * 1024) throw new Error('Project file is too large.');
        const proj = JSON.parse(await fsp.readFile(file, 'utf8')) as UveProject;
        if (!proj || proj.schema !== 'uve-project') throw new Error('Not a UVE Manager project file.');
        const version = 'version' in proj ? proj.version : undefined;
        if (version !== 1 && version !== 2) throw new Error(`Unsupported project version ${String(version)}.`);
        await pushRecent('recentProjects', file);
        const missing: string[] = [];
        const paths = proj.version === 1
            ? [proj.media?.path].filter((x): x is string => !!x)
            : proj.timeline.sources.map(s => s.path);
        for (const mp of paths) {
            if (!(await fsp.stat(mp).catch(() => null))) missing.push(mp);
        }
        return { path: file, project: proj, mediaOk: missing.length === 0, missing };
    });
    h('project:save', async (o: { path: string | null; project: UveProject; saveAs?: boolean }) => {
        let file = o.path;
        if (!file || o.saveAs) {
            const r = await dialog.showSaveDialog(mainWin!, { title: 'Save project', defaultPath: file || 'Untitled.uveproj', filters: [{ name: 'UVE Manager Project', extensions: ['uveproj'] }] });
            if (r.canceled || !r.filePath) return null;
            file = r.filePath;
        }
        await writeJson(file, o.project);
        await pushRecent('recentProjects', file);
        return file;
    });
    h('project:locateMedia', async () => {
        const r = await dialog.showOpenDialog(mainWin!, { title: 'Locate missing video', properties: ['openFile'] });
        return r.canceled ? null : r.filePaths[0];
    });

    // rendering
    h('render:outputPath', async (o: { media: MediaInfo; exportSettings: JobSpec['exportSettings']; profileName: string }) => {
        const prefs = await getPrefs();
        const dir = o.exportSettings.outputDir || prefs.lastOutputDir || path.join(path.dirname(o.media.path), 'UVE_Enhanced');
        await fsp.mkdir(dir, { recursive: true });
        return uniqueOutput(dir, fileNameFromTemplate(o.exportSettings.fileName || '{name}_UVE', o.media, o.profileName), outputExtension(o.exportSettings), o.media.path);
    });
    h('render:enqueue', (spec: JobSpec) => {
        if (path.resolve(spec.output).toLowerCase() === path.resolve(spec.input).toLowerCase()) throw new Error('The output must not overwrite the source file.');
        return enqueue(spec);
    });
    h('render:list', () => listJobs());
    h('render:cancel', (id: string) => cancelJob(id));
    h('render:retry', (id: string) => retryJob(id));
    h('render:remove', (id: string) => removeJob(id));
    h('render:clearFinished', () => clearFinished());

    h('shell:showItem', (p: string) => shell.showItemInFolder(p));
    h('shell:openExternal', (url: string) => {
        if (!/^https:\/\/(github\.com|svg\.ts3x\.cc|greasyfork\.org|ffmpeg\.org)\//.test(url)) throw new Error('URL not allowed');
        return shell.openExternal(url);
    });
    h('window:setTitle', (t: string) => mainWin?.setTitle(t));
}

/**
 * v1.0.2 macOS: a minimal app menu so ⌘Q/⌘H/⌘M/⌘W and ⌘X/⌘C/⌘V/⌘A in text fields work. Undo/redo and
 * ⌘R are left to the in-app shortcuts (a menu accelerator would swallow the key event).
 */
function macMenu(): Menu {
    return Menu.buildFromTemplate([
        { role: 'appMenu' },
        { label: 'Edit', submenu: [{ role: 'cut' }, { role: 'copy' }, { role: 'paste' }, { role: 'pasteAndMatchStyle' }, { role: 'delete' }, { role: 'selectAll' }] },
        { role: 'windowMenu' }
    ]);
}

app.whenReady().then(async () => {
    Menu.setApplicationMenu(process.platform === 'darwin' ? macMenu() : null);
    protocol.handle('uve-media', serveMedia);
    protocol.handle('uve-app', serveApp);
    registerIpc();
    initRenderQueue({
        broadcast: jobs => send('render:jobs', jobs),
        workerUrl: () => pageUrl('export.html'),
        workerPreload: path.join(__dirname, 'exportPreload.js')
    });
    // v1.0.1 test harness: capability probe, parity reference exports, benchmark (headless, no capture)
    const harnessArg = (n: string) => process.argv.find(x => x.startsWith(`--${n}=`))?.slice(n.length + 3);
    const probeOut = harnessArg('probe-capabilities'), parityOut = harnessArg('parity-export'), benchOut = harnessArg('benchmark');
    if (migrationCheck) {
        const h = await import('./harness');
        const ok = await h.migrationCheck().catch(e => { console.error(e); return false; });
        app.exit(ok ? 0 : 1);
        return;
    }
    if (probeOut || parityOut || benchOut) {
        const h = await import('./harness');
        try {
            if (probeOut) await h.probeCapabilities(path.resolve(probeOut));
            if (parityOut) await h.parityExport(path.resolve(parityOut));
            if (benchOut) await h.benchmark(path.resolve(benchOut));
            app.exit(0);
        } catch (e) { console.error(e); app.exit(1); }
        return;
    }
    if (process.argv.includes('--selftest')) {
        const { runSelftest } = await import('./selftest');
        app.exit(await runSelftest() ? 1 : 0);
        return;
    }
    await createWindow();
    await initLuts();
    // test-only UI snapshot for automated layout checks: --ui-snapshot=<png> [--ui-video=<file>] [--ui-view=<view>]
    const snap = process.argv.find(a => a.startsWith('--ui-snapshot='))?.slice(14);
    if (snap && mainWin) {
        const video = process.argv.find(a => a.startsWith('--ui-video='))?.slice(11);
        const view = process.argv.find(a => a.startsWith('--ui-view='))?.slice(10);
        mainWin.webContents.on('console-message', e => console.log(`[renderer:${e.level}] ${e.message} (${e.sourceId}:${e.lineNumber})`));
        mainWin.webContents.on('did-fail-load', (_e, code, desc, url) => console.log(`[load-failed] ${code} ${desc} ${url}`));
        mainWin.webContents.once('did-finish-load', async () => {
            await new Promise(r => setTimeout(r, 2500));
            if (video) {
                await mainWin!.webContents.executeJavaScript(`window.__uveTest && window.__uveTest.load(${JSON.stringify(video)})`);
                await new Promise(r => setTimeout(r, 6000));
            }
            if (view) await mainWin!.webContents.executeJavaScript(`window.__uveTest && window.__uveTest.view(${JSON.stringify(view)})`);
            const evalJs = process.argv.find(a => a.startsWith('--ui-eval='))?.slice(10);
            if (evalJs) {
                await mainWin!.webContents.executeJavaScript(evalJs);
                await new Promise(r => setTimeout(r, 300));
                console.log('[ui-probe-after-eval]', JSON.stringify(await mainWin!.webContents.executeJavaScript('window.__uveProbe ? window.__uveProbe() : null')));
            }
            await new Promise(r => setTimeout(r, 1500));
            console.log('[ui-probe]', JSON.stringify(await mainWin!.webContents.executeJavaScript('window.__uveProbe ? window.__uveProbe() : null')));
            console.log('[ui-window]', JSON.stringify(mainWin!.getBounds()), JSON.stringify(await mainWin!.webContents.executeJavaScript('({ view: document.querySelector(".rail button.active span")?.textContent, video: document.title })')));
            const img = await mainWin!.webContents.capturePage();
            await fsp.writeFile(snap, img.toPNG());
            app.exit(0);
        });
    }
});

app.on('window-all-closed', () => { if (!process.argv.some(a => /^--(selftest|probe-capabilities|parity-export|benchmark|migration-check)/.test(a))) app.quit(); });
