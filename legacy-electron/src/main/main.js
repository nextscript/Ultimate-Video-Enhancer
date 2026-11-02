'use strict';
// UVE Player 1.0.0 – Launcher (Gegenstück zu vlc.exe).
// vlc.exe ist ein dünner Starter: libvlc_new → libvlc_add_intf → libvlc_playlist_play → libvlc_wait.
// Genauso hier: Einzelinstanz sichern, Kernmodule initialisieren, Oberfläche laden, Dateien übergeben.
const { app, BrowserWindow, ipcMain, dialog, shell, powerSaveBlocker, screen } = require('electron');
const path = require('path');
const fs = require('fs');

const settings = require('./settings');
const protocolMod = require('./protocol');
const registry = require('./media-registry');
const pluginHost = require('./plugin-host');
const gmBridge = require('./gm-bridge');
const transcode = require('./transcode');
const menu = require('./menu');

const VERSION = require('../../package.json').version;
const SELFTEST = process.argv.includes('--selftest');

protocolMod.registerPrivileged();
app.setAppUserModelId('com.nextscript.uveplayer');
if (SELFTEST) {
  const dir = path.join(app.getPath('temp'), 'uve-player-selftest');
  // Kann scheitern, solange eine vorherige Testinstanz noch Dateien hält – dann eigenen Ordner nehmen
  let target = dir;
  try { fs.rmSync(dir, { recursive: true, force: true }); } catch (_) { target = dir + '-' + process.pid; }
  app.setPath('userData', target);
}

let win = null;
let pendingOpen = [];
let psbId = null;

function argsToPaths(argv, isSecond) {
  const list = isSecond ? argv.slice(1) : argv.slice(process.defaultApp ? 2 : 1);
  return list.filter((a) => a && !a.startsWith('--') && a !== '.' && (registry.isUrl(a) || fs.existsSync(a)))
    .map((a) => (registry.isUrl(a) ? a : path.resolve(a)));
}

if (!SELFTEST && !app.requestSingleInstanceLock()) {
  app.quit();
} else {
  // Hardwarebeschleunigung muss vor "ready" entschieden werden
  try {
    const raw = JSON.parse(fs.readFileSync(path.join(app.getPath('userData'), 'settings.json'), 'utf8'));
    if (raw.hardwareAcceleration === false) app.disableHardwareAcceleration();
  } catch (_) { }
  app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required');
  app.commandLine.appendSwitch('enable-features', 'PlatformHEVCDecoderSupport');
  app.commandLine.appendSwitch('enable-blink-features', 'AudioVideoTracks');

  pendingOpen = argsToPaths(process.argv, false);

  app.on('second-instance', (_e, argv) => {
    const files = argsToPaths(argv, true);
    if (win) {
      if (win.isMinimized()) win.restore();
      win.focus();
      if (files.length) openInRenderer(files, true);
    }
  });

  app.whenReady().then(start);
  app.on('window-all-closed', () => app.quit());
  app.on('before-quit', () => { transcode.cancelAll(); require('./children').killAll(); gmBridge.flushAll(); settings.flush(); });
  app.on('will-quit', () => require('./children').killAll());
  // auch bei app.exit() (z. B. Selbsttest) und unerwartetem Ende keine FFmpeg-Prozesse zurücklassen
  process.on('exit', () => require('./children').killAll());
}

function openInRenderer(paths, play) {
  const items = registry.resolve(paths);
  if (items.length && win) win.webContents.send('open-items', { items, play });
  return items;
}

function rebuildMenu() {
  const s = settings.get();
  menu.build(win, { recent: s.recent, plugins: pluginHost.list(), shuffle: s.shuffle, alwaysOnTop: s.alwaysOnTop, showPlaylist: s.showPlaylist });
}

function validBounds(b) {
  if (!b) return null;
  const visible = screen.getAllDisplays().some((d) => {
    const a = d.workArea;
    return b.x < a.x + a.width - 50 && b.x + b.width > a.x + 50 && b.y >= a.y - 10 && b.y < a.y + a.height - 50;
  });
  return visible ? b : null;
}

