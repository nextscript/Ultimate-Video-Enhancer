'use strict';
// Einstellungen (Gegenstück zu VLCs vlcrc): JSON-Datei im userData-Ordner.
const fs = require('fs');
const path = require('path');
const { app } = require('electron');

const DEFAULTS = {
  volume: 1,
  muted: false,
  playbackRate: 1,
  repeat: 'none',            // none | all | one
  shuffle: false,
  resumePlayback: true,
  restoreSession: true,
  autoSubtitles: true,
  compatMode: 'ask',         // ask | auto | off  (FFmpeg-Fallback)
  ffmpegPath: '',
  ytdlpPath: '',
  ytdlpCookiesBrowser: '',   // z. B. "firefox", "chrome", "edge" für altersbeschränkte/angemeldete Inhalte
  streamMaxHeight: 1080,
  checkToolUpdates: true,    // beim Start nach FFmpeg-/yt-dlp-Updates suchen
  skippedToolVersions: {},   // tool -> übersprungene Version
  hardwareAcceleration: true,
  alwaysOnTop: false,
  showPlaylist: true,
  fitMode: 'contain',
  seekStep: 10,
  plugins: {},               // id -> { enabled: bool }
  recent: [],
  resume: {},                // key -> { t, d, at }
  session: [],
  windowBounds: null
};

let file = null;
let data = null;
let timer = null;

function load() {
  if (data) return data;
  file = path.join(app.getPath('userData'), 'settings.json');
  try {
    data = Object.assign({}, DEFAULTS, JSON.parse(fs.readFileSync(file, 'utf8')));
  } catch (_) {
    data = Object.assign({}, DEFAULTS);
  }
  return data;
}

function flush() {
  if (!data) return;
  clearTimeout(timer);
  timer = null;
  try {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const tmp = file + '.tmp';
    fs.writeFileSync(tmp, JSON.stringify(data, null, 2));
    fs.renameSync(tmp, file);
  } catch (e) {
    console.error('[settings] speichern fehlgeschlagen', e);
  }
}

function save() {
  clearTimeout(timer);
  timer = setTimeout(flush, 400);
}

function get(key) { return key ? load()[key] : load(); }

function set(key, value) {
  load()[key] = value;
  save();
  return value;
}

function patch(obj) {
  Object.assign(load(), obj || {});
  save();
  return data;
}

module.exports = { load, get, set, patch, flush, DEFAULTS };
