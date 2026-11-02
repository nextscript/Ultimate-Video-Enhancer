'use strict';
// Main-Prozess-Hälfte der Greasemonkey/Tampermonkey-Kompatibilitätsschicht.
// Speicher (GM_getValue/GM_setValue), Netzwerk (GM_xmlhttpRequest, @connect-Prüfung)
// und Downloads (GM_download) laufen hier – außerhalb von CORS/CSP der Oberfläche.
const fs = require('fs');
const path = require('path');
const { app, ipcMain, net, dialog, BrowserWindow } = require('electron');
const pluginHost = require('./plugin-host');

const stores = new Map();
const timers = new Map();
const requests = new Map();

const storeFile = (id) => path.join(app.getPath('userData'), 'plugin-data', id.replace(/[^\w.-]/g, '_') + '.json');

function loadStore(id) {
  if (stores.has(id)) return stores.get(id);
  let data = {};
  try { data = JSON.parse(fs.readFileSync(storeFile(id), 'utf8')); } catch (_) { }
  stores.set(id, data);
  return data;
}

function flushStore(id) {
  clearTimeout(timers.get(id));
  timers.delete(id);
  const data = stores.get(id);
  if (!data) return;
  const f = storeFile(id);
  try {
    fs.mkdirSync(path.dirname(f), { recursive: true });
    fs.writeFileSync(f + '.tmp', JSON.stringify(data));
    fs.renameSync(f + '.tmp', f);
  } catch (e) { console.error('[gm] speichern fehlgeschlagen', id, e); }
}

function scheduleFlush(id) {
  clearTimeout(timers.get(id));
  timers.set(id, setTimeout(() => flushStore(id), 300));
}

function flushAll() { for (const id of stores.keys()) flushStore(id); }

function hostAllowed(plugin, url) {
  let host;
  try { host = new URL(url).hostname.toLowerCase(); } catch (_) { return false; }
  const list = (plugin && plugin.meta && plugin.meta.connect) || [];
  if (list.includes('*')) return true;
  return list.some((c) => {
    c = c.toLowerCase();
    return host === c || host.endsWith('.' + c);
  });
}

function broadcast(sender, id, key, value) {
  for (const w of BrowserWindow.getAllWindows()) {
    if (w.webContents !== sender) w.webContents.send('gm:changed', { id, key, value });
  }
}

function register() {
  ipcMain.handle('gm:storage-load', (_e, id) => loadStore(String(id)));

  ipcMain.on('gm:storage-set', (e, { id, key, value }) => {
    const data = loadStore(id);
    if (value === undefined) delete data[key]; else data[key] = value;
    scheduleFlush(id);
    broadcast(e.sender, id, key, value);
  });

  ipcMain.handle('gm:storage-export', async (e, id) => {
    const win = BrowserWindow.fromWebContents(e.sender);
    const r = await dialog.showSaveDialog(win, { defaultPath: id + '-daten.json', filters: [{ name: 'JSON', extensions: ['json'] }] });
    if (r.canceled || !r.filePath) return false;
    fs.writeFileSync(r.filePath, JSON.stringify(loadStore(id), null, 2));
    return true;
  });

  ipcMain.handle('gm:storage-reset', (_e, id) => {
    stores.set(id, {});
    flushStore(id);
    return true;
  });

  ipcMain.handle('gm:xhr', async (_e, d) => {
    const plugin = pluginHost.get(d.pluginId);
    if (!plugin) return { error: 'unknown-plugin' };
    if (!hostAllowed(plugin, d.url)) {
      return { error: 'blocked', message: 'Host nicht per @connect freigegeben: ' + d.url };
    }
    const ctrl = new AbortController();
    requests.set(d.reqId, ctrl);
    let timer = null;
    if (d.timeout > 0) timer = setTimeout(() => { ctrl.abort('timeout'); }, d.timeout);
    try {
      const res = await net.fetch(d.url, {
        method: d.method || 'GET',
        headers: d.headers || {},
        body: d.data == null || /^(GET|HEAD)$/i.test(d.method || 'GET') ? undefined : d.data,
        signal: ctrl.signal,
        redirect: 'follow',
        bypassCustomProtocolHandlers: true
      });
      const buf = Buffer.from(await res.arrayBuffer());
      const hdr = [];
      res.headers.forEach((v, k) => hdr.push(k + ': ' + v));
      return { status: res.status, statusText: res.statusText, finalUrl: res.url || d.url, headers: hdr.join('\r\n'), body: new Uint8Array(buf) };
    } catch (err) {
      if (ctrl.signal.aborted) return { error: ctrl.signal.reason === 'timeout' ? 'timeout' : 'abort' };
      return { error: 'network', message: String(err && err.message || err) };
    } finally {
      clearTimeout(timer);
      requests.delete(d.reqId);
    }
  });

  ipcMain.on('gm:xhr-abort', (_e, reqId) => {
    const c = requests.get(reqId);
    if (c) c.abort('abort');
  });

  ipcMain.handle('gm:download', async (e, { name, data, saveAs }) => {
    const win = BrowserWindow.fromWebContents(e.sender);
    const safe = String(name || 'download').replace(/[\\/:*?"<>|]+/g, '_');
    let target = path.join(app.getPath('downloads'), safe);
    if (saveAs !== false) {
      const r = await dialog.showSaveDialog(win, { defaultPath: target });
      if (r.canceled || !r.filePath) return { canceled: true };
      target = r.filePath;
    }
    fs.writeFileSync(target, Buffer.from(data));
    return { path: target };
  });
}

module.exports = { register, flushAll };
