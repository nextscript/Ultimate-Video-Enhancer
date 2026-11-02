// Wiedergabe-Kern (Gegenstück zu VLCs input/es_out/vout/aout).
// Dekodiert wird von Chromium (Hardware-beschleunigt), Effekte liefert das UVE-Plugin.
import { StreamSession } from './stream-session.js';

const host = window.uveHost;

const EQ_FREQS = [60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000];
export const EQ_PRESETS = {
  'Flach': [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  'Klassik': [0, 0, 0, 0, 0, 0, -7, -7, -7, -9],
  'Club': [0, 0, 8, 5, 5, 5, 3, 0, 0, 0],
  'Tanz': [9, 7, 2, 0, 0, -5, -7, -7, 0, 0],
  'Volle Bässe': [-8, 9, 9, 5, 1, -4, -8, -10, -11, -11],
  'Volle Höhen': [-9, -9, -9, -4, 2, 11, 16, 16, 16, 16],
  'Kopfhörer': [4, 11, 5, -3, -2, 1, 4, 9, 12, 14],
  'Großer Saal': [10, 10, 5, 5, 0, -4, -4, -4, 0, 0],
  'Live': [-4, 0, 4, 5, 5, 5, 4, 2, 2, 2],
  'Party': [7, 7, 0, 0, 0, 0, 0, 0, 7, 7],
  'Pop': [-1, 4, 7, 8, 5, 0, -2, -2, -1, -1],
  'Reggae': [0, 0, 0, -5, 0, 6, 6, 0, 0, 0],
  'Rock': [8, 4, -5, -8, -3, 4, 8, 11, 11, 11],
  'Sanft': [4, 1, 0, -2, 0, 4, 8, 9, 11, 12],
  'Techno': [8, 5, 0, -5, -4, 0, 8, 9, 9, 8],
  'Stimme': [-2, -4, -4, 2, 6, 6, 4, 0, -2, -4]
};

function srtToVtt(text) {
  return 'WEBVTT\n\n' + text.replace(/^﻿/, '').replace(/\r/g, '')
    .replace(/(\d{1,2}:\d{2}:\d{2}),(\d{3})/g, '$1.$2')
    .replace(/<\/?font[^>]*>/gi, '')
    .replace(/\{\\[^}]*\}/g, '');
}

export class Player extends EventTarget {
  constructor(video) {
    super();
    this.video = video;
    this.item = null;
    this.ab = { a: null, b: null };
    this.subDelay = 0;
    this.fps = 25;
    this.graph = null;
    this.volumeLevel = 1;   // 0..2 (200 %)
    this.eq = { enabled: false, preamp: 0, bands: EQ_FREQS.map(() => 0), preset: 'Flach' };
    this._subUrls = [];
    this._fpsSamples = [];
    this._bind();
    this._installSharedAudio();
  }

  emit(type, detail) { this.dispatchEvent(new CustomEvent(type, { detail })); }

