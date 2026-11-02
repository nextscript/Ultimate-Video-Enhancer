'use strict';
// Stream-Modul (Gegenstück zu VLCs access/demux-Modulen für Netzwerkquellen).
// Quellen, die Chromium nicht direkt abspielen kann (tcp://, udp://, rtmp://, rtsp://, srt://,
// HLS, YouTube, Twitch …), werden von FFmpeg gelesen und als fragmentiertes MP4 über
// uve://app/live?id=…&ss=… an die Oberfläche geliefert (Media Source Extensions).
// YouTube/Twitch/sonstige Webseiten werden vorher mit yt-dlp in direkte Medien-URLs aufgelöst.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { spawn, spawnSync } = require('child_process');
const settings = require('./settings');
const transcode = require('./transcode');

const FFMPEG_SCHEMES = ['tcp', 'udp', 'rtp', 'rtmp', 'rtmps', 'rtmpt', 'rtsp', 'rtsps', 'srt', 'mms', 'mmsh'];
const DIRECT_EXT = /\.(mp4|m4v|webm|mov|mp3|m4a|aac|ogg|oga|opus|flac|wav|ogv)(\?|#|$)/i;
const sessions = new Map();
let cachedYtdlp;

function exe(n) { return process.platform === 'win32' ? n + '.exe' : n; }

// ---------------------------------------------------------------- Klassifizierung
function classify(url) {
  let u;
  try { u = new URL(url); } catch (_) { return null; }
  const scheme = u.protocol.replace(':', '').toLowerCase();
  const host = u.hostname.toLowerCase().replace(/^www\.|^m\./, '');
  if (FFMPEG_SCHEMES.includes(scheme)) return scheme;
  if (scheme !== 'http' && scheme !== 'https') return null;
  if (/(^|\.)youtube\.com$|^youtu\.be$|(^|\.)youtube-nocookie\.com$/.test(host)) return 'youtube';
  if (/(^|\.)twitch\.tv$/.test(host)) return 'twitch';
  if (/\.(m3u8|mpd|ts|flv|mkv|avi)(\?|#|$)/i.test(u.pathname)) return 'ffmpeg-http';
  if (DIRECT_EXT.test(u.pathname)) return null; // direkt abspielbar (bestehender Proxy)
  return 'web'; // beliebige Seite → yt-dlp versuchen
}

function displayName(url, type) {
  try {
    const u = new URL(url);
    if (type === 'youtube') return 'YouTube: ' + (u.searchParams.get('v') || u.pathname.split('/').filter(Boolean).pop() || '');
    if (type === 'twitch') return 'Twitch: ' + (u.pathname.split('/').filter(Boolean)[0] || '');
    if (FFMPEG_SCHEMES.includes(type)) return type.toUpperCase() + ': ' + u.host + u.pathname.replace(/\/$/, '');
    return decodeURIComponent(u.pathname.split('/').filter(Boolean).pop() || u.hostname);
  } catch (_) { return url; }
}

// ---------------------------------------------------------------- yt-dlp
function findYtdlp(force) {
  if (cachedYtdlp !== undefined && !force) return cachedYtdlp;
  cachedYtdlp = null;
  const list = [];
  if (process.env.UVE_YTDLP) list.push(process.env.UVE_YTDLP);
  const custom = settings.get('ytdlpPath');
  if (custom) list.push(fs.existsSync(custom) && fs.statSync(custom).isDirectory() ? path.join(custom, exe('yt-dlp')) : custom);
  const best = require('./tools-updater').preferredYtdlp(); // neueste aus Benutzer-Updates oder Paket
  if (best) list.push(best);
  list.push(exe('yt-dlp'));
  for (const c of list) {
    if (c.includes(path.sep) && !fs.existsSync(c)) continue;
    try {
      const r = spawnSync(c, ['--version'], { windowsHide: true, timeout: 15000, encoding: 'utf8' });
      if (r.status === 0) { cachedYtdlp = { path: c, version: String(r.stdout).trim() }; break; }
    } catch (_) { }
  }
  return cachedYtdlp;
}

function runJson(bin, args, timeoutMs, env) {
  return new Promise((resolve, reject) => {
    const p = require('./children').spawnTracked(bin, args, { windowsHide: true, env: env ? Object.assign({}, process.env, env) : process.env });
    let out = ''; let err = '';
    const t = setTimeout(() => { p.kill(); reject(new Error('Zeitüberschreitung beim Auflösen des Streams')); }, timeoutMs);
    p.stdout.on('data', (d) => { out += d; });
    p.stderr.on('data', (d) => { err += d; if (err.length > 20000) err = err.slice(-10000); });
    p.on('error', (e) => { clearTimeout(t); reject(e); });
    p.on('close', (code) => {
      clearTimeout(t);
      if (code !== 0) {
        const msg = err.split(/\r?\n/).filter((l) => /ERROR/i.test(l)).pop() || err.trim().split(/\r?\n/).pop() || ('Exit-Code ' + code);
        return reject(new Error(msg.replace(/^ERROR:\s*/, '')));
      }
      try { resolve(JSON.parse(out)); } catch (e) { reject(new Error('Ungültige Antwort von ' + path.basename(bin))); }
    });
  });
}

async function resolveWithYtdlp(url, type) {
  const y = findYtdlp();
  if (!y) throw new Error('yt-dlp wurde nicht gefunden. yt-dlp.exe nach tools\\yt-dlp\\ legen oder Pfad unter Werkzeuge → Einstellungen setzen.');
  const h = Number(settings.get('streamMaxHeight')) || 1080;
  // H.264/AAC bevorzugen → ohne Neukodierung in fMP4 umverpackbar
  const fmt = type === 'twitch'
    ? `best[height<=?${h}]/best`
    : `bv*[vcodec^=avc1][height<=?${h}]+ba[acodec^=mp4a][audio_channels<=2]/bv*[vcodec^=avc1][height<=?${h}]+ba[acodec^=mp4a]/b[vcodec^=avc1][height<=?${h}]/bv*[height<=?${h}]+ba/b[height<=?${h}]/b`;
  // Electron enthält Node.js – als JS-Laufzeit für yt-dlp (YouTube-Signaturen), kein Deno nötig
  const args = ['-J', '--no-playlist', '--no-warnings', '--no-progress', '--js-runtimes', 'node:' + process.execPath, '-f', fmt];
  const cookies = settings.get('ytdlpCookiesBrowser');
  if (cookies) args.push('--cookies-from-browser', cookies);
  args.push(url);
  const info = await runJson(y.path, args, 90000, { ELECTRON_RUN_AS_NODE: '1' });
  const formats = info.requested_formats && info.requested_formats.length ? info.requested_formats : [info];
  const inputs = formats.map((f) => ({
    url: f.url,
    protocol: f.protocol || '',
    headers: f.http_headers || info.http_headers || {},
    vcodec: f.vcodec && f.vcodec !== 'none' ? f.vcodec : null,
    acodec: f.acodec && f.acodec !== 'none' ? f.acodec : null
  })).filter((i) => i.url);
  if (!inputs.length) throw new Error('Keine abspielbare Quelle gefunden.');
  const live = !!info.is_live;
  return {
    title: info.title || displayName(url, type),
    duration: live ? null : (Number(info.duration) || null),
    live,
    thumbnail: info.thumbnail || null,
    uploader: info.uploader || info.channel || null,
    inputs,
    // Codec-Entscheidung aus den Formatangaben
    video: inputs.map((i) => i.vcodec).find(Boolean) || null,
    audio: inputs.map((i) => i.acodec).find(Boolean) || null
  };
}

// ---------------------------------------------------------------- FFmpeg-Quellen
function inputArgs(inp, ss, type, transcode) {
  const a = [];
  // Beim Neukodieren: Decoder-Fehler im Quellstrom verdecken statt abbrechen
  if (transcode) a.push('-err_detect', 'ignore_err', '-ec', 'favor_inter+guess_mvs+deblock');
  const isHttp = /^https?:/i.test(inp.url);
  // HLS/DASH-Manifeste: der Demuxer lädt Segmente selbst nach. reconnect_streamed würde dort
  // nach jedem fertig geladenen Segment ("End of file") endlos neu verbinden.
  const isManifest = /\.(m3u8|mpd)(\?|$)/i.test(inp.url) || inp.protocol === 'm3u8' || inp.protocol === 'm3u8_native' || inp.protocol === 'http_dash_segments';
  if (isHttp) {
    if (!isManifest) a.push('-reconnect', '1', '-reconnect_streamed', '1', '-reconnect_on_network_error', '1', '-reconnect_delay_max', '5');
    else if (!/\.mpd(\?|$)/i.test(inp.url) && inp.protocol !== 'http_dash_segments') a.push('-live_start_index', '-3', '-seg_max_retry', '5');
    const hdr = Object.assign({}, inp.headers || {});
    const ua = hdr['User-Agent'] || hdr['user-agent'];
    delete hdr['User-Agent']; delete hdr['user-agent'];
    if (ua) a.push('-user_agent', ua);
    const lines = Object.entries(hdr).map(([k, v]) => `${k}: ${v}`).join('\r\n');
    if (lines) a.push('-headers', lines + '\r\n');
  }
  if (/^(tcp|udp|rtp|srt):/i.test(inp.url)) a.push('-rw_timeout', '15000000');
  if (/^rtsp/i.test(inp.url)) a.push('-rtsp_transport', 'tcp', '-timeout', '15000000');
  if (type !== 'youtube' && type !== 'web' && !ss) a.push('-fflags', '+genpts+discardcorrupt');
  if (ss > 0) a.push('-ss', String(ss));
  a.push('-i', inp.url);
  return a;
}

async function probeInput(url) {
  const bin = transcode.find();
  if (!bin || !bin.ffprobe) return null;
  try {
    const info = await runJson(bin.ffprobe, ['-v', 'error', '-rw_timeout', '15000000', '-analyzeduration', '3000000', '-probesize', '3000000',
      '-show_entries', 'stream=codec_type,codec_name:format=duration,format_name', '-of', 'json', url], 25000);
    const v = (info.streams || []).find((s) => s.codec_type === 'video');
    const a = (info.streams || []).find((s) => s.codec_type === 'audio');
    const d = Number(info.format && info.format.duration);
    return { video: v ? v.codec_name : null, audio: a ? a.codec_name : null, duration: isFinite(d) && d > 0 ? d : null };
  } catch (_) { return null; }
}

// ---------------------------------------------------------------- Sitzungen
// ---------------------------------------------------------------- Protokoll
function log(...parts) {
  try {
    const { app } = require('electron');
    const dir = path.join(app.getPath('userData'), 'logs');
    fs.mkdirSync(dir, { recursive: true });
    const file = path.join(dir, 'uve-player.log');
    try { if (fs.statSync(file).size > 2 * 1048576) fs.renameSync(file, file + '.old'); } catch (_) { }
    fs.appendFileSync(file, `[${new Date().toISOString()}] ${parts.join(' ')}\n`);
  } catch (_) { }
}

async function open(url, opts = {}) {
  const type = classify(url);
  if (!type) throw new Error('Dieser Link wird direkt abgespielt, nicht als Stream.');
  if (!transcode.find()) throw new Error('FFmpeg wird für Netzwerkstreams benötigt.');
  let s;
  if (type === 'youtube' || type === 'twitch' || type === 'web') {
    s = await resolveWithYtdlp(url, type);
  } else {
    const p = await probeInput(url);
    const live = FFMPEG_SCHEMES.includes(type) || !(p && p.duration);
    s = {
      title: displayName(url, type), duration: live ? null : p.duration, live,
      inputs: [{ url, headers: {} }], video: p ? p.video : 'h264', audio: p ? p.audio : 'aac'
    };
  }
  // opts.transcode: nach wiederholten Decoder-Fehlern sauber neu kodieren statt umverpacken
  const vcopy = !opts.transcode && (!s.video || /^(avc1|h264)/i.test(s.video));
  const acopy = !opts.transcode && (!s.audio || /^(mp4a|aac)/i.test(s.audio));
  log('open', type, url, `video=${s.video} audio=${s.audio} vcopy=${vcopy} acopy=${acopy} live=${s.live}${opts.transcode ? ' (Neukodierung erzwungen)' : ''}`);
  const id = crypto.randomUUID();
  sessions.set(id, Object.assign(s, { id, url, type, vcopy, acopy, created: Date.now() }));
  while (sessions.size > 30) sessions.delete(sessions.keys().next().value);
  return {
    id, url, type, title: s.title, duration: s.duration, live: s.live, uploader: s.uploader || null,
    hasVideo: s.video !== null || s.inputs.length > 1, plan: { video: s.video, audio: s.audio, vcopy, acopy }
  };
}

// ---------------------------------------------------------------- Lokale Dateien
// Für Dateien, deren Codecs/Container Chromium nicht kann (z. B. MKV mit AC3/DTS-Ton, AVI, WMV):
// Echtzeit-Umverpackung nach fMP4 (Video meist nur kopiert, Ton → AAC) statt vorheriger Komplettumwandlung.
const COPY_VIDEO = ['h264', 'vp9', 'av1'];
const COPY_AUDIO = ['aac', 'opus', 'flac'];

function describeStreams(info) {
  const streams = info.streams || [];
  const audio = streams.filter((s) => s.codec_type === 'audio');
  const subs = streams.filter((s) => s.codec_type === 'subtitle');
  return {
    video: streams.find((s) => s.codec_type === 'video' && !(s.disposition && s.disposition.attached_pic)) || null,
    audio,
    subs,
    duration: Number(info.format && info.format.duration) || null,
    format: info.format && info.format.format_name
  };
}

async function openLocal(file, opts = {}) {
  const registry = require('./media-registry');
  if (!registry.isAllowed(file)) throw new Error('Datei nicht freigegeben');
  if (!transcode.find()) throw new Error('FFmpeg wird für dieses Format benötigt.');
  const d = describeStreams(await transcode.probe(file));
  const v = d.video;
  const defAudio = Math.max(0, d.audio.findIndex((a) => a.disposition && a.disposition.default));
  const ai = Math.min(Math.max(0, opts.audio != null ? opts.audio : defAudio), Math.max(0, d.audio.length - 1));
  const a = d.audio[ai] || null;
  const tenBit = v && /10|12/.test(String(v.pix_fmt || '')) ;
  let vcopy = !!v && !opts.transcode && !opts.transcodeVideo && (
    (COPY_VIDEO.includes(v.codec_name) && !(v.codec_name === 'h264' && tenBit)) ||
    (v.codec_name === 'hevc' && (tenBit ? opts.hevc10 : opts.hevc))
  );
  const acopy = !!a && !opts.transcode && COPY_AUDIO.includes(a.codec_name);
  const s = {
    title: path.basename(file), duration: d.duration, live: false,
    inputs: [{ url: file, local: true }],
    video: v ? v.codec_name : null, audio: a ? a.codec_name : null,
    videoMap: v ? '0:' + v.index : null, audioMap: a ? '0:' + a.index : null,
    audioChannels: a ? Number(a.channels) || 2 : 0,
    hevcTag: vcopy && v.codec_name === 'hevc'
  };
  const id = crypto.randomUUID();
  sessions.set(id, Object.assign(s, { id, url: file, type: 'local', vcopy, acopy, created: Date.now() }));
  while (sessions.size > 30) sessions.delete(sessions.keys().next().value);
  log('open', 'local', file, `video=${s.video}${tenBit ? '(10bit)' : ''} audio=${s.audio}#${ai} vcopy=${vcopy} acopy=${acopy}`);
  return {
    id, url: file, type: 'local', title: s.title, duration: s.duration, live: false, hasVideo: !!v,
    plan: { video: s.video, audio: s.audio, vcopy, acopy },
    audioIndex: ai,
    audioTracks: d.audio.map((x, i) => ({
      index: i, codec: x.codec_name, channels: x.channels, lang: (x.tags && x.tags.language) || '',
      title: (x.tags && x.tags.title) || '', default: !!(x.disposition && x.disposition.default)
    }))
  };
}

function ffmpegArgs(s, ss) {
  const args = ['-hide_banner', '-loglevel', 'warning', '-nostdin'];
  s.inputs.forEach((inp) => args.push(...inputArgs(inp, ss, s.type, !s.vcopy)));
  if (s.type === 'local') {
    if (s.videoMap) args.push('-map', s.videoMap);
    if (s.audioMap) args.push('-map', s.audioMap);
  } else if (s.inputs.length > 1) {
    const vi = s.inputs.findIndex((i) => i.vcodec);
    const ai = s.inputs.findIndex((i) => i.acodec);
    args.push('-map', `${vi < 0 ? 0 : vi}:v:0`, '-map', `${ai < 0 ? 1 : ai}:a:0`);
  } else {
    args.push('-map', '0:v:0?', '-map', '0:a:0?');
  }
  if (s.vcopy) { args.push('-c:v', 'copy'); if (s.hevcTag) args.push('-tag:v', 'hvc1'); }
  else if (s.type === 'local') args.push('-c:v', 'libx264', '-preset', 'veryfast', '-crf', '19', '-pix_fmt', 'yuv420p', '-g', '96');
  else args.push('-c:v', 'libx264', '-preset', 'veryfast', '-tune', 'zerolatency', '-crf', '20', '-pix_fmt', 'yuv420p', '-g', '60', '-sc_threshold', '0',
    '-vf', "scale=-2:'min(ih,1080)'", '-fps_mode', 'cfr');
  // ADTS-AAC (MPEG-TS, HLS) muss für MP4 in ASC umgewandelt werden – bei empty_moov nicht automatisch
  if (s.acopy) args.push('-c:a', 'copy', '-bsf:a', 'aac_adtstoasc');
  else if (s.type === 'local' && s.audioChannels > 2) args.push('-c:a', 'aac', '-b:a', '384k', '-ac', '6'); // 5.1 erhalten
  else args.push('-c:a', 'aac', '-b:a', s.type === 'local' ? '192k' : '160k', '-ac', '2');
  args.push('-sn', '-dn', '-map_metadata', '-1');
  // Zeitstempel bleiben relativ (Start ≈ 0). Fragmentiertes MP4 beginnt jede Spur bei 0; einen absoluten
  // Versatz könnte FFmpeg nur per Edit-List ausdrücken, die Chromium bei großen Werten falsch anwendet.
  // Stattdessen setzt die Oberfläche SourceBuffer.timestampOffset (Startzeit kommt per X-UVE-Start).
  args.push('-f', 'mp4', '-movflags', 'frag_keyframe+empty_moov+default_base_moof+delay_moov', '-frag_duration', '2000000', 'pipe:1');
  return args;
}

// Sprungziel auf den vorherigen Keyframe legen (wie es FFmpeg beim Kopieren ohnehin tut).
// Dann beginnen Bild und Ton gemeinsam bei 0, und die Oberfläche kennt die exakte Startzeit.
async function keyframeBefore(s, ss) {
  const bin = transcode.find();
  if (!bin || !bin.ffprobe) return ss;
  const inp = s.inputs.find((i) => i.vcodec) || s.inputs[0];
  const args = ['-v', 'error'];
  if (/^https?:/i.test(inp.url)) {
    const hdr = Object.assign({}, inp.headers || {});
    const ua = hdr['User-Agent'] || hdr['user-agent'];
    delete hdr['User-Agent']; delete hdr['user-agent'];
    if (ua) args.push('-user_agent', ua);
    const lines = Object.entries(hdr).map(([k, v]) => `${k}: ${v}`).join('\r\n');
    if (lines) args.push('-headers', lines + '\r\n');
  }
  const sel = s.type === 'local' ? (s.videoMap ? s.videoMap.replace(/^0:/, '') : 'a:0') : (s.video ? 'v:0' : 'a:0');
  args.push('-read_intervals', `${ss}%+#1`, '-select_streams', sel, '-show_entries', 'packet=pts_time', '-of', 'json', inp.url);
  try {
    const r = await runJson(bin.ffprobe, args, 10000);
    const t = Number(r.packets && r.packets[0] && r.packets[0].pts_time);
    return isFinite(t) && t >= 0 && t <= ss + 1 ? t : ss;
  } catch (_) { return ss; }
}

// Liefert die HTTP-Antwort für uve://app/live?id=…&ss=…
async function serve(id, ss) {
  const s = sessions.get(id);
  if (!s) return new Response('Stream-Sitzung abgelaufen', { status: 410 });
  const bin = transcode.find();
  if (!bin) return new Response('FFmpeg fehlt', { status: 500 });
  if (ss > 0 && !s.live) ss = await keyframeBefore(s, ss);
  const fargs = ffmpegArgs(s, ss);
  const proc = require('./children').spawnTracked(bin.ffmpeg, fargs, { windowsHide: true });
  const t0 = Date.now();
  log('ffmpeg-start', s.type, 'ss=' + ss, s.type === 'local' ? fargs.filter((a) => !String(a).includes(path.sep)).join(' ') : '');
  let err = '';
  let logged = 0;
  proc.stderr.on('data', (d) => {
    err += d; if (err.length > 8000) err = err.slice(-4000);
    // Warnungen protokollieren (begrenzt), z. B. Diskontinuitäten bei Twitch-Werbung
    if (logged < 200) {
      for (const line of String(d).split(/\r?\n/)) {
        if (!line.trim() || /Discarding stream metadata|^\s*\[aost#[^\]]*\]\s*$/.test(line)) continue; // harmlos
        if (logged++ < 200) log('ffmpeg', s.type, line.trim());
      }
    }
  });
  proc.on('close', (code) => log('ffmpeg-ende', s.type, 'code=' + code));

  // Auf erste Daten warten, damit Verbindungsfehler als HTTP-Fehler ankommen
  const first = await new Promise((resolve) => {
    const t = setTimeout(() => resolve({ timeout: true }), 45000);
    proc.stdout.once('data', (chunk) => { clearTimeout(t); proc.stdout.pause(); resolve({ chunk }); });
    proc.once('close', (code) => { clearTimeout(t); resolve({ code }); });
    proc.once('error', (e) => { clearTimeout(t); resolve({ error: e }); });
  });
  log('ffmpeg-erste-daten', s.type, first.chunk ? `nach ${Date.now() - t0} ms` : 'keine: ' + JSON.stringify({ timeout: first.timeout, code: first.code }));
  if (!first.chunk) {
    try { proc.kill(); } catch (_) { }
    const msg = first.timeout ? 'Keine Daten vom Stream (Zeitüberschreitung)' : (err.trim().split(/\r?\n/).pop() || (first.error && first.error.message) || 'Stream beendet');
    return new Response(msg, { status: 502, headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
  }

  let closed = false;
  const body = new ReadableStream({
    start(ctrl) {
      ctrl.enqueue(new Uint8Array(first.chunk));
      proc.stdout.on('data', (chunk) => {
        if (closed) return;
        try {
          ctrl.enqueue(new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength));
          if (ctrl.desiredSize !== null && ctrl.desiredSize <= 0) proc.stdout.pause();
        } catch (_) { closed = true; try { proc.kill(); } catch (__) { } }
      });
      proc.stdout.on('end', () => { if (!closed) { closed = true; try { ctrl.close(); } catch (_) { } } });
      proc.on('error', (e) => { if (!closed) { closed = true; try { ctrl.error(e); } catch (_) { } } });
    },
    pull() { proc.stdout.resume(); },
    cancel() { closed = true; try { proc.kill(); } catch (_) { } }
  }, { highWaterMark: 8 });
  return new Response(body, { status: 200, headers: { 'Content-Type': 'video/mp4', 'Cache-Control': 'no-store', 'X-UVE-Start': String(ss || 0) } });
}

module.exports = { classify, displayName, open, openLocal, serve, findYtdlp, log, FFMPEG_SCHEMES };
