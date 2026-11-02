'use strict';
// Werkzeug-Updater für FFmpeg (BtbN-Builds) und yt-dlp.
// Mitgelieferte Versionen liegen in <App>\tools\…, Updates landen versioniert im Benutzerordner
// (<userData>\tools\<tool>\<version>\), weil die portable EXE bei jedem Start neu entpackt wird
// und Programmordner nicht immer beschreibbar sind. Verwendet wird jeweils die neueste Version.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { spawnSync } = require('child_process');
const { app, net } = require('electron');

const FFMPEG_REPO = 'BtbN/FFmpeg-Builds';
const FFMPEG_ASSET = 'ffmpeg-master-latest-win64-gpl.zip';
const YTDLP_REPO = 'yt-dlp/yt-dlp';

const exe = (n) => (process.platform === 'win32' ? n + '.exe' : n);
const userToolsDir = () => path.join(app.getPath('userData'), 'tools');
const bundledToolsDirs = () => [
  path.join(path.dirname(process.execPath), 'tools'),
  path.join(__dirname, '..', '..', 'tools')
];

function readJson(file) { try { return JSON.parse(fs.readFileSync(file, 'utf8')); } catch (_) { return null; } }

// ---------------------------------------------------------------- installierte Versionen
function ffmpegInstalls() {
  const list = [];
  for (const base of bundledToolsDirs()) {
    const dir = path.join(base, 'ffmpeg');
    if (fs.existsSync(path.join(dir, exe('ffmpeg')))) list.push({ dir, source: 'bundled', meta: readJson(path.join(dir, 'version.json')) || {} });
  }
  const udir = path.join(userToolsDir(), 'ffmpeg');
  try {
    for (const v of fs.readdirSync(udir)) {
      const dir = path.join(udir, v);
      if (fs.existsSync(path.join(dir, exe('ffmpeg'))) && fs.existsSync(path.join(dir, 'version.json'))) list.push({ dir, source: 'user', meta: readJson(path.join(dir, 'version.json')) || {} });
    }
  } catch (_) { }
  return list.sort((a, b) => String(b.meta.publishedAt || '').localeCompare(String(a.meta.publishedAt || '')));
}

function ytdlpVersion(file) {
  const meta = readJson(path.join(path.dirname(file), 'version.json'));
  if (meta && meta.version) return meta.version;
  try {
    const r = spawnSync(file, ['--version'], { windowsHide: true, timeout: 15000, encoding: 'utf8' });
    return r.status === 0 ? String(r.stdout).trim() : '';
  } catch (_) { return ''; }
}

function ytdlpInstalls() {
  const list = [];
  for (const base of bundledToolsDirs()) {
    const file = path.join(base, 'yt-dlp', exe('yt-dlp'));
    if (fs.existsSync(file)) list.push({ file, source: 'bundled', version: ytdlpVersion(file) });
  }
  const udir = path.join(userToolsDir(), 'yt-dlp');
  try {
    for (const v of fs.readdirSync(udir)) {
      const file = path.join(udir, v, exe('yt-dlp'));
      if (fs.existsSync(file)) list.push({ file, source: 'user', version: ytdlpVersion(file) });
    }
  } catch (_) { }
  return list.sort((a, b) => String(b.version).localeCompare(String(a.version), undefined, { numeric: true }));
}

// Pfade für transcode.js / stream.js (neueste zuerst)
function preferredFfmpeg() { const i = ffmpegInstalls()[0]; return i ? path.join(i.dir, exe('ffmpeg')) : null; }
function preferredYtdlp() { const i = ytdlpInstalls()[0]; return i ? i.file : null; }

// ---------------------------------------------------------------- Netzwerk
async function ghRelease(repo, tag) {
  const url = `https://api.github.com/repos/${repo}/releases/${tag ? 'tags/' + tag : 'latest'}`;
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), 15000);
  try {
    const res = await net.fetch(url, { headers: { Accept: 'application/vnd.github+json', 'User-Agent': 'UVE-Player' }, signal: ctrl.signal });
    if (!res.ok) throw new Error('GitHub HTTP ' + res.status);
    return await res.json();
  } finally { clearTimeout(t); }
}