function start() {
  settings.load();
  protocolMod.handle();
  gmBridge.register();
  registerIpc();

  const b = validBounds(settings.get('windowBounds'));
  win = new BrowserWindow({
    width: b ? b.width : 1280,
    height: b ? b.height : 760,
    x: b ? b.x : undefined,
    y: b ? b.y : undefined,
    minWidth: 480,
    minHeight: 320,
    backgroundColor: '#0b0d10',
    title: 'UVE Player',
    icon: path.join(__dirname, '..', 'renderer', 'assets', 'icon.png'),
    show: false,
    alwaysOnTop: !!settings.get('alwaysOnTop'),
    webPreferences: {
      preload: path.join(__dirname, '..', 'preload', 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      backgroundThrottling: false,
      spellcheck: false,
      devTools: !app.isPackaged
    }
  });
  if (b && b.maximized) win.maximize();

  win.once('ready-to-show', () => { if (!SELFTEST) win.show(); });
  const saveBounds = () => {
    if (!win || win.isDestroyed() || win.isFullScreen() || win.isMinimized()) return;
    settings.set('windowBounds', Object.assign(win.isMaximized() ? (settings.get('windowBounds') || win.getBounds()) : win.getBounds(), { maximized: win.isMaximized() }));
  };
  win.on('resize', saveBounds);
  win.on('move', saveBounds);
  win.on('enter-full-screen', () => win.webContents.send('fullscreen', true));
  win.on('leave-full-screen', () => win.webContents.send('fullscreen', false));

  // Externe Links im Standardbrowser öffnen, keine neuen Fenster im Player
  win.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https?:/i.test(url)) shell.openExternal(url);
    return { action: 'deny' };
  });
  win.webContents.on('will-navigate', (e, url) => { if (!url.startsWith('uve://app/')) e.preventDefault(); });

  rebuildMenu();
  win.loadURL('uve://app/index.html');

  if (SELFTEST) runSelftest();
}

