import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import { DEFAULT_EXPORT, type ExportSettings } from '../shared/types';
import { DEFAULT_VIDEO2X, providerPatch, resolveUpscaleProvider, video2xOptions } from '../shared/upscale-provider';
import {
    VIDEO2X_COMPAT, buildArgs, classifyError, cmpVersion, estimateTempBytes, isFilterSafePath, parseDevices, parseHelp, parseProgress, parseVersion, video2xPlatform
} from '../shared/video2x-cli';

// the mock's help text is a copy of the 6.4.0 help layout (boost::program_options)
const mockSrc = readFileSync(join(process.cwd(), 'tests', 'fixtures', 'video2x-mock', 'video2x-mock.mjs'), 'utf8');
const HELP = mockSrc.slice(mockSrc.indexOf('const HELP = `') + 14, mockSrc.indexOf('`;', mockSrc.indexOf('const HELP = `')));
const caps = parseHelp(HELP, 'Video2X version 6.4.0');
const ex = (p: Partial<ExportSettings>): ExportSettings => ({ ...DEFAULT_EXPORT, ...p });

/* ------------------------------------------------------------------ provider discrimination / migration */

test('legacy settings keep their meaning: no provider + model = RVE, no model = off', () => {
    assert.equal(resolveUpscaleProvider(ex({ aiUpscale: '' })), 'off');
    assert.equal(resolveUpscaleProvider(ex({ aiUpscale: 'janai-v2' })), 'rve');
    // a 1.0.6 project merged over 1.0.7 defaults: DEFAULT_EXPORT must not carry a provider
    assert.equal('upscaleProvider' in DEFAULT_EXPORT, false);
    const old = JSON.parse(JSON.stringify({ aiUpscale: 'nomos-weak', resolution: 'source' }));
    assert.equal(resolveUpscaleProvider({ ...DEFAULT_EXPORT, ...old }), 'rve');
});

test('an absent or unknown provider is never Video2X', () => {
    assert.equal(resolveUpscaleProvider(ex({ aiUpscale: '', video2x: { ...DEFAULT_VIDEO2X } })), 'off');
    assert.equal(resolveUpscaleProvider(ex({ aiUpscale: '', upscaleProvider: 'bogus' as never })), 'off');
    assert.equal(resolveUpscaleProvider(ex({ aiUpscale: 'janai-v2', upscaleProvider: 'video2x' })), 'video2x');
    assert.equal(resolveUpscaleProvider(ex({ aiUpscale: 'janai-v2', upscaleProvider: 'off' })), 'off');
    assert.equal(resolveUpscaleProvider(ex({ aiUpscale: '', upscaleProvider: 'rve' })), 'off');
});

test('provider switch only touches upscale fields and survives a JSON round trip', () => {
    const s = ex({ aiUpscale: 'janai-v2', resolution: 'x2', quality: 17, container: 'mkv' });
    const v = { ...s, ...providerPatch(s, 'video2x') };
    assert.equal(v.upscaleProvider, 'video2x');
    assert.equal(v.aiUpscale, 'janai-v2', 'RVE model is kept for switching back');
    assert.equal(v.resolution, 'source');
    assert.equal(v.quality, 17); assert.equal(v.container, 'mkv');
    assert.deepEqual(v.video2x, DEFAULT_VIDEO2X);
    const back = JSON.parse(JSON.stringify(v)) as ExportSettings;
    assert.equal(resolveUpscaleProvider(back), 'video2x');
    const off = { ...back, ...providerPatch(back, 'off') };
    assert.equal(resolveUpscaleProvider(off), 'off');
    assert.equal(off.aiUpscale, '');
    assert.equal(resolveUpscaleProvider({ ...off, ...providerPatch(off, 'rve'), aiUpscale: 'janai-v2' }), 'rve');
});

test('video2x options are sanitized', () => {
    assert.deepEqual(video2xOptions(ex({})), DEFAULT_VIDEO2X);
    assert.deepEqual(video2xOptions(ex({ video2x: { presetId: 'builtin:anime4k-v4-a+a', scale: 4, device: 1 } })), { presetId: 'builtin:anime4k-v4-a+a', scale: 4, device: 1 });
    assert.deepEqual(video2xOptions(ex({ video2x: { presetId: '../../etc', scale: 7, device: -5 } as never })), DEFAULT_VIDEO2X);
});