  _bind() {
    const v = this.video;
    v.addEventListener('timeupdate', () => {
      if (this.ab.a != null && this.ab.b != null && v.currentTime >= this.ab.b) v.currentTime = this.ab.a;
    });
    v.addEventListener('play', () => host.send('power:playing', true));
    v.addEventListener('pause', () => host.send('power:playing', false));
    v.addEventListener('ended', () => { host.send('power:playing', false); this.emit('ended'); });
    v.addEventListener('error', () => {
      if (!this.item || !v.error) return;
      this.emit('media-error', { code: v.error.code, message: v.error.message, item: this.item });
    });
    v.addEventListener('loadedmetadata', () => {
      // Video-Datei, aber kein Bild dekodierbar (z. B. Codec fehlt) → Kompatibilitätsmodus anbieten
      if (this.item && this.item.kind === 'video' && !this.item.compat && v.videoWidth === 0) {
        setTimeout(() => {
          if (v.videoWidth === 0 && this.item && !this.item.compat) this.emit('media-error', { code: 4, message: 'Kein dekodierbares Videobild', item: this.item, soft: true });
        }, 1200);
      }
      this.emit('meta', { duration: v.duration, width: v.videoWidth, height: v.videoHeight });
    });
    if ('requestVideoFrameCallback' in HTMLVideoElement.prototype) {
      let last = null;
      const tick = (_now, md) => {
        if (last && !v.paused && md.presentedFrames > last.presentedFrames) {
          const dt = (md.mediaTime - last.mediaTime) / (md.presentedFrames - last.presentedFrames);
          if (dt > 0.004 && dt < 0.2) {
            this._fpsSamples.push(1 / dt);
            if (this._fpsSamples.length > 60) this._fpsSamples.shift();
            const s = [...this._fpsSamples].sort((a, b) => a - b);
            this.fps = s[Math.floor(s.length / 2)];
          }
        }
        last = md;
        v.requestVideoFrameCallback(tick);
      };
      v.requestVideoFrameCallback(tick);
    }
  }

  // ---------- Laden ----------
  async load(item, { startAt = 0, autoplay = true } = {}) {
    const v = this.video;
    this.item = item;
    this._loadGen = (this._loadGen || 0) + 1;
    this.ab = { a: null, b: null };
    this._fpsSamples = [];
    this.clearSubtitles();
    this._disposeStream();
    v.removeAttribute('src');
    v.load();
    v.playbackRate = this.rate || 1;
    v.defaultPlaybackRate = this.rate || 1;
    if (item.kind === 'stream' || item.localStream) {
      await this._loadStream(item, startAt, autoplay);
      if (item.localStream && this.item === item) this._afterLocalLoad(item);
      return;
    }
    v.src = item.compatUrl || item.url;
    if (startAt > 0) {
      const seek = () => { try { v.currentTime = startAt; } catch (_) { } };
      v.addEventListener('loadedmetadata', seek, { once: true });
    }
    this._afterLocalLoad(item);
    if (item.compatSub) await this.addSubtitle({ url: item.compatSub, name: 'Eingebettet', ext: 'vtt' }, false).catch(() => { });
    if (autoplay) {
      try { await v.play(); } catch (e) { if (e.name !== 'AbortError') console.warn(e); }
    }
    this.emit('loaded', item);
  }

  // Externe (gleichnamige) und eingebettete Untertitel einer lokalen Datei laden
  async _afterLocalLoad(item) {
    // Ladezähler: ein inzwischen neu gestarteter Ladevorgang (z. B. Audiospurwechsel) übernimmt
    const gen = this._loadGen;
    const current = () => this.item === item && this._loadGen === gen;
    for (const s of item.subs || []) { if (!current()) return; await this.addSubtitle(s, false).catch(() => { }); }
    if (!/\.(mkv|mk3d|mp4|m4v|mov|webm)$/i.test(item.path || '')) return;
    let list = [];
    try { list = await host.invoke('compat:subs', item.path); } catch (_) { return; }
    if (!current() || !list.length) return;
    // Beim Neuladen (Audiospurwechsel) zuvor gewählte Untertitelspur wiederherstellen
    const restore = item._restoreSub;
    item._restoreSub = undefined;
    // Anzeige wie VLC: als "default" markierte Spur, sonst erzwungene (Forced) Spur, sonst keine
    const nameOf = (s) => [s.title || s.lang.toUpperCase(), s.forced && !/forced/i.test(s.title) ? '(erzwungen)' : ''].filter(Boolean).join(' ');
    let show = list.find((s) => s.default) || list.find((s) => s.forced) || null;
    if (restore !== undefined) show = restore === null ? null : (list.find((s) => nameOf(s) === restore) || show);
    const anyShowing = this.subtitleTracks().some((t) => t.showing);
    for (const s of list) {
      if (!current()) return;
      await this.addSubtitle({ url: s.url, name: nameOf(s), ext: 'vtt', lang: s.lang }, !anyShowing && s === show).catch(() => { });
    }
    if (!anyShowing && !show) { const tt = this.video.textTracks; for (let i = 0; i < tt.length; i++) tt[i].mode = 'disabled'; }
    this.emit('subs-loaded', { item, count: list.length, showing: show ? (show.title || show.lang) : null });
  }

