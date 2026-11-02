// UVE Player – Interface-Modul (Gegenstück zu VLCs libqt_plugin).
import { Player, EQ_PRESETS, EQ_FREQS } from './core/player.js';
import { Playlist } from './core/playlist.js';
import { loadPlugins } from './plugins/loader.js';

const host = window.uveHost;
const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

const appEl = $('#app');
const video = $('#video');
const player = new Player(video);
const playlist = new Playlist();

let S = {};              // Einstellungen
let INFO = {};           // App-Info
let PLUGINS = [];
let PLUGIN_RESULTS = [];
let selected = new Set();
const FITS = [['contain', 'Einpassen'], ['cover', 'Füllen'], ['fill', 'Strecken'], ['none', 'Original']];

// ---------------------------------------------------------------- Hilfen
function fmt(t) {
  if (!isFinite(t) || t < 0) t = 0;
  const h = Math.floor(t / 3600), m = Math.floor((t % 3600) / 60), s = Math.floor(t % 60);
  return (h ? h + ':' + String(m).padStart(2, '0') : String(m).padStart(2, '0')) + ':' + String(s).padStart(2, '0');
}
function parseTime(str) {
  const parts = String(str).trim().split(':').map(Number);
  if (parts.some((n) => isNaN(n))) return NaN;
  return parts.reduce((acc, n) => acc * 60 + n, 0);
}
let osdTimer = null;
function osd(msg, ms = 1400) {
  const el = $('#osd');
  el.textContent = msg;
  el.classList.add('show');
  clearTimeout(osdTimer);
  osdTimer = setTimeout(() => el.classList.remove('show'), ms);
}
window.addEventListener('uve:osd', (e) => osd(String(e.detail), 3000));
function patchSettings(obj) { Object.assign(S, obj); return host.invoke('settings:patch', obj); }
const isLocal = (it) => !!it && (it.kind === 'video' || it.kind === 'audio');
const resumeKey = (it) => (it && it.path ? it.path.toLowerCase() : null);

// ---------------------------------------------------------------- Popup-Menüs
const popup = $('#popup');
function showPopup(anchor, entries, at) {
  popup.innerHTML = '';
  // doppelte/führende/abschließende Trenner entfernen (bei bedingten Einträgen)
  const list = entries.filter(Boolean).filter((e, i, a) => e !== '-' || (i > 0 && i < a.length - 1 && a[i - 1] !== '-'));
  for (const e of list) {
    if (e === '-') { popup.appendChild(Object.assign(document.createElement('div'), { className: 'sep' })); continue; }
    if (e.header) { popup.appendChild(Object.assign(document.createElement('div'), { className: 'hdr', textContent: e.header })); continue; }
    const b = document.createElement('button');
    b.className = 'mi' + (e.on ? ' on' : '') + (e.danger ? ' danger' : '') + (e.sub ? ' has-sub' : '') + (e.back ? ' back' : '');
    b.disabled = !!e.disabled;
    b.innerHTML = `<span>${esc(e.label)}</span>${e.kbd ? `<kbd>${esc(e.kbd)}</kbd>` : ''}${e.sub ? '<kbd>›</kbd>' : ''}`;
    if (e.sub) {
      // Untermenü im selben Popup an derselben Stelle öffnen
      b.onclick = () => {
        const pos = { x: popup.offsetLeft, y: popup.offsetTop };
        const subEntries = typeof e.sub === 'function' ? e.sub() : e.sub;
        showPopup(anchor, [{ label: '‹ ' + e.label, back: true, run: () => showPopup(anchor, entries, pos) }, '-', ...subEntries], pos);
      };
    } else if (e.back) b.onclick = () => e.run();
    else b.onclick = () => { hidePopup(); e.run(); };
    popup.appendChild(b);
  }
  popup.classList.remove('hidden');
  const pr = popup.getBoundingClientRect();
  if (at) {
    // Kontextmenü an der Mausposition, im Fenster halten
    popup.style.left = Math.max(6, Math.min(window.innerWidth - pr.width - 6, at.x)) + 'px';
    popup.style.top = Math.max(6, Math.min(window.innerHeight - pr.height - 6, at.y)) + 'px';
    return;
  }
  const r = anchor.getBoundingClientRect();
  popup.style.left = Math.max(6, Math.min(window.innerWidth - pr.width - 6, r.left + r.width / 2 - pr.width / 2)) + 'px';
  popup.style.top = Math.max(6, r.top - pr.height - 6) + 'px';
}
function hidePopup() { popup.classList.add('hidden'); }
document.addEventListener('mousedown', (e) => { if (!popup.contains(e.target)) hidePopup(); }, true);

// ---------------------------------------------------------------- Dialoge
const dlg = $('#dialog');
const dlgForm = $('#dialog-form');
function openDialog({ title, body, buttons = [{ label: 'Schließen', value: 'close', primary: true }], onRender, wide }) {
  return new Promise((resolve) => {
    dlgForm.innerHTML = `<div class="dh">${esc(title)}</div><div class="db">${body}</div><div class="df">${buttons.map((b) =>
      `<button class="btn${b.primary ? ' primary' : ''}${b.danger ? ' danger' : ''}" value="${esc(b.value)}">${esc(b.label)}</button>`).join('')}</div>`;
    dlg.style.width = wide ? 'min(820px, 94vw)' : '';
    if (onRender) onRender(dlgForm);
    const done = () => { dlg.removeEventListener('close', done); resolve(dlg.returnValue); };
    dlg.addEventListener('close', done);
    dlg.returnValue = '';
    dlg.showModal();
    const first = dlgForm.querySelector('input, select');
    if (first) first.focus();
  });
}

// ---------------------------------------------------------------- Playlist-Ansicht
const plList = $('#pl-list');
function renderPlaylist() {
  const q = $('#pl-search').value.trim().toLowerCase();
  plList.innerHTML = '';
  const frag = document.createDocumentFragment();
  playlist.items.forEach((it, i) => {
    if (q && !it.name.toLowerCase().includes(q)) return;
    const li = document.createElement('li');
    li.dataset.uid = it.uid;
    li.draggable = true;
    li.title = it.path;
    li.className = (i === playlist.index ? 'current ' : '') + (selected.has(it.uid) ? 'selected ' : '') + (it.error ? 'error' : '');
    li.innerHTML = `<span class="n">${i + 1}</span><span class="t">${esc(it.name)}</span><span class="d">${it.duration ? fmt(it.duration) : ''}</span>`;
    frag.appendChild(li);
  });
  plList.appendChild(frag);
  $('#pl-count').textContent = playlist.items.length ? playlist.items.length + ' Titel' : '';
  appEl.classList.toggle('has-media', playlist.items.length > 0 && !!player.item);
  saveSessionSoon();
}
playlist.addEventListener('change', (e) => {
  renderPlaylist();
  if (e.detail && e.detail.removedCurrent) { player.unload(); updateNowPlaying(); appEl.classList.remove('has-media'); }
});
playlist.addEventListener('item', renderPlaylist);
$('#pl-search').addEventListener('input', renderPlaylist);

plList.addEventListener('click', (e) => {
  const li = e.target.closest('li'); if (!li) return;
  const uid = li.dataset.uid;
  if (e.ctrlKey) { selected.has(uid) ? selected.delete(uid) : selected.add(uid); }
  else if (e.shiftKey && selected.size) {
    const ids = playlist.items.map((i) => i.uid);
    const last = ids.indexOf([...selected].pop()); const cur = ids.indexOf(uid);
    ids.slice(Math.min(last, cur), Math.max(last, cur) + 1).forEach((x) => selected.add(x));
  } else { selected = new Set([uid]); }
  renderPlaylist();
});
plList.addEventListener('dblclick', (e) => {
  const li = e.target.closest('li'); if (!li) return;
  playIndex(playlist.items.findIndex((i) => i.uid === li.dataset.uid));
});
plList.addEventListener('keydown', (e) => {
  if (e.key === 'Delete') { playlist.remove([...selected]); selected.clear(); e.preventDefault(); e.stopPropagation(); }
  if (e.key === 'Enter' && selected.size) { playIndex(playlist.items.findIndex((i) => i.uid === [...selected][0])); e.stopPropagation(); }
  if ((e.key === 'a' || e.key === 'A') && e.ctrlKey) { selected = new Set(playlist.items.map((i) => i.uid)); renderPlaylist(); e.preventDefault(); e.stopPropagation(); }
  if (e.altKey && (e.key === 'ArrowUp' || e.key === 'ArrowDown')) { moveSelection(e.key === 'ArrowUp' ? 'up' : 'down'); e.preventDefault(); e.stopPropagation(); }
});
// Kontextmenü (Rechtsklick) der Wiedergabeliste
function moveSelection(where) {
  const ids = playlist.items.filter((i) => selected.has(i.uid)).map((i) => i.uid);
  if (!ids.length) return;
  const idx = (uid) => playlist.items.findIndex((i) => i.uid === uid);
  if (where === 'top') ids.reverse().forEach((u) => playlist.move(u, 0));
  else if (where === 'bottom') ids.forEach((u) => playlist.move(u, playlist.items.length));
  else if (where === 'up') ids.forEach((u) => { const i = idx(u); if (i > 0) playlist.move(u, i - 1); });
  else if (where === 'down') ids.reverse().forEach((u) => { const i = idx(u); if (i < playlist.items.length - 1) playlist.move(u, i + 2); });
  else if (where === 'next') {
    ids.filter((u) => u !== (playlist.current && playlist.current.uid)).reverse().forEach((u) => {
      const cur = playlist.index;
      playlist.move(u, cur < 0 ? 0 : cur + 1);
    });
  }
}

async function copyText(text, what) {
  try { await navigator.clipboard.writeText(text); osd(what + ' kopiert'); } catch (_) { osd('Kopieren nicht möglich'); }
}

plList.addEventListener('contextmenu', (e) => {
  e.preventDefault();
  const li = e.target.closest('li');
  if (li && !selected.has(li.dataset.uid)) { selected = new Set([li.dataset.uid]); renderPlaylist(); }
  if (!li && !e.ctrlKey) { selected.clear(); renderPlaylist(); }
  const sel = playlist.items.filter((i) => selected.has(i.uid));
  const one = sel.length === 1 ? sel[0] : null;
  const n = sel.length;
  const hasErrors = playlist.items.some((i) => i.error);
  const at = { x: e.clientX, y: e.clientY };
  const anyItems = playlist.items.length > 0;
  showPopup(null, [
    one && { label: 'Abspielen', kbd: 'Enter', run: () => playIndex(playlist.items.indexOf(one), one.error ? { startAt: 0.001 } : {}) },
    n && { label: n > 1 ? `Als Nächstes abspielen (${n})` : 'Als Nächstes abspielen', run: () => moveSelection('next') },
    '-',
    n && { label: n > 1 ? `${n} Einträge entfernen` : 'Entfernen', kbd: 'Entf', danger: true, run: () => { playlist.remove([...selected]); selected.clear(); } },
    n && n < playlist.items.length && { label: 'Alle anderen entfernen', run: () => { playlist.remove(playlist.items.filter((i) => !selected.has(i.uid)).map((i) => i.uid)); } },
    hasErrors && { label: 'Fehlerhafte Einträge entfernen', run: () => playlist.remove(playlist.items.filter((i) => i.error).map((i) => i.uid)) },
    '-',
    n && { label: 'Nach oben', kbd: 'Alt+↑', run: () => moveSelection('up') },
    n && { label: 'Nach unten', kbd: 'Alt+↓', run: () => moveSelection('down') },
    n && { label: 'An den Anfang', run: () => moveSelection('top') },
    n && { label: 'Ans Ende', run: () => moveSelection('bottom') },
    '-',
    one && isLocal(one) && { label: 'Im Explorer zeigen', run: () => host.invoke('shell:show-item', one.path) },
    one && { label: isLocal(one) ? 'Dateipfad kopieren' : 'Link kopieren', run: () => copyText(one.path, isLocal(one) ? 'Dateipfad' : 'Link') },
    n > 1 && { label: `${n} Pfade/Links kopieren`, run: () => copyText(sel.map((i) => i.path).join('\r\n'), 'Pfade') },
    one && one === player.item && { label: 'Medieninformationen', kbd: 'Strg+I', run: () => act('media-info') },
    '-',
    anyItems && { label: 'Alle auswählen', kbd: 'Strg+A', run: () => { selected = new Set(playlist.items.map((i) => i.uid)); renderPlaylist(); } },
    { label: 'Dateien hinzufügen…', run: () => openKind('files', false) },
    { label: 'Ordner hinzufügen…', run: () => openKind('folder', false) },
    { label: 'Netzwerkstream hinzufügen…', run: () => openUrlDialog(false) },
    '-',
    anyItems && { label: 'Nach Name sortieren', run: () => playlist.sort() },
    anyItems && { label: 'Wiedergabeliste speichern…', kbd: 'Strg+Y', run: () => act('save-playlist') },
    anyItems && { label: 'Liste leeren', danger: true, run: () => { playlist.clear(); selected.clear(); } }
  ], at);
});