async function download(url, file, onProgress) {
  const res = await net.fetch(url, { headers: { 'User-Agent': 'UVE-Player' } });
  if (!res.ok) throw new Error('Download fehlgeschlagen (HTTP ' + res.status + ')');
  const total = Number(res.headers.get('content-length')) || 0;
  const hash = crypto.createHash('sha256');
  const out = fs.createWriteStream(file);
  let done = 0; let last = 0;
  const reader = res.body.getReader();
  try {
    for (;;) {
      const { value, done: end } = await reader.read();
      if (end) break;
      hash.update(value);
      done += value.length;
      if (!out.write(value)) await new Promise((r) => out.once('drain', r));
      if (onProgress && Date.now() - last > 200) { last = Date.now(); onProgress(done, total); }
    }
  } finally {
    await new Promise((r) => out.end(r));
  }
  if (onProgress) onProgress(done, total);
  return hash.digest('hex');
}

async function fetchText(url) {
  const res = await net.fetch(url, { headers: { 'User-Agent': 'UVE-Player' } });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return res.text();
}

function checksumFor(sums, name) {
  for (const line of sums.split(/\r?\n/)) {
    const m = /^([a-f0-9]{64})\s+\*?(.+)$/i.exec(line.trim());
    if (m && m[2].trim() === name) return m[1].toLowerCase();
  }
  return null;
}

// ---------------------------------------------------------------- Prüfen
async function check() {
  const result = { updates: [], errors: [] };
  const tasks = [
    (async () => {
      const rel = await ghRelease(FFMPEG_REPO, 'latest');
      const asset = (rel.assets || []).find((a) => a.name === FFMPEG_ASSET);
      if (!asset) throw new Error('FFmpeg-Build nicht gefunden');
      const cur = ffmpegInstalls()[0];
      const curPub = cur && cur.meta.publishedAt;
      if (!curPub || new Date(rel.published_at) > new Date(curPub)) {
        result.updates.push({
          tool: 'ffmpeg', name: 'FFmpeg', current: cur ? (cur.meta.version || 'unbekannt') : 'nicht installiert',
          currentDate: curPub || null, latest: rel.name || rel.published_at, latestDate: rel.published_at, size: asset.size, id: rel.published_at
        });
      }
    })().catch((e) => result.errors.push('FFmpeg: ' + e.message)),
    (async () => {
      const rel = await ghRelease(YTDLP_REPO);
      const asset = (rel.assets || []).find((a) => a.name === exe('yt-dlp'));
      if (!asset) throw new Error('yt-dlp.exe nicht gefunden');
      const cur = ytdlpInstalls()[0];
      if (!cur || String(rel.tag_name).localeCompare(String(cur.version), undefined, { numeric: true }) > 0) {
        result.updates.push({
          tool: 'ytdlp', name: 'yt-dlp', current: cur ? cur.version : 'nicht installiert', latest: rel.tag_name,
          latestDate: rel.published_at, size: asset.size, id: rel.tag_name
        });
      }
    })().catch((e) => result.errors.push('yt-dlp: ' + e.message))
  ];
  await Promise.all(tasks);
  return result;
}

// ---------------------------------------------------------------- Installieren
async function installFfmpeg(onProgress) {
  const rel = await ghRelease(FFMPEG_REPO, 'latest');
  const asset = rel.assets.find((a) => a.name === FFMPEG_ASSET);
  const sumsAsset = rel.assets.find((a) => a.name === 'checksums.sha256');
  const base = path.join(userToolsDir(), 'ffmpeg');
  const tmp = path.join(userToolsDir(), '.tmp-ffmpeg-' + Date.now());
  fs.mkdirSync(tmp, { recursive: true });
  try {
    const zip = path.join(tmp, asset.name);
    const sha = await download(asset.browser_download_url, zip, (d, t) => onProgress({ stage: 'download', done: d, total: t || asset.size }));
    if (sumsAsset) {
      const expected = checksumFor(await fetchText(sumsAsset.browser_download_url), asset.name);
      if (expected && expected !== sha) throw new Error('Prüfsumme von FFmpeg stimmt nicht – Download verworfen.');
    }
    onProgress({ stage: 'extract' });
    const ex = path.join(tmp, 'x');
    fs.mkdirSync(ex);
    const tar = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'tar.exe');
    const r = spawnSync(fs.existsSync(tar) ? tar : 'tar', ['-xf', zip, '-C', ex], { windowsHide: true, timeout: 300000 });
    if (r.status !== 0) throw new Error('Entpacken fehlgeschlagen: ' + String(r.stderr || '').trim());
    const find = (dir, name) => {
      for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) { const f = find(p, name); if (f) return f; } else if (e.name.toLowerCase() === name) return p;
      }
      return null;
    };
    const ff = find(ex, exe('ffmpeg')); const fp = find(ex, exe('ffprobe'));
    if (!ff || !fp) throw new Error('ffmpeg.exe/ffprobe.exe nicht im Archiv gefunden');
    const ver = spawnSync(ff, ['-hide_banner', '-version'], { windowsHide: true, encoding: 'utf8', timeout: 15000 });
    if (ver.status !== 0) throw new Error('Neue ffmpeg.exe startet nicht');
    const version = (/ffmpeg version (\S+)/.exec(ver.stdout) || [])[1] || rel.published_at;
    const dest = path.join(base, rel.published_at.replace(/[^0-9TZ]/g, ''));
    fs.mkdirSync(dest, { recursive: true });
    fs.copyFileSync(ff, path.join(dest, exe('ffmpeg')));
    fs.copyFileSync(fp, path.join(dest, exe('ffprobe')));
    const lic = find(ex, 'license.txt');
    if (lic) fs.copyFileSync(lic, path.join(dest, 'LICENSE.txt'));
    fs.writeFileSync(path.join(dest, 'version.json'), JSON.stringify({
      version, build: rel.name, publishedAt: rel.published_at, installedAt: Date.now(), source: asset.browser_download_url
    }, null, 2));
    return { version };
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