  // Audiospur einer im Kompatibilitätsmodus laufenden Datei wechseln (FFmpeg mit neuer Spur neu starten)
  async selectLocalAudio(index) {
    const item = this.item;
    if (!item || !item.localStream) return null;
    item.audioIndex = index;
    const t = this.video.currentTime;
    const playing = !this.video.paused;
    const showing = this.subtitleTracks().find((x) => x.showing);
    // nur merken, wenn Untertitel schon geladen waren – sonst gilt die Standardauswahl
    item._restoreSub = this.subtitleTracks().length ? (showing ? showing.label : null) : undefined;
    await this.load(item, { startAt: t > 1 ? t : 0, autoplay: playing });
    const tr = item.streamInfo && item.streamInfo.audioTracks && item.streamInfo.audioTracks[index];
    return tr ? this._audioLabel(tr) : null;
  }

  _audioLabel(t) {
    const ch = t.channels === 6 ? '5.1' : t.channels === 8 ? '7.1' : t.channels === 2 ? 'Stereo' : t.channels === 1 ? 'Mono' : (t.channels || '') + ' Kanäle';
    return [t.title || '', (t.lang || '').toUpperCase(), `${String(t.codec || '').toUpperCase()} ${ch}`].filter(Boolean).join(' · ');
  }

  stop() {
    const v = this.video;
    v.pause();
    try { v.currentTime = 0; } catch (_) { }
    this.emit('stopped');
  }

  unload() {
    this.item = null;
    this.clearSubtitles();
    this._disposeStream();
    this.video.removeAttribute('src');
    this.video.load();
  }

