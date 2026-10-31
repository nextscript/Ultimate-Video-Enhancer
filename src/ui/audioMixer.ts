// Preview audio mixer for the timeline monitor (WebAudio). It mirrors the export audio law of
// `buildAudioGraph` in shared/timeline.ts with the SAME helper functions:
//   gain   = dbToLin(clip.gainDb + track.gainDb + masterGainDb) x audio fades x crossfade ramps x gainCurve
//            (`clipAudioGain`, evaluated per frame on the clip's stream time)
//   fold   = `channelFold(media.audio[0])` coefficients per source channel (mono duplicated at unity,
//            surrounds x 1/sqrt(2), LFE dropped) with `panLaw(clip.pan + track.pan)` folded into them
//   tempo  = the element's playbackRate (= clip speed, pitch preserved like `atempo`)
//   mute/solo = `PlanClip.audible` (trackAudible / hasSoloTrack already applied by buildPlan)
// Placement follows from playback itself: a clip's element is playing exactly while the timeline is
// inside [streamStartF, endF + tailF), i.e. it starts at (streamStartF - rangeStartF) / F like `adelay`.
import type { AudioStreamInfo, Timeline } from '../../shared/types';
import { channelFold, panLaw, type PlanClip } from '../../shared/timeline';
import { clipAudioGain } from './timelineMath';

interface Strip {
    el: HTMLMediaElement;
    src: MediaElementAudioSourceNode;
    /** fold/pan network (absent for a plain stereo source at centre pan) */
    nodes: AudioNode[];
    gain: GainNode;
    key: string;
}

export class AudioMixer {
    private ctx: AudioContext | null = null;
    private master: GainNode | null = null;
    private strips = new Map<HTMLMediaElement, Strip>();
    private volume = 1;
    private muted = false;

    private ensure(): AudioContext {
        if (!this.ctx) {
            this.ctx = new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' });
            this.master = this.ctx.createGain();
            this.master.connect(this.ctx.destination);
            this.applyMonitor();
        }
        if (this.ctx.state === 'suspended') void this.ctx.resume();
        return this.ctx;
    }

    /** Transport monitor volume (not part of the mix law; export ignores it). */
    setMonitor(volume: number, muted: boolean) {
        this.volume = volume;
        this.muted = muted;
        this.applyMonitor();
    }

    private applyMonitor() {
        if (this.master && this.ctx) this.master.gain.setValueAtTime(this.muted ? 0 : this.volume, this.ctx.currentTime);
    }

    /** Routes an element through a strip that matches the clip's fold/pan; rebuilt only when they change. */
    private strip(el: HTMLMediaElement, pc: PlanClip): Strip {
        const ctx = this.ensure();
        const stream: AudioStreamInfo | undefined = pc.source.media.audio[0];
        const pan = pc.clip.pan + pc.track.pan;
        const key = `${stream?.layout ?? ''}|${stream?.channels ?? 0}|${pan}`;
        let s = this.strips.get(el);
        if (s && s.key === key) return s;
        if (!s) {
            const src = ctx.createMediaElementSource(el);
            const gain = ctx.createGain();
            gain.connect(this.master!);
            s = { el, src, nodes: [], gain, key: '' };
            this.strips.set(el, s);
        }
        s.src.disconnect();
        s.nodes.forEach(n => n.disconnect());
        s.nodes = [];
        s.key = key;
        const fold = stream ? channelFold(stream) : null;
        const plainStereo = fold && fold.l.length === 2 && fold.l[0] === 1 && fold.l[1] === 0 && fold.r[0] === 0 && fold.r[1] === 1;
        if (!fold || (plainStereo && pan === 0)) {
            // centre-panned stereo needs no pan nodes; unknown layouts take the browser's own down-mix
            // (export: ffmpeg's aformat=stereo rematrix) with the pan law applied after it
            if (!fold && pan !== 0) {
                const { left, right } = panLaw(pan);
                const split = ctx.createChannelSplitter(2), merge = ctx.createChannelMerger(2);
                const gl = ctx.createGain(), gr = ctx.createGain();
                gl.gain.value = left; gr.gain.value = right;
                s.src.connect(split);
                split.connect(gl, 0); split.connect(gr, 1);
                gl.connect(merge, 0, 0); gr.connect(merge, 0, 1);
                merge.connect(s.gain);
                s.nodes = [split, gl, gr, merge];
            } else {
                s.src.connect(s.gain);
            }
            return s;
        }
        const { left, right } = panLaw(pan);
        const n = fold.l.length;
        const split = ctx.createChannelSplitter(n);
        const merge = ctx.createChannelMerger(2);
        s.src.connect(split);
        s.nodes.push(split, merge);
        for (let i = 0; i < n; i++) {
            for (const [out, coeff] of [[0, fold.l[i] * left], [1, fold.r[i] * right]] as const) {
                if (coeff === 0) continue;
                const g = ctx.createGain();
                g.gain.value = coeff;
                split.connect(g, i);
                g.connect(merge, 0, out);
                s.nodes.push(g);
            }
        }
        merge.connect(s.gain);
        return s;
    }

    /** Per-frame update of one audible clip's gain (stream-time law shared with the export). */
    update(el: HTMLMediaElement, tl: Timeline, pc: PlanClip, frame: number) {
        const s = this.strip(el, pc);
        const ctx = this.ctx!;
        s.gain.gain.setTargetAtTime(clipAudioGain(tl, pc, frame), ctx.currentTime, 0.005);
    }

    /** Silences an element that is not heard at the current frame (muted track, outside its span…). */
    silence(el: HTMLMediaElement) {
        const s = this.strips.get(el);
        if (s && this.ctx) s.gain.gain.setTargetAtTime(0, this.ctx.currentTime, 0.005);
    }

    forget(el: HTMLMediaElement) {
        const s = this.strips.get(el);
        if (!s) return;
        s.src.disconnect();
        s.nodes.forEach(n => n.disconnect());
        s.gain.disconnect();
        this.strips.delete(el);
    }

    dispose() {
        for (const el of [...this.strips.keys()]) this.forget(el);
        void this.ctx?.close();
        this.ctx = null;
        this.master = null;
    }
}
