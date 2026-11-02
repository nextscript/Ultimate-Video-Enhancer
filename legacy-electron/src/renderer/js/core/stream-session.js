// Stream-Sitzung über Media Source Extensions.
// Der Main-Prozess liefert unter uve://app/live?id=…&ss=… ein endloses fragmentiertes MP4
// (FFmpeg). Hier wird es in einen SourceBuffer gespeist. Die Quelle ist gleichen Ursprungs,
// daher bleiben Canvas/WebGL-Zugriffe (UVE GPU-Modus, Scopes) erlaubt.
const BACK_KEEP = 30;        // Sekunden hinter der Abspielposition behalten
const AHEAD_MAX_VOD = 90;    // bei VOD höchstens so viel vorpuffern (Gegendruck auf FFmpeg)
const LIVE_MAX_LAG = 20;     // Live: weiter hinter der Live-Kante → aufholen

function box(buf, type) {
  // Sucht einen Box-Typ (4 ASCII-Zeichen) im Puffer und liefert den Offset der Nutzdaten
  const t = [...type].map((c) => c.charCodeAt(0));
  for (let i = 4; i < buf.length - 4; i++) {
    if (buf[i] === t[0] && buf[i + 1] === t[1] && buf[i + 2] === t[2] && buf[i + 3] === t[3]) return i + 4;
  }
  return -1;
}

// Liefert die Länge von ftyp+moov (Init-Segment), sobald vollständig vorhanden
function initLength(buf) {
  let off = 0;
  while (off + 8 <= buf.length) {
    const size = ((buf[off] << 24) >>> 0) + (buf[off + 1] << 16) + (buf[off + 2] << 8) + buf[off + 3];
    const type = String.fromCharCode(buf[off + 4], buf[off + 5], buf[off + 6], buf[off + 7]);
    if (size < 8) return -1;
    if (off + size > buf.length) return -1;
    off += size;
    if (type === 'moov') return off;
  }
  return -1;
}

function codecsFromInit(init) {
  const codecs = [];
  const avcC = box(init, 'avcC');
  const hvcC = box(init, 'hvcC');
  if (avcC > 0) {
    const h = (n) => n.toString(16).padStart(2, '0');
    codecs.push('avc1.' + h(init[avcC + 1]) + h(init[avcC + 2]) + h(init[avcC + 3]));
  } else if (hvcC > 0) {
    // HEVC-Codec-String nach ISO/IEC 14496-15 Anhang E (z. B. hvc1.2.4.L120.90 für Main10)
    const b = init[hvcC + 1];
    const space = ['', 'A', 'B', 'C'][b >> 6];
    const tier = (b >> 5) & 1 ? 'H' : 'L';
    const profile = b & 0x1f;
    let compat = ((init[hvcC + 2] << 24) | (init[hvcC + 3] << 16) | (init[hvcC + 4] << 8) | init[hvcC + 5]) >>> 0;
    let rev = 0;
    for (let i = 0; i < 32; i++) { rev = (rev << 1) | (compat & 1); compat >>>= 1; }
    const cons = [];
    for (let i = 6; i < 12; i++) cons.push(init[hvcC + i]);
    while (cons.length && cons[cons.length - 1] === 0) cons.pop();
    const entry = box(init, 'hev1') > 0 && box(init, 'hvc1') < 0 ? 'hev1' : 'hvc1';
    codecs.push([entry, space + profile, (rev >>> 0).toString(16), tier + init[hvcC + 12], ...cons.map((c) => c.toString(16).toUpperCase())].join('.'));
  }
  else if (box(init, 'av1C') > 0) codecs.push('av01.0.08M.08');
  else if (box(init, 'vpcC') > 0) codecs.push('vp09.00.40.08');
  if (box(init, 'mp4a') > 0) codecs.push('mp4a.40.2');
  else if (box(init, 'Opus') > 0) codecs.push('opus');
  else if (box(init, 'fLaC') > 0) codecs.push('flac');
  return codecs;
}

const ranges = (b) => { const r = []; for (let i = 0; i < b.length; i++) r.push(b.start(i).toFixed(1) + '-' + b.end(i).toFixed(1)); return '[' + r.join(',') + ']'; };
const dbg = (msg) => { try { window.uveHost.send('stream:log', 'mse ' + msg); } catch (_) { } };

const concat = (parts) => {
  const len = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(len);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
};

