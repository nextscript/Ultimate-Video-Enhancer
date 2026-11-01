// Drives the real process adapter (electron/video2x-process.ts) against the mock Video2X executable.
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { buildArgs, parseHelp } from '../shared/video2x-cli';
import { type Video2xLauncher, execute, runQuick } from '../electron/video2x-process';

const mock = join(process.cwd(), 'tests', 'fixtures', 'video2x-mock', 'video2x-mock.mjs');
const L = (env: Record<string, string> = {}): Video2xLauncher => ({ exe: process.execPath, prefix: [mock], env });
const dir = mkdtempSync(join(os.tmpdir(), 'uve-v2x-test-'));
process.on('exit', () => rmSync(dir, { recursive: true, force: true }));

async function capsOf() {
    const h = await runQuick(L(), ['--help']);
    const v = await runQuick(L(), ['--version']);
    // like the real binary: --help / --version exit with code 1
    assert.equal(h.code, 1);
    return parseHelp(h.out + h.err, v.out + v.err);
}

test('probe through the process adapter', async () => {
    const caps = await capsOf();
    assert.equal(caps.version, '6.4.0');
    assert.equal(caps.versionSupported, true);
    const d = await runQuick(L(), ['--list-devices']);
    assert.match(d.out, /0\. Mock Vulkan GPU/);
    const none = await runQuick(L({ VIDEO2X_MOCK_NODEVICES: '1' }), ['--list-devices']);
    assert.equal(none.out, '');
    assert.match(none.err, /Vulkan/);
});

test('successful run: progress is parsed and monotonic, output written', async () => {
    const caps = await capsOf();
    const out = join(dir, 'out ü.mkv');
    const args = buildArgs(caps, { input: join(dir, 'in.mkv'), output: out, shader: 'anime4k-v4-a+a', width: 256, height: 144, device: 0, lossless: 'ffv1' });
    writeFileSync(join(dir, 'in.mkv'), 'x');
    const seen: number[] = [];
    const logs: string[] = [];
    const r = execute(L({ VIDEO2X_MOCK_FRAMES: '25' }), args, { onProgress: p => seen.push(p.frames), onLog: l => logs.push(l) });
    const res = await r.done;
    assert.equal(res.ok, true, res.error);
    assert.equal(seen[seen.length - 1], 25);
    assert.ok(seen.every((v, i) => i === 0 || v >= seen[i - 1]), 'progress is monotonic');
    assert.ok(logs.some(l => /Video processed successfully/.test(l)));
    assert.equal(readFileSync(out, 'utf8'), 'mock 256x144 ffv1 anime4k-v4-a+a');
});

test('arguments arrive verbatim (shell: false) – spaces, Unicode, quotes, shell metacharacters', async () => {
    const caps = await capsOf();
    const argvFile = join(dir, 'argv.json');
    const weird = join(dir, `clip "final" & | ; $(whoami) %PATH% 日本語 ${'x'.repeat(120)}.mkv`);
    const args = buildArgs(caps, { input: weird, output: join(dir, 'o.mkv'), shader: 'anime4k-v4-a', width: 2, height: 2, device: -1, lossless: 'ffv1' });
    const res = await execute(L({ VIDEO2X_MOCK_ARGV: argvFile }), args, {}).done;
    assert.equal(res.ok, true, res.error);
    assert.deepEqual(JSON.parse(readFileSync(argvFile, 'utf8')), args);
});

test('failures are classified (Vulkan, shader, crash)', async () => {
    const caps = await capsOf();
    const args = buildArgs(caps, { input: join(dir, 'in.mkv'), output: join(dir, 'f.mkv'), shader: 'anime4k-v4-a', width: 2, height: 2, device: -1, lossless: 'ffv1' });
    const v = await execute(L({ VIDEO2X_MOCK_FAIL: 'vulkan' }), args, {}).done;
    assert.equal(v.ok, false); assert.equal(v.kind, 'vulkan'); assert.match(v.error!, /Vulkan is not usable/);
    const s = await execute(L({ VIDEO2X_MOCK_FAIL: 'shader' }), args, {}).done;
    assert.equal(s.kind, 'shader');
    const c = await execute(L({ VIDEO2X_MOCK_FAIL: 'crash' }), args, {}).done;
    assert.equal(c.ok, false); assert.match(c.error!, /exit code 3/);
    assert.equal(existsSync(join(dir, 'f.mkv')), false, 'no output on failure');
});

test('cancel terminates the process tree; no output, no orphan', async () => {
    const caps = await capsOf();
    const out = join(dir, 'cancel.mkv');
    const args = buildArgs(caps, { input: join(dir, 'in.mkv'), output: out, shader: 'anime4k-v4-a', width: 2, height: 2, device: -1, lossless: 'ffv1' });
    let frames = 0;
    const r = execute(L({ VIDEO2X_MOCK_FRAMES: '200', VIDEO2X_MOCK_SLEEP: '50' }), args, { onProgress: p => { frames = p.frames; } });
    await new Promise(res => setTimeout(res, 600));
    r.kill();
    const t0 = Date.now();
    const res = await r.done;
    assert.ok(Date.now() - t0 < 5000, 'process ended promptly');
    assert.equal(res.ok, false);
    assert.equal(res.error, 'canceled');
    assert.ok(frames < 200);
    assert.equal(existsSync(out), false);
    // the pid must be gone
    await new Promise(res2 => setTimeout(res2, 300));
    let alive = true;
    try { process.kill(r.pid!, 0); } catch { alive = false; }
    assert.equal(alive, false);
});

test('a missing executable reports a start error instead of hanging', async () => {
    const res = await execute({ exe: join(dir, 'does-not-exist.exe'), prefix: [] }, ['--help'], {}).done;
    assert.equal(res.ok, false);
    assert.match(res.error!, /could not be started/);
    const q = await runQuick({ exe: join(dir, 'nope'), prefix: [] }, ['--version']);
    assert.ok(q.spawnError);
});
