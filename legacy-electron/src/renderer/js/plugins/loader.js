// Plugin-Loader – lädt aktivierte Plugins in die Oberfläche.
//  type "userscript": Userscript mit GM-Kompatibilitätsschicht (z. B. Ultimate Video Enhancer)
//  type "script":     ES-Modul mit  export default function activate(api) { … }
import { createGM } from './gm-shim.js';

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.async = false;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error('Konnte nicht geladen werden: ' + src));
    document.head.appendChild(s);
  });
}

async function runUserscript(plugin, appVersion) {
  for (const req of plugin.require) await loadScript(plugin.baseUrl + req);
  const res = await fetch(plugin.baseUrl + plugin.main, { cache: 'no-store' });
  if (!res.ok) throw new Error('Hauptdatei fehlt (HTTP ' + res.status + ')');
  const code = await res.text();
  const gm = await createGM(plugin, appVersion);
  const names = Object.keys(gm);
  // Wie ein Userscript-Manager: Code in eigener Funktion mit GM_*-Parametern ausführen.
  const fn = new Function(...names, code + '\n//# sourceURL=uve-plugin://' + plugin.id + '/' + plugin.main);
  fn.apply(window, names.map((n) => gm[n]));
}

async function runModule(plugin, api) {
  const mod = await import(plugin.baseUrl + plugin.main);
  const activate = mod.default || mod.activate;
  if (typeof activate === 'function') await activate(Object.assign({ plugin }, api));
}

export async function loadPlugins(plugins, ctx) {
  const results = [];
  for (const p of plugins) {
    if (!p.enabled) { results.push({ id: p.id, state: 'disabled' }); continue; }
    if (p.broken) { results.push({ id: p.id, state: 'error', error: 'Plugin-Dateien beschädigt' }); continue; }
    const t0 = performance.now();
    try {
      if (p.type === 'userscript') await runUserscript(p, ctx.appVersion);
      else await runModule(p, ctx.api);
      results.push({ id: p.id, state: 'loaded', ms: Math.round(performance.now() - t0) });
      console.info(`[UVE Player] Plugin geladen: ${p.name} ${p.version} (${Math.round(performance.now() - t0)} ms)`);
    } catch (e) {
      console.error('[UVE Player] Plugin-Fehler', p.id, e);
      results.push({ id: p.id, state: 'error', error: String(e && e.message || e) });
    }
  }
  return results;
}