export class StreamSession extends EventTarget {
  constructor(video, info) {
    super();
    this.video = video;
    this.info = info;          // { id, live, duration, title }
    this.live = !!info.live;
    this.ms = null;
    this.sb = null;
    this.ctrl = null;
    this.gen = 0;
    this.disposed = false;
    this.objectUrl = null;
    this._timer = null;
  }

  emit(type, detail) { this.dispatchEvent(new CustomEvent(type, { detail })); }

  async start(at = 0) {
    this.ms = new MediaSource();
    this.objectUrl = URL.createObjectURL(this.ms);
    this.video.src = this.objectUrl;
    await new Promise((res) => this.ms.addEventListener('sourceopen', res, { once: true }));
    if (!this.live && this.info.duration) this.ms.duration = this.info.duration;
    this._timer = setInterval(() => this._maintain(), 1000);
    await this._run(at);
  }

  get buffering() { return !!this.ctrl; }

  isBuffered(t) {
    const b = this.video.buffered;
    for (let i = 0; i < b.length; i++) if (t >= b.start(i) - 0.1 && t <= b.end(i) - 0.5) return true;
    return false;
  }

  // VOD-Sprung außerhalb des Puffers: FFmpeg ab neuer Position neu starten
  async seekTo(t) {
    if (this.live) return;
    if (this.isBuffered(t)) { this.video.currentTime = t; return; }
    await this._run(t);
  }

  async _waitIdle() {
    while (this.sb && this.sb.updating) await new Promise((r) => this.sb.addEventListener('updateend', r, { once: true }));
  }

  async _append(data, gen) {
    await this._waitIdle();
    // veraltete Daten eines abgebrochenen Laufs nicht mehr anhängen (z. B. nach einem Sprung)
    if (this.disposed || !this.sb || (gen !== undefined && gen !== this.gen)) return;
    try {
      this.sb.appendBuffer(data);
    } catch (e) {
      if (e.name === 'QuotaExceededError') {
        await this._evict(true);
        await this._waitIdle();
        this.sb.appendBuffer(data);
      } else throw e;
    }
    await this._waitIdle();
  }

  async _evict(hard) {
    const b = this.video.buffered;
    if (!this.sb || !b.length) return;
    const t = this.video.currentTime;
    const keep = hard ? 10 : BACK_KEEP;
    if (t - b.start(0) > keep + 5) {
      await this._waitIdle();
      try { this.sb.remove(b.start(0), t - keep); } catch (_) { }
      await this._waitIdle();
    }
  }