async function installYtdlp(onProgress) {
  const rel = await ghRelease(YTDLP_REPO);
  const asset = rel.assets.find((a) => a.name === exe('yt-dlp'));
  const sumsAsset = rel.assets.find((a) => a.name === 'SHA2-256SUMS');
  const dest = path.join(userToolsDir(), 'yt-dlp', rel.tag_name.replace(/[^\w.-]/g, '_'));
  fs.mkdirSync(dest, { recursive: true });
  const file = path.join(dest, exe('yt-dlp'));
  const part = file + '.part';
  try {
    const sha = await download(asset.browser_download_url, part, (d, t) => onProgress({ stage: 'download', done: d, total: t || asset.size }));
    if (sumsAsset) {
      const expected = checksumFor(await fetchText(sumsAsset.browser_download_url), asset.name);
      if (!expected || expected !== sha) throw new Error('Prüfsumme von yt-dlp stimmt nicht – Download verworfen.');
    }
    fs.renameSync(part, file);
    const v = spawnSync(file, ['--version'], { windowsHide: true, encoding: 'utf8', timeout: 20000 });
    if (v.status !== 0) throw new Error('Neue yt-dlp.exe startet nicht');
    fs.writeFileSync(path.join(dest, 'version.json'), JSON.stringify({ version: String(v.stdout).trim(), publishedAt: rel.published_at, installedAt: Date.now() }, null, 2));
    return { version: String(v.stdout).trim() };
  } catch (e) {
    fs.rmSync(dest, { recursive: true, force: true });
    throw e;
  }
}

async function install(tool, onProgress) {
  const r = tool === 'ffmpeg' ? await installFfmpeg(onProgress) : await installYtdlp(onProgress);
  cleanup();
  return r;
}

// Alte Benutzer-Versionen entfernen (die jeweils neueste bleibt; gesperrte Dateien werden übersprungen)
function cleanup() {
  const keep = new Set([path.dirname(preferredFfmpeg() || ''), path.dirname(preferredYtdlp() || '')]);
  for (const t of ['ffmpeg', 'yt-dlp']) {
    const dir = path.join(userToolsDir(), t);
    try {
      for (const v of fs.readdirSync(dir)) {
        const p = path.join(dir, v);
        if (!keep.has(p)) { try { fs.rmSync(p, { recursive: true, force: true }); } catch (_) { } }
      }
    } catch (_) { }
  }
  try { for (const e of fs.readdirSync(userToolsDir())) if (e.startsWith('.tmp-')) fs.rmSync(path.join(userToolsDir(), e), { recursive: true, force: true }); } catch (_) { }
}

function status() {
  const f = ffmpegInstalls()[0];
  const y = ytdlpInstalls()[0];
  return {
    ffmpeg: f ? { path: path.join(f.dir, exe('ffmpeg')), version: f.meta.version || '', publishedAt: f.meta.publishedAt || '', source: f.source } : null,
    ytdlp: y ? { path: y.file, version: y.version, source: y.source } : null
  };
}

module.exports = { check, install, cleanup, status, preferredFfmpeg, preferredYtdlp };