let dragUid = null;
plList.addEventListener('dragstart', (e) => { const li = e.target.closest('li'); dragUid = li && li.dataset.uid; e.dataTransfer.effectAllowed = 'move'; });
plList.addEventListener('dragover', (e) => {
  if (!dragUid) return;
  e.preventDefault();
  plList.querySelectorAll('.drop-before').forEach((x) => x.classList.remove('drop-before'));
  const li = e.target.closest('li'); if (li) li.classList.add('drop-before');
});
plList.addEventListener('drop', (e) => {
  if (!dragUid) return;
  e.preventDefault(); e.stopPropagation();
  const li = e.target.closest('li');
  const to = li ? playlist.items.findIndex((i) => i.uid === li.dataset.uid) : playlist.items.length;
  playlist.move(dragUid, to);
  dragUid = null;
});
plList.addEventListener('dragend', () => { dragUid = null; plList.querySelectorAll('.drop-before').forEach((x) => x.classList.remove('drop-before')); });

// ---------------------------------------------------------------- Wiedergabe
async function playIndex(i, opts = {}) {
  const it = playlist.select(i);
  if (!it) return;
  rememberPosition();
  let startAt = opts.startAt || 0;
  if (!startAt && S.resumePlayback && isLocal(it)) {
    const r = (S.resume || {})[resumeKey(it)];
    if (r && r.t > 10 && (!r.d || r.t < r.d - 10)) { startAt = r.t; setTimeout(() => osd('Fortgesetzt bei ' + fmt(r.t), 2200), 300); }
  }
  appEl.classList.add('has-media');
  appEl.classList.toggle('audio', it.kind === 'audio');
  $('#audio-art').classList.toggle('hidden', it.kind !== 'audio');
  $('#audio-title').textContent = it.name;
  if (isLocal(it) && it.localStream === undefined) it.localStream = await needsLocalStream(it);
  if (playlist.current !== it) return; // während der Prüfung anderes Medium gewählt
  await player.load(it, { startAt, autoplay: opts.autoplay !== false });
  updateNowPlaying();
  renderPlaylist();
  addRecent(it.path);
}

// Prüft per ffprobe, ob Chromium Container + Video + Ton direkt abspielen kann.
// Sonst läuft die Datei über FFmpeg in Echtzeit (Video meist kopiert, Ton → AAC), z. B. MKV mit AC3/DTS.
const NATIVE_AUDIO = ['aac', 'mp3', 'opus', 'vorbis', 'flac', 'pcm_s16le', 'pcm_s24le', 'pcm_s32le', 'pcm_f32le', 'pcm_u8', 'pcm_mulaw', 'pcm_alaw'];
const NATIVE_CONTAINER = /(mov|mp4|m4a|3gp|matroska|webm|ogg|mp3|wav|flac|aac)/;
let hevcSupport = null;
async function needsLocalStream(it) {
  if (S.compatMode === 'off' || !(await compatAvailable())) return false;
  let info;
  try { info = await host.invoke('compat:probe', it.path); } catch (_) { return false; }
  const streams = info.streams || [];
  const v = streams.find((s) => s.codec_type === 'video' && !(s.disposition && s.disposition.attached_pic));
  const audio = streams.filter((s) => s.codec_type === 'audio');
  const a = audio.find((s) => s.disposition && s.disposition.default) || audio[0];
  if (hevcSupport === null) {
    hevcSupport = {
      main: MediaSource.isTypeSupported('video/mp4; codecs="hvc1.1.6.L120.90"'),
      main10: MediaSource.isTypeSupported('video/mp4; codecs="hvc1.2.4.L120.90"')
    };
  }
  const tenBit = v && /10|12/.test(String(v.pix_fmt || ''));
  const vOk = !v || ['vp8', 'vp9', 'av1', 'theora'].includes(v.codec_name) || (v.codec_name === 'h264' && !tenBit) ||
    (v.codec_name === 'hevc' && (tenBit ? hevcSupport.main10 : hevcSupport.main));
  const aOk = !a || NATIVE_AUDIO.includes(a.codec_name);
  const cOk = NATIVE_CONTAINER.test(String(info.format && info.format.format_name));
  it.probeSummary = { container: info.format && info.format.format_name, video: v ? v.codec_name + (tenBit ? ' 10-Bit' : '') : null, audio: a ? a.codec_name : null, audioCount: audio.length };
  const need = !(vOk && aOk && cOk);
  if (need) host.send('stream:log', `Kompatibilitätsmodus für ${it.name}: container=${it.probeSummary.container} video=${it.probeSummary.video} (ok=${vOk}) audio=${it.probeSummary.audio} (ok=${aOk})`);
  return need;
}

function updateNowPlaying() {
  const it = player.item;
  $('#now-playing').textContent = it ? it.name : '';
  host.send('win:title', it ? it.name + ' – UVE Player' : 'UVE Player');
}

function addToPlaylist(items, { play = false, replace = false } = {}) {
  const subs = items.filter((i) => i.kind === 'subtitle');
  const media = items.filter((i) => i.kind !== 'subtitle');
  if (subs.length && player.item) subs.forEach((s) => player.addSubtitle(s, true).then(() => osd('Untertitel: ' + s.name)).catch((e) => osd('Untertitel-Fehler: ' + e.message)));
  if (!media.length) return;
  const at = playlist.add(media, { replace });
  if (play || playlist.index < 0) playIndex(replace ? 0 : at);
}

host.on('open-items', ({ items, play }) => addToPlaylist(items, { play }));

player.addEventListener('ended', () => {
  const it = player.item;
  if (it && S.resume) { delete S.resume[resumeKey(it)]; patchSettings({ resume: S.resume }); }
  const next = playlist.nextIndex(true);
  if (next >= 0) playIndex(next, { startAt: 0.001 });
  else { player.stop(); updatePlayState(); }
});
player.addEventListener('meta', (e) => {
  const it = player.item; if (!it) return;
  if (isFinite(e.detail.duration) && e.detail.duration > 0) playlist.update(it.uid, { duration: e.detail.duration });
  if (it.kind === 'video' && e.detail.width === 0 && !it.compat) { /* wird ggf. über media-error behandelt */ }
});
player.addEventListener('media-error', (e) => {
  if (player.item && (player.item.kind === 'stream' || player.item.localStream)) return recoverStream(player.item, 'Wiedergabefehler: ' + (e.detail.message || e.detail.code));
  handleMediaError(e.detail);
});

// Automatische Wiederherstellung bei Stream-Störungen (Decoder-Fehler, Abbruch, Twitch-Werbewechsel).
// Abbruch → neu verbinden. Decoder-Fehler (beschädigte Daten im Quellstrom) → sofort neu kodieren,
// da sie beim Umverpacken wieder ankämen; FFmpegs Decoder verdeckt Fehler, Chromium erhält saubere Daten.
// Mehr als 4 Störungen in 2 Minuten → Fehlerdialog.
const recovery = { uid: null, times: [], pending: false };
function recoverStream(item, message, opts = {}) {
  if (!item || item !== player.item) return;
  // Folgefehler derselben Störung ignorieren (z. B. appendBuffer nach Decoder-Fehler)
  if (recovery.pending && recovery.uid === item.uid) return;
  host.send('stream:log', `${opts.connect ? 'Verbindungsfehler' : 'Störung'} bei ${item.path}: ${message}`);
  const now = Date.now();
  if (recovery.uid !== item.uid) { recovery.uid = item.uid; recovery.times = []; recovery.connects = 0; }
  // Quelle nicht erreichbar: zweimal erneut versuchen, dann klar melden (Neukodierung hilft hier nicht)
  if (opts.connect) {
    recovery.connects = (recovery.connects || 0) + 1;
    if (recovery.connects > 2) { recovery.connects = 0; return showStreamError(item, 'Quelle nicht erreichbar: ' + message); }
  } else recovery.connects = 0;
  recovery.times = recovery.times.filter((t) => now - t < 120000);
  if (recovery.times.length >= 4) { recovery.times = []; return showStreamError(item, message); }
  recovery.times.push(now);
  recovery.pending = true;
  const decodeError = /DECODE|decode error|parsing failed|APPEND_FAILED/i.test(message || '');
  if (!opts.connect && (decodeError || recovery.times.length >= 3) && !item.forceTranscode) {
    item.forceTranscode = true;
    host.send('stream:log', 'Wechsel auf Neukodierung (H.264/AAC) – ' + (decodeError ? 'Decoder-Fehler im Quellstrom' : 'wiederholte Störungen'));
  }
  const vod = item.streamInfo && !item.streamInfo.live;
  const at = vod ? video.currentTime : 0;
  osd(`Stream-Störung – verbinde neu${item.forceTranscode ? ' (Kompatibilitätsmodus)' : ''}…`, 8000);
  setTimeout(() => {
    recovery.pending = false;
    if (player.item === item) playIndex(playlist.items.indexOf(item), { startAt: at > 1 ? at : 0.001 });
  }, 700);
}

// ---------------------------------------------------------------- Netzwerkstreams
const STREAM_LABEL = { youtube: 'YouTube', twitch: 'Twitch', web: 'Web', 'ffmpeg-http': 'HTTP' };
player.addEventListener('stream-state', (e) => {
  const { item, state } = e.detail;
  if (item !== player.item) return;
  if (item.localStream) {
    // Lokale Datei im Kompatibilitätsmodus: nur kurzer Hinweis, keine Stream-Meldungen
    if (state === 'playing') {
      if (recovery.uid === item.uid) recovery.connects = 0;
      const p = item.streamInfo && item.streamInfo.plan;
      if (p && !item._compatNoted) {
        item._compatNoted = true;
        osd(`Kompatibilitätsmodus · Video ${p.vcopy ? 'direkt' : '→ H.264'} · Ton ${p.acopy ? 'direkt' : String(p.audio || '').toUpperCase() + ' → AAC'}`, 3000);
      }
    } else if (state === 'interrupted') recoverStream(item, 'FFmpeg beendet');
    return;
  }
  const label = STREAM_LABEL[item.streamType] || String(item.streamType || '').toUpperCase();
  if (state === 'resolving') osd(`${label}: Stream wird aufgelöst…`, 20000);
  else if (state === 'connecting') osd(`${label}: Verbinde…`, 20000);
  else if (state === 'playing') {
    if (recovery.uid === item.uid) recovery.connects = 0;
    osd(item.streamInfo && item.streamInfo.live ? `● LIVE · ${label}` : `${label}: ${item.title || item.name}`, 2500);
    if (item.title && item.title !== item.name) { playlist.update(item.uid, { name: item.title }); updateNowPlaying(); }
    if (item.streamInfo && item.streamInfo.duration) playlist.update(item.uid, { duration: item.streamInfo.duration });
  } else if (state === 'ended') osd(`${label}: Stream beendet`, 3000);
  else if (state === 'interrupted') recoverStream(item, 'Verbindung unterbrochen');
});
player.addEventListener('subs-loaded', (e) => {
  if (e.detail.item !== player.item) return;
  osd(`${e.detail.count} eingebettete Untertitel verfügbar${e.detail.showing ? ' · angezeigt: ' + e.detail.showing : ''} (Rechtsklick → Untertitel)`, 3500);
});
player.addEventListener('stream-error', (e) => {
  // Auflösungsfehler (offline, privat, yt-dlp fehlt) → sofort melden; Wiedergabefehler → neu verbinden
  if (e.detail.phase === 'play') recoverStream(e.detail.item, e.detail.message);
  else if (e.detail.phase === 'connect') recoverStream(e.detail.item, e.detail.message, { connect: true });
  else showStreamError(e.detail.item, e.detail.message);
});