/* ------------------------------------------------------------------ platform */

test('platform support: Windows / Linux x64 only, macOS disabled', () => {
    assert.equal(video2xPlatform('win32', 'x64').supported, true);
    assert.equal(video2xPlatform('linux', 'x64').supported, true);
    for (const a of ['arm64', 'x64']) {
        const m = video2xPlatform('darwin', a);
        assert.equal(m.supported, false);
        assert.match(m.reason, /macOS/);
    }
    assert.equal(video2xPlatform('win32', 'arm64').supported, false);
    for (const k of Object.keys(VIDEO2X_COMPAT.assets)) assert.match(VIDEO2X_COMPAT.assets[k].sha256, /^[0-9a-f]{64}$/);
});

/* ------------------------------------------------------------------ capability parser */

test('help parser finds options, processors and built-in shaders of 6.4.0', () => {
    assert.equal(caps.version, '6.4.0');
    assert.equal(caps.versionSupported, true, caps.problem);
    assert.deepEqual(caps.processors, ['libplacebo', 'realesrgan', 'realcugan', 'rife']);
    assert.deepEqual(caps.builtinShaders, ['anime4k-v4-a', 'anime4k-v4-a+a', 'anime4k-v4-b', 'anime4k-v4-b+b', 'anime4k-v4-c', 'anime4k-v4-c+a', 'anime4k-v4.1-gan']);
    assert.ok(caps.hasPixFmt && caps.hasDevice && caps.hasExtraEncoderOption && caps.hasListDevices && caps.hasLogLevel);
});

test('unsupported versions and builds are rejected with an upgrade path', () => {
    const old = parseHelp(HELP, 'Video2X version 6.0.0');
    assert.equal(old.versionSupported, false);
    assert.match(old.problem, /too old.*6\.4\.0/);
    const future = parseHelp(HELP, 'Video2X version 7.0.0');
    assert.equal(future.versionSupported, false);
    assert.match(future.problem, /newer/);
    const noShaderOpt = parseHelp(HELP.replace(/--libplacebo-shader/g, '--shader'), 'Video2X version 6.4.0');
    assert.equal(noShaderOpt.versionSupported, false);
    assert.match(noShaderOpt.problem, /--libplacebo-shader/);
    assert.equal(parseHelp('', '').versionSupported, false);
    assert.equal(parseVersion('Video2X version 6.4.0\n'), '6.4.0');
    assert.ok(cmpVersion('6.10.0', '6.4.0') > 0);
});

test('device list parser (Vulkan present / absent)', () => {
    const d = parseDevices('0. NVIDIA GeForce RTX 3080\n\tType: Discrete GPU\n\tVulkan API Version: 1.3.280\n1. Intel(R) UHD Graphics 770\n\tType: Integrated GPU\n');
    assert.deepEqual(d, [{ index: 0, name: 'NVIDIA GeForce RTX 3080', type: 'Discrete GPU' }, { index: 1, name: 'Intel(R) UHD Graphics 770', type: 'Integrated GPU' }]);
    assert.deepEqual(parseDevices('[error] Failed to enumerate Vulkan physical devices.'), []);
});

/* ------------------------------------------------------------------ argument assembly */

const run = (o: Partial<Parameters<typeof buildArgs>[1]> = {}) => buildArgs(caps, {
    input: 'C:\\Videos\\in.uvestage1.mkv', output: 'C:\\Videos\\out.uvestage2.mkv', shader: 'anime4k-v4-a', width: 3840, height: 2160, device: -1, lossless: 'ffv1', ...o
});

test('argv for a built-in preset', () => {
    assert.deepEqual(run(), ['-i', 'C:\\Videos\\in.uvestage1.mkv', '-o', 'C:\\Videos\\out.uvestage2.mkv', '-p', 'libplacebo', '--libplacebo-shader', 'anime4k-v4-a',
        '-w', '3840', '-h', '2160', '-c', 'ffv1', '--pix-fmt', 'gbrp', '--no-copy-streams', '--log-level', 'info']);
    assert.deepEqual(run({ device: 1, lossless: 'x264rgb' }).slice(12), ['-c', 'libx264rgb', '-e', 'qp=0', '-e', 'preset=ultrafast', '--no-copy-streams', '-d', '1', '--log-level', 'info']);
});

