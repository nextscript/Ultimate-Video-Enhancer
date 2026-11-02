'use strict';
// Kompatibilitäts-Modul: Chromium dekodiert nicht alles, was VLC über libavcodec kann
// (AVI/WMV/FLV, MPEG-2, AC3/DTS …). Mit FFmpeg wird solches Material verlustfrei
// umverpackt (Remux) oder – falls nötig – in H.264/AAC umkodiert.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { spawn, spawnSync } = require('child_process');
const { app } = require('electron');
const settings = require('./settings');

const OK_VIDEO = ['h264', 'hevc', 'vp8', 'vp9', 'av1'];
const OK_AUDIO = ['aac', 'mp3', 'opus', 'vorbis', 'flac'];
const TEXT_SUBS = ['subrip', 'srt', 'ass', 'ssa', 'webvtt', 'mov_text', 'text'];

const jobs = new Map();
let cachedBin = null;

function exe(name) { return process.platform === 'win32' ? name + '.exe' : name; }

function candidates() {
  const list = [];
  if (process.env.UVE_FFMPEG) list.push(process.env.UVE_FFMPEG);
  const custom = settings.get('ffmpegPath');
  if (custom) {
    list.push(fs.existsSync(custom) && fs.statSync(custom).isDirectory() ? path.join(custom, exe('ffmpeg')) : custom);
  }
  const best = require('./tools-updater').preferredFfmpeg(); // neueste aus Benutzer-Updates oder Paket
  if (best) list.push(best);
  list.push(exe('ffmpeg')); // PATH
  return list;
}

function works(bin) {
  try {
    const r = spawnSync(bin, ['-hide_banner', '-version'], { windowsHide: true, timeout: 8000 });
    return r.status === 0;
  } catch (_) { return false; }
}

function find(force) {
  if (cachedBin && !force) return cachedBin;
  cachedBin = null;
  for (const c of candidates()) {
    if (c.includes(path.sep) && !fs.existsSync(c)) continue;
    if (works(c)) {
      const dir = path.dirname(c);
      const probe = c.includes(path.sep) ? path.join(dir, exe('ffprobe')) : exe('ffprobe');
      cachedBin = { ffmpeg: c, ffprobe: (c.includes(path.sep) ? fs.existsSync(probe) : works(probe)) ? probe : null };
      break;
    }
  }
  return cachedBin;
}

function run(bin, args, opts = {}) {
  return new Promise((resolve, reject) => {
    const p = require('./children').spawnTracked(bin, args, { windowsHide: true });
    let out = ''; let err = '';
    p.stdout.on('data', (d) => { out += d; if (opts.onStdout) opts.onStdout(String(d)); });
    p.stderr.on('data', (d) => { err += d; if (err.length > 64000) err = err.slice(-32000); });
    p.on('error', reject);
    p.on('close', (code) => (code === 0 ? resolve(out) : reject(new Error((err.trim().split(/\r?\n/).slice(-3).join(' ')) || ('Exit-Code ' + code)))));
    if (opts.onSpawn) opts.onSpawn(p);
  });
}

