import assert from 'node:assert/strict';
import { test } from 'node:test';
import { DEFAULT_SHORTCUTS, comboFromEvent, defaultShortcuts, formatCombo, sameCombo } from '../shared/shortcuts';

const ev = (key: string, code: string, m: { ctrl?: boolean; alt?: boolean; shift?: boolean; meta?: boolean } = {}) =>
    ({ key, code, ctrlKey: !!m.ctrl, altKey: !!m.alt, shiftKey: !!m.shift, metaKey: !!m.meta });

test('macOS defaults: Command for editor commands, Control+Option for the userscript toggles', () => {
    const mac = defaultShortcuts('darwin');
    assert.equal(mac.undo, 'Meta+Z');
    assert.equal(mac.redo, 'Meta+Shift+Z');
    assert.equal(mac.save, 'Meta+S');
    assert.equal(mac.base, 'Ctrl+Alt+B');
    assert.deepEqual(defaultShortcuts('win32'), DEFAULT_SHORTCUTS);
    assert.deepEqual(defaultShortcuts('linux'), DEFAULT_SHORTCUTS);
});

test('comboFromEvent: Option-modified characters on macOS map to the physical key', () => {
    assert.equal(comboFromEvent(ev('∫', 'KeyB', { ctrl: true, alt: true })), 'Ctrl+Alt+B');
    assert.equal(comboFromEvent(ev('b', 'KeyB', { ctrl: true, alt: true })), 'Ctrl+Alt+B');
    // German layout: the key labelled Y has code KeyZ – the character wins when it is a plain letter
    assert.equal(comboFromEvent(ev('y', 'KeyZ', { ctrl: true, alt: true })), 'Ctrl+Alt+Y');
    assert.equal(comboFromEvent(ev('z', 'KeyZ', { meta: true, shift: true })), 'Shift+Meta+Z');
    assert.equal(comboFromEvent(ev('z', 'KeyZ', { meta: true })), 'Meta+Z');
    assert.equal(comboFromEvent(ev('+', 'BracketRight', { shift: true })), '+');
    assert.equal(comboFromEvent(ev(' ', 'Space')), 'Space');
});

test('sameCombo ignores modifier order and case; formatCombo uses Mac symbols', () => {
    assert.ok(sameCombo('Meta+Shift+Z', 'Shift+Meta+Z'));
    assert.ok(sameCombo('ctrl+alt+b', 'Ctrl+Alt+B'));
    assert.ok(!sameCombo('Meta+Z', 'Meta+Shift+Z'));
    assert.ok(sameCombo('+', '+'));
    assert.equal(formatCombo('Meta+Shift+Z', 'darwin'), '⇧⌘Z');
    assert.equal(formatCombo('Ctrl+Alt+B', 'darwin'), '⌃⌥B');
    assert.equal(formatCombo('ArrowLeft', 'darwin'), '←');
    assert.equal(formatCombo('Ctrl+Alt+B', 'win32'), 'Ctrl+Alt+B');
    assert.equal(formatCombo('Shift+Meta+Z', 'linux'), 'Shift+Super+Z');
});
