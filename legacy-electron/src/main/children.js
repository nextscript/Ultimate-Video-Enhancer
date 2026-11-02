'use strict';
// Zentrale Verwaltung aller Kindprozesse (FFmpeg, ffprobe, yt-dlp).
// Beim Beenden des Players werden alle noch laufenden Prozesse beendet – sonst würden z. B. eine
// Untertitel-Extraktion oder ein Stream-FFmpeg weiterlaufen und Dateien im Cache sperren.
const { spawn } = require('child_process');

const procs = new Set();

function track(proc) {
  procs.add(proc);
  const done = () => procs.delete(proc);
  proc.once('exit', done);
  proc.once('error', done);
  return proc;
}

function spawnTracked(bin, args, opts) {
  return track(spawn(bin, args, opts));
}

function killAll() {
  for (const p of procs) {
    try { p.kill(); } catch (_) { }
  }
  procs.clear();
}

module.exports = { spawnTracked, track, killAll, count: () => procs.size };
