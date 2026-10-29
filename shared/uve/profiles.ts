// User profiles in the userscript format ({id,name,createdAt,updatedAt,settings}) plus the
// "gvf-settings" single-settings export. Ported from importSingleUserProfileObject() /
// exportAllUserProfilesAsZip() (source lines ~7363-7870).
import type { UserProfile } from '../types';
import { DEFAULT_SETTINGS, sanitizeSettings, settingsFromGvfSettings } from './settings';

export function sanitizeProfileFilename(name: string): string {
    const s = String(name || '').replace(/[\\/:*?"<>|\u0000-\u001f]+/g, '_').replace(/\s+/g, ' ').trim();
    return (s || 'Profile').slice(0, 80);
}

export function newProfileId() { return 'profile_' + Date.now() + '_' + Math.random().toString(36).slice(2, 11); }

export function defaultUserProfile(): UserProfile {
    const now = Date.now();
    return { id: 'default', name: 'Default', createdAt: now, updatedAt: now, settings: { ...DEFAULT_SETTINGS } };
}

/**
 * Accepts a userscript user profile, a bare settings object (must contain renderMode or profile),
 * or a "gvf-settings" export. Returns null when the object is none of these.
 */
export function parseUserProfileObject(obj: unknown, fallbackName: string): UserProfile | null {
    if (!obj || typeof obj !== 'object') return null;
    const o = obj as Record<string, unknown>;
    const now = Date.now();
    if (o.schema === 'gvf-settings') {
        return { id: newProfileId(), name: sanitizeProfileFilename(fallbackName), createdAt: now, updatedAt: now, settings: settingsFromGvfSettings(o) };
    }
    const isProfileObj = typeof o.name === 'string' && o.settings && typeof o.settings === 'object';
    const settingsObj = (isProfileObj ? o.settings : o) as Record<string, unknown>;
    if (!settingsObj || (!('renderMode' in settingsObj) && !('profile' in settingsObj))) return null;
    const desktop = (o.desktop && typeof o.desktop === 'object') ? o.desktop as UserProfile['desktop'] : undefined;
    return {
        id: isProfileObj && typeof o.id === 'string' ? o.id.slice(0, 80) : newProfileId(),
        name: sanitizeProfileFilename(isProfileObj ? String(o.name) : fallbackName),
        createdAt: Number(o.createdAt) || now,
        updatedAt: now,
        settings: sanitizeSettings(settingsObj),
        desktop
    };
}

/** Merge rule of the userscript: a profile with the same (normalized) name is replaced, keeping its id. */
export function mergeProfiles(existing: UserProfile[], incoming: UserProfile[]): { list: UserProfile[]; added: number; replaced: number } {
    const list = existing.slice();
    let added = 0, replaced = 0;
    const norm = (s: string) => sanitizeProfileFilename(s).toLowerCase();
    for (const p of incoming) {
        const idx = list.findIndex(x => norm(x.name) === norm(p.name));
        if (idx >= 0) { list[idx] = { ...p, id: list[idx].id, createdAt: list[idx].createdAt }; replaced++; }
        else {
            const id = list.some(x => x.id === p.id) ? newProfileId() : p.id;
            list.push({ ...p, id });
            added++;
        }
    }
    return { list, added, replaced };
}