async function probe(file) {
  const bin = find();
  if (!bin) throw new Error('FFmpeg wurde nicht gefunden.');
  if (bin.ffprobe) {
    const out = await run(bin.ffprobe, ['-v', 'error', '-print_format', 'json', '-show_format', '-show_streams', file]);
    return JSON.parse(out);
  }
  // Fallback ohne ffprobe: grobe Auswertung der ffmpeg-Ausgabe
  const r = spawnSync(bin.ffmpeg, ['-hide_banner', '-i', file], { windowsHide: true, encoding: 'utf8' });
  const streams = [];
  for (const m of String(r.stderr).matchAll(/Stream #\d+:\d+.*?: (Video|Audio|Subtitle): (\w+)/g)) {
    streams.push({ codec_type: m[1].toLowerCase(), codec_name: m[2].toLowerCase() });
  }
  const d = /Duration: (\d+):(\d+):([\d.]+)/.exec(String(r.stderr));
  return { streams, format: { duration: d ? (+d[1] * 3600 + +d[2] * 60 + +d[3]) : 0 } };
}

function cacheDir() { return path.join(app.getPath('userData'), 'cache', 'transcode'); }

function cacheKey(file) {
  const st = fs.statSync(file);
  return crypto.createHash('sha1').update(path.resolve(file) + '|' + st.size + '|' + st.mtimeMs).digest('hex').slice(0, 16);
}

function plan(info) {
  const v = (info.streams || []).find((s) => s.codec_type === 'video' && !(s.disposition && s.disposition.attached_pic));
  const a = (info.streams || []).find((s) => s.codec_type === 'audio');
  const sub = (info.streams || []).find((s) => s.codec_type === 'subtitle' && TEXT_SUBS.includes(String(s.codec_name)));
  const vOk = !v || OK_VIDEO.includes(v.codec_name);
  const aOk = !a || OK_AUDIO.includes(a.codec_name);
  const webm = v && ['vp8', 'vp9', 'av1'].includes(v.codec_name) && (!a || ['opus', 'vorbis'].includes(a.codec_name));
  const args = [];
  if (v) args.push('-map', '0:v:0');
  if (a) args.push('-map', '0:a:0');
  args.push('-c:v', vOk ? 'copy' : 'libx264');
  if (!vOk) args.push('-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p');
  if (a) args.push('-c:a', aOk ? 'copy' : 'aac', ...(aOk ? [] : ['-b:a', '192k', '-ac', '2']));
  args.push('-sn', '-dn');
  return {
    mode: vOk && aOk ? 'remux' : 'transcode',
    ext: webm ? 'webm' : 'mp4',
    args: webm ? args : [...args, '-movflags', '+faststart'],
    video: v ? v.codec_name : null,
    audio: a ? a.codec_name : null,
    subtitle: sub ? sub.index : null,
    duration: Number(info.format && info.format.duration) || 0
  };
}

async function prepare(jobId, file, onProgress) {
  const bin = find();
  if (!bin) throw new Error('FFmpeg wurde nicht gefunden. Pfad unter Werkzeuge → Einstellungen setzen.');
  const info = await probe(file);
  const p = plan(info);
  const dir = cacheDir();
  fs.mkdirSync(dir, { recursive: true });
  const key = cacheKey(file);
  const out = path.join(dir, key + '.' + p.ext);
  const subOut = path.join(dir, key + '.vtt');

  if (!fs.existsSync(out)) {
    const tmp = out + '.part.' + p.ext;
    try { fs.rmSync(tmp, { force: true }); } catch (_) { }
    onProgress({ stage: p.mode, percent: 0, plan: p });
    let last = 0;
    await run(bin.ffmpeg, ['-hide_banner', '-nostats', '-y', '-i', file, ...p.args, '-progress', 'pipe:1', tmp], {
      onSpawn: (proc) => jobs.set(jobId, proc),
      onStdout: (txt) => {
        const m = /out_time_(?:ms|us)=(\d+)/g; let x; let us = null;
        while ((x = m.exec(txt))) us = Number(x[1]);
        if (us != null && p.duration > 0) {
          const pct = Math.min(99, (us / 1e6 / p.duration) * 100);
          if (pct - last >= 0.5) { last = pct; onProgress({ stage: p.mode, percent: pct, plan: p }); }
        }
      }
    }).finally(() => jobs.delete(jobId));
    fs.renameSync(tmp, out);
  }

  if (p.subtitle != null && !fs.existsSync(subOut)) {
    try { await run(bin.ffmpeg, ['-hide_banner', '-y', '-i', file, '-map', '0:' + p.subtitle, '-c:s', 'webvtt', subOut]); } catch (_) { }
  }
  onProgress({ stage: 'done', percent: 100, plan: p });
  return { out, sub: fs.existsSync(subOut) ? subOut : null, plan: p };
}

// Konvertiert ASS/SSA/SUB-Untertitel in WebVTT.
async function convertSubtitle(file) {
  const bin = find();
  if (!bin) throw new Error('FFmpeg wird für dieses Untertitelformat benötigt.');
  const dir = cacheDir();
  fs.mkdirSync(dir, { recursive: true });
  const out = path.join(dir, cacheKey(file) + '.sub.vtt');
  if (!fs.existsSync(out)) await run(bin.ffmpeg, ['-hide_banner', '-y', '-i', file, '-c:s', 'webvtt', out]);
  return out;
}

// Extrahiert alle eingebetteten Text-Untertitel (SRT/ASS/WebVTT/mov_text) in einem Durchlauf als WebVTT.
// Laufende Extraktionen derselben Datei werden geteilt (z. B. bei Audiospurwechsel während des Auslesens).
const subJobs = new Map();
function extractSubtitles(file) {
  const k = path.resolve(file).toLowerCase();
  if (!subJobs.has(k)) subJobs.set(k, extractSubtitlesOnce(file).finally(() => subJobs.delete(k)));
  return subJobs.get(k).then((list) => list.map((s) => Object.assign({}, s)));
}

async function extractSubtitlesOnce(file) {
  const bin = find();
  if (!bin) return [];
  const info = await probe(file);
  const subs = (info.streams || []).filter((s) => s.codec_type === 'subtitle' && TEXT_SUBS.includes(String(s.codec_name)));
  if (!subs.length) return [];
  const dir = cacheDir();
  fs.mkdirSync(dir, { recursive: true });
  const key = cacheKey(file);
  const out = subs.map((s) => ({
    index: s.index,
    file: path.join(dir, `${key}.s${s.index}.vtt`),
    lang: (s.tags && s.tags.language) || 'und',
    title: (s.tags && s.tags.title) || '',
    default: !!(s.disposition && s.disposition.default),
    forced: !!(s.disposition && s.disposition.forced) || /forced/i.test((s.tags && s.tags.title) || '')
  }));
  const missing = out.filter((o) => !fs.existsSync(o.file));
  if (missing.length) {
    const args = ['-hide_banner', '-loglevel', 'error', '-y', '-i', file];
    const tag = process.pid + '-' + Date.now();
    for (const m of missing) args.push('-map', '0:' + m.index, '-c:s', 'webvtt', `${m.file}.${tag}.part.vtt`);
    await run(bin.ffmpeg, args);
    for (const m of missing) {
      try { fs.renameSync(`${m.file}.${tag}.part.vtt`, m.file); }
      catch (e) { if (!fs.existsSync(m.file)) throw e; try { fs.rmSync(`${m.file}.${tag}.part.vtt`, { force: true }); } catch (_) { } }
    }
  }
  return out;
}

function cancel(jobId) {
  const p = jobs.get(jobId);
  if (p) { try { p.kill(); } catch (_) { } jobs.delete(jobId); }
}

function cancelAll() { for (const id of [...jobs.keys()]) cancel(id); }

function clearCache() {
  cancelAll();
  fs.rmSync(cacheDir(), { recursive: true, force: true });
}

function cacheSize() {
  let total = 0;
  try { for (const f of fs.readdirSync(cacheDir())) total += fs.statSync(path.join(cacheDir(), f)).size; } catch (_) { }
  return total;
}

module.exports = { find, probe, prepare, convertSubtitle, extractSubtitles, cancel, cancelAll, clearCache, cacheSize };
