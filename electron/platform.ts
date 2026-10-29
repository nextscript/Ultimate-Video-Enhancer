// Platform helpers (v1.0.1): executable names, PATH lookup, process-tree termination and archive tools
// for Windows, Linux and macOS.
import { execFile, spawn } from 'node:child_process';
import path from 'node:path';

export const IS_WIN = process.platform === 'win32';
export const IS_MAC = process.platform === 'darwin';
export const exe = (name: string) => (IS_WIN ? `${name}.exe` : name);

/** First match of an executable on PATH (where.exe / which). */
export function whichExe(name: string): Promise<string | null> {
    return new Promise(resolve => {
        execFile(IS_WIN ? 'where.exe' : 'which', [IS_WIN ? exe(name) : name], { timeout: 5000, windowsHide: true }, (err, out) => {
            const first = String(out || '').split(/\r?\n/).map(s => s.trim()).find(Boolean);
            resolve(!err && first ? first : null);
        });
    });
}

/** Terminates a process and its children (taskkill /T on Windows, process group / SIGKILL elsewhere). */
export function killTree(pid: number | undefined) {
    if (!pid) return;
    if (IS_WIN) { spawn('taskkill', ['/PID', String(pid), '/T', '/F'], { windowsHide: true }); return; }
    try { process.kill(-pid, 'SIGKILL'); } catch { try { process.kill(pid, 'SIGKILL'); } catch { /* already gone */ } }
}

/** Spawn options so that killTree() can terminate the whole group on POSIX. */
export const groupSpawn = IS_WIN ? {} : { detached: true };

/** tar (bsdtar ships with Windows 10 1803+, GNU/BSD tar elsewhere). */
export function tarCommand(): string {
    return IS_WIN ? path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'tar.exe') : 'tar';
}
