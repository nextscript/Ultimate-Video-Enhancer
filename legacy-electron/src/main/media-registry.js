'use strict';
// Medien-Registry: nur Dateien/URLs, die der Nutzer selbst geöffnet hat, werden
// über uve://app/media bzw. uve://app/remote ausgeliefert (Allowlist).
const fs = require('fs');
const path = require('path');

const VIDEO_EXT = ['mp4', 'm4v', 'mkv', 'webm', 'mov', 'avi', 'wmv', 'flv', 'ts', 'm2ts', 'mts', 'mpg', 'mpeg', '3gp', 'ogv', 'vob', 'divx', 'xvid', 'rm', 'rmvb', 'asf', 'f4v'];
const AUDIO_EXT = ['mp3', 'm4a', 'aac', 'flac', 'wav', 'ogg', 'oga', 'opus', 'wma', 'ac3', 'dts', 'mka', 'aiff', 'alac'];
const SUB_EXT = ['srt', 'vtt', 'ass', 'ssa', 'sub'];
const PLAYLIST_EXT = ['m3u', 'm3u8', 'pls'];

const MIME = {
  mp4: 'video/mp4', m4v: 'video/mp4', mov: 'video/quicktime', webm: 'video/webm', mkv: 'video/x-matroska',
  ogv: 'video/ogg', avi: 'video/x-msvideo', ts: 'video/mp2t', m2ts: 'video/mp2t', mts: 'video/mp2t',
  mpg: 'video/mpeg', mpeg: 'video/mpeg', '3gp': 'video/3gpp', wmv: 'video/x-ms-wmv', flv: 'video/x-flv',
  mp3: 'audio/mpeg', m4a: 'audio/mp4', aac: 'audio/aac', flac: 'audio/flac', wav: 'audio/wav',
  ogg: 'audio/ogg', oga: 'audio/ogg', opus: 'audio/ogg', mka: 'audio/x-matroska', wma: 'audio/x-ms-wma',
  srt: 'text/plain; charset=utf-8', vtt: 'text/vtt; charset=utf-8', ass: 'text/plain; charset=utf-8', ssa: 'text/plain; charset=utf-8',
  png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg'
};

const allowed = new Set();
const allowedRemote = new Set();

const ext = (p) => path.extname(p).slice(1).toLowerCase();
const key = (p) => path.resolve(p).toLowerCase();
const isUrl = (s) => /^(https?|ftp|tcp|udp|rtp|rtmps?|rtmpt|rtsps?|srt|mmsh?):\/\//i.test(String(s));
const isMedia = (p) => VIDEO_EXT.includes(ext(p)) || AUDIO_EXT.includes(ext(p));

function mimeOf(p) { return MIME[ext(p)] || 'application/octet-stream'; }

function allow(p) { allowed.add(key(p)); }
function isAllowed(p) { return allowed.has(key(p)); }
function isAllowedRemote(u) { return allowedRemote.has(u); }

function mediaUrl(p) { return 'uve://app/media?p=' + encodeURIComponent(path.resolve(p)); }

function walk(dir, out, depth = 0) {
  if (depth > 6) return;
  let entries = [];
  try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch (_) { return; }
  entries.sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: 'base' }));
  for (const e of entries) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) walk(full, out, depth + 1);
    else if (isMedia(full)) out.push(full);
  }
}

