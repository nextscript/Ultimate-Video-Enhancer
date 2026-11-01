// v1.1.2: keeps a separate sound element in sync with a picture, for files whose audio Chromium cannot
// decode (AC3 / E-AC3 / DTS / TrueHD in MKV). The main process streams that sound live as PCM WAV
// (electron/audio-stream.ts), so it starts at once, without converting the file first.
// Sync law: parked → the sound is pre-positioned on the picture time (playback then starts together);
// running → the sound element runs on its own clock (picture and sound both run in real time, so they do
// not drift apart); only a real jump (a seek while playing, a stall) is fixed by one seek that leads by the
// measured seek latency. The speed is never nudged: changing playbackRate re-starts the pitch-preserving
// time stretcher and is audible as crackling.

const clampRate = (r: number) => Math.max(0.0625, Math.min(16, r));

export class AudioSidecar {
    /** live instances (read by the automated UI test only) */
    static readonly live = new Set<AudioSidecar>();
    readonly el: HTMLAudioElement;
    private lastFix = 0;
    /** measured time from a seek until the sound plays again (s), used as lead for seeks while running */
    private latency = 0.35;
    private seekAt = 0;

    constructor() {
        const el = this.el = new Audio();
        AudioSidecar.live.add(this);
        el.preload = 'auto';
        el.crossOrigin = 'anonymous';
        el.addEventListener('seeking', () => { if (!el.paused) this.seekAt = performance.now(); });
        el.addEventListener('playing', () => {
            if (!this.seekAt) return;
            const l = (performance.now() - this.seekAt) / 1000;
            this.seekAt = 0;
            this.latency = Math.max(0.05, Math.min(1.5, this.latency * 0.6 + l * 0.4));
        });
    }

    setUrl(url: string) {
        if (this.el.dataset.url === url) return;
        this.el.dataset.url = url;
        this.el.src = url;
    }

    /**
     * Call on every picture update. `t` = media time of the picture, `run` = the sound should be heard
     * now, `rate` = playback speed.
     */
    follow(t: number, run: boolean, rate: number) {
        const el = this.el;
        if (!el.src) return;
        const now = performance.now();
        const r = clampRate(rate);
        if (!run) {
            if (!el.paused) el.pause();
            // pre-position (debounced while scrubbing: every seek is a new decode in the main process)
            if (!el.seeking && Math.abs(el.currentTime - t) > 0.04 && now - this.lastFix > 120) { this.lastFix = now; el.currentTime = t; }
            return;
        }
        if (el.paused) {
            if (Math.abs(el.currentTime - t) > 0.08) el.currentTime = t;
            this.setRate(r);
            this.lastFix = now;
            void el.play().catch(() => undefined);
            return;
        }
        this.setRate(r);
        if (el.seeking || el.readyState < 3) return;
        const drift = t - el.currentTime; // > 0: the sound is behind the picture
        if (Math.abs(drift) > 0.25) {
            if (now - this.lastFix > 1500) { this.lastFix = now; el.currentTime = t + this.latency * r; }
        }
    }

    /** Only touches playbackRate when the speed really changes (every assignment resets the time stretcher). */
    private setRate(r: number) {
        if (Math.abs(this.el.playbackRate - r) > 1e-6) this.el.playbackRate = r;
    }

    pause() { if (!this.el.paused) this.el.pause(); }

    dispose() {
        AudioSidecar.live.delete(this);
        this.el.pause();
        this.el.removeAttribute('src');
        delete this.el.dataset.url;
        this.el.load();
    }
}
