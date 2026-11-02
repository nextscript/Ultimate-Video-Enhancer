'use strict';
// Preload: schmale, explizit freigegebene Brücke zwischen Oberfläche und Main-Prozess.
const { contextBridge, ipcRenderer, webUtils } = require('electron');

const INVOKE = new Set([
  'app:info', 'app:ready', 'settings:patch', 'media:resolve', 'dialog:open', 'playlist:save', 'snapshot:save',
  'plugins:list', 'plugins:set-enabled', 'plugins:update', 'plugins:reset', 'plugins:uninstall', 'plugins:install', 'plugins:open-folder',
  'compat:available', 'stream:open', 'stream:open-local', 'compat:subs','stream:tools', 'tools:check', 'tools:status', 'tools:install', 'compat:probe', 'compat:prepare', 'compat:cancel', 'compat:subtitle', 'compat:cache',
  'gm:storage-load', 'gm:storage-export', 'gm:storage-reset', 'gm:xhr', 'gm:download',
  'shell:open-external', 'shell:show-item'
]);
const SEND = new Set(['stream:log', 'gm:storage-set', 'gm:xhr-abort', 'power:playing', 'win:title', 'win:fullscreen', 'app:quit', 'selftest:result']);
const ON = new Set(['menu', 'open-items', 'fullscreen', 'compat:progress', 'gm:changed', 'tools:progress']);

contextBridge.exposeInMainWorld('uveHost', {
  invoke: (ch, ...args) => {
    if (!INVOKE.has(ch)) return Promise.reject(new Error('Kanal nicht erlaubt: ' + ch));
    return ipcRenderer.invoke(ch, ...args);
  },
  send: (ch, ...args) => { if (SEND.has(ch)) ipcRenderer.send(ch, ...args); },
  on: (ch, cb) => {
    if (!ON.has(ch)) return () => { };
    const fn = (_e, payload) => cb(payload);
    ipcRenderer.on(ch, fn);
    return () => ipcRenderer.removeListener(ch, fn);
  },
  pathForFile: (file) => { try { return webUtils.getPathForFile(file); } catch (_) { return ''; } }
});