let streamErrShown = null;
async function showStreamError(item, message) {
  if (item !== player.item || streamErrShown === item) return;
  streamErrShown = item;
  playlist.update(item.uid, { error: true });
  $('#osd').classList.remove('show');
  const r = await openDialog({
    title: 'Stream konnte nicht geöffnet werden',
    body: `<p><b>${esc(item.path)}</b></p><p class="muted">${esc(message || 'Unbekannter Fehler')}</p>
      ${/yt-dlp/.test(message || '') ? '<p>Für YouTube, Twitch und andere Webseiten wird <b>yt-dlp</b> benötigt: <code>yt-dlp.exe</code> nach <code>tools\\yt-dlp\\</code> legen oder den Pfad unter Werkzeuge → Einstellungen setzen.</p>' : ''}`,
    buttons: [{ label: 'Einstellungen…', value: 'settings' }, { label: 'Erneut versuchen', value: 'retry' }, { label: 'Schließen', value: 'close', primary: true }]
  });
  streamErrShown = null;
  if (r === 'retry' && player.item === item) { item.error = false; playIndex(playlist.items.indexOf(item)); }
  if (r === 'settings') settingsDialog();
}

// ---------------------------------------------------------------- Kompatibilität (FFmpeg)
let compatAvail = null;
async function compatAvailable() { if (compatAvail === null) compatAvail = await host.invoke('compat:available'); return compatAvail; }
let compatJob = null;