  // ---------- Netzwerkstreams (YouTube, Twitch, tcp:// …) ----------
  async _loadStream(item, startAt, autoplay) {
    const v = this.video;
    this.emit('stream-state', { item, state: 'resolving' });
    let info;
    try {
      info = item.localStream
        ? await host.invoke('stream:open-local', item.path, {
          audio: item.audioIndex, transcode: !!item.forceTranscode, transcodeVideo: !!item.transcodeVideo,
          hevc: MediaSource.isTypeSupported('video/mp4; codecs="hvc1.1.6.L120.90"'),
          hevc10: MediaSource.isTypeSupported('video/mp4; codecs="hvc1.2.4.L120.90"')
        })
        : await host.invoke('stream:open', item.path, { transcode: !!item.forceTranscode });
    } catch (e) {
      if (this.item === item) this.emit('stream-error', { item, phase: 'resolve', message: String(e.message || e).replace(/^Error invoking remote method '[^']+': (Error: )?/, '') });
      return;
    }
    if (this.item !== item) return; // inzwischen anderes Medium gewählt
    item.streamInfo = info;
    if (info.audioIndex != null) item.audioIndex = info.audioIndex;
    if (info.title && !item.localStream) item.title = info.title;
    const s = new StreamSession(v, info);
    this.stream = s;
    s.addEventListener('state', (e) => this.emit('stream-state', Object.assign({ item }, e.detail)));
    s.addEventListener('codecs', (e) => { item.streamCodecs = e.detail.codecs; });
    s.addEventListener('error', (e) => { if (e.detail.message && this.stream === s) this.emit('stream-error', { item, phase: e.detail.connect ? 'connect' : 'play', message: e.detail.message }); });
    s.start(startAt > 1 && !info.live ? startAt : 0).catch((e) => { if (this.stream === s) this.emit('stream-error', { item, phase: 'play', message: e.message }); });
    if (autoplay) v.play().catch(() => { });
    this.emit('loaded', item);
  }

  _disposeStream() {
    if (this.stream) { this.stream.dispose(); this.stream = null; }
  }

  get isLive() { return !!(this.stream && this.stream.live); }

  toggle() { const v = this.video; if (!v.src) return; if (v.paused || v.ended) v.play().catch(() => { }); else v.pause(); }

  seek(t) {
    const v = this.video;
    if (this.stream) {
      if (this.stream.live) {
        const b = v.buffered;
        if (b.length) v.currentTime = Math.max(b.start(0), Math.min(this.stream.liveEdge() - 1, t));
        return;
      }
      const d = isFinite(v.duration) ? v.duration : Infinity;
      this.stream.seekTo(Math.max(0, Math.min(d - 0.5, t)));
      return;
    }
    if (!isFinite(v.duration)) return;
    v.currentTime = Math.max(0, Math.min(v.duration - 0.05, t));
  }

  seekBy(d) { this.seek(this.video.currentTime + d); }

  frameStep(dir = 1) {
    const v = this.video;
    v.pause();
    this.seek(v.currentTime + dir / (this.fps || 25));
  }

  setRate(r) {
    this.rate = Math.max(0.25, Math.min(4, Math.round(r * 100) / 100));
    this.video.playbackRate = this.rate;
    this.video.defaultPlaybackRate = this.rate;
    return this.rate;
  }

  // ---------- A-B-Wiederholung ----------
  cycleAB() {
    const t = this.video.currentTime;
    if (this.ab.a == null) this.ab.a = t;
    else if (this.ab.b == null && t > this.ab.a + 0.2) this.ab.b = t;
    else this.ab = { a: null, b: null };
    this.emit('ab', this.ab);
    return this.ab;
  }

  // ---------- Audio ----------
  // Gemeinsamer Audio-Graph. UVE ruft window.__gvfAudio(video) auf und nutzt dann
  // genau diesen Graphen (es prüft "if (unsafeWindow.__gvfAudio) return;"), sodass
  // createMediaElementSource() nur einmal aufgerufen wird.
  _installSharedAudio() {
    const self = this;
    window.__gvfAudio = function (video) {
      if (!video) return null;
      if (video !== self.video) return null;
      const g = self.ensureGraph();
      if (g && g.actx.state === 'suspended') g.actx.resume();
      return g;
    };
  }

  ensureGraph() {
    if (this.graph) return this.graph;
    try {
      const actx = new AudioContext({ latencyHint: 'playback' });
      const src = actx.createMediaElementSource(this.video);
      const analyser = actx.createAnalyser();
      analyser.fftSize = 2048;
      analyser.smoothingTimeConstant = 0.8;
      const pre = actx.createGain();
      const bands = EQ_FREQS.map((f, i) => {
        const b = actx.createBiquadFilter();
        b.type = i === 0 ? 'lowshelf' : i === EQ_FREQS.length - 1 ? 'highshelf' : 'peaking';
        b.frequency.value = f;
        b.Q.value = 1.1;
        b.gain.value = 0;
        return b;
      });
      const boost = actx.createGain();
      const comp = actx.createDynamicsCompressor(); // Schutz vor Übersteuerung beim Boost
      comp.threshold.value = -2; comp.knee.value = 6; comp.ratio.value = 12; comp.attack.value = 0.003; comp.release.value = 0.15;
      src.connect(analyser);
      let node = src.connect(pre);
      for (const b of bands) node = node.connect(b);
      node.connect(boost).connect(comp).connect(actx.destination);
      this.graph = { actx, src, analyser, pre, bands, boost, comp };
      this._applyEq();
      this._applyVolume();
      this.video.addEventListener('play', () => { if (actx.state === 'suspended') actx.resume(); });
      return this.graph;
    } catch (e) {
      console.warn('[Player] Audio-Graph nicht verfügbar', e);
      return null;
    }
  }

  setVolume(level) {
    this.volumeLevel = Math.max(0, Math.min(2, Math.round(level * 100) / 100));
    if (this.volumeLevel > 1) this.ensureGraph();
    this._applyVolume();
    this.emit('volume', { level: this.volumeLevel, muted: this.video.muted });
    return this.volumeLevel;
  }

  setMuted(m) { this.video.muted = !!m; this.emit('volume', { level: this.volumeLevel, muted: this.video.muted }); }

  _applyVolume() {
    const l = this.volumeLevel;
    this.video.volume = Math.min(1, l);
    if (this.graph) this.graph.boost.gain.value = l > 1 ? l : 1;
  }

  setEq(eq) {
    Object.assign(this.eq, eq);
    if (this.eq.enabled) this.ensureGraph();
    this._applyEq();
  }

  _applyEq() {
    if (!this.graph) return;
    const on = this.eq.enabled;
    this.graph.pre.gain.value = on ? Math.pow(10, (this.eq.preamp || 0) / 20) : 1;
    this.graph.bands.forEach((b, i) => { b.gain.value = on ? (this.eq.bands[i] || 0) : 0; });
  }

  // ---------- Spuren ----------
  audioTracks() {
    const it = this.item;
    if (it && it.localStream && it.streamInfo && it.streamInfo.audioTracks) {
      return it.streamInfo.audioTracks.map((t) => ({ index: t.index, label: this._audioLabel(t), enabled: t.index === it.audioIndex }));
    }
    const t = this.video.audioTracks;
    if (!t) return [];
    return Array.from({ length: t.length }, (_, i) => ({ index: i, label: t[i].label || t[i].language || ('Spur ' + (i + 1)), enabled: t[i].enabled }));
  }

  async setAudioTrack(index) {
    if (this.item && this.item.localStream) return this.selectLocalAudio(index);
    const t = this.video.audioTracks;
    if (!t || !t[index]) return null;
    for (let i = 0; i < t.length; i++) t[i].enabled = i === index;
    return t[index].label || t[index].language || ('Spur ' + (index + 1));
  }

  async cycleAudioTrack() {
    const list = this.audioTracks();
    if (list.length < 2) return null;
    if (this.item && this.item.localStream) {
      const cur = list.findIndex((x) => x.enabled);
      return this.selectLocalAudio(list[(cur + 1) % list.length].index);
    }
    const t = this.video.audioTracks;
    if (!t || t.length < 2) return null;
    let cur = 0;
    for (let i = 0; i < t.length; i++) if (t[i].enabled) cur = i;
    const next = (cur + 1) % t.length;
    for (let i = 0; i < t.length; i++) t[i].enabled = i === next;
    return t[next].label || t[next].language || ('Spur ' + (next + 1));
  }

  // ---------- Untertitel ----------
  clearSubtitles() {
    for (const t of [...this.video.querySelectorAll('track')]) t.remove();
    for (const u of this._subUrls) URL.revokeObjectURL(u);
    this._subUrls = [];
    this.subDelay = 0;
  }

  async addSubtitle(sub, show = true) {
    let src = sub.url;
    const ext = (sub.ext || '').toLowerCase();
    if (ext === 'srt') {
      const text = await (await fetch(sub.url)).text();
      src = URL.createObjectURL(new Blob([srtToVtt(text)], { type: 'text/vtt' }));
      this._subUrls.push(src);
    } else if (ext && ext !== 'vtt') {
      src = await host.invoke('compat:subtitle', sub.path);
    }
    const tr = document.createElement('track');
    tr.kind = 'subtitles';
    tr.label = sub.name || 'Untertitel';
    tr.srclang = (/\.([a-z]{2,3})\.[a-z]+$/i.exec(sub.name || '') || [])[1] || 'und';
    tr.src = src;
    tr.addEventListener('load', () => this._applySubDelay());
    this.video.appendChild(tr);
    const tracks = this.video.textTracks;
    const idx = tracks.length - 1;
    for (let i = 0; i < tracks.length; i++) {
      if (show) tracks[i].mode = i === idx ? 'showing' : 'disabled';
      else if (i === idx) tracks[i].mode = idx === 0 ? 'showing' : 'disabled';
    }
    return tr;
  }

  subtitleTracks() {
    const t = this.video.textTracks;
    return Array.from({ length: t.length }, (_, i) => ({ index: i, label: t[i].label, showing: t[i].mode === 'showing' }));
  }

  // Zyklus: Spur 1 → Spur 2 → … → aus
  cycleSubtitle() {
    const t = this.video.textTracks;
    if (!t.length) return null;
    let cur = -1;
    for (let i = 0; i < t.length; i++) if (t[i].mode === 'showing') cur = i;
    const next = cur + 1 >= t.length ? -1 : cur + 1;
    for (let i = 0; i < t.length; i++) t[i].mode = i === next ? 'showing' : 'disabled';
    return next < 0 ? 'Aus' : t[next].label;
  }

  shiftSubtitles(ms) {
    this.subDelay = Math.round((this.subDelay + ms / 1000) * 1000) / 1000;
    this._applySubDelay();
    return this.subDelay;
  }

  _applySubDelay() {
    const t = this.video.textTracks;
    for (let i = 0; i < t.length; i++) {
      const prev = t[i].mode;
      if (prev === 'disabled') t[i].mode = 'hidden';
      const cues = t[i].cues;
      if (!cues) continue;
      for (const c of cues) {
        if (c._o === undefined) c._o = [c.startTime, c.endTime];
        c.startTime = Math.max(0, c._o[0] + this.subDelay);
        c.endTime = Math.max(0, c._o[1] + this.subDelay);
      }
      if (prev === 'disabled') t[i].mode = 'disabled';
    }
  }

  // ---------- Schnappschuss ----------
  async snapshot() {
    const v = this.video;
    if (!v.videoWidth) throw new Error('Kein Videobild');
    const c = document.createElement('canvas');
    c.width = v.videoWidth; c.height = v.videoHeight;
    const ctx = c.getContext('2d');
    // Aktive CSS/SVG-Filter (z. B. von UVE) mit in das Bild übernehmen
    const f = getComputedStyle(v).filter;
    if (f && f !== 'none') ctx.filter = f;
    ctx.drawImage(v, 0, 0);
    // UVE-GPU-/Overlay-Canvas über dem Video, falls vorhanden, bevorzugen
    const r = v.getBoundingClientRect();
    for (const cv of document.querySelectorAll('canvas')) {
      if (cv === c || !cv.isConnected || cv.closest('.ui')) continue;
      const cr = cv.getBoundingClientRect();
      const cs = getComputedStyle(cv);
      if (cs.display === 'none' || cs.visibility === 'hidden' || Number(cs.opacity) === 0) continue;
      if (Math.abs(cr.width - r.width) < 4 && Math.abs(cr.height - r.height) < 4 && cv.width > 0) {
        try {
          // Nur übernehmen, wenn die Overlay-Canvas tatsächlich Bildinhalt hat
          const probe = document.createElement('canvas');
          probe.width = 16; probe.height = 16;
          const pc = probe.getContext('2d', { willReadFrequently: true });
          pc.drawImage(cv, 0, 0, 16, 16);
          const px = pc.getImageData(0, 0, 16, 16).data;
          let lit = 0;
          for (let i = 3; i < px.length; i += 4) if (px[i] > 0) lit++;
          if (lit > 128) { ctx.filter = 'none'; ctx.drawImage(cv, 0, 0, c.width, c.height); }
        } catch (_) { }
      }
    }
    const blob = await new Promise((res) => c.toBlob(res, 'image/png'));
    return new Uint8Array(await blob.arrayBuffer());
  }
}

export { EQ_FREQS };
