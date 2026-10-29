// Platform-aware shortcut labels for the UI (v1.0.2): ⌃⌥⇧⌘ symbols on macOS, "Ctrl+Alt+B" elsewhere.
import { defaultShortcuts, formatCombo } from '../../shared/shortcuts';
import { getState } from '../state/store';

export const PLATFORM = /Mac/i.test(navigator.platform) ? 'darwin' : /Win/i.test(navigator.platform) ? 'win32' : 'linux';
export const IS_MAC = PLATFORM === 'darwin';

/** Display text of a literal combo, e.g. kbd('Ctrl+Alt+B') → "⌃⌥B" on macOS. */
export const kbd = (combo: string) => formatCombo(combo, PLATFORM);

/** Display text of the (possibly user-changed) shortcut of an action, e.g. sc('save') → "⌘S" / "Ctrl+S". */
export const sc = (action: string) => kbd(getState().prefs?.shortcuts[action] || defaultShortcuts(PLATFORM)[action] || '');
