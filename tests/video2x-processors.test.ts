// v1.0.8: Real-ESRGAN / Real-CUGAN through Video2X – adapter, capability parsing, persistence and the process runner.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import os from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { DEFAULT_EXPORT, type ExportSettings } from '../shared/types';
import { DEFAULT_VIDEO2X, resolveUpscaleProvider, video2xOptions, video2xProcessor } from '../shared/upscale-provider';
import { buildArgs, classifyError, parseHelp } from '../shared/video2x-cli';
import { NEURAL_MODELS, buildNeuralArgs, comboKey, modelFiles, neuralComboProblem, parseNeuralCaps } from '../shared/video2x-processors';
import { type Video2xLauncher, execute, runQuick } from '../electron/video2x-process';

const mock = join(process.cwd(), 'tests', 'fixtures', 'video2x-mock', 'video2x-mock.mjs');
// git on Windows runners checks the fixture out with CRLF line endings
const src = readFileSync(mock, 'utf8').replace(/\r\n/g, '\n');
const HELP = src.slice(src.indexOf('const HELP = `') + 14, src.indexOf('`;', src.indexOf('const HELP = `')));
const caps = parseHelp(HELP, 'Video2X version 6.4.0');
const neural = parseNeuralCaps(HELP, caps);
const L = (env: Record<string, string> = {}): Video2xLauncher => ({ exe: process.execPath, prefix: [mock], env });
const dir = mkdtempSync(join(os.tmpdir(), 'uve-v2x-proc-'));
process.on('exit', () => rmSync(dir, { recursive: true, force: true }));
const run = (o: Partial<Parameters<typeof buildNeuralArgs>[2]> = {}) => buildNeuralArgs(caps, neural, {
    input: 'C:\\Videos\\a.uvestage1.mkv', output: 'C:\\Videos\\a.uvestage2.mkv', processor: 'realesrgan', modelId: 'realesr-animevideov3', scale: 2, device: -1, lossless: 'ffv1', ...o
});

/* ------------------------------------------------------------------ capabilities */

test('capabilities of the pinned 6.4.0 help text', () => {
    assert.equal(neural.realesrgan.available, true, neural.realesrgan.problem);
    assert.deepEqual(neural.realesrgan.models.map(m => m.id), ['realesr-animevideov3', 'realesrgan-plus-anime', 'realesrgan-plus']);
    assert.equal(neural.realcugan.available, true, neural.realcugan.problem);
    assert.deepEqual(neural.realcugan.models.map(m => m.id), ['models-se', 'models-pro', 'models-nose']);
    assert.equal(neural.realcugan.noise, true);
});

test('missing model / noise flags or processors make only that processor unavailable', () => {
    const noNoise = HELP.replace(/-n \[ --noise-level \][^\n]*\n/, '');
    const c1 = parseHelp(noNoise, 'Video2X version 6.4.0');
    const n1 = parseNeuralCaps(noNoise, c1);
    assert.equal(c1.versionSupported, true, 'Anime4K stays usable');
    assert.equal(n1.realesrgan.available, true);
    assert.equal(n1.realcugan.available, false);
    assert.match(n1.realcugan.problem, /--noise-level/);
    const noCugan = HELP.replace(/RealCUGAN options:[\s\S]*?\n\n/, '').replace('realesrgan, realcugan, rife', 'realesrgan, rife');
    const n2 = parseNeuralCaps(noCugan, parseHelp(noCugan, 'Video2X version 6.4.0'));
    assert.equal(n2.realcugan.available, false);
    const unknownModels = HELP.replace('(realesr-animevideov3,\n                                        realesrgan-plus-anime, realesrgan-plus)', '(foo-x, bar-y)');
    const n3 = parseNeuralCaps(unknownModels, caps);
    assert.equal(n3.realesrgan.available, false);
    assert.match(n3.realesrgan.problem, /foo-x, bar-y/);
    const old = parseNeuralCaps(HELP, parseHelp(HELP, 'Video2X version 6.0.0'));
    assert.equal(old.realesrgan.available, false);
    assert.match(old.realesrgan.problem, /too old/);
});

