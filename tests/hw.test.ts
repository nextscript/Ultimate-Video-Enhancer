// v1.0.1: hardware candidate selection (pure logic; the actual encoders are verified at runtime by scratch encodes).
import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { CustomFilterEntry } from '../shared/uve/customFilters';
import { allHwEncoderNames, encoderTestArgs, familyOrder, hwaccelOrder, hwEncoderArgs, vendorOf } from '../shared/hw';
import { outputFps, parseFps } from '../shared/fps';
import { hasCustomGlsl } from '../src/engine/types';

test('vendorOf recognises the common GPU names', () => {
    assert.equal(vendorOf('NVIDIA GeForce RTX 5060 Ti'), 'nvidia');
    assert.equal(vendorOf('AMD Radeon RX 7800 XT'), 'amd');
    assert.equal(vendorOf('Intel(R) UHD Graphics 770'), 'intel');
    assert.equal(vendorOf('Intel(R) Arc(TM) A770 Graphics'), 'intel');
    assert.equal(vendorOf('Apple M2 Pro'), 'apple');
    assert.equal(vendorOf('Microsoft Basic Render Driver'), 'other');
});

test('familyOrder puts the primary vendor first and never offers foreign-platform APIs', () => {
    assert.deepEqual(familyOrder('win32', 'nvidia'), ['nvenc', 'amf', 'qsv']);
    assert.deepEqual(familyOrder('win32', 'amd'), ['amf', 'nvenc', 'qsv']);
    assert.deepEqual(familyOrder('win32', 'intel'), ['qsv', 'nvenc', 'amf']);
    assert.deepEqual(familyOrder('linux', 'amd'), ['vaapi', 'nvenc', 'qsv']);
    assert.deepEqual(familyOrder('darwin', 'apple'), ['videotoolbox']);
    assert.ok(!familyOrder('linux', 'other').includes('amf'));
    assert.ok(!familyOrder('win32', 'other').includes('videotoolbox'));
    assert.ok(!familyOrder('win32', 'other').includes('vaapi'));
});

test('hwaccelOrder only returns decoders FFmpeg reports as available', () => {
    assert.deepEqual(hwaccelOrder('win32', 'nvidia', ['cuda', 'dxva2', 'd3d11va', 'qsv']), ['cuda', 'd3d11va', 'dxva2']);
    assert.deepEqual(hwaccelOrder('win32', 'amd', ['dxva2']), ['dxva2']);
    assert.deepEqual(hwaccelOrder('linux', 'intel', ['vaapi']), ['vaapi']);
    assert.deepEqual(hwaccelOrder('darwin', 'apple', ['videotoolbox']), ['videotoolbox']);
    assert.deepEqual(hwaccelOrder('linux', 'nvidia', []), []);
});

test('hwEncoderArgs: 10-bit H.264 is rejected, 10-bit HEVC uses main10/p010', () => {
    for (const f of ['nvenc', 'qsv', 'amf', 'videotoolbox', 'vaapi'] as const) {
        assert.equal(hwEncoderArgs('h264', f, 20, 'medium', true), null, f);
        const c = hwEncoderArgs('hevc', f, 20, 'medium', true)!;
        assert.ok(c.args.includes('main10'), f);
        assert.match(c.pix, /^p010/, f);
        assert.equal(c.name, `hevc_${f}`);
        assert.equal(c.vaapi, f === 'vaapi');
    }
    const nv = hwEncoderArgs('h264', 'nvenc', 23, 'slow', false)!;
    assert.deepEqual(nv.args.slice(0, 4), ['-c:v', 'h264_nvenc', '-preset', 'p7']);
    assert.ok(nv.args.includes('23'));
    const vt = hwEncoderArgs('hevc', 'videotoolbox', 51, 'fast', false)!;
    const q = Number(vt.args[vt.args.indexOf('-q:v') + 1]);
    assert.ok(q >= 1 && q <= 100);
});

test('encoderTestArgs drops container tags and uploads frames for VA-API', () => {
    const nv = encoderTestArgs(hwEncoderArgs('hevc', 'nvenc', 20, 'medium', false)!, 1920, 1080);
    assert.ok(!nv.includes('-tag:v') && !nv.includes('hvc1'));
    assert.ok(nv.includes('testsrc2=s=1920x1080:d=0.2:r=10'));
    assert.deepEqual(nv.slice(-3), ['-f', 'null', '-']);
    const va = encoderTestArgs(hwEncoderArgs('h264', 'vaapi', 20, 'medium', false)!);
    assert.equal(va[va.indexOf('-vaapi_device') + 1], '/dev/dri/renderD128');
    assert.ok(va.includes('format=nv12,hwupload'));
    assert.equal(allHwEncoderNames().length, 15);
});

test('custom GLSL filters force the WebGL2 engine, disabled or SVG ones do not', () => {
    const f = (type: string, enabled: boolean) => ({ id: type, label: type, type, enabled, code: '', blendMode: 'normal', tags: [], category: '', uniforms: {} }) as unknown as CustomFilterEntry;
    assert.equal(hasCustomGlsl([f('webgl', true)]), true);
    assert.equal(hasCustomGlsl([f('webgl', false), f('svg', true)]), false);
    assert.equal(hasCustomGlsl([]), false);
});

test('output frame rate: parsing, NTSC rationals, limits and source fallback', () => {
    assert.deepEqual(parseFps('30'), { num: 30, den: 1 });
    assert.deepEqual(parseFps('29.97'), { num: 30000, den: 1001 });
    assert.deepEqual(parseFps('29,97'), { num: 30000, den: 1001 });
    assert.deepEqual(parseFps('23.976'), { num: 24000, den: 1001 });
    assert.deepEqual(parseFps('24000/1001'), { num: 24000, den: 1001 });
    assert.deepEqual(parseFps('12.5'), { num: 12500, den: 1000 });
    assert.equal(parseFps(''), null);
    assert.equal(parseFps('0'), null);
    assert.equal(parseFps('500'), null);
    assert.equal(parseFps('abc'), null);
    assert.equal(parseFps('30/0'), null);
    assert.deepEqual(outputFps('', 25, 1), { num: 25, den: 1 });
    assert.deepEqual(outputFps('bad', 30000, 1001), { num: 30000, den: 1001 });
    assert.deepEqual(outputFps('60', 25, 1), { num: 60, den: 1 });
});
