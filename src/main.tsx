import { createRoot } from 'react-dom/client';
import { loadVideo, toggleSetting } from './state/actions';
import { type View, setState } from './state/store';
import { App } from './ui/App';
import { validateWebGpu, validationDetail } from './engine/createEngine';
import './ui/styles.css';

// hooks for the automated UI snapshot test (electron . --ui-snapshot=…)
(window as unknown as { __uveTest: unknown }).__uveTest = {
    load: (p: string) => loadVideo(p),
    view: (v: View) => setState({ view: v }),
    validationDetail: (q?: boolean, a?: boolean) => validationDetail(q, a),
    validateWebGpu: () => validateWebGpu(),
    toggle: (k: 'enabled' | 'darkMoody' | 'tealOrange' | 'vibrantSat') => toggleSetting(k)
};

// harness windows (capability probe) only need the engine validation, not the UI and its IPC calls
if (!new URLSearchParams(location.search).has('harness')) createRoot(document.getElementById('root')!).render(<App />);