/* ------------------------------------------------------------------ combinations */

test('model / scale / noise combinations follow the 6.4.0 model files', () => {
    assert.equal(neuralComboProblem('realesrgan', 'realesr-animevideov3', 3, undefined), '');
    assert.match(neuralComboProblem('realesrgan', 'realesrgan-plus', 2, undefined), /only 4×/);
    assert.equal(neuralComboProblem('realcugan', 'models-se', 2, 2), '');
    assert.match(neuralComboProblem('realcugan', 'models-se', 4, 1), /noise levels No denoise, Denoise 3×/);
    // the 6.4.0 CLI rejects -n -1 while parsing ("the argument for option 'noise-level' is invalid")
    for (const m of NEURAL_MODELS.realcugan) for (const ns of Object.values(m.scales)) assert.ok(ns.every(n => n >= 0), `${m.id} offers no negative noise level`);
    assert.match(neuralComboProblem('realcugan', 'models-se', 2, -1), /noise levels/);
    assert.match(neuralComboProblem('realcugan', 'models-pro', 4, 0), /2× \/ 3×/);
    assert.match(neuralComboProblem('realcugan', 'models-nose', 2, -1), /noise/);
    assert.match(neuralComboProblem('realesrgan', 'nope', 2, undefined), /Unknown/);
    // every table entry maps to the file names the 6.4.0 filters load
    assert.deepEqual(modelFiles('realcugan', 'models-se', 3, -1), ['models/realcugan/models-se/up3x-conservative.param', 'models/realcugan/models-se/up3x-conservative.bin']);
    assert.deepEqual(modelFiles('realesrgan', 'realesrgan-plus-anime', 4, 0)[0], 'models/realesrgan/realesrgan-plus-anime-x4.param');
    let combos = 0;
    for (const p of ['realesrgan', 'realcugan'] as const) for (const m of NEURAL_MODELS[p]) for (const [sc, ns] of Object.entries(m.scales)) for (const n of ns) {
        assert.ok(src.includes(modelFiles(p, m.id, Number(sc), n)[0].replace(/^models\//, '').replace(/\.param$/, '').split('/').pop()!), `${p} ${m.id} ${sc} ${n} in the mock table`);
        combos++;
    }
    assert.equal(combos, 3 + 1 + 1 + 8 + 4 + 1);
});

/* ------------------------------------------------------------------ argv */

test('argv snapshots for Real-ESRGAN and Real-CUGAN', () => {
    assert.deepEqual(run(), ['-i', 'C:\\Videos\\a.uvestage1.mkv', '-o', 'C:\\Videos\\a.uvestage2.mkv', '-p', 'realesrgan', '-s', '2', '--realesrgan-model', 'realesr-animevideov3',
        '-c', 'ffv1', '--pix-fmt', 'gbrp', '--no-copy-streams', '--log-level', 'info']);
    assert.deepEqual(run({ processor: 'realcugan', modelId: 'models-se', scale: 3, noiseLevel: 0, device: 1, lossless: 'x264rgb' }),
        ['-i', 'C:\\Videos\\a.uvestage1.mkv', '-o', 'C:\\Videos\\a.uvestage2.mkv', '-p', 'realcugan', '-s', '3', '-n', '0', '--realcugan-model', 'models-se',
            '-c', 'libx264rgb', '-e', 'qp=0', '-e', 'preset=ultrafast', '--no-copy-streams', '-d', '1', '--log-level', 'info']);
    // never --libplacebo-shader, never -w / -h (the model scale is fixed)
    for (const a of [run(), run({ processor: 'realcugan', modelId: 'models-pro', scale: 2, noiseLevel: 3 })]) {
        assert.ok(!a.includes('--libplacebo-shader') && !a.includes('-w') && !a.includes('-h'));
    }
});

test('invalid scale, noise, model and paths are rejected before rendering', () => {
    assert.throws(() => run({ scale: 5 }), /supports only/);
    assert.throws(() => run({ modelId: 'realesrgan-plus', scale: 2 }), /only 4×/);
    assert.throws(() => run({ processor: 'realcugan', modelId: 'models-se', scale: 4, noiseLevel: 2 }), /noise levels/);
    assert.throws(() => run({ modelId: 'models-se' }), /not available in this Video2X build/);
    assert.throws(() => run({ modelId: '--libplacebo-shader=evil' }), /not available/);
    assert.throws(() => run({ modelId: 'realesr-animevideov3 -o x' }), /not available/);
    assert.throws(() => run({ processor: 'rife' as never }), /Unknown Video2X processor/);
    assert.throws(() => run({ input: '-i' }), /absolute/);
    assert.throws(() => run({ output: 'a\u0000b' }), /Invalid output/);
    const odd = 'C:\\Ü ber "quoted" & | ;\\日本 clip.mkv';
    assert.equal(run({ input: odd })[1], odd);
    const noNoise = HELP.replace(/-n \[ --noise-level \][^\n]*\n/, '');
    assert.throws(() => buildNeuralArgs(parseHelp(noNoise, 'Video2X version 6.4.0'), parseNeuralCaps(noNoise, parseHelp(noNoise, 'Video2X version 6.4.0')),
        { input: '/a', output: '/b', processor: 'realcugan', modelId: 'models-se', scale: 2, noiseLevel: 0, device: -1, lossless: 'ffv1' }), /--noise-level/);
});

test('Anime4K argv is unchanged by the extension', () => {
    assert.deepEqual(buildArgs(caps, { input: '/a.mkv', output: '/b.mkv', shader: 'anime4k-v4-a', width: 3840, height: 2160, device: -1, lossless: 'ffv1' }),
        ['-i', '/a.mkv', '-o', '/b.mkv', '-p', 'libplacebo', '--libplacebo-shader', 'anime4k-v4-a', '-w', '3840', '-h', '2160', '-c', 'ffv1', '--pix-fmt', 'gbrp', '--no-copy-streams', '--log-level', 'info']);
});

/* ------------------------------------------------------------------ persistence */

const ex = (p: Partial<ExportSettings>): ExportSettings => ({ ...DEFAULT_EXPORT, ...p });

test('1.0.7 settings without processor keep the identical Anime4K options', () => {
    const v107 = { presetId: 'custom:abc123', scale: 3, device: 1 };
    const o = video2xOptions(ex({ upscaleProvider: 'video2x', video2x: v107 }));
    assert.deepEqual(o, v107, 'no new keys appear');
    assert.equal(video2xProcessor(o), 'libplacebo');
    assert.deepEqual(video2xOptions(ex({})), DEFAULT_VIDEO2X);
    assert.equal(resolveUpscaleProvider(ex({ aiUpscale: 'janai-v2' })), 'rve', 'RVE projects unchanged');
});

test('neural options round-trip; unknown values fall back to safe defaults', () => {
    const o = { presetId: 'builtin:anime4k-v4-a+a', scale: 4, device: -1, processor: 'realcugan' as const, modelId: 'models-se', noiseLevel: 3, postResize: true };
    const back = video2xOptions(JSON.parse(JSON.stringify(ex({ upscaleProvider: 'video2x', video2x: o }))));
    assert.deepEqual(back, o);
    assert.equal(back.presetId, 'builtin:anime4k-v4-a+a', 'the Anime4K preset is kept while another processor is active');
    // a -1 stored by 1.0.8 / 1.0.9 is dropped (the CLI rejects it) → default noise level
    assert.equal(video2xOptions(ex({ video2x: { presetId: 'builtin:anime4k-v4-a', scale: 2, device: -1, processor: 'realcugan', modelId: 'models-pro', noiseLevel: -1 } })).noiseLevel, undefined);
    const bad = video2xOptions(ex({ video2x: { presetId: 'builtin:anime4k-v4-a', scale: 2, device: -1, processor: 'rife' as never, modelId: '../x y', noiseLevel: 9, postResize: 'yes' as never } }));
    assert.deepEqual(bad, { presetId: 'builtin:anime4k-v4-a', scale: 2, device: -1 });
    assert.equal(video2xProcessor(bad), 'libplacebo');
    assert.equal(comboKey('realesrgan', 'realesr-animevideov3', 2, 3, -1), 'realesrgan|realesr-animevideov3|2|-|-1');
});

/* ------------------------------------------------------------------ errors */

test('neural failures get specific messages', () => {
    const oom = classifyError(1, null, '[error] vkAllocateMemory failed: VK_ERROR_OUT_OF_DEVICE_MEMORY');
    assert.equal(oom.kind, 'oom');
    assert.match(oom.message, /smaller scale, another model or another GPU/);
    const args = classifyError(4294967295, null, "[critical] Error parsing arguments: the argument for option 'noise-level' is invalid");
    assert.equal(args.kind, 'arguments', 'an argument error is not a crash');
    assert.doesNotMatch(args.message, /crashed/);
    const mdl = classifyError(1, null, '[error] Real-ESRGAN model param file not found: models/realesrgan/x-x2.param');
    assert.equal(mdl.kind, 'model');
    assert.equal(classifyError(1, null, '[error] Failed to create Vulkan instance: VK_ERROR_INCOMPATIBLE_DRIVER').kind, 'vulkan');
});

/* ------------------------------------------------------------------ mock process */

const neuralRun = (o: Partial<Parameters<typeof buildNeuralArgs>[2]> = {}) =>
    run({ input: join(dir, 'in.mkv'), output: join(dir, `out-${Math.random().toString(36).slice(2)}.mkv`), ...o });

test('mock: Real-ESRGAN and Real-CUGAN runs succeed with monotonic progress', async () => {
    for (const a of [neuralRun(), neuralRun({ processor: 'realcugan', modelId: 'models-se', scale: 4, noiseLevel: 3 })]) {
        const seen: number[] = [];
        const r = await execute(L(), a, { onProgress: p => seen.push(p.frames) }).done;
        assert.equal(r.ok, true, r.error);
        assert.equal(seen[seen.length - 1], 10);
        assert.ok(seen.every((v, i) => !i || v >= seen[i - 1]));
        assert.match(readFileSync(a[3], 'utf8'), /^mock (realesrgan|realcugan) /);
    }
});

test('mock: missing model files, out of memory, crash and no Vulkan device', async () => {
    const miss = await execute(L({ VIDEO2X_MOCK_NOMODELS: 'realesrgan/realesr-animevideov3-x2' }), neuralRun(), {}).done;
    assert.equal(miss.ok, false); assert.equal(miss.kind, 'model');
    const oom = await execute(L({ VIDEO2X_MOCK_FAIL: 'oom' }), neuralRun({ scale: 4 }), {}).done;
    assert.equal(oom.kind, 'oom');
    const crash = await execute(L({ VIDEO2X_MOCK_FAIL: 'crash' }), neuralRun(), {}).done;
    assert.equal(crash.ok, false);
    const vk = await execute(L({ VIDEO2X_MOCK_FAIL: 'vulkan' }), neuralRun({ processor: 'realcugan', modelId: 'models-pro', scale: 3, noiseLevel: 0 }), {}).done;
    assert.equal(vk.kind, 'vulkan');
    const nodev = await runQuick(L({ VIDEO2X_MOCK_NODEVICES: '1' }), ['--list-devices']);
    assert.equal(nodev.out, '');
});

test('mock: cancel and timeout leave no process behind', async () => {
    const out = neuralRun();
    const r = execute(L({ VIDEO2X_MOCK_SLEEP: '50', VIDEO2X_MOCK_FRAMES: '200' }), out, {});
    await new Promise(res => setTimeout(res, 500));
    r.kill();
    const res = await r.done;
    assert.equal(res.error, 'canceled');
    await new Promise(res2 => setTimeout(res2, 300));
    assert.throws(() => process.kill(r.pid!, 0));
    const t0 = Date.now();
    const q = await runQuick(L({ VIDEO2X_MOCK_HANG: '1' }), ['--version'], 1500);
    assert.equal(q.timedOut, true);
    assert.ok(Date.now() - t0 < 10000);
});
