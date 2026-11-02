import { createRoot } from 'react-dom/client';
import { loadVideo, quickRender, toggleSetting } from './state/actions';
import { type View, getState, setState } from './state/store';
import { player } from './ui/Player';
import { AudioSidecar } from './ui/audioSidecar';
import { App } from './ui/App';
import { validateWebGpu, validationDetail } from './engine/createEngine';
import './ui/styles.css';

// hooks for the automated UI snapshot test (electron . --ui-snapshot=…)
(window as unknown as { __uveTest: unknown }).__uveTest = {
    load: (p: string, place?: boolean) => loadVideo(p, { place: !!place }),
    play: () => player.play(),
    pause: () => player.pause(),
    seek: (t: number) => player.seek(t),
    time: () => getState().time,
    quickRender: () => quickRender(),
    jobs: () => getState().jobs.map(j => ({ status: j.status, progress: +j.progress.toFixed(4), output: j.output, error: (j as { error?: string }).error })),
    /** v1.1.2: state of the live sound streams (AC3 / DTS preview sound) */
    sound: () => [...AudioSidecar.live].map(a => ({ src: a.el.src, t: a.el.currentTime, paused: a.el.paused, muted: a.el.muted, rate: a.el.playbackRate, ready: a.el.readyState, bytes: (a.el as HTMLMediaElement & { webkitAudioDecodedByteCount?: number }).webkitAudioDecodedByteCount ?? 0 })),
    view: (v: View) => setState({ view: v }),
    validationDetail: (q?: boolean, a?: boolean) => validationDetail(q, a),
    validateWebGpu: () => validateWebGpu(),
    toggle: (k: 'enabled' | 'darkMoody' | 'tealOrange' | 'vibrantSat') => toggleSetting(k)
};

// harness windows (capability probe) only need the engine validation, not the UI and its IPC calls
if (!new URLSearchParams(location.search).has('harness')) createRoot(document.getElementById('root')!).render(<App />);