function registerIpc() {
  ipcMain.handle('app:info', () => ({
    version: VERSION,
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    node: process.versions.node,
    platform: process.platform + ' ' + process.arch,
    userData: app.getPath('userData'),
    ffmpeg: transcode.find() || null
  }));

  ipcMain.handle('app:ready', () => {
    const files = pendingOpen; pendingOpen = [];
    const session = settings.get('restoreSession') && !files.length ? (settings.get('session') || []) : [];
    return {
      settings: settings.get(),
      plugins: pluginHost.list(),
      open: registry.resolve(files),
      session: registry.resolve(session.filter((p) => registry.isUrl(p) || fs.existsSync(p))),
      selftest: SELFTEST,
      selftestHold: SELFTEST ? Number(process.env.UVE_SELFTEST_HOLD) || 0 : 0,
      selftestTools: SELFTEST ? process.env.UVE_SELFTEST_TOOLS || '' : '',
      selftestUi: SELFTEST && process.env.UVE_SELFTEST_UI === '1',
      selftestAudioSwitch: SELFTEST && process.env.UVE_SELFTEST_AUDIOSWITCH === '1',
      selftestUiSub: SELFTEST ? process.env.UVE_SELFTEST_UISUB || '' : '',
      selftestFarSeek: SELFTEST && process.env.UVE_SELFTEST_FARSEEK === '1'
    };
  });

  ipcMain.handle('settings:patch', (_e, obj) => {
    settings.patch(obj);
    if (obj && ('alwaysOnTop' in obj) && win) win.setAlwaysOnTop(!!obj.alwaysOnTop);
    if (obj && ('ffmpegPath' in obj)) transcode.find(true);
    if (obj && ['recent', 'shuffle', 'alwaysOnTop', 'showPlaylist'].some((k) => k in obj)) rebuildMenu();
    return settings.get();
  });

  ipcMain.handle('media:resolve', (_e, paths) => registry.resolve(paths));

  ipcMain.handle('dialog:open', async (_e, kind) => {
    const filters = [
      { name: 'Medien', extensions: [...registry.VIDEO_EXT, ...registry.AUDIO_EXT, ...registry.PLAYLIST_EXT] },
      { name: 'Video', extensions: registry.VIDEO_EXT },
      { name: 'Audio', extensions: registry.AUDIO_EXT },
      { name: 'Wiedergabelisten', extensions: registry.PLAYLIST_EXT },
      { name: 'Alle Dateien', extensions: ['*'] }
    ];
    let opts;
    if (kind === 'folder') opts = { properties: ['openDirectory', 'multiSelections'] };
    else if (kind === 'subtitle') opts = { properties: ['openFile'], filters: [{ name: 'Untertitel', extensions: registry.SUB_EXT }] };
    else if (kind === 'userscript') opts = { properties: ['openFile'], filters: [{ name: 'Userscript', extensions: ['js'] }] };
    else if (kind === 'ffmpeg') opts = { properties: ['openFile'], filters: [{ name: 'ffmpeg', extensions: ['exe', '*'] }] };
    else opts = { properties: kind === 'files' ? ['openFile', 'multiSelections'] : ['openFile'], filters };
    const r = await dialog.showOpenDialog(win, opts);
    if (r.canceled) return [];
    if (kind === 'userscript' || kind === 'ffmpeg') return r.filePaths;
    return registry.resolve(r.filePaths);
  });

  ipcMain.handle('playlist:save', async (_e, items) => {
    const r = await dialog.showSaveDialog(win, { defaultPath: 'Wiedergabeliste.m3u', filters: [{ name: 'M3U', extensions: ['m3u'] }] });
    if (r.canceled || !r.filePath) return false;
    registry.writePlaylist(r.filePath, items);
    return r.filePath;
  });

  ipcMain.handle('snapshot:save', async (_e, { data, name }) => {
    const dir = path.join(app.getPath('pictures'), 'UVE Player');
    fs.mkdirSync(dir, { recursive: true });
    const file = path.join(dir, String(name).replace(/[\\/:*?"<>|]+/g, '_'));
    fs.writeFileSync(file, Buffer.from(data));
    return file;
  });

  ipcMain.handle('plugins:list', () => pluginHost.list(true));
  ipcMain.handle('plugins:set-enabled', (_e, { id, enabled }) => { const l = pluginHost.setEnabled(id, enabled); rebuildMenu(); return l; });
  ipcMain.handle('plugins:update', async (_e, id) => { const r = await pluginHost.update(id); rebuildMenu(); return r; });
  ipcMain.handle('plugins:reset', (_e, id) => { const l = pluginHost.resetToBuiltin(id); rebuildMenu(); return l; });
  ipcMain.handle('plugins:uninstall', (_e, id) => { const l = pluginHost.uninstall(id); rebuildMenu(); return l; });
  ipcMain.handle('plugins:install', (_e, file) => { const p = pluginHost.installUserscript(file); rebuildMenu(); return p; });
  ipcMain.handle('plugins:open-folder', () => {
    const dir = pluginHost.userDir();
    fs.mkdirSync(dir, { recursive: true });
    return shell.openPath(dir);
  });

  ipcMain.handle('compat:available', () => !!transcode.find());
  ipcMain.handle('stream:open', (_e, url, opts) => require('./stream').open(url, opts || {}));
  ipcMain.handle('stream:open-local', (_e, file, opts) => require('./stream').openLocal(file, opts || {}));
  ipcMain.handle('compat:subs', async (_e, file) => {
    if (!registry.isAllowed(file)) return [];
    const list = await transcode.extractSubtitles(file);
    return list.map((s) => { registry.allow(s.file); return Object.assign(s, { url: registry.mediaUrl(s.file) }); });
  });
  ipcMain.on('stream:log', (_e, msg) => require('./stream').log('renderer', String(msg).slice(0, 2000)));
  ipcMain.handle('tools:check', () => require('./tools-updater').check());
  ipcMain.handle('tools:status', () => require('./tools-updater').status());
  ipcMain.handle('tools:install', async (e, tool) => {
    const r = await require('./tools-updater').install(tool, (p) => { if (!e.sender.isDestroyed()) e.sender.send('tools:progress', Object.assign({ tool }, p)); });
    transcode.find(true);
    require('./stream').findYtdlp(true);
    return r;
  });
  ipcMain.handle('stream:tools', (_e, force) => ({ ffmpeg: transcode.find(force) || null, ytdlp: require('./stream').findYtdlp(force) || null }));
  ipcMain.handle('compat:probe', (_e, file) => transcode.probe(file));
  ipcMain.handle('compat:prepare', async (e, { jobId, file }) => {
    const r = await transcode.prepare(jobId, file, (p) => { if (!e.sender.isDestroyed()) e.sender.send('compat:progress', { jobId, ...p }); });
    registry.allow(r.out);
    if (r.sub) registry.allow(r.sub);
    return { url: registry.mediaUrl(r.out), sub: r.sub ? registry.mediaUrl(r.sub) : null, plan: r.plan };
  });
  ipcMain.handle('compat:cancel', (_e, jobId) => transcode.cancel(jobId));
  ipcMain.handle('compat:subtitle', async (_e, file) => {
    const out = await transcode.convertSubtitle(file);
    registry.allow(out);
    return registry.mediaUrl(out);
  });
  ipcMain.handle('compat:cache', (_e, op) => { if (op === 'clear') transcode.clearCache(); return transcode.cacheSize(); });

  ipcMain.on('power:playing', (_e, playing) => {
    if (playing && psbId === null) psbId = powerSaveBlocker.start('prevent-display-sleep');
    if (!playing && psbId !== null) { powerSaveBlocker.stop(psbId); psbId = null; }
  });

  ipcMain.on('win:title', (_e, title) => { if (win) win.setTitle(title || 'UVE Player'); });
  ipcMain.on('win:fullscreen', (_e, on) => { if (win) win.setFullScreen(on === undefined ? !win.isFullScreen() : !!on); });
  ipcMain.on('app:quit', () => app.quit());
  ipcMain.handle('shell:open-external', (_e, url) => { if (/^https?:\/\//i.test(url)) return shell.openExternal(url); });
  ipcMain.handle('shell:show-item', (_e, p) => shell.showItemInFolder(p));
  ipcMain.on('selftest:result', (_e, r) => {
    console.log('[selftest] ' + JSON.stringify(r));
    const shot = process.env.UVE_SELFTEST_SHOT;
    const finish = () => setTimeout(() => { require('./children').killAll(); app.exit(r && r.ok ? 0 : 1); }, 100);
    if (shot && win) win.webContents.capturePage().then((img) => fs.writeFileSync(shot, img.toPNG())).catch(() => { }).finally(finish);
    else finish();
  });
}

function runSelftest() {
  setTimeout(() => { console.log('[selftest] Zeitüberschreitung'); require('./children').killAll(); app.exit(2); }, 120000);
}
