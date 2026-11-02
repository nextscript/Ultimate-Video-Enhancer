// Greasemonkey/Tampermonkey-API für Userscript-Plugins (Renderer-Hälfte).
// Umgesetzt sind alle Grants, die Ultimate Video Enhancer anfordert:
// GM_getValue, GM_setValue, GM_addValueChangeListener, GM_info, GM_download,
// GM_xmlhttpRequest, unsafeWindow, GM_addElement – plus gängige Ergänzungen.

const host = window.uveHost;
let reqSeq = 0;

const clone = (v) => (v === undefined ? undefined : JSON.parse(JSON.stringify(v)));

export async function createGM(plugin, appVersion) {
  const store = Object.assign({}, await host.invoke('gm:storage-load', plugin.id));
  const listeners = new Map();
  let listenerSeq = 0;

  host.on('gm:changed', ({ id, key, value }) => {
    if (id !== plugin.id) return;
    const old = store[key];
    if (value === undefined) delete store[key]; else store[key] = value;
    for (const l of listeners.values()) {
      if (l.key === key) { try { l.cb(key, old, value, true); } catch (e) { console.error(e); } }
    }
  });

  const requireMap = plugin.requireMap || {};
  const localize = (url) => {
    if (!url) return url;
    const base = String(url).split('#')[0];
    for (const [remote, local] of Object.entries(requireMap)) {
      if (base === remote || base.startsWith(remote)) return plugin.baseUrl + local;
    }
    return url;
  };

  const GM_getValue = (key, def) => (Object.prototype.hasOwnProperty.call(store, key) ? clone(store[key]) : def);
  const GM_setValue = (key, value) => {
    store[key] = clone(value);
    host.send('gm:storage-set', { id: plugin.id, key, value: store[key] });
  };
  const GM_deleteValue = (key) => {
    delete store[key];
    host.send('gm:storage-set', { id: plugin.id, key, value: undefined });
  };
  const GM_listValues = () => Object.keys(store);
  const GM_addValueChangeListener = (key, cb) => { const id = ++listenerSeq; listeners.set(id, { key, cb }); return id; };
  const GM_removeValueChangeListener = (id) => { listeners.delete(id); };

  const meta = plugin.meta || {};
  const GM_info = Object.freeze({
    script: Object.freeze({
      name: meta.name || plugin.name, namespace: meta.namespace || '', version: plugin.version,
      description: meta.description || plugin.description, author: meta.author || plugin.author,
      grant: meta.grant || [], matches: meta.match || [], excludes: meta.exclude || [], connects: meta.connect || [],
      'run-at': meta['run-at'] || 'document-idle'
    }),
    scriptHandler: 'UVE Player',
    version: appVersion,
    platform: { os: 'win', arch: 'x86-64', browserName: 'chrome' },
    scriptMetaStr: ''
  });

  function GM_xmlhttpRequest(details) {
    const d = details || {};
    const reqId = plugin.id + ':' + (++reqSeq);
    let aborted = false;
    const fire = (name, arg) => { try { if (typeof d[name] === 'function') d[name](arg); } catch (e) { console.error(e); } };
    let data = d.data;
    const prep = (async () => {
      if (data instanceof Blob) data = new Uint8Array(await data.arrayBuffer());
      else if (data instanceof ArrayBuffer) data = new Uint8Array(data);
      else if (data instanceof FormData || data instanceof URLSearchParams) data = String(new URLSearchParams(data));
      else if (data != null && typeof data === 'object' && !(data instanceof Uint8Array)) data = JSON.stringify(data);
    })();
    prep.then(() => host.invoke('gm:xhr', {
      pluginId: plugin.id, reqId, url: String(d.url), method: d.method || 'GET',
      headers: d.headers || {}, data: data == null ? null : data, timeout: d.timeout || 0
    })).then((r) => {
      if (aborted) return;
      if (r.error === 'timeout') { fire('ontimeout', { status: 0, readyState: 4 }); fire('onloadend', {}); return; }
      if (r.error === 'abort') { fire('onabort', {}); fire('onloadend', {}); return; }
      if (r.error) {
        console.warn('[UVE Player] GM_xmlhttpRequest', r.message || r.error);
        fire('onerror', { status: 0, readyState: 4, error: r.message || r.error, finalUrl: d.url });
        fire('onloadend', {}); return;
      }
      const bytes = r.body instanceof Uint8Array ? r.body : new Uint8Array(r.body || []);
      let text = null;
      const getText = () => (text === null ? (text = new TextDecoder().decode(bytes)) : text);
      const type = (d.responseType || '').toLowerCase();
      const ctype = (/content-type:\s*([^\r\n;]+)/i.exec(r.headers) || [])[1] || '';
      let response;
      if (type === 'arraybuffer') response = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
      else if (type === 'blob') response = new Blob([bytes], { type: ctype });
      else if (type === 'json') { try { response = JSON.parse(getText()); } catch (_) { response = null; } }
      else response = getText();
      const resp = {
        status: r.status, statusText: r.statusText, readyState: 4, finalUrl: r.finalUrl,
        responseHeaders: r.headers, response,
        get responseText() { return getText(); },
        context: d.context
      };
      fire('onreadystatechange', resp);
      fire('onload', resp);
      fire('onloadend', resp);
    }).catch((e) => { if (!aborted) { fire('onerror', { status: 0, readyState: 4, error: String(e) }); fire('onloadend', {}); } });
    return { abort() { aborted = true; host.send('gm:xhr-abort', reqId); fire('onabort', {}); } };
  }

  function GM_download(arg, name) {
    const d = typeof arg === 'string' ? { url: arg, name } : (arg || {});
    (async () => {
      try {
        const res = await fetch(d.url);
        const buf = new Uint8Array(await res.arrayBuffer());
        const r = await host.invoke('gm:download', { name: d.name || 'download', data: buf, saveAs: d.saveAs !== false });
        if (r.canceled) { if (d.onerror) d.onerror({ error: 'not_succeeded', details: 'canceled' }); return; }
        if (d.onload) d.onload({ path: r.path });
      } catch (e) {
        if (d.onerror) d.onerror({ error: 'not_succeeded', details: String(e) });
      }
    })();
    return { abort() { } };
  }

  function GM_addElement(a, b, c) {
    let parent, tag, attrs;
    if (typeof a === 'string') { tag = a; attrs = b; } else { parent = a; tag = b; attrs = c; }
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (k === 'textContent') el.textContent = v;
      else if (k === 'innerHTML') el.innerHTML = v;
      else if (k === 'src' || k === 'href') el.setAttribute(k, localize(v));
      else if (typeof v === 'boolean') { if (v) el.setAttribute(k, ''); el[k] = v; }
      else el.setAttribute(k, v);
    }
    (parent || (tag === 'script' || tag === 'style' || tag === 'link' ? document.head : document.body) || document.documentElement).appendChild(el);
    return el;
  }

  const GM_addStyle = (css) => GM_addElement('style', { textContent: css });
  const GM_openInTab = (url) => { host.invoke('shell:open-external', url); return { close() { } }; };
  const GM_setClipboard = (text) => navigator.clipboard && navigator.clipboard.writeText(String(text));
  const GM_registerMenuCommand = (caption, fn) => {
    window.dispatchEvent(new CustomEvent('uve:plugin-menu', { detail: { plugin: plugin.id, caption, fn } }));
    return caption;
  };
  const GM_notification = (opt, title) => {
    const text = typeof opt === 'string' ? opt : (opt && opt.text) || '';
    window.dispatchEvent(new CustomEvent('uve:osd', { detail: (title ? title + ': ' : '') + text }));
  };

  const api = {
    GM_getValue, GM_setValue, GM_deleteValue, GM_listValues, GM_addValueChangeListener, GM_removeValueChangeListener,
    GM_info, GM_xmlhttpRequest, GM_download, GM_addElement, GM_addStyle, GM_openInTab, GM_setClipboard,
    GM_registerMenuCommand, GM_notification, unsafeWindow: window
  };
  // Promise-basierte GM.*-Variante (Greasemonkey 4)
  api.GM = Object.freeze({
    info: GM_info,
    getValue: async (k, d) => GM_getValue(k, d), setValue: async (k, v) => GM_setValue(k, v),
    deleteValue: async (k) => GM_deleteValue(k), listValues: async () => GM_listValues(),
    xmlHttpRequest: GM_xmlhttpRequest, download: GM_download, addElement: GM_addElement, addStyle: GM_addStyle,
    openInTab: GM_openInTab, setClipboard: GM_setClipboard, notification: GM_notification, registerMenuCommand: GM_registerMenuCommand
  });
  return api;
}
