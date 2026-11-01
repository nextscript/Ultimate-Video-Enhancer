// v1.0.7: launches the Video2X executable (Node built-ins only, so the unit tests can drive it with a mock).
// spawn(shell: false) with an argv array – shader names / paths are never part of a command line string.
import { spawn } from 'node:child_process';
import { type Video2xProgress, classifyError, parseProgress } from '../shared/video2x-cli';
import { IS_WIN, groupSpawn, killTree } from './platform';

/** How to start Video2X: the executable plus fixed leading arguments (tests: node + mock script). */
export interface Video2xLauncher { exe: string; prefix: string[]; env?: Record<string, string> }

export interface Video2xExec {
    pid: number | undefined;
    kill: () => void;
    done: Promise<{ ok: boolean; code: number | null; signal: string | null; error?: string; kind?: string; log: string }>;
}

const childEnv = (l: Video2xLauncher): NodeJS.ProcessEnv => ({
    ...process.env,
    // AppImages without FUSE: the type-2 runtime extracts itself into a temp dir instead of mounting
    ...(process.platform === 'linux' && /\.AppImage$/i.test(l.exe) ? { APPIMAGE_EXTRACT_AND_RUN: '1' } : {}),
    ...(l.env || {})
});

/** Runs a short command (--help, --version, --list-devices) with a timeout; exit codes are not judged here. */
export function runQuick(l: Video2xLauncher, args: string[], timeoutMs = 20000): Promise<{ code: number | null; out: string; err: string; timedOut: boolean; spawnError?: string }> {
    return new Promise(resolve => {
        let out = '', err = '', timedOut = false, settled = false;
        let p: ReturnType<typeof spawn>;
        try {
            p = spawn(l.exe, [...l.prefix, ...args], { shell: false, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], env: childEnv(l), ...groupSpawn });
        } catch (e) { resolve({ code: null, out: '', err: '', timedOut: false, spawnError: e instanceof Error ? e.message : String(e) }); return; }
        const t = setTimeout(() => { timedOut = true; killTree(p.pid); }, timeoutMs);
        p.stdout?.on('data', d => { if (out.length < 1 << 20) out += d; });
        p.stderr?.on('data', d => { if (err.length < 1 << 20) err += d; });
        p.on('error', e => { if (settled) return; settled = true; clearTimeout(t); resolve({ code: null, out, err, timedOut, spawnError: e.message }); });
        p.on('close', code => { if (settled) return; settled = true; clearTimeout(t); resolve({ code, out, err, timedOut }); });
    });
}

/**
 * Runs one Video2X job. Progress segments are split at \r and \n (the CLI rewrites one line with \r).
 * stdin is ignored: the CLI reads key presses (space = pause, q = abort) from it.
 */
export function execute(l: Video2xLauncher, args: string[], o: { onProgress?: (p: Video2xProgress) => void; onLog?: (line: string) => void }): Video2xExec {
    const p = spawn(l.exe, [...l.prefix, ...args], { shell: false, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], env: childEnv(l), ...groupSpawn });
    let tail = '';
    let killed = false;
    // Video2X prints this right before returning 0; a non-zero exit after it is ncnn / the Vulkan driver crashing
    // while the process shuts down (seen with Real-ESRGAN / Real-CUGAN on Windows). The caller verifies the output.
    let completed = false;
    const remember = (line: string) => { tail = (tail + '\n' + line).slice(-8000); };
    const feeder = () => {
        let buf = '';
        return (d: Buffer) => {
            buf += String(d);
            const parts = buf.split(/[\r\n]+/);
            buf = parts.pop() ?? '';
            for (const part of parts) handle(part);
            // progress lines are terminated by the next \r only – parse an unterminated one right away
            if (buf.length > 4096) { handle(buf); buf = ''; }
            else { const pr = parseProgress(buf); if (pr) { o.onProgress?.(pr); } }
        };
    };
    const handle = (part: string) => {
        const line = part.replace(/\u001b\[[0-9;]*[A-Za-z]/g, '').trim();
        if (!line) return;
        const pr = parseProgress(line);
        if (pr) { o.onProgress?.(pr); return; }
        if (/Video processed successfully/i.test(line)) completed = true;
        remember(line);
        o.onLog?.(line);
    };
    p.stdout?.on('data', feeder());
    p.stderr?.on('data', feeder());
    const done = new Promise<{ ok: boolean; code: number | null; signal: string | null; error?: string; kind?: string; log: string }>(resolve => {
        p.on('error', e => resolve({ ok: false, code: null, signal: null, error: `Video2X could not be started: ${e.message}`, kind: 'crash', log: tail }));
        p.on('close', (code, signal) => {
            if (code === 0 && !killed) { resolve({ ok: true, code, signal, log: tail }); return; }
            if (killed) { resolve({ ok: false, code, signal, error: 'canceled', kind: 'canceled', log: tail }); return; }
            if (completed) {
                o.onLog?.(`Video2X exited with ${code ?? signal} after "Video processed successfully" (crash while shutting down) – the output is kept and verified.`);
                resolve({ ok: true, code, signal, log: tail });
                return;
            }
            const c = classifyError(code, signal, tail);
            resolve({ ok: false, code, signal, error: c.message, kind: c.kind, log: tail });
        });
    });
    return {
        pid: p.pid,
        // whole process tree: Video2X may have helper processes (AppImage runtime)
        kill: () => { killed = true; killTree(p.pid); if (!IS_WIN) try { p.kill('SIGKILL'); } catch { /* gone */ } },
        done
    };
}
