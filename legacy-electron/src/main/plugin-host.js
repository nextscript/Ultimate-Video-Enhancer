'use strict';
// Plugin-Host – Gegenstück zur VLC "module bank" (plugins/*/lib*_plugin.dll + plugins.dat).
// VLC lädt Module über das Symbol vlc_entry__3_0_0f; hier beschreibt eine plugin.json
// jedes Plugin (Typ, Einstiegsdatei, Abhängigkeiten, Fähigkeiten).
//
// Suchreihenfolge: <userData>/plugins/<id>  vor  <app>/plugins/<id>
// (so können Updates des UVE-Userscripts ohne Schreibrechte im Programmordner landen).
const fs = require('fs');
const path = require('path');
const { app, net } = require('electron');
const settings = require('./settings');

const BUILTIN_DIR = path.join(__dirname, '..', '..', 'plugins').replace('app.asar' + path.sep, 'app.asar.unpacked' + path.sep);
const userDir = () => path.join(app.getPath('userData'), 'plugins');

function parseUserscriptMeta(text) {
  const meta = {};
  const block = /\/\/\s*==UserScript==([\s\S]*?)\/\/\s*==\/UserScript==/.exec(text);
  if (!block) return null;
  for (const line of block[1].split(/\r?\n/)) {
    const m = /^\s*\/\/\s*@([\w:.-]+)\s*(.*)$/.exec(line);
    if (!m) continue;
    const k = m[1]; const v = m[2].trim();
    if (['grant', 'match', 'include', 'exclude', 'connect', 'require', 'resource'].includes(k)) (meta[k] = meta[k] || []).push(v);
    else if (!(k in meta)) meta[k] = v;
  }
  return meta;
}

function readManifest(dir) {
  try {
    const mf = JSON.parse(fs.readFileSync(path.join(dir, 'plugin.json'), 'utf8'));
    if (!mf.id || !mf.main) return null;
    mf.dir = dir;
    if (mf.type === 'userscript') {
      try {
        const text = fs.readFileSync(path.join(dir, mf.main), 'utf8');
        mf.meta = parseUserscriptMeta(text) || {};
        if (mf.meta.version) mf.version = mf.meta.version;
        if (!mf.description && mf.meta.description) mf.description = mf.meta.description;
      } catch (_) { mf.meta = {}; mf.broken = true; }
    }
    return mf;
  } catch (_) {
    return null;
  }
}

function scan(dir, source, map) {
  let entries = [];
  try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch (_) { return; }
  for (const e of entries) {
    if (!e.isDirectory()) continue;
    const mf = readManifest(path.join(dir, e.name));
    if (mf && !map.has(mf.id)) { mf.source = source; map.set(mf.id, mf); }
  }
}

let cache = null;

function list(force) {
  if (cache && !force) return cache;
  const map = new Map();
  scan(userDir(), 'user', map);
  scan(BUILTIN_DIR, 'builtin', map);
  const states = settings.get('plugins') || {};
  cache = [...map.values()].map((mf) => {
    const st = states[mf.id];
    const enabled = st && typeof st.enabled === 'boolean' ? st.enabled : mf.enabledByDefault !== false;
    return {
      id: mf.id,
      name: mf.name || mf.id,
      version: mf.version || '0.0.0',
      description: mf.description || '',
      author: mf.author || (mf.meta && mf.meta.author) || '',
      homepage: mf.homepage || '',
      type: mf.type || 'script',
      main: mf.main,
      require: mf.require || [],
      requireMap: mf.requireMap || {},
      capabilities: mf.capabilities || [],
      updateUrl: mf.updateUrl || (mf.meta && (mf.meta.downloadURL || mf.meta.updateURL)) || '',
      meta: mf.meta || null,
      source: mf.source,
      dir: mf.dir,
      broken: !!mf.broken,
      enabled,
      baseUrl: `uve://app/plugins/${encodeURIComponent(mf.id)}/`
    };
  }).sort((a, b) => (a.id === 'uve' ? -1 : b.id === 'uve' ? 1 : a.name.localeCompare(b.name)));
  return cache;
}

function get(id) { return list().find((p) => p.id === id) || null; }

function resolveFile(id, rel) {
  const p = get(id);
  if (!p) return null;
  const full = path.normalize(path.join(p.dir, rel));
  if (!full.toLowerCase().startsWith(path.normalize(p.dir + path.sep).toLowerCase())) return null;
  return full;
}

function setEnabled(id, enabled) {
  const states = Object.assign({}, settings.get('plugins'));
  states[id] = Object.assign({}, states[id], { enabled: !!enabled });
  settings.set('plugins', states);
  return list(true);
}

function copyDir(src, dst) {
  fs.mkdirSync(dst, { recursive: true });
  for (const e of fs.readdirSync(src, { withFileTypes: true })) {
    const s = path.join(src, e.name); const d = path.join(dst, e.name);
    if (e.isDirectory()) copyDir(s, d); else fs.copyFileSync(s, d);
  }
}

// Holt die aktuelle Version eines Userscript-Plugins (z. B. UVE von GitHub/Greasy Fork).
async function update(id) {
  const p = get(id);
  if (!p) throw new Error('Plugin nicht gefunden: ' + id);
  if (p.type !== 'userscript' || !p.updateUrl) throw new Error('Plugin besitzt keine Update-Quelle.');
  const res = await net.fetch(p.updateUrl, { cache: 'no-store' });
  if (!res.ok) throw new Error('Download fehlgeschlagen (HTTP ' + res.status + ')');
  const text = await res.text();
  const meta = parseUserscriptMeta(text);
  if (!meta || !meta.name) throw new Error('Antwort ist kein gültiges Userscript.');
  const target = path.join(userDir(), id);
  if (p.source === 'builtin') copyDir(p.dir, target);
  fs.writeFileSync(path.join(target, p.main), text, 'utf8');
  list(true);
  return { previous: p.version, version: meta.version || '?' };
}

// Entfernt das Nutzer-Update und fällt auf die mitgelieferte Version zurück.
function resetToBuiltin(id) {
  const target = path.join(userDir(), id);
  if (fs.existsSync(path.join(BUILTIN_DIR, id))) fs.rmSync(target, { recursive: true, force: true });
  return list(true);
}

// Installiert eine beliebige *.user.js als Plugin (wie ein Userscript-Manager).
function installUserscript(file) {
  const text = fs.readFileSync(file, 'utf8');
  const meta = parseUserscriptMeta(text);
  if (!meta || !meta.name) throw new Error('Datei ist kein gültiges Userscript (==UserScript== fehlt).');
  const id = (meta.namespace ? meta.namespace + '-' : '') + meta.name;
  const slug = id.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 60) || 'userscript';
  if (slug === 'uve') throw new Error('Die ID "uve" ist reserviert.');
  const dir = path.join(userDir(), slug);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, 'script.user.js'), text, 'utf8');
  fs.writeFileSync(path.join(dir, 'plugin.json'), JSON.stringify({
    id: slug, name: meta.name, type: 'userscript', main: 'script.user.js', require: [], enabledByDefault: true
  }, null, 2));
  return list(true).find((p) => p.id === slug);
}

function uninstall(id) {
  const p = get(id);
  if (!p) return list();
  if (p.source !== 'user') throw new Error('Mitgelieferte Plugins können nur deaktiviert werden.');
  fs.rmSync(p.dir, { recursive: true, force: true });
  return list(true);
}

module.exports = { list, get, resolveFile, setEnabled, update, resetToBuiltin, installUserscript, uninstall, userDir, BUILTIN_DIR, parseUserscriptMeta };