async function prepareCompat(it) {
  const jobId = crypto.randomUUID();
  compatJob = jobId;
  const busy = $('#busy');
  $('#busy-title').textContent = 'Analysiere ' + it.name + '…';
  $('#busy-bar').style.width = '0%';
  $('#busy-detail').textContent = '';
  busy.classList.remove('hidden');
  const off = host.on('compat:progress', (p) => {
    if (p.jobId !== jobId) return;
    const mode = p.plan && p.plan.mode === 'remux' ? 'Verlustfreies Umverpacken (Remux)' : 'Umwandeln nach H.264/AAC';
    $('#busy-title').textContent = p.stage === 'done' ? 'Fertig' : mode + '…';
    $('#busy-bar').style.width = (p.percent || 0).toFixed(1) + '%';
    if (p.plan) $('#busy-detail').textContent = `Video: ${p.plan.video || '–'} · Audio: ${p.plan.audio || '–'} · ${Math.floor(p.percent || 0)} %`;
  });
  $('#busy-cancel').onclick = () => { host.invoke('compat:cancel', jobId); };
  try {
    const r = await host.invoke('compat:prepare', { jobId, file: it.path });
    it.compat = true; it.compatUrl = r.url; it.compatSub = r.sub;
    return true;
  } catch (e) {
    if (compatJob === jobId) osd('Kompatibilitätsmodus fehlgeschlagen: ' + String(e.message || e).replace(/^Error invoking remote method '[^']+': (Error: )?/, ''), 5000);
    it.error = true;
    return false;
  } finally {
    off();
    busy.classList.add('hidden');
    compatJob = null;
  }
}

let errorBusy = false;
async function handleMediaError({ item, message, soft }) {
  if (errorBusy || !item || item !== player.item) return;
  errorBusy = true;
  try {
    const canCompat = isLocal(item) && !item.localStream && S.compatMode !== 'off' && await compatAvailable();
    if (!canCompat) {
      if (!soft) {
        playlist.update(item.uid, { error: true });
        osd('Nicht abspielbar: ' + item.name + (isLocal(item) && !(await compatAvailable()) ? ' – FFmpeg für Kompatibilitätsmodus einrichten' : ''), 4500);
        const next = playlist.nextIndex(true);
        if (next >= 0 && next !== playlist.index) setTimeout(() => playIndex(next), 1500);
      }
      return;
    }
    // Direkte Wiedergabe gescheitert → automatisch in den Echtzeit-Kompatibilitätsmodus (FFmpeg)
    host.send('stream:log', `Direkte Wiedergabe fehlgeschlagen (${message || 'unbekannt'}) → Kompatibilitätsmodus: ${item.path}`);
    item.localStream = true;
    if (soft) item.transcodeVideo = true; // Bild nicht dekodierbar → Video neu kodieren
    osd('Format nicht direkt unterstützt – Kompatibilitätsmodus', 2500);
    const t = video.currentTime || 0;
    await player.load(item, { startAt: t > 1 ? t : 0 });
    renderPlaylist();
  } finally { errorBusy = false; }
}

// ---------------------------------------------------------------- Steuerleiste
const seek = $('#seek');
function updateTime() {
  const d = video.duration, t = video.currentTime;
  const pct = isFinite(d) && d > 0 ? (t / d) * 100 : 0;
  $('#seek-fill').style.width = pct + '%';
  $('#seek-thumb').style.left = pct + '%';
  if (player.isLive) {
    const lag = player.stream.liveEdge() - t;
    $('#time').textContent = (lag > 6 ? '−' + fmt(lag) + ' ' : '') + '● LIVE';
    $('#seek-fill').style.width = '100%';
    $('#seek-thumb').style.left = '100%';
    return;
  }
  $('#time').textContent = fmt(t) + ' / ' + (isFinite(d) ? fmt(d) : '--:--');
  if (video.buffered.length && isFinite(d) && d > 0) $('#seek-buffer').style.width = (video.buffered.end(video.buffered.length - 1) / d) * 100 + '%';
}
video.addEventListener('timeupdate', updateTime);
video.addEventListener('durationchange', updateTime);
video.addEventListener('progress', updateTime);

function seekFromEvent(e) {
  const r = seek.getBoundingClientRect();
  return Math.max(0, Math.min(1, (e.clientX - r.left) / r.width));
}
seek.addEventListener('mousemove', (e) => {
  const f = seekFromEvent(e);
  const tip = $('#seek-tip');
  tip.style.left = f * 100 + '%';
  tip.textContent = fmt(f * (video.duration || 0));
});
seek.addEventListener('mousedown', (e) => {
  if (!isFinite(video.duration)) return;
  seek.classList.add('drag');
  // Bei Streams erst beim Loslassen springen (jeder Sprung außerhalb des Puffers startet FFmpeg neu)
  const deferred = !!player.stream;
  let lastF = seekFromEvent(e);
  const move = (ev) => {
    lastF = seekFromEvent(ev);
    if (deferred) { $('#seek-fill').style.width = lastF * 100 + '%'; $('#seek-thumb').style.left = lastF * 100 + '%'; return; }
    player.seek(lastF * video.duration); updateTime();
  };
  move(e);
  const up = () => {
    seek.classList.remove('drag'); window.removeEventListener('mousemove', move); window.removeEventListener('mouseup', up);
    if (deferred) player.seek(lastF * video.duration);
  };
  window.addEventListener('mousemove', move);
  window.addEventListener('mouseup', up);
});

function updateAB() {
  const el = $('#seek-ab'); const d = video.duration; const { a, b } = player.ab;
  if (a == null || !isFinite(d)) { el.style.display = 'none'; return; }
  el.style.display = 'block';
  el.style.left = (a / d) * 100 + '%';
  el.style.width = (((b ?? video.currentTime) - a) / d) * 100 + '%';
}
player.addEventListener('ab', updateAB);

function updatePlayState() {
  const playing = !video.paused && !video.ended;
  $('#btn-play').textContent = playing ? '❚❚' : '▶';
  appEl.classList.toggle('playing', playing);
}
video.addEventListener('play', updatePlayState);
video.addEventListener('pause', () => { updatePlayState(); rememberPosition(); });

const vol = $('#volume');
function updateVolumeUi() {
  const pct = Math.round(player.volumeLevel * 100);
  vol.value = pct;
  $('#vol-label').textContent = pct + ' %';
  $('#btn-mute').textContent = video.muted || pct === 0 ? '🔇' : pct > 100 ? '📢' : '🔊';
}
vol.addEventListener('input', () => { player.setVolume(vol.value / 100); if (video.muted) player.setMuted(false); });
vol.addEventListener('change', () => patchSettings({ volume: player.volumeLevel, muted: video.muted }));
player.addEventListener('volume', updateVolumeUi);

function setVolume(level, show = true) {
  const l = player.setVolume(level);
  if (video.muted && level > 0) player.setMuted(false);
  patchSettings({ volume: l, muted: video.muted });
  if (show) osd('Lautstärke ' + Math.round(l * 100) + ' %');
}

function setRate(r) {
  const v = player.setRate(r);
  $('#btn-speed').textContent = v.toFixed(2) + '×';
  patchSettings({ playbackRate: v });
  osd('Geschwindigkeit ' + v.toFixed(2) + '×');
}

function setFit(mode) {
  video.classList.remove('fit-cover', 'fit-fill', 'fit-none');
  if (mode !== 'contain') video.classList.add('fit-' + mode);
  const f = FITS.find((x) => x[0] === mode) || FITS[0];
  $('#btn-fit').textContent = f[1];
  patchSettings({ fitMode: mode });
  return f[1];
}

function updateRepeatShuffle() {
  const rb = $('#btn-repeat');
  rb.classList.toggle('on', playlist.repeat !== 'none');
  rb.textContent = playlist.repeat === 'one' ? '↻¹' : '↻';
  rb.title = { none: 'Wiederholen: aus', all: 'Wiederholen: alle', one: 'Wiederholen: eins' }[playlist.repeat] + ' (R)';
  $('#btn-shuffle').classList.toggle('on', playlist.shuffle);
}

// Position merken (Fortsetzen)
let lastSaved = 0;
function rememberPosition() {
  const it = player.item;
  if (!S.resumePlayback || !it || !isLocal(it)) return;
  const d = video.duration, t = video.currentTime;
  if (!isFinite(d) || d < 60) return;
  const k = resumeKey(it);
  S.resume = S.resume || {};
  if (t > 10 && t < d - 10) S.resume[k] = { t: Math.round(t), d: Math.round(d), at: Date.now() };
  else delete S.resume[k];
  const keys = Object.keys(S.resume);
  if (keys.length > 300) keys.sort((a, b) => S.resume[a].at - S.resume[b].at).slice(0, keys.length - 300).forEach((x) => delete S.resume[x]);
  patchSettings({ resume: S.resume });
}
video.addEventListener('timeupdate', () => { if (Date.now() - lastSaved > 5000) { lastSaved = Date.now(); rememberPosition(); } });
window.addEventListener('beforeunload', () => { rememberPosition(); saveSession(); });

let sessionTimer = null;
function saveSession() { patchSettings({ session: playlist.items.map((i) => i.path), sessionIndex: playlist.index }); }
function saveSessionSoon() { clearTimeout(sessionTimer); sessionTimer = setTimeout(saveSession, 800); }

function addRecent(p) {
  if (!p) return;
  const list = [p, ...(S.recent || []).filter((x) => x !== p)].slice(0, 15);
  patchSettings({ recent: list });
}

// ---------------------------------------------------------------- Vollbild & Leerlauf
function toggleFullscreen(force) {
  const on = force === undefined ? !document.fullscreenElement : force;
  if (on && !document.fullscreenElement) document.documentElement.requestFullscreen().catch(() => host.send('win:fullscreen', true));
  else if (!on && document.fullscreenElement) document.exitFullscreen();
}
document.addEventListener('fullscreenchange', () => appEl.classList.toggle('fullscreen', !!document.fullscreenElement));
host.on('fullscreen', (on) => appEl.classList.toggle('fullscreen', on || !!document.fullscreenElement));
let idleTimer = null;
function poke() {
  appEl.classList.remove('idle');
  clearTimeout(idleTimer);
  idleTimer = setTimeout(() => { if (!video.paused && popup.classList.contains('hidden')) appEl.classList.add('idle'); }, 2500);
}
document.addEventListener('mousemove', poke);
// Einfacher Klick aufs Bild: Wiedergabe/Pause · Doppelklick: Vollbild (der Einzelklick wird dann verworfen)
const onPicture = (e) => e.target === video || e.target.id === 'stage' || e.target.id === 'audio-art';
let clickTimer = null;
$('#stage').addEventListener('click', (e) => {
  if (!onPicture(e) || e.button !== 0 || !player.item) return;
  if (e.detail > 1) return;
  clearTimeout(clickTimer);
  clickTimer = setTimeout(() => {
    player.toggle();
    setTimeout(() => osd(video.paused ? '❚❚ Pause' : '▶ Wiedergabe', 900), 50);
  }, 230);
});
$('#stage').addEventListener('dblclick', (e) => {
  if (!onPicture(e)) return;
  clearTimeout(clickTimer);
  toggleFullscreen();
});
$('#stage').addEventListener('wheel', (e) => {
  if (e.target !== video && e.target.id !== 'stage') return;
  e.preventDefault();
  setVolume(player.volumeLevel + (e.deltaY < 0 ? 0.05 : -0.05));
}, { passive: false });

// ---------------------------------------------------------------- Drag & Drop
let dragDepth = 0;
const isFileDrag = (e) => e.dataTransfer && [...e.dataTransfer.types].includes('Files');
window.addEventListener('dragenter', (e) => { if (!isFileDrag(e)) return; dragDepth++; $('#dropzone').classList.remove('hidden'); });
window.addEventListener('dragleave', () => { if (--dragDepth <= 0) { dragDepth = 0; $('#dropzone').classList.add('hidden'); } });
window.addEventListener('dragover', (e) => { if (isFileDrag(e)) e.preventDefault(); });
window.addEventListener('drop', async (e) => {
  dragDepth = 0; $('#dropzone').classList.add('hidden');
  if (!isFileDrag(e)) return;
  e.preventDefault();
  const paths = [...e.dataTransfer.files].map((f) => host.pathForFile(f)).filter(Boolean);
  const text = e.dataTransfer.getData('text/uri-list') || '';
  if (!paths.length && /^https?:/i.test(text)) paths.push(text.split(/\r?\n/)[0]);
  const items = await host.invoke('media:resolve', paths);
  const onlySubs = items.length && items.every((i) => i.kind === 'subtitle');
  const inPlaylist = e.target.closest && e.target.closest('#playlist');
  addToPlaylist(items, { play: !onlySubs && !inPlaylist, replace: false });
});

// ---------------------------------------------------------------- UVE-Bedienung
function uveKey(k) {
  const opts = k === 'F8'
    ? { key: 'F8', code: 'F8', bubbles: true, cancelable: true }
    : { key: k, code: 'Key' + k.toUpperCase(), ctrlKey: true, altKey: true, bubbles: true, cancelable: true };
  (document.activeElement && document.activeElement !== document.body ? document.activeElement : document).dispatchEvent(new KeyboardEvent('keydown', opts));
  setTimeout(() => (document.activeElement || document).dispatchEvent(new KeyboardEvent('keyup', opts)), 30);
}
const uveLoaded = () => PLUGIN_RESULTS.some((r) => r.id === 'uve' && r.state === 'loaded');
function uveMenu(anchor) {
  showPopup(anchor, [{ header: uveLoaded() ? 'Ultimate Video Enhancer' : 'Ultimate Video Enhancer ist nicht aktiv' }, ...uveEntries()]);
}
function uveEntries() {
  if (!uveLoaded()) return [{ label: 'Plugins verwalten…', run: () => act('plugins') }];
  return [
    { label: 'Einstellungen / Import-Export', kbd: 'Strg+Alt+I', run: () => uveKey('i') },
    { label: 'Grading-HUD', kbd: 'Strg+Alt+G', run: () => uveKey('g') },
    { label: 'Vorher/Nachher-Vergleich', kbd: 'Strg+Alt+U', run: () => uveKey('u') },
    { label: 'Scopes-HUD', kbd: 'Strg+Alt+S', run: () => uveKey('s') },
    '-',
    { label: 'HDR an/aus', kbd: 'Strg+Alt+P', run: () => uveKey('p') },
    { label: 'Profil an/aus', kbd: 'Strg+Alt+C', run: () => uveKey('c') },
    { label: 'Auto-Scene-Match', kbd: 'Strg+Alt+A', run: () => uveKey('a') },
    { label: 'GPU-Modus', kbd: 'Strg+Alt+X', run: () => uveKey('x') },
    { label: 'Nächstes Profil', kbd: 'F8', run: () => uveKey('F8') },
    '-',
    ...pluginMenuCommands.filter((c) => c.plugin === 'uve').map((c) => ({ label: c.caption, run: c.fn })),
    { label: 'Plugin verwalten…', run: () => act('plugins') }
  ];
}

// ---------------------------------------------------------------- Kontextmenü des Videobilds (Rechtsklick)
function subtitleEntries() {
  const tracks = player.subtitleTracks();
  const setSub = (idx) => { const tt = video.textTracks; for (let i = 0; i < tt.length; i++) tt[i].mode = i === idx ? 'showing' : 'disabled'; };
  return [
    ...tracks.map((t) => ({ label: t.label || ('Spur ' + (t.index + 1)), on: t.showing, run: () => { setSub(t.index); osd('Untertitel: ' + t.label); } })),
    { label: 'Aus', on: !tracks.some((t) => t.showing), run: () => { setSub(-1); osd('Untertitel aus'); } },
    '-',
    { label: 'Untertiteldatei hinzufügen…', run: () => act('add-subtitle') },
    { label: `Verzögerung −50 ms`, kbd: 'G', run: () => act('sub-delay-minus') },
    { label: `Verzögerung +50 ms`, kbd: 'H', run: () => act('sub-delay-plus') },
    player.subDelay ? { label: `Verzögerung zurücksetzen (${Math.round(player.subDelay * 1000)} ms)`, run: () => { player.shiftSubtitles(-player.subDelay * 1000); osd('Untertitelverzögerung 0 ms'); } } : null
  ];
}

function audioEntries() {
  const tracks = player.audioTracks();
  return [
    ...(tracks.length
      ? tracks.map((t) => ({ label: t.label, on: t.enabled, run: async () => { if (t.enabled) return; osd('Wechsle Audiospur…', 3000); const l = await player.setAudioTrack(t.index); if (l) osd('Audiospur: ' + l); } }))
      : [{ label: 'Nur eine Audiospur', disabled: true }]),
    '-',
    { label: video.muted ? 'Ton einschalten' : 'Stumm', kbd: 'M', run: () => act('mute') },
    { label: 'Equalizer / Lautstärke-Boost…', kbd: 'Strg+E', run: () => act('equalizer') }
  ];
}

$('#stage').addEventListener('contextmenu', (e) => {
  const onEmpty = e.target.closest && e.target.closest('#empty');
  if (!onPicture(e) && !onEmpty) return; // Plugin-Oberflächen (UVE-Panels) behalten ihr eigenes Menü
  e.preventDefault();
  clearTimeout(clickTimer);
  const at = { x: e.clientX, y: e.clientY };
  if (!player.item) {
    showPopup(null, [
      { label: 'Datei öffnen…', kbd: 'Strg+O', run: () => act('open-file') },
      { label: 'Ordner öffnen…', kbd: 'Strg+F', run: () => act('open-folder') },
      { label: 'Netzwerkstream öffnen…', kbd: 'Strg+N', run: () => act('open-url') },
      '-',
      { label: 'Ultimate Video Enhancer', sub: uveEntries },
      { label: 'Plugins…', kbd: 'Strg+P', run: () => act('plugins') },
      { label: 'Einstellungen…', run: () => act('settings') }
    ], at);
    return;
  }
  const fit = S.fitMode || 'contain';
  const rate = player.rate || 1;
  showPopup(null, [
    { label: video.paused ? 'Wiedergabe' : 'Pause', kbd: 'Leertaste', run: () => act('toggle-play') },
    { label: 'Stopp', kbd: 'S', run: () => act('stop') },
    { label: 'Vorheriger Titel', kbd: 'P', run: () => act('prev') },
    { label: 'Nächster Titel', kbd: 'N', run: () => act('next') },
    '-',
    { label: 'Audiospur', sub: audioEntries },
    { label: 'Untertitel', sub: subtitleEntries },
    {
      label: `Geschwindigkeit (${rate.toFixed(2)}×)`,
      sub: () => [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 3, 4].map((r) => ({ label: r.toFixed(2) + '×' + (r === 1 ? ' (normal)' : ''), on: Math.abs(rate - r) < 0.01, run: () => setRate(r) }))
    },
    {
      label: 'Bild',
      sub: () => [
        ...FITS.map(([m, l]) => ({ label: l, on: fit === m, run: () => osd('Bild: ' + setFit(m)) })),
        '-',
        { label: document.fullscreenElement ? 'Vollbild beenden' : 'Vollbild', kbd: 'F', run: () => act('fullscreen') },
        { label: 'Bildschirmfoto', kbd: 'Umschalt+S', run: () => act('snapshot') },
        { label: 'Immer im Vordergrund', on: !!S.alwaysOnTop, run: () => act('always-on-top') },
        { label: 'Minimale Oberfläche', kbd: 'Strg+H', run: () => act('minimal') }
      ]
    },
    { label: 'Ultimate Video Enhancer', sub: uveEntries },
    '-',
    { label: player.ab.a == null ? 'A-B-Wiederholung: A setzen' : player.ab.b == null ? 'A-B-Wiederholung: B setzen' : 'A-B-Wiederholung aus', kbd: 'L', run: () => act('ab-loop') },
    { label: 'Zu Zeit springen…', kbd: 'Strg+T', run: () => act('goto-time'), disabled: player.isLive },
    '-',
    { label: document.fullscreenElement ? 'Vollbild beenden' : 'Vollbild', kbd: 'F', run: () => act('fullscreen') },
    { label: 'Medieninformationen…', kbd: 'Strg+I', run: () => act('media-info') },
    { label: appEl.classList.contains('no-playlist') ? 'Wiedergabeliste einblenden' : 'Wiedergabeliste ausblenden', kbd: 'Strg+L', run: () => act('toggle-playlist') }
  ], at);
});
const pluginMenuCommands = [];
window.addEventListener('uve:plugin-menu', (e) => pluginMenuCommands.push(e.detail));

// ---------------------------------------------------------------- Aktionen
async function openKind(kind, play = true) {
  const items = await host.invoke('dialog:open', kind);
  if (items.length) addToPlaylist(items, { play });
}

const actions = {
  'open-file': () => openKind('file'),
  'open-files': () => openKind('files'),
  'open-folder': () => openKind('folder'),
  'open-url': openUrlDialog,
  'open-recent': async (p) => addToPlaylist(await host.invoke('media:resolve', [p]), { play: true }),
  'clear-recent': () => patchSettings({ recent: [] }),
  'save-playlist': async () => { if (!playlist.items.length) return osd('Wiedergabeliste ist leer'); const f = await host.invoke('playlist:save', playlist.items); if (f) osd('Gespeichert'); },
  'quit': () => host.send('app:quit'),
  'toggle-play': () => { if (!player.item && playlist.items.length) playIndex(Math.max(0, playlist.index)); else if (!player.item) openKind('file'); else player.toggle(); },
  'stop': () => { player.stop(); updatePlayState(); osd('Stopp'); },
  'prev': () => { if (video.currentTime > 5 && player.item) return player.seek(0); const it = playlist.prev(); if (it) playIndex(playlist.index); },
  'next': () => { const i = playlist.nextIndex(false); if (i >= 0) playIndex(i); },
  'seek-back': (s) => { player.seekBy(-(s || S.seekStep || 10)); osd('◀◀ ' + fmt(video.currentTime)); },
  'seek-fwd': (s) => { player.seekBy(s || S.seekStep || 10); osd('▶▶ ' + fmt(video.currentTime)); },
  'goto-time': gotoTimeDialog,
  'frame-step': () => { player.frameStep(1); osd('Einzelbild ▶'); },
  'faster': () => setRate((player.rate || 1) + (player.rate >= 2 ? 0.25 : 0.1)),
  'slower': () => setRate((player.rate || 1) - (player.rate > 2 ? 0.25 : 0.1)),
  'normal-speed': () => setRate(1),
  'speed-menu': (_a, el) => showPopup(el, [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 3, 4].map((r) => ({ label: r.toFixed(2) + '×', on: Math.abs((player.rate || 1) - r) < 0.01, run: () => setRate(r) }))),
  'ab-loop': () => { const ab = player.cycleAB(); osd(ab.a == null ? 'A-B: aus' : ab.b == null ? 'A gesetzt ' + fmt(ab.a) : 'A-B: ' + fmt(ab.a) + ' – ' + fmt(ab.b)); updateAB(); },
  'cycle-repeat': () => { playlist.repeat = { none: 'all', all: 'one', one: 'none' }[playlist.repeat]; patchSettings({ repeat: playlist.repeat }); updateRepeatShuffle(); osd({ none: 'Wiederholen: aus', all: 'Wiederholen: alle', one: 'Wiederholen: eins' }[playlist.repeat]); },
  'toggle-shuffle': () => { playlist.shuffle = !playlist.shuffle; patchSettings({ shuffle: playlist.shuffle }); updateRepeatShuffle(); osd('Zufall: ' + (playlist.shuffle ? 'an' : 'aus')); },
  'vol-up': () => setVolume(player.volumeLevel + 0.05),
  'vol-down': () => setVolume(player.volumeLevel - 0.05),
  'mute': () => { player.setMuted(!video.muted); patchSettings({ muted: video.muted }); osd(video.muted ? 'Stumm' : 'Ton an'); },
  'equalizer': equalizerDialog,
  'cycle-audio': async () => { const l = await player.cycleAudioTrack(); osd(l ? 'Audiospur: ' + l : 'Nur eine Audiospur'); },
  'fullscreen': () => toggleFullscreen(),
  'cycle-fit': () => { const i = FITS.findIndex((f) => f[0] === (S.fitMode || 'contain')); osd('Bild: ' + setFit(FITS[(i + 1) % FITS.length][0])); },
  'snapshot': async () => {
    try {
      const data = await player.snapshot();
      const base = (player.item ? player.item.name.replace(/\.[^.]+$/, '') : 'uve');
      const f = await host.invoke('snapshot:save', { data, name: `${base}_${fmt(video.currentTime).replace(/:/g, '-')}_${Date.now() % 100000}.png` });
      osd('Bildschirmfoto gespeichert: ' + f.split(/[\\/]/).pop(), 2500);
    } catch (e) { osd('Bildschirmfoto fehlgeschlagen: ' + e.message); }
  },
  'always-on-top': () => { patchSettings({ alwaysOnTop: !S.alwaysOnTop }); osd('Immer im Vordergrund: ' + (S.alwaysOnTop ? 'an' : 'aus')); },
  'uve-key': (k) => uveKey(k),
  'uve-menu': (_a, el) => uveMenu(el),
  'add-subtitle': async () => { if (!player.item) return osd('Zuerst ein Video öffnen'); const items = await host.invoke('dialog:open', 'subtitle'); addToPlaylist(items); },
  'cycle-sub': () => { const l = player.cycleSubtitle(); osd(l ? 'Untertitel: ' + l : 'Keine Untertitel'); },
  'sub-delay-minus': () => osd('Untertitelverzögerung ' + Math.round(player.shiftSubtitles(-50) * 1000) + ' ms'),
  'sub-delay-plus': () => osd('Untertitelverzögerung ' + Math.round(player.shiftSubtitles(50) * 1000) + ' ms'),
  'sub-menu': (_a, el) => showPopup(el, [
    { header: 'Untertitel' },
    ...player.subtitleTracks().map((t) => ({ label: t.label, on: t.showing, run: () => { const tt = video.textTracks; for (let i = 0; i < tt.length; i++) tt[i].mode = i === t.index ? 'showing' : 'disabled'; } })),
    { label: 'Aus', on: !player.subtitleTracks().some((t) => t.showing), run: () => { const tt = video.textTracks; for (let i = 0; i < tt.length; i++) tt[i].mode = 'disabled'; } },
    '-',
    { label: 'Datei hinzufügen…', run: () => act('add-subtitle') },
    { label: 'Verzögerung −50 ms', kbd: 'G', run: () => act('sub-delay-minus') },
    { label: 'Verzögerung +50 ms', kbd: 'H', run: () => act('sub-delay-plus') },
    ...(player.audioTracks().length > 1 ? ['-', { header: 'Audiospuren' }, ...player.audioTracks().map((t) => ({ label: t.label, on: t.enabled, run: async () => { const l = await player.setAudioTrack(t.index); if (l) osd('Audiospur: ' + l); } }))] : [])
  ]),
  'plugins': pluginsDialog,
  'plugin-toggle': async (id) => { const p = PLUGINS.find((x) => x.id === id); if (p) await setPluginEnabled(p, !p.enabled); },
  'media-info': mediaInfoDialog,
  'settings': settingsDialog,
  'toggle-playlist': () => { const show = appEl.classList.toggle('no-playlist') === false; patchSettings({ showPlaylist: show }); },
  'minimal': () => { appEl.classList.toggle('minimal'); poke(); },
  'reload': () => reloadUi(),
  'shortcuts': shortcutsDialog,
  'check-tools': () => checkToolUpdates(true),
  'open-uve-repo': () => host.invoke('shell:open-external', 'https://github.com/nextscript/Ultimate-Video-Enhancer'),
  'about': aboutDialog,
  'pl-add': (_a, el) => showPopup(el, [
    { label: 'Dateien…', run: () => openKind('files', false) },
    { label: 'Ordner…', run: () => openKind('folder', false) },
    { label: 'Netzwerkstream…', run: () => openUrlDialog(false) }
  ]),
  'pl-remove': () => { playlist.remove([...selected]); selected.clear(); },
  'pl-clear': () => { playlist.clear(); selected.clear(); },
  'pl-sort': () => playlist.sort()
};

function act(name, arg, el) {
  const fn = actions[name];
  if (!fn) return console.warn('Unbekannte Aktion', name);
  try { const r = fn(arg, el); if (r && r.catch) r.catch((e) => osd('Fehler: ' + e.message, 4000)); } catch (e) { osd('Fehler: ' + e.message, 4000); }
}
host.on('menu', ({ action, arg }) => act(action, arg));
document.addEventListener('click', (e) => {
  const b = e.target.closest('[data-action]');
  if (b && !dlg.contains(b)) { act(b.dataset.action, undefined, b); b.blur(); }
});

// ---------------------------------------------------------------- Tastenkürzel (VLC-Belegung)
const KEYS = [
  [' ', 'toggle-play'], ['f', 'fullscreen'], ['s', 'stop'], ['n', 'next'], ['p', 'prev'], ['m', 'mute'],
  ['e', 'frame-step'], ['l', 'ab-loop'], ['r', 'cycle-repeat'], ['z', 'toggle-shuffle'], ['a', 'cycle-fit'],
  ['v', 'cycle-sub'], ['b', 'cycle-audio'], ['g', 'sub-delay-minus'], ['h', 'sub-delay-plus'],
  [']', 'faster'], ['[', 'slower'], ['=', 'normal-speed'], ['+', 'faster'], ['-', 'slower'], ['F1', 'shortcuts'],
  ['MediaPlayPause', 'toggle-play'], ['MediaTrackNext', 'next'], ['MediaTrackPrevious', 'prev'], ['MediaStop', 'stop']
];
window.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && !popup.classList.contains('hidden')) { hidePopup(); e.preventDefault(); return; }
  if (e.defaultPrevented || e.isComposing) return;                 // z. B. von UVE verarbeitet
  if (e.ctrlKey && e.altKey) return;                               // STRG+ALT gehört UVE
  const t = e.target;
  if (t && (t.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName) || (t.closest && t.closest('dialog')))) return;
  if (t && t !== document.body && t.closest && !t.closest('.ui-root')) return; // Plugin-Oberflächen (UVE-Panels)
  poke();
  const k = e.key;
  let action = null, arg;
  if (e.ctrlKey && !e.shiftKey) {
    action = { o: 'open-file', f: 'open-folder', n: 'open-url', y: 'save-playlist', q: 'quit', t: 'goto-time', e: 'equalizer', p: 'plugins', i: 'media-info', l: 'toggle-playlist', h: 'minimal', r: 'reload', ',': 'settings' }[k.toLowerCase()];
    if (k === 'ArrowLeft') { action = 'seek-back'; arg = 60; }
    if (k === 'ArrowRight') { action = 'seek-fwd'; arg = 60; }
  } else if (e.ctrlKey && e.shiftKey) {
    action = { o: 'open-files' }[k.toLowerCase()];
  } else if (e.shiftKey && k.toLowerCase() === 's') {
    action = 'snapshot';
  } else if (k === 'ArrowLeft') { action = 'seek-back'; arg = e.shiftKey ? 3 : e.altKey ? 10 : undefined; }
  else if (k === 'ArrowRight') { action = 'seek-fwd'; arg = e.shiftKey ? 3 : e.altKey ? 10 : undefined; }
  else if (k === 'ArrowUp') action = 'vol-up';
  else if (k === 'ArrowDown') action = 'vol-down';
  else if (k === 'Escape') { if (document.fullscreenElement) action = 'fullscreen'; else if (appEl.classList.contains('minimal')) action = 'minimal'; }
  else if (!e.altKey && !e.metaKey) { const m = KEYS.find(([kk]) => kk === (k.length === 1 ? k.toLowerCase() : k)); if (m) action = m[1]; }
  if (k >= '0' && k <= '9' && !e.ctrlKey && !e.altKey && player.item && isFinite(video.duration)) { player.seek(video.duration * Number(k) / 10); osd(k + '0 %'); e.preventDefault(); return; }
  if (action) { e.preventDefault(); act(action, arg); }
});

