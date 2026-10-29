// Default in-app keyboard shortcuts (userscript Ctrl+Alt mapping + editor keys)
export const DEFAULT_SHORTCUTS: Record<string, string> = {
    base: 'Ctrl+Alt+B', moody: 'Ctrl+Alt+D', teal: 'Ctrl+Alt+O', vibrant: 'Ctrl+Alt+V', hdr: 'Ctrl+Alt+P',
    cycleProfile: 'Ctrl+Alt+C', hud: 'Ctrl+Alt+H', gpu: 'Ctrl+Alt+X', expert: 'Ctrl+Alt+I', manager: 'Ctrl+Alt+G',
    scopes: 'Ctrl+Alt+S', auto: 'Ctrl+Alt+A', compare: 'Ctrl+Alt+U', bypass: 'Ctrl+Alt+Y',
    playPause: 'Space', frameBack: 'ArrowLeft', frameFwd: 'ArrowRight', start: 'Home', end: 'End',
    setIn: 'I', setOut: 'O', zoomIn: '+', zoomOut: '-', undo: 'Ctrl+Z', redo: 'Ctrl+Y', save: 'Ctrl+S',
    open: 'Ctrl+O', render: 'Ctrl+R', escape: 'Escape'
};
