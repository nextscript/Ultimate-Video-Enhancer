import { createRoot } from 'react-dom/client';
import { loadVideo, toggleSetting } from './state/actions';
import { type View, setState } from './state/store';
import { App } from './ui/App';
import './ui/styles.css';

// hooks for the automated UI snapshot test (electron . --ui-snapshot=…)
(window as unknown as { __uveTest: unknown }).__uveTest = {
    load: (p: string) => loadVideo(p),
    view: (v: View) => setState({ view: v }),
    toggle: (k: 'enabled' | 'darkMoody' | 'tealOrange' | 'vibrantSat') => toggleSetting(k)
};

createRoot(document.getElementById('root')!).render(<App />);