test('paths with spaces, Unicode, quotes and long Windows paths stay single argv entries', () => {
    const long = 'C:\\' + 'Sehr langer Ordner äöü 日本 '.repeat(12) + '\\clip "final" & co.mkv';
    const a = run({ input: long, output: '/home/ü ser/out put.mkv' });
    assert.equal(a[1], long);
    assert.equal(a[3], '/home/ü ser/out put.mkv');
    assert.equal(a.length, run().length);
});

test('malicious shader names and paths are rejected', () => {
    assert.throws(() => run({ shader: '"; rm -rf / #' }), /neither a built-in shader/);
    assert.throws(() => run({ shader: 'anime4k-v4-a\' :custom_shader_path=x' }), /neither a built-in/);
    assert.throws(() => run({ shader: "C:\\shaders\\it's.glsl" }), /cannot pass to libplacebo/);
    assert.throws(() => run({ shader: 'C:\\shaders\\a,b[x];y.glsl' }), /cannot pass to libplacebo/);
    assert.throws(() => run({ shader: 'C:\\shaders\\..\\x.glsl' }), /cannot pass/);
    assert.throws(() => run({ shader: 'C:\\shaders\\evil.sh' }), /\.glsl/);
    assert.throws(() => run({ input: '-o' }), /absolute/);
    assert.throws(() => run({ input: 'a\nb' }), /Invalid input/);
    assert.throws(() => run({ width: 3841 }), /even/);
    assert.ok(isFilterSafePath('C:\\Users\\Jürgen\\AppData\\Roaming\\UVE Manager\\video2x\\shaders\\chains\\ab12.glsl'));
    assert.deepEqual(run({ shader: '/home/me/.config/UVE Manager/video2x/shaders/chains/ab.glsl' })[7], '/home/me/.config/UVE Manager/video2x/shaders/chains/ab.glsl');
});

test('argv building refuses an unsupported binary', () => {
    assert.throws(() => buildArgs(parseHelp(HELP, 'Video2X version 5.0.0'), { input: '/a', output: '/b', shader: 'anime4k-v4-a', width: 2, height: 2, device: -1, lossless: 'ffv1' }), /too old/);
});

/* ------------------------------------------------------------------ progress / errors */

test('progress parser reads the 6.4.0 progress line', () => {
    const p = parseProgress('\r\u001b[Kframe=120/2400 (5.00%); fps=48.25; elapsed=00:00:02; remaining=00:00:47');
    assert.deepEqual(p, { frames: 120, total: 2400, percent: 5, fps: 48.25, remaining: 47 });
    assert.equal(parseProgress('[info] Processing file: x.mkv'), null);
    assert.equal(parseProgress('frame=1/0 (0.00%); fps=inf; elapsed=00:00:00; remaining=00:00:00')?.fps, 0);
});

test('error classification gives actionable messages', () => {
    assert.equal(classifyError(1, null, '[error] Failed to create Vulkan instance: VK_ERROR_INCOMPATIBLE_DRIVER').kind, 'vulkan');
    assert.equal(classifyError(1, null, '[error] libplacebo: Failed to parse custom shader').kind, 'shader');
    assert.equal(classifyError(2, null, '[warning] Video processing aborted').kind, 'canceled');
    assert.equal(classifyError(null, 'SIGSEGV', '').kind, 'crash');
    assert.equal(classifyError(3221225477, null, '').kind, 'crash');
    assert.match(classifyError(1, null, '[critical] boom').message, /exit code 1.*boom/);
});

test('temporary disk estimate grows with the output size', () => {
    const a = estimateTempBytes(1800, 1920, 1080, 3840, 2160), b = estimateTempBytes(1800, 1920, 1080, 1920 * 2, 1080 * 2);
    assert.equal(a, b);
    assert.ok(a > 1800 * 1920 * 1080 * 3 * 0.5);
});