function parsePlaylist(file) {
  const out = [];
  let text = '';
  try { text = fs.readFileSync(file, 'utf8').replace(/^﻿/, ''); } catch (_) { return out; }
  const base = path.dirname(file);
  if (ext(file) === 'pls') {
    for (const line of text.split(/\r?\n/)) {
      const m = /^File\d+=(.+)$/i.exec(line.trim());
      if (m) out.push(isUrl(m[1]) ? m[1] : path.resolve(base, m[1]));
    }
    return out;
  }
  // HLS-Manifeste sind keine Wiedergabelisten im Sinne von M3U
  if (/#EXT-X-(TARGETDURATION|STREAM-INF|MEDIA-SEQUENCE)/.test(text)) return null;
  for (let line of text.split(/\r?\n/)) {
    line = line.trim();
    if (!line || line.startsWith('#')) continue;
    if (/^file:\/\//i.test(line)) { try { line = decodeURIComponent(new URL(line).pathname).replace(/^\/([A-Za-z]:)/, '$1'); } catch (_) { continue; } }
    out.push(isUrl(line) ? line : path.resolve(base, line));
  }
  return out;
}

function findSidecarSubs(p) {
  const dir = path.dirname(p);
  const stem = path.basename(p, path.extname(p)).toLowerCase();
  let entries = [];
  try { entries = fs.readdirSync(dir); } catch (_) { return []; }
  return entries
    .filter((n) => SUB_EXT.includes(ext(n)) && n.toLowerCase().startsWith(stem))
    .map((n) => path.join(dir, n));
}

function describe(p) {
  if (isUrl(p)) {
    const stype = require('./stream').classify(p);
    if (stype) {
      // Netzwerkquelle über FFmpeg/yt-dlp – wird erst beim Abspielen aufgelöst (URLs laufen ab)
      return { kind: 'stream', streamType: stype, path: p, name: require('./stream').displayName(p, stype), url: null, subs: [] };
    }
    allowedRemote.add(p);
    let name = p;
    try { const u = new URL(p); name = decodeURIComponent(u.pathname.split('/').filter(Boolean).pop() || u.hostname); } catch (_) { }
    return { kind: 'remote', path: p, name, url: 'uve://app/remote?u=' + encodeURIComponent(p), subs: [] };
  }
  allow(p);
  const subs = findSidecarSubs(p);
  subs.forEach(allow);
  let size = 0;
  try { size = fs.statSync(p).size; } catch (_) { }
  return {
    kind: AUDIO_EXT.includes(ext(p)) ? 'audio' : 'video',
    path: path.resolve(p),
    name: path.basename(p),
    url: mediaUrl(p),
    size,
    subs: subs.map((s) => ({ path: s, name: path.basename(s), url: mediaUrl(s), ext: ext(s) }))
  };
}

// Löst Dateien, Ordner, Wiedergabelisten und URLs zu abspielbaren Einträgen auf.
function resolve(inputs) {
  const out = [];
  const seen = new Set();
  const push = (p) => {
    const k = isUrl(p) ? p : key(p);
    if (seen.has(k)) return;
    seen.add(k);
    out.push(describe(p));
  };
  for (const raw of inputs || []) {
    if (!raw) continue;
    const p = String(raw);
    if (isUrl(p)) { push(p); continue; }
    let st;
    try { st = fs.statSync(p); } catch (_) { continue; }
    if (st.isDirectory()) {
      const files = [];
      walk(p, files);
      files.forEach(push);
    } else if (PLAYLIST_EXT.includes(ext(p))) {
      const list = parsePlaylist(p);
      if (list === null) push(p); // HLS-Manifest direkt abspielen
      else list.forEach((x) => { if (isUrl(x) || fs.existsSync(x)) push(x); });
    } else if (SUB_EXT.includes(ext(p))) {
      allow(p);
      out.push({ kind: 'subtitle', path: path.resolve(p), name: path.basename(p), url: mediaUrl(p), ext: ext(p) });
    } else {
      push(p);
    }
  }
  return out;
}

function writePlaylist(file, items) {
  const lines = ['#EXTM3U'];
  for (const it of items) {
    lines.push('#EXTINF:-1,' + (it.name || ''));
    lines.push(it.path);
  }
  fs.writeFileSync(file, lines.join('\r\n') + '\r\n', 'utf8');
}

module.exports = {
  VIDEO_EXT, AUDIO_EXT, SUB_EXT, PLAYLIST_EXT,
  resolve, describe, allow, isAllowed, isAllowedRemote, mimeOf, mediaUrl, writePlaylist, isUrl, ext
};