// ---------------------------------------------------------------- Dialog-Implementierungen
async function openUrlDialog(play = true) {
  const r = await openDialog({
    title: 'Netzwerkstream öffnen',
    wide: true,
    body: `<label class="field"><span>Adresse</span><input type="text" id="url-in" list="url-recent" placeholder="https://www.youtube.com/watch?v=…  ·  https://www.twitch.tv/kanal  ·  tcp://host:port" spellcheck="false"></label>
           <datalist id="url-recent">${(S.recent || []).filter((p) => /^[a-z]+:\/\//i.test(p)).map((p) => `<option value="${esc(p)}">`).join('')}</datalist>
           <table>
             <tr><th>YouTube, Twitch</th><td>Videos, Livestreams, VODs – über yt-dlp (H.264 bevorzugt, max. ${S.streamMaxHeight || 1080}p)</td></tr>
             <tr><th>tcp:// udp:// rtmp:// rtsp:// srt://</th><td>Live-Übertragungen (z. B. MPEG-TS) – über FFmpeg</td></tr>
             <tr><th>.m3u8 / .mpd / .ts</th><td>HLS/DASH und Transportstreams – über FFmpeg</td></tr>
             <tr><th>.mp4 / .webm / .mp3 …</th><td>Direkte Wiedergabe über den Player-Proxy</td></tr>
             <tr><th>andere Webseiten</th><td>Versuch über yt-dlp (über 1000 unterstützte Seiten)</td></tr>
           </table>
           <p class="muted">Alle Quellen laufen über den Player, damit UVE-Effekte inklusive GPU-Modus und Bildschirmfotos funktionieren.</p>`,
    buttons: [{ label: 'Abbrechen', value: 'no' }, { label: 'Öffnen', value: 'ok', primary: true }]
  });
  const url = dlgForm.querySelector('#url-in')?.value.trim();
  if (r !== 'ok' || !url) return;
  if (!/^(https?|tcp|udp|rtp|rtmps?|rtmpt|rtsps?|srt|mmsh?):\/\//i.test(url)) return osd('Nicht unterstütztes Adressformat', 3000);
  addToPlaylist(await host.invoke('media:resolve', [url]), { play: play !== false });
}

async function gotoTimeDialog() {
  if (!player.item) return;
  const r = await openDialog({
    title: 'Zu Zeit springen',
    body: `<label class="field"><span>Zeit (hh:mm:ss)</span><input type="text" id="t-in" value="${fmt(video.currentTime)}"></label>`,
    buttons: [{ label: 'Abbrechen', value: 'no' }, { label: 'Springen', value: 'ok', primary: true }]
  });
  const t = parseTime(dlgForm.querySelector('#t-in')?.value || '');
  if (r === 'ok' && isFinite(t)) player.seek(t);
}

async function equalizerDialog() {
  const eq = player.eq;
  const bands = EQ_FREQS.map((f, i) => `<div class="band"><span id="eqv${i}">${eq.bands[i]}</span><input type="range" min="-20" max="20" step="0.5" value="${eq.bands[i]}" data-band="${i}"><span>${f >= 1000 ? f / 1000 + 'k' : f}</span></div>`).join('');
  await openDialog({
    title: 'Equalizer & Lautstärke',
    wide: true,
    body: `<label class="field"><span>Equalizer aktiv</span><input type="checkbox" id="eq-on" ${eq.enabled ? 'checked' : ''}></label>
      <label class="field"><span>Voreinstellung</span><select id="eq-preset">${Object.keys(EQ_PRESETS).map((n) => `<option ${n === eq.preset ? 'selected' : ''}>${esc(n)}</option>`).join('')}</select></label>
      <label class="field"><span>Vorverstärkung <b id="eq-prev">${eq.preamp} dB</b></span><input type="range" id="eq-pre" min="-20" max="20" step="0.5" value="${eq.preamp}"></label>
      <div class="eq">${bands}</div>
      <label class="field"><span>Lautstärke <b id="eq-volv">${Math.round(player.volumeLevel * 100)} %</b></span><input type="range" id="eq-vol" min="0" max="200" value="${Math.round(player.volumeLevel * 100)}"></label>
      <p class="muted">Über 100 % wird digital verstärkt (mit Limiter). Equalizer und UVE-Audiofilter teilen sich denselben Audio-Graphen.</p>`,
    onRender: (f) => {
      const apply = () => {
        const b = [...f.querySelectorAll('[data-band]')].map((x) => Number(x.value));
        b.forEach((v, i) => { f.querySelector('#eqv' + i).textContent = v; });
        const pre = Number(f.querySelector('#eq-pre').value);
        f.querySelector('#eq-prev').textContent = pre + ' dB';
        player.setEq({ enabled: f.querySelector('#eq-on').checked, preamp: pre, bands: b, preset: f.querySelector('#eq-preset').value });
        patchSettings({ eq: player.eq });
      };
      f.querySelectorAll('[data-band], #eq-pre').forEach((x) => x.addEventListener('input', apply));
      f.querySelector('#eq-on').addEventListener('change', apply);
      f.querySelector('#eq-preset').addEventListener('change', (e) => {
        (EQ_PRESETS[e.target.value] || []).forEach((v, i) => { f.querySelector(`[data-band="${i}"]`).value = v; });
        f.querySelector('#eq-on').checked = true;
        apply();
      });
      f.querySelector('#eq-vol').addEventListener('input', (e) => { setVolume(e.target.value / 100, false); f.querySelector('#eq-volv').textContent = e.target.value + ' %'; });
    }
  });
}

async function setPluginEnabled(p, enabled) {
  PLUGINS = await host.invoke('plugins:set-enabled', { id: p.id, enabled });
  const r = await openDialog({
    title: 'Neustart der Oberfläche',
    body: `<p><b>${esc(p.name)}</b> wird ${enabled ? 'aktiviert' : 'deaktiviert'}. Damit das wirksam wird, muss die Oberfläche neu geladen werden. Wiedergabeliste und Position bleiben erhalten.</p>`,
    buttons: [{ label: 'Später', value: 'no' }, { label: 'Jetzt neu laden', value: 'yes', primary: true }]
  });
  if (r === 'yes') reloadUi();
}

function reloadUi() {
  rememberPosition();
  saveSession();
  try { sessionStorage.setItem('uve-reload', JSON.stringify({ index: playlist.index, t: video.currentTime, playing: !video.paused })); } catch (_) { }
  setTimeout(() => location.reload(), 150);
}

async function pluginsDialog() {
  PLUGINS = await host.invoke('plugins:list');
  const card = (p) => {
    const res = PLUGIN_RESULTS.find((r) => r.id === p.id);
    const state = !p.enabled ? '<span class="muted">deaktiviert</span>'
      : res && res.state === 'loaded' ? `<span class="state-loaded">● geladen (${res.ms} ms)</span>`
        : res && res.state === 'error' ? `<span class="state-error">● Fehler: ${esc(res.error)}</span>` : '<span class="muted">wird beim Neuladen aktiv</span>';
    return `<div class="plugin-card" data-id="${esc(p.id)}">
      <div class="top"><span class="name">${esc(p.name)}</span><span class="tag">v${esc(p.version)}</span><span class="tag">${p.type === 'userscript' ? 'Userscript' : 'Modul'}</span><span class="tag">${p.source === 'user' ? 'Benutzer' : 'Mitgeliefert'}</span><span class="grow"></span>
        <label><input type="checkbox" data-op="toggle" ${p.enabled ? 'checked' : ''}> aktiv</label></div>
      <div class="muted">${esc(p.description)}</div>
      <div>${state}</div>
      <div class="actions">
        ${p.updateUrl ? '<button type="button" class="btn" data-op="update">Nach Update suchen</button>' : ''}
        ${p.source === 'user' && p.id === 'uve' ? '<button type="button" class="btn" data-op="reset">Mitgelieferte Version verwenden</button>' : ''}
        ${p.type === 'userscript' ? '<button type="button" class="btn" data-op="export">Daten exportieren</button><button type="button" class="btn danger" data-op="wipe">Daten zurücksetzen</button>' : ''}
        ${p.homepage ? '<button type="button" class="btn" data-op="home">Projektseite</button>' : ''}
        ${p.source === 'user' && p.id !== 'uve' ? '<button type="button" class="btn danger" data-op="remove">Entfernen</button>' : ''}
      </div></div>`;
  };
  await openDialog({
    title: 'Plugins',
    wide: true,
    body: `<div id="plugin-list">${PLUGINS.map(card).join('')}</div>
      <p class="muted">Plugins liegen im Programmordner (<code>plugins\\</code>) und im Benutzerordner. Userscripts erhalten eine Tampermonkey-kompatible API (GM_*).</p>
      <div class="actions" style="display:flex;gap:6px"><button type="button" class="btn" data-op="install">Userscript installieren…</button><button type="button" class="btn" data-op="folder">Plugin-Ordner öffnen</button></div>`,
    onRender: (f) => f.addEventListener('click', async (e) => {
      const b = e.target.closest('[data-op]'); if (!b) return;
      const id = b.closest('[data-id]')?.dataset.id;
      const p = PLUGINS.find((x) => x.id === id);
      const op = b.dataset.op;
      try {
        if (op === 'toggle') { dlg.close(); await setPluginEnabled(p, b.checked); return; }
        if (op === 'update') {
          b.disabled = true; b.textContent = 'Lade…';
          const r = await host.invoke('plugins:update', id);
          b.textContent = r.previous === r.version ? 'Aktuell (v' + r.version + ')' : 'Aktualisiert: ' + r.previous + ' → ' + r.version;
          if (r.previous !== r.version) { dlg.close(); const q = await openDialog({ title: 'Update installiert', body: `<p>${esc(p.name)} wurde auf v${esc(r.version)} aktualisiert.</p>`, buttons: [{ label: 'Später', value: 'no' }, { label: 'Jetzt neu laden', value: 'yes', primary: true }] }); if (q === 'yes') reloadUi(); }
        }
        if (op === 'reset') { await host.invoke('plugins:reset', id); dlg.close(); reloadUi(); }
        if (op === 'export') await host.invoke('gm:storage-export', id);
        if (op === 'wipe') { await host.invoke('gm:storage-reset', id); osd('Plugin-Daten zurückgesetzt'); dlg.close(); reloadUi(); }
        if (op === 'home') host.invoke('shell:open-external', p.homepage);
        if (op === 'remove') { await host.invoke('plugins:uninstall', id); dlg.close(); reloadUi(); }
        if (op === 'folder') host.invoke('plugins:open-folder');
        if (op === 'install') {
          const files = await host.invoke('dialog:open', 'userscript');
          if (!files.length) return;
          const np = await host.invoke('plugins:install', files[0]);
          dlg.close();
          const q = await openDialog({ title: 'Userscript installiert', body: `<p><b>${esc(np.name)}</b> (v${esc(np.version)}) wurde installiert.</p>`, buttons: [{ label: 'Später', value: 'no' }, { label: 'Jetzt neu laden', value: 'yes', primary: true }] });
          if (q === 'yes') reloadUi();
        }
      } catch (err) {
        b.disabled = false;
        osd(String(err.message || err).replace(/^Error invoking remote method '[^']+': (Error: )?/, ''), 5000);
      }
    })
  });
}

async function mediaInfoDialog() {
  const it = player.item;
  if (!it) return osd('Kein Medium geladen');
  const rows = [['Datei', it.path], ['Größe', it.size ? (it.size / 1048576).toFixed(1) + ' MB' : '–'], ['Dauer', fmt(video.duration)],
    ['Auflösung', video.videoWidth ? video.videoWidth + ' × ' + video.videoHeight : '–'], ['Bildrate (gemessen)', player.fps ? player.fps.toFixed(2) + ' fps' : '–'],
    ['Wiedergabe über', it.kind === 'stream' ? 'Netzwerkstream (FFmpeg → fMP4 → Media Source)' : it.localStream ? 'Kompatibilitätsmodus (FFmpeg in Echtzeit → Media Source)' : 'Direkt (Chromium-Decoder)']];
  if (it.localStream && it.streamInfo) {
    const p = it.streamInfo.plan;
    rows.push(['Verarbeitung', `Video ${p.vcopy ? 'kopiert (' + p.video + ')' : p.video + ' → H.264'} · Ton ${p.acopy ? 'kopiert (' + p.audio + ')' : (p.audio || '–') + ' → AAC'}`],
      ['Aktive Audiospur', (player.audioTracks().find((t) => t.enabled) || {}).label || '–'], ['MSE-Codecs', (it.streamCodecs || []).join(', ') || '–']);
  }
  if (it.kind === 'stream' && it.streamInfo) {
    const si = it.streamInfo;
    rows.splice(1, 2, ['Titel', si.title || it.name], ['Quelle', (STREAM_LABEL[si.type] || si.type.toUpperCase()) + (si.live ? ' · LIVE' : '')],
      ['Dauer', si.live ? 'live' : fmt(si.duration || video.duration)]);
    if (si.uploader) rows.push(['Kanal', si.uploader]);
    rows.push(['Codecs (Quelle)', [si.plan.video, si.plan.audio].filter(Boolean).join(' / ') || '–'],
      ['Verarbeitung', `Video ${si.plan.vcopy ? 'kopiert' : 'neu kodiert (H.264)'} · Audio ${si.plan.acopy ? 'kopiert' : 'neu kodiert (AAC)'}`],
      ['MSE-Codecs', (it.streamCodecs || []).join(', ') || '–']);
  }
  const q = video.getVideoPlaybackQuality ? video.getVideoPlaybackQuality() : null;
  if (q) rows.push(['Bilder verworfen / gesamt', q.droppedVideoFrames + ' / ' + q.totalVideoFrames]);
  let streams = '';
  if (isLocal(it) && await compatAvailable()) {
    try {
      const info = await host.invoke('compat:probe', it.path);
      streams = (info.streams || []).map((s) => `<tr><th>#${s.index ?? ''} ${esc(s.codec_type)}</th><td>${esc(s.codec_long_name || s.codec_name)}${s.width ? ` · ${s.width}×${s.height}` : ''}${s.pix_fmt ? ' · ' + esc(s.pix_fmt) : ''}${s.r_frame_rate ? ' · ' + esc(s.r_frame_rate) + ' fps' : ''}${s.sample_rate ? ` · ${s.sample_rate} Hz` : ''}${s.channels ? ` · ${s.channels} Kanäle` : ''}${s.tags && s.tags.language ? ' · ' + esc(s.tags.language) : ''}</td></tr>`).join('');
      if (info.format) rows.push(['Container', info.format.format_long_name || info.format.format_name], ['Bitrate', info.format.bit_rate ? Math.round(info.format.bit_rate / 1000) + ' kbit/s' : '–']);
    } catch (_) { }
  }
  await openDialog({
    title: 'Medieninformationen', wide: true,
    body: `<table>${rows.map(([k, v]) => `<tr><th>${esc(k)}</th><td>${esc(v)}</td></tr>`).join('')}${streams}</table>`,
    buttons: isLocal(it) ? [{ label: 'Im Explorer zeigen', value: 'show' }, { label: 'Schließen', value: 'close', primary: true }] : undefined
  }).then((r) => { if (r === 'show') host.invoke('shell:show-item', it.path); });
}

async function settingsDialog() {
  const ff = INFO.ffmpeg;
  const cache = await host.invoke('compat:cache', 'size');
  const tools = await host.invoke('stream:tools');
  const r = await openDialog({
    title: 'Einstellungen', wide: true,
    body: `
      <label class="field"><span>Wiedergabe fortsetzen</span><input type="checkbox" id="s-resume" ${S.resumePlayback ? 'checked' : ''}></label>
      <label class="field"><span>Letzte Wiedergabeliste wiederherstellen</span><input type="checkbox" id="s-session" ${S.restoreSession ? 'checked' : ''}></label>
      <label class="field"><span>Sprungweite Pfeiltasten (s)</span><input type="text" id="s-step" value="${S.seekStep || 10}"></label>
      <label class="field"><span>Hardwarebeschleunigung<br><small class="muted">(Neustart nötig)</small></span><input type="checkbox" id="s-hw" ${S.hardwareAcceleration !== false ? 'checked' : ''}></label>
      <hr style="border-color:var(--line)">
      <label class="field"><span>Kompatibilitätsmodus</span><select id="s-compat">
        <option value="auto" ${S.compatMode !== 'off' ? 'selected' : ''}>Automatisch (empfohlen)</option>
        <option value="off" ${S.compatMode === 'off' ? 'selected' : ''}>Aus</option></select></label>
      <label class="field"><span>FFmpeg-Pfad</span><span style="display:flex;gap:6px"><input type="text" id="s-ff" value="${esc(S.ffmpegPath || '')}" placeholder="automatisch (tools\\ffmpeg oder PATH)"><button type="button" class="btn" id="s-ff-browse">…</button></span></label>
      <p class="muted">Erkannt: ${ff ? esc(ff.ffmpeg) : '<b>nicht gefunden</b> – ffmpeg.exe nach <code>tools\\ffmpeg\\</code> legen oder Pfad angeben.'}</p>
      <label class="field"><span>Umwandlungs-Cache</span><span>${(cache / 1048576).toFixed(1)} MB <button type="button" class="btn" id="s-clear">Leeren</button></span></label>
      <hr style="border-color:var(--line)">
      <label class="field"><span>yt-dlp-Pfad<br><small class="muted">(YouTube, Twitch, Webseiten)</small></span><span style="display:flex;gap:6px"><input type="text" id="s-yt" value="${esc(S.ytdlpPath || '')}" placeholder="automatisch (tools\\yt-dlp oder PATH)"><button type="button" class="btn" id="s-yt-browse">…</button></span></label>
      <p class="muted">Erkannt: ${tools.ytdlp ? esc(tools.ytdlp.path) + ' (' + esc(tools.ytdlp.version) + ')' : '<b>nicht gefunden</b> – yt-dlp.exe von github.com/yt-dlp/yt-dlp nach <code>tools\\yt-dlp\\</code> legen.'}</p>
      <label class="field"><span>Beim Start nach Updates für FFmpeg und yt-dlp suchen</span><input type="checkbox" id="s-toolupd" ${S.checkToolUpdates !== false ? 'checked' : ''}></label>
      <label class="field"><span>Maximale Stream-Auflösung</span><select id="s-maxh">${[480, 720, 1080, 1440, 2160].map((h) => `<option value="${h}" ${Number(S.streamMaxHeight || 1080) === h ? 'selected' : ''}>${h}p</option>`).join('')}</select></label>
      <label class="field"><span>Cookies aus Browser<br><small class="muted">(für Anmeldung/Altersfreigabe)</small></span><select id="s-cookies">${['', 'firefox', 'chrome', 'edge', 'brave', 'opera', 'vivaldi'].map((b) => `<option value="${b}" ${(S.ytdlpCookiesBrowser || '') === b ? 'selected' : ''}>${b || 'keine'}</option>`).join('')}</select></label>`,
    buttons: [{ label: 'Abbrechen', value: 'no' }, { label: 'Speichern', value: 'ok', primary: true }],
    onRender: (f) => {
      f.querySelector('#s-ff-browse').onclick = async () => { const x = await host.invoke('dialog:open', 'ffmpeg'); if (x[0]) f.querySelector('#s-ff').value = x[0]; };
      f.querySelector('#s-yt-browse').onclick = async () => { const x = await host.invoke('dialog:open', 'ffmpeg'); if (x[0]) f.querySelector('#s-yt').value = x[0]; };
      f.querySelector('#s-clear').onclick = async (e) => { await host.invoke('compat:cache', 'clear'); e.target.parentElement.firstChild.textContent = '0.0 MB '; };
    }
  });
  if (r !== 'ok') return;
  const g = (id) => dlgForm.querySelector(id);
  const step = Math.max(1, Math.min(600, Number(g('#s-step').value) || 10));
  await patchSettings({
    resumePlayback: g('#s-resume').checked, restoreSession: g('#s-session').checked, seekStep: step,
    hardwareAcceleration: g('#s-hw').checked, compatMode: g('#s-compat').value, ffmpegPath: g('#s-ff').value.trim(),
    ytdlpPath: g('#s-yt').value.trim(), streamMaxHeight: Number(g('#s-maxh').value), ytdlpCookiesBrowser: g('#s-cookies').value,
    checkToolUpdates: g('#s-toolupd').checked
  });
  await host.invoke('stream:tools', true);
  compatAvail = null;
  INFO = await host.invoke('app:info');
  osd('Einstellungen gespeichert');
}

function shortcutsDialog() {
  const rows = [
    ['Leertaste', 'Wiedergabe / Pause'], ['F / Doppelklick', 'Vollbild'], ['Esc', 'Vollbild verlassen'], ['S', 'Stopp'], ['N / P', 'Nächster / vorheriger Titel'],
    ['← / →', '10 s zurück / vor'], ['Umschalt + ← / →', '3 s'], ['Strg + ← / →', '1 min'], ['0 … 9', 'Zu 0 … 90 % springen'], ['E', 'Einzelbild vor'],
    ['↑ / ↓ / Mausrad', 'Lautstärke (bis 200 %)'], ['M', 'Stumm'], ['[ / ] / =', 'Langsamer / schneller / normal'], ['L', 'A-B-Wiederholung'],
    ['R / Z', 'Wiederholen / Zufall'], ['A', 'Seitenverhältnis'], ['V / B', 'Untertitel- / Audiospur'], ['G / H', 'Untertitelverzögerung ∓50 ms'],
    ['Umschalt + S', 'Bildschirmfoto'], ['Strg + O / F / N', 'Datei / Ordner / Stream öffnen'], ['Strg + L', 'Wiedergabeliste'], ['Strg + H', 'Minimale Oberfläche'],
    ['Strg + E', 'Equalizer'], ['Strg + I', 'Medieninformationen'], ['Strg + P', 'Plugins'], ['Strg + T', 'Zu Zeit springen'],
    ['Strg + Alt + I', 'UVE: Einstellungen'], ['Strg + Alt + G', 'UVE: Grading-HUD'], ['Strg + Alt + U', 'UVE: Vorher/Nachher'], ['Strg + Alt + S', 'UVE: Scopes'],
    ['Strg + Alt + P / C / A / X', 'UVE: HDR / Profil / Auto / GPU'], ['F8', 'UVE: Nächstes Profil']
  ];
  openDialog({ title: 'Tastenkürzel', wide: true, body: `<table>${rows.map(([k, v]) => `<tr><th><kbd>${esc(k)}</kbd></th><td>${esc(v)}</td></tr>`).join('')}</table>` });
}

function aboutDialog() {
  const uve = PLUGINS.find((p) => p.id === 'uve');
  openDialog({
    title: 'Über UVE Player',
    body: `<div style="display:flex;gap:16px;align-items:center"><img src="assets/icon.png" width="72" height="72" alt="">
      <div><div style="font-size:20px;font-weight:600">UVE Player ${esc(INFO.version)}</div><div class="muted">Medienplayer mit Ultimate Video Enhancer</div></div></div>
      <table style="margin-top:12px">
        <tr><th>Ultimate Video Enhancer</th><td>${uve ? 'v' + esc(uve.version) + (uve.enabled ? '' : ' (deaktiviert)') : 'nicht installiert'}</td></tr>
        <tr><th>Electron / Chromium</th><td>${esc(INFO.electron)} / ${esc(INFO.chrome)}</td></tr>
        <tr><th>Node.js</th><td>${esc(INFO.node)}</td></tr>
        <tr><th>FFmpeg</th><td>${INFO.ffmpeg ? esc(INFO.ffmpeg.ffmpeg) : 'nicht gefunden'}</td></tr>
        <tr><th>Datenordner</th><td>${esc(INFO.userData)}</td></tr>
      </table>
      <p class="muted">Architektur nach dem Vorbild von VLC 3.0: schlanker Launcher, Kern, Modul-/Plugin-Host und austauschbare Oberfläche. Ultimate Video Enhancer © Freak288 / nextscript.</p>`
  });
}

// ---------------------------------------------------------------- Werkzeug-Updates (FFmpeg, yt-dlp)
const fmtMB = (b) => (b / 1048576).toFixed(1) + ' MB';
const fmtDate = (d) => { try { return new Date(d).toLocaleDateString('de-DE', { day: '2-digit', month: '2-digit', year: 'numeric' }); } catch (_) { return d || ''; } };

async function checkToolUpdates(manual = false) {
  if (manual) osd('Suche nach Updates für FFmpeg und yt-dlp…', 15000);
  let res;
  try { res = await host.invoke('tools:check'); } catch (e) { if (manual) osd('Update-Prüfung fehlgeschlagen: ' + e.message, 5000); return; }
  const skipped = S.skippedToolVersions || {};
  const updates = res.updates.filter((u) => manual || skipped[u.tool] !== u.id);
  if (manual) $('#osd').classList.remove('show');
  if (!updates.length) {
    if (manual) {
      const st = await host.invoke('tools:status');
      openDialog({
        title: 'Werkzeuge sind aktuell',
        body: `<table>
          <tr><th>FFmpeg</th><td>${st.ffmpeg ? esc(st.ffmpeg.version) + (st.ffmpeg.publishedAt ? ' (' + fmtDate(st.ffmpeg.publishedAt) + ')' : '') : 'nicht installiert'}</td></tr>
          <tr><th>yt-dlp</th><td>${st.ytdlp ? esc(st.ytdlp.version) : 'nicht installiert'}</td></tr></table>
          ${res.errors.length ? `<p class="muted">${res.errors.map(esc).join('<br>')}</p>` : ''}`
      });
    }
    return;
  }
  // Nicht mitten in einen anderen Dialog platzen
  while (dlg.open) await new Promise((r) => setTimeout(r, 500));
  const rows = updates.map((u) => `<tr>
      <th><label><input type="checkbox" data-tool="${u.tool}" checked> ${esc(u.name)}</label></th>
      <td>${esc(u.current)}${u.currentDate ? ' <span class="muted">(' + fmtDate(u.currentDate) + ')</span>' : ''}<br>→ <b>${esc(u.tool === 'ffmpeg' ? 'Build vom ' + fmtDate(u.latestDate) : u.latest)}</b> <span class="muted">· ${fmtMB(u.size)}</span></td></tr>`).join('');
  const r = await openDialog({
    title: 'Updates verfügbar',
    body: `<p>Für folgende Werkzeuge gibt es neue Versionen:</p><table>${rows}</table>
      <p class="muted">FFmpeg wird für Netzwerkstreams und den Kompatibilitätsmodus verwendet, yt-dlp für YouTube, Twitch und andere Webseiten. Neue yt-dlp-Versionen sind oft nötig, wenn YouTube etwas ändert.</p>
      <label><input type="checkbox" id="tools-skip"> Diese Versionen überspringen</label>`,
    buttons: [{ label: 'Später', value: 'later' }, { label: 'Jetzt aktualisieren', value: 'update', primary: true }]
  });
  const chosen = [...dlgForm.querySelectorAll('[data-tool]')].filter((c) => c.checked).map((c) => c.dataset.tool);
  if (r !== 'update') {
    if (dlgForm.querySelector('#tools-skip')?.checked) {
      const sk = Object.assign({}, S.skippedToolVersions);
      updates.forEach((u) => { sk[u.tool] = u.id; });
      patchSettings({ skippedToolVersions: sk });
    }
    return;
  }
  await installToolUpdates(updates.filter((u) => chosen.includes(u.tool)));
}

async function installToolUpdates(list) {
  if (!list.length) return;
  const busy = $('#busy');
  const cancelBtn = $('#busy-cancel');
  cancelBtn.classList.add('hidden');
  busy.classList.remove('hidden');
  const done = []; const failed = [];
  const off = host.on('tools:progress', (p) => {
    const u = list.find((x) => x.tool === p.tool);
    if (p.stage === 'download') {
      const pct = p.total ? (p.done / p.total) * 100 : 0;
      $('#busy-bar').style.width = pct.toFixed(1) + '%';
      $('#busy-detail').textContent = `${fmtMB(p.done)} / ${fmtMB(p.total || u.size)} · ${Math.floor(pct)} %`;
    } else if (p.stage === 'extract') {
      $('#busy-title').textContent = u.name + ' wird entpackt und geprüft…';
      $('#busy-bar').style.width = '100%';
    }
  });
  try {
    for (const u of list) {
      $('#busy-title').textContent = u.name + ' wird heruntergeladen…';
      $('#busy-bar').style.width = '0%';
      $('#busy-detail').textContent = '';
      try { const r = await host.invoke('tools:install', u.tool); done.push(u.name + ' ' + r.version); }
      catch (e) { failed.push(u.name + ': ' + String(e.message || e).replace(/^Error invoking remote method '[^']+': (Error: )?/, '')); }
    }
  } finally {
    off();
    busy.classList.add('hidden');
    cancelBtn.classList.remove('hidden');
  }
  compatAvail = null;
  INFO = await host.invoke('app:info');
  await openDialog({
    title: failed.length ? 'Update teilweise fehlgeschlagen' : 'Update abgeschlossen',
    body: `${done.length ? `<p>Aktualisiert:</p><ul>${done.map((d) => `<li>${esc(d)}</li>`).join('')}</ul>` : ''}
      ${failed.length ? `<p>Fehler:</p><ul>${failed.map((d) => `<li>${esc(d)}</li>`).join('')}</ul>` : ''}
      <p class="muted">Neue Versionen werden sofort verwendet – ein Neustart ist nicht nötig.</p>`
  });
}

// ---------------------------------------------------------------- Start
async function init() {
  const ready = await host.invoke('app:ready');
  INFO = await host.invoke('app:info');
  S = ready.settings;
  PLUGINS = ready.plugins;

  player.setVolume(S.volume ?? 1);
  player.setMuted(!!S.muted);
  setRate(S.playbackRate || 1);
  setFit(S.fitMode || 'contain');
  if (S.eq && S.eq.enabled) player.setEq(S.eq); else if (S.eq) Object.assign(player.eq, S.eq);
  playlist.repeat = S.repeat || 'none';
  playlist.shuffle = !!S.shuffle;
  updateRepeatShuffle();
  updateVolumeUi();
  appEl.classList.toggle('no-playlist', S.showPlaylist === false);
  $('#osd').classList.remove('show');

  // Sitzung / Übergabe
  let reloadState = null;
  try { reloadState = JSON.parse(sessionStorage.getItem('uve-reload') || 'null'); sessionStorage.removeItem('uve-reload'); } catch (_) { }
  if (ready.open.length) addToPlaylist(ready.open, { play: true });
  else if (ready.session.length) {
    playlist.add(ready.session);
    const idx = reloadState ? reloadState.index : (S.sessionIndex ?? -1);
    if (idx >= 0 && idx < playlist.items.length) {
      await playIndex(idx, { startAt: reloadState ? reloadState.t : 0, autoplay: !!(reloadState && reloadState.playing) });
    }
  }
  renderPlaylist();

  // Plugins (UVE) laden – Userscripts laufen "document-idle"
  const api = {
    version: INFO.version, video, player, playlist, osd,
    registerAction: (name, fn) => { actions[name] = fn; },
    run: act
  };
  window.UVEPlayer = Object.freeze(api);
  PLUGIN_RESULTS = await loadPlugins(PLUGINS, { appVersion: INFO.version, api });
  const uve = PLUGIN_RESULTS.find((r) => r.id === 'uve');
  const uveP = PLUGINS.find((p) => p.id === 'uve');
  $('#btn-uve').classList.toggle('on', !!(uve && uve.state === 'loaded'));
  $('#plugin-hint').textContent = uve && uve.state === 'loaded'
    ? `Ultimate Video Enhancer v${uveP.version} aktiv · Strg+Alt+I für Einstellungen`
    : uve && uve.state === 'error' ? 'Ultimate Video Enhancer konnte nicht geladen werden (Werkzeuge → Plugins)' : 'Ultimate Video Enhancer ist deaktiviert';

  // Nach jedem Start: neue FFmpeg-/yt-dlp-Versionen suchen (Modal bei Fund)
  if (!ready.selftest && S.checkToolUpdates !== false) setTimeout(() => checkToolUpdates(false), 2500);

  if (ready.selftest && ready.selftestTools) {
    // Selbsttest des Werkzeug-Updaters: 'modal' = Dialog anzeigen (Screenshot), 'install' = yt-dlp aktualisieren
    const before = await host.invoke('tools:status');
    const check = await host.invoke('tools:check');
    let installed = null;
    if (ready.selftestTools === 'modal') { checkToolUpdates(false); await new Promise((r) => setTimeout(r, 3000)); }
    if (ready.selftestTools === 'install') installed = await host.invoke('tools:install', 'ytdlp').catch((e) => ({ error: e.message }));
    if (ready.selftestTools === 'install-ffmpeg') installed = await host.invoke('tools:install', 'ffmpeg').catch((e) => ({ error: e.message }));
    const after = await host.invoke('tools:status');
    const st = await host.invoke('stream:tools');
    host.send('selftest:result', { ok: true, before, check, installed, after, used: st.ytdlp });
    return;
  }

  if (ready.selftest) {
    S.compatMode = 'auto'; // nur im Speicher, Selbsttest läuft mit eigenem Datenordner
    const playback = [];
    for (let i = 0; i < playlist.items.length; i++) {
      const it = playlist.items[i];
      if (playlist.index !== i) await playIndex(i);
      const t0 = Date.now();
      while (Date.now() - t0 < 30000 && !(video.currentTime > 1.5 && video.videoWidth > 0)) await new Promise((res) => setTimeout(res, 200));
      let readback = 'n/a';
      try {
        const c = document.createElement('canvas'); c.width = 8; c.height = 8;
        const cx = c.getContext('2d'); cx.drawImage(video, 0, 0, 8, 8); cx.getImageData(0, 0, 8, 8);
        readback = 'ok';
      } catch (e) { readback = 'tainted: ' + e.name; }
      player.seek(3); await new Promise((res) => setTimeout(res, 800));
      const hold = Number(ready.selftestHold) || 0; // Langzeittest (z. B. Twitch-Werbung → Stream)
      if (hold) {
        const t1 = video.currentTime; it.holdTrace = [];
        for (let s = 0; s < hold; s++) { await new Promise((res) => setTimeout(res, 1000)); it.holdTrace.push(`${video.currentTime.toFixed(1)}:${video.videoHeight}${video.error ? '!E' + video.error.code : ''}${video.paused ? 'P' : ''}`); }
        it.holdAdvance = Math.round((video.currentTime - t1) * 10) / 10;
      }
      // Ton-Nachweis: tatsächlich dekodierte Audiodaten (Chromium-Zähler)
      const audio = { decodedBytes: video.webkitAudioDecodedByteCount || 0, localStream: !!it.localStream, plan: it.streamInfo && it.streamInfo.plan, tracks: player.audioTracks().map((t) => (t.enabled ? '*' : '') + t.label) };
      if (ready.selftestAudioSwitch && it.localStream && audio.tracks.length > 1) {
        const before = audio.decodedBytes;
        const lbl = await player.setAudioTrack(1);
        const t0 = Date.now();
        while (Date.now() - t0 < 20000 && !(video.currentTime > 4 && video.webkitAudioDecodedByteCount > 0)) await new Promise((res) => setTimeout(res, 300));
        audio.switched = { to: lbl, decodedBytes: video.webkitAudioDecodedByteCount, t: Math.round(video.currentTime * 10) / 10, before };
      }
      if ((ready.selftestAudioSwitch || ready.selftestFarSeek) && !player.isLive && isFinite(video.duration)) {
        // Weiter Sprung (außerhalb des Puffers → FFmpeg startet an neuer Position)
        const target = Math.min(2000, (video.duration || 100) * 0.4);
        const b0 = video.webkitAudioDecodedByteCount;
        player.seek(target);
        const t1 = Date.now();
        while (Date.now() - t1 < 20000 && !(video.currentTime > target + 2 && !video.paused)) await new Promise((res) => setTimeout(res, 300));
        const br = []; for (let i = 0; i < video.buffered.length; i++) br.push(video.buffered.start(i).toFixed(1) + '-' + video.buffered.end(i).toFixed(1));
        audio.farSeek = { target: Math.round(target), t: Math.round(video.currentTime * 10) / 10, ms: Date.now() - t1, audioGrew: video.webkitAudioDecodedByteCount > b0, buffered: br, readyState: video.readyState, paused: video.paused, seeking: video.seeking, fetching: !!(player.stream && player.stream.ctrl), dur: video.duration };
      }
      // Untertitel-Extraktion im Hintergrund abwarten (beim ersten Mal liest FFmpeg die ganze Datei)
      { const t0 = Date.now(); while (Date.now() - t0 < (ready.selftestAudioSwitch ? 90000 : 2500) && !player.subtitleTracks().length) await new Promise((res) => setTimeout(res, 500)); }
      audio.subsAfterMs = player.subtitleTracks().length ? 'ok' : 'timeout';
      audio.subs = player.subtitleTracks().map((t) => (t.showing ? '*' : '') + t.label);
      playback.push({ name: it.name, kind: it.kind, audio, holdAdvance: it.holdAdvance, trace: it.holdTrace, live: player.isLive, codecs: it.streamCodecs, buffered: video.buffered.length ? Math.round((video.buffered.end(video.buffered.length - 1) - video.buffered.start(0)) * 10) / 10 : 0, compat: !!it.compat, playing: !video.paused, t: Math.round(video.currentTime * 10) / 10, w: video.videoWidth, h: video.videoHeight, readback, err: video.error ? video.error.code + ' ' + video.error.message : null, videoFilter: getComputedStyle(video).filter.slice(0, 60) });
    }
    let ui = null;
    if (ready.selftestUi && playlist.items.length) {
      // Klick aufs Bild → Pause, erneut → Wiedergabe; Doppelklick darf nicht pausieren; Rechtsklick → Kontextmenü
      const vr = video.getBoundingClientRect();
      const click = (detail) => video.dispatchEvent(new MouseEvent('click', { bubbles: true, button: 0, detail, clientX: vr.x + 50, clientY: vr.y + 50 }));
      const wait = (ms) => new Promise((res) => setTimeout(res, ms));
      if (video.paused) await video.play().catch(() => { });
      click(1); await wait(500); const afterFirst = video.paused;
      click(1); await wait(500); const afterSecond = video.paused;
      click(1); click(2); video.dispatchEvent(new MouseEvent('dblclick', { bubbles: true, detail: 2 })); await wait(500);
      const afterDbl = video.paused;
      if (document.fullscreenElement) await document.exitFullscreen().catch(() => { });
      appEl.classList.remove('fullscreen'); await wait(600);
      const li = plList.querySelector('li');
      const lr = li.getBoundingClientRect();
      li.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2, clientX: lr.x + 40, clientY: lr.y + 10 }));
      await wait(300);
      ui = { clickPause: afterFirst, clickResume: !afterSecond, dblclickKeepsPlaying: !afterDbl, contextItems: [...popup.querySelectorAll('.mi')].map((b) => b.textContent.replace(/(Enter|Entf|Alt\+.|Strg\+.)$/, '')), menuVisible: !popup.classList.contains('hidden') };
      // Kontextmenü des Bildes → Untermenü (Audiospur bzw. Untertitel)
      hidePopup();
      const vr2 = video.getBoundingClientRect();
      video.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2, clientX: vr2.x + vr2.width / 2, clientY: vr2.y + 60 }));
      await wait(300);
      ui.videoMenu = [...popup.querySelectorAll('.mi')].map((b) => b.querySelector('span').textContent);
      const subName = ready.selftestUiSub || 'Audiospur';
      const subBtn = [...popup.querySelectorAll('.mi')].find((b) => b.querySelector('span').textContent === subName);
      if (subBtn) { subBtn.click(); await wait(300); ui.subMenu = [...popup.querySelectorAll('.mi')].map((b) => (b.classList.contains('on') ? '✓ ' : '') + b.querySelector('span').textContent); }
    }
    const r = {
      ui,
      playback,
      ok: !!(uve && uve.state === 'loaded') && typeof window.__gvfAudio === 'function' && playback.every((p) => p.w > 0 && p.t > 1 && p.readback === 'ok'),
      version: INFO.version,
      plugins: PLUGIN_RESULTS,
      uveVersion: uveP && uveP.version,
      gvfAudio: typeof window.__gvfAudio,
      uveDomNodes: document.querySelectorAll('[id^="gvf"], [class*="gvf"]').length
    };
    host.send('selftest:result', r);
  }
}

init().catch((e) => { console.error(e); osd('Startfehler: ' + e.message, 8000); });
