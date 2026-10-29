import { contextBridge, ipcRenderer, webUtils } from 'electron';

const invoke = (ch: string, ...a: unknown[]) => ipcRenderer.invoke(ch, ...a);
const on = (ch: string, cb: (...a: any[]) => void) => { // eslint-disable-line @typescript-eslint/no-explicit-any
    const l = (_e: unknown, ...a: unknown[]) => cb(...a);
    ipcRenderer.on(ch, l);
    return () => { ipcRenderer.removeListener(ch, l); };
};

const EVENTS = new Set(['rve:progress', 'rve:log', 'tools:progress', 'updates:progress', 'media:proxyProgress', 'analysis:progress', 'luts:changed', 'render:jobs']);

const api = {
    invoke(ch: string, ...a: unknown[]) {
        if (!/^(app|prefs|caps|tools|rve|updates|dialog|media|analysis|cache|luts|profiles|filters|state|files|project|render|shell|window):[\w]+$/.test(ch)) throw new Error('channel not allowed');
        return invoke(ch, ...a);
    },
    on(ch: string, cb: (...a: any[]) => void) { // eslint-disable-line @typescript-eslint/no-explicit-any
        if (!EVENTS.has(ch)) throw new Error('event not allowed');
        return on(ch, cb);
    },
    /** Absolute path of a dropped File (Electron ≥ 32 replacement for File.path). */
    pathForFile(f: File) { return webUtils.getPathForFile(f); }
};

contextBridge.exposeInMainWorld('uve', api);
export type UveApi = typeof api;
