// Default in-app keyboard shortcuts (userscript Ctrl+Alt mapping + editor keys)
export const DEFAULT_SHORTCUTS: Record<string, string> = {
    base: 'Ctrl+Alt+B', moody: 'Ctrl+Alt+D', teal: 'Ctrl+Alt+O', vibrant: 'Ctrl+Alt+V', hdr: 'Ctrl+Alt+P',
    cycleProfile: 'Ctrl+Alt+C', hud: 'Ctrl+Alt+H', gpu: 'Ctrl+Alt+X', expert: 'Ctrl+Alt+I', manager: 'Ctrl+Alt+G',
    scopes: 'Ctrl+Alt+S', auto: 'Ctrl+Alt+A', compare: 'Ctrl+Alt+U',
    playPause: 'Space', frameBack: 'ArrowLeft', frameFwd: 'ArrowRight', jumpBack: 'Shift+ArrowLeft', jumpFwd: 'Shift+ArrowRight',
    start: 'Home', end: 'End', zoomIn: '+', zoomOut: '-', zoomFit: 'Z',
    undo: 'Ctrl+Z', redo: 'Ctrl+Shift+Z', redoAlt: 'Ctrl+Y', save: 'Ctrl+S', open: 'Ctrl+O', render: 'Ctrl+R', escape: 'Escape',
    // v1.0.4 timeline clipboard and range marking
    cut: 'Ctrl+X', copy: 'Ctrl+C', paste: 'Ctrl+V', selectAll: 'Ctrl+A', markIn: 'I', markOut: 'O',
    razor: 'Q', rippleTrimEnd: 'Shift+Q', lift: 'Delete', ripple: 'Shift+Delete', duplicate: 'Shift+D',
    nudgeLeft: 'Alt+ArrowLeft', nudgeRight: 'Alt+ArrowRight', nudgeLeft10: 'Alt+Shift+ArrowLeft', nudgeRight10: 'Alt+Shift+ArrowRight',
    transition: 'T', addVideoTrack: 'Ctrl+Shift+V', addAudioTrack: 'Ctrl+Shift+A', marker: 'M',
    nextEdit: 'PageDown', prevEdit: 'PageUp', nextMarker: 'Shift+PageDown', prevMarker: 'Shift+PageUp',
    snapping: 'S'
};

/** Default combos of earlier versions that were replaced; a saved copy of one of these is moved to the new default. */
export const REPLACED_DEFAULTS: Record<string, string[]> = { redo: ['Ctrl+Y'] };

/**
 * v1.0.2 macOS: editor commands use Command (⌘) like every Mac app. The userscript toggles stay on the
 * physical Control+Option (⌃⌥) keys – ⌘⌥H / ⌘⌥D / ⌘⌥Esc are taken by the system.
 */
export const MAC_SHORTCUTS: Record<string, string> = {
    undo: 'Meta+Z', redo: 'Meta+Shift+Z', redoAlt: 'Meta+Y', save: 'Meta+S', open: 'Meta+O', render: 'Meta+R',
    cut: 'Meta+X', copy: 'Meta+C', paste: 'Meta+V', selectAll: 'Meta+A',
    lift: 'Backspace', ripple: 'Shift+Backspace', duplicate: 'Meta+D',
    addVideoTrack: 'Meta+Shift+V', addAudioTrack: 'Meta+Shift+A'
};

/**
 * Saved shortcuts merged onto the defaults: actions that no longer exist are dropped, new actions get their
 * default, and a saved combo that was only an old default (REPLACED_DEFAULTS) follows the new default.
 */
export function mergeShortcuts(platform: string, saved: Record<string, string> | undefined): Record<string, string> {
    const def = defaultShortcuts(platform);
    const out: Record<string, string> = { ...def };
    for (const [k, v] of Object.entries(saved || {})) {
        if (!(k in def) || typeof v !== 'string') continue;
        if (REPLACED_DEFAULTS[k]?.some(old => sameCombo(old, v))) continue;
        out[k] = v;
    }
    return out;
}

export function defaultShortcuts(platform: string): Record<string, string> {
    return platform === 'darwin' ? { ...DEFAULT_SHORTCUTS, ...MAC_SHORTCUTS } : { ...DEFAULT_SHORTCUTS };
}

const MODS = ['Ctrl', 'Alt', 'Shift', 'Meta'] as const;

function parseCombo(combo: string): { mods: Set<string>; key: string } {
    const mods = new Set<string>();
    let rest = combo;
    for (;;) {
        const m = /^(Ctrl|Alt|Shift|Meta)\+(.+)$/i.exec(rest);
        if (!m) break;
        mods.add(MODS.find(x => x.toLowerCase() === m[1].toLowerCase())!);
        rest = m[2];
    }
    return { mods, key: rest };
}

/** Same key combination regardless of modifier order and letter case. */
export function sameCombo(a: string, b: string): boolean {
    const x = parseCombo(a), y = parseCombo(b);
    return x.key.toUpperCase() === y.key.toUpperCase() && x.mods.size === y.mods.size && [...x.mods].every(m => y.mods.has(m));
}

type KeyLike = Pick<KeyboardEvent, 'key' | 'code' | 'ctrlKey' | 'altKey' | 'shiftKey' | 'metaKey'>;

/** Canonical combo string of a key event ("Ctrl+Alt+B", "Meta+Shift+Z", "Space", "+"). */
export function comboFromEvent(e: KeyLike): string {
    let key = e.key === ' ' ? 'Space' : e.key;
    // Option on macOS (and AltGr = Ctrl+Alt on Windows) turns letters into other characters (⌥B = ∫):
    // use the physical key then
    if (e.altKey && !/^[a-z0-9]$/i.test(key) && /^(Key[A-Z]|Digit[0-9])$/.test(e.code)) key = e.code.slice(-1);
    else if (key.length === 1) key = key.toUpperCase();
    // Shift is part of the combo for named keys and together with another modifier (⌘⇧Z); a lone Shift
    // only selects the character ("+")
    const shift = e.shiftKey && (e.key.length > 1 || e.ctrlKey || e.metaKey || e.altKey);
    return `${e.ctrlKey ? 'Ctrl+' : ''}${e.altKey ? 'Alt+' : ''}${shift ? 'Shift+' : ''}${e.metaKey ? 'Meta+' : ''}${key}`;
}

const MAC_KEYS: Record<string, string> = { ArrowLeft: '←', ArrowRight: '→', ArrowUp: '↑', ArrowDown: '↓', Escape: 'Esc', Enter: '↩', Backspace: '⌫', Delete: '⌦' };

/** Display text: macOS symbols (⌃⌥⇧⌘B), elsewhere the combo itself (Meta shown as "Win"/"Super"). */
export function formatCombo(combo: string, platform: string): string {
    const { mods, key } = parseCombo(combo);
    if (platform === 'darwin') {
        return `${mods.has('Ctrl') ? '⌃' : ''}${mods.has('Alt') ? '⌥' : ''}${mods.has('Shift') ? '⇧' : ''}${mods.has('Meta') ? '⌘' : ''}${MAC_KEYS[key] || key}`;
    }
    const meta = platform === 'win32' ? 'Win' : 'Super';
    return [...MODS.filter(m => mods.has(m)).map(m => (m === 'Meta' ? meta : m)), key].join('+');
}