  async _run(at) {
    const gen = ++this.gen;
    if (this.ctrl) this.ctrl.abort();
    const ctrl = new AbortController();
    this.ctrl = ctrl;
    this.emit('state', { state: 'connecting' });

    // Bei Neustart: alten Inhalt verwerfen
    if (this.sb) {
      await this._waitIdle();
      try { this.sb.abort(); } catch (e) { dbg('abort-Fehler ' + e.message); }
      const b = this.video.buffered;
      if (b.length) { try { this.sb.remove(0, Infinity); } catch (e) { dbg('remove-Fehler ' + e.message); } await this._waitIdle(); }
      dbg(`run#${gen} Neustart bei ${at}s, Puffer nach Leeren ${ranges(this.video.buffered)}`);
    }

    let res;
    try {
      res = await fetch(`uve://app/live?id=${encodeURIComponent(this.info.id)}&ss=${at || 0}`, { signal: ctrl.signal });
    } catch (e) {
      if (gen === this.gen) this.emit('error', { message: e.name === 'AbortError' ? null : e.message });
      return;
    }
    if (!res.ok) {
      const msg = await res.text().catch(() => 'HTTP ' + res.status);
      // Quelle nicht erreichbar / keine Daten (Antwort vor dem ersten Byte) → Verbindungsfehler
      if (gen === this.gen) { this.ctrl = null; this.emit('error', { message: msg || 'HTTP ' + res.status, connect: true }); }
      return;
    }
    // Exakte Startzeit dieses Laufs (Keyframe vor dem Sprungziel); FFmpeg liefert relative Zeitstempel ab 0
    const startAt = Number(res.headers.get('X-UVE-Start')) || 0;
    const reader = res.body.getReader();
    let pending = [];
    let initDone = false;
    let first = true;
    let offsetSet = false;
    try {
      for (;;) {
        // Gegendruck: bei VOD nicht unbegrenzt vorpuffern
        while (!this.live && initDone && gen === this.gen && this._ahead() > AHEAD_MAX_VOD) await new Promise((r) => setTimeout(r, 500));
        const { value, done } = await reader.read();
        if (gen !== this.gen || this.disposed) { reader.cancel().catch(() => { }); return; }
        if (done) break;
        pending.push(value);
        if (!initDone) {
          const buf = concat(pending);
          const n = initLength(buf);
          if (n < 0) { pending = [buf]; continue; }
          if (!this.sb) {
            const codecs = codecsFromInit(buf.subarray(0, n));
            const audioOnly = codecs.every((c) => /^(mp4a|opus|flac)/.test(c));
            const mime = `${audioOnly ? 'audio' : 'video'}/mp4; codecs="${codecs.join(',')}"`;
            if (!MediaSource.isTypeSupported(mime)) throw new Error('Codec nicht unterstützt: ' + codecs.join(', '));
            this.sb = this.ms.addSourceBuffer(mime);
            this.sb.mode = 'segments';
            this.emit('codecs', { codecs, mime });
          }
          initDone = true;
          pending = [buf];
        }
        // gesammelte Fragmente gemeinsam anhängen
        const data = concat(pending);
        pending = [];
        if (!offsetSet && this.sb) {
          await this._waitIdle();
          if (gen !== this.gen) { reader.cancel().catch(() => { }); return; }
          try { this.sb.timestampOffset = startAt; } catch (e) { dbg('timestampOffset-Fehler ' + e.message); }
          offsetSet = true;
        }
        await this._append(data, gen);
        if (gen !== this.gen) { reader.cancel().catch(() => { }); return; }
        if (first && this.video.buffered.length) {
          if (at > 0) dbg(`run#${gen} Sprung ${at}s → Keyframe ${startAt}s, gepuffert ${ranges(this.video.buffered)}`);
          first = false;
          const start = this.video.buffered.start(0);
          if (this.live) this.video.currentTime = Math.max(start, this.video.buffered.end(this.video.buffered.length - 1) - 3);
          else if (Math.abs(this.video.currentTime - at) > 1) this.video.currentTime = Math.max(start, at);
          this.emit('state', { state: 'playing' });
        }
      }
      if (gen === this.gen && !this.disposed) {
        await this._waitIdle();
        // Live: Ende heißt meist Verbindungsabbruch → nicht als Titelende melden, die Oberfläche verbindet neu
        if (this.live) { this.emit('state', { state: 'interrupted' }); return; }
        if (this.ms.readyState === 'open') { try { this.ms.endOfStream(); } catch (_) { } }
        this.emit('state', { state: 'ended' });
      }
    } catch (e) {
      if (gen === this.gen && !this.disposed && e.name !== 'AbortError') this.emit('error', { message: e.message });
    } finally {
      if (gen === this.gen) this.ctrl = null;
    }
  }

  _ahead() {
    const b = this.video.buffered;
    const t = this.video.currentTime;
    for (let i = 0; i < b.length; i++) if (t >= b.start(i) - 0.5 && t <= b.end(i)) return b.end(i) - t;
    return 0;
  }

  async _maintain() {
    if (this.disposed || !this.sb) return;
    const v = this.video;
    const b = v.buffered;
    if (!b.length) return;
    // Pufferlücke an der Abspielposition überspringen
    if (!this.isBuffered(v.currentTime) && !v.paused) {
      for (let i = 0; i < b.length; i++) if (b.start(i) > v.currentTime && b.start(i) - v.currentTime < 5) { v.currentTime = b.start(i) + 0.05; break; }
    }
    if (this.live && !v.paused) {
      const edge = b.end(b.length - 1);
      if (edge - v.currentTime > LIVE_MAX_LAG) v.currentTime = edge - 3;
    }
    if (!this.sb.updating) await this._evict(false);
  }

  liveEdge() {
    const b = this.video.buffered;
    return b.length ? b.end(b.length - 1) : 0;
  }

  dispose() {
    this.disposed = true;
    this.gen++;
    clearInterval(this._timer);
    if (this.ctrl) this.ctrl.abort();
    this.ctrl = null;
    try { if (this.ms && this.ms.readyState === 'open') this.ms.endOfStream(); } catch (_) { }
    if (this.objectUrl) URL.revokeObjectURL(this.objectUrl);
    this.sb = null;
    this.ms = null;
  }
}
