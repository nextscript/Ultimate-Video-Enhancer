import assert from 'node:assert/strict';
import { test } from 'node:test';
import { SHADER_LIMITS, checkChain, checkShader, combineChain, decodeShaderBytes } from '../shared/video2x-shaders';

// shape of an Anime4K v4 pass file (two passes: the second binds what the first saves)
const A = `// Anime4K-v4.0-Restore-CNN-S
//!DESC Anime4K-v4.0-Restore-CNN-(S)-Conv-4x3x3x3
//!HOOK MAIN
//!BIND MAIN
//!SAVE conv2d_tf
//!WIDTH MAIN.w
//!HEIGHT MAIN.h
//!COMPONENTS 4
//!WHEN OUTPUT.w MAIN.w / 1.200 > OUTPUT.h MAIN.h / 1.200 > *
#define go_0(x_off, y_off) (max((MAIN_texOff(vec2(x_off, y_off))), 0.0))
vec4 hook() {
    return go_0(0.0, 0.0);
}
//!DESC Anime4K-v4.0-Restore-CNN-(S)-Conv-4x3x3x8
//!HOOK MAIN
//!BIND conv2d_tf
//!SAVE conv2d_1_tf
//!COMPONENTS 4
vec4 hook() {
    return conv2d_tf_tex(conv2d_tf_pos);
}
`;
const B = `//!DESC Anime4K-v4.0-Restore-CNN-(S)-Depth-to-Space
//!HOOK MAIN
//!BIND MAIN
//!BIND conv2d_1_tf
vec4 hook() {
    return MAIN_tex(MAIN_pos) + conv2d_1_tf_tex(conv2d_1_tf_pos);
}
`;
const TEX = `//!TEXTURE LUT
//!SIZE 2 1
//!FORMAT rgba8
//!FILTER LINEAR
ff000000ff000000
//!HOOK MAIN
//!BIND HOOKED
//!BIND LUT
vec4 hook() { return HOOKED_tex(HOOKED_pos) * LUT_tex(vec2(0.5)); }
`;

test('Anime4K-style pass file is accepted with its passes', () => {
    const c = checkShader(A);
    assert.equal(c.ok, true, JSON.stringify(c.errors));
    assert.equal(c.passes.length, 2);
    assert.deepEqual(c.passes.map(p => p.save), ['conv2d_tf', 'conv2d_1_tf']);
    assert.equal(c.passes[0].desc, 'Anime4K-v4.0-Restore-CNN-(S)-Conv-4x3x3x3');
});

test('textures declared in the file satisfy BIND', () => {
    const c = checkShader(TEX);
    assert.equal(c.ok, true, JSON.stringify(c.errors));
    assert.deepEqual(c.textures, ['LUT']);
});

test('missing dependency: a pass binds a texture nothing saves', () => {
    const c = checkShader(B);
    assert.equal(c.ok, false);
    assert.match(c.errors[0].message, /Missing dependency.*conv2d_1_tf/);
});

test('unknown / unsupported directives are reported with their line', () => {
    const c = checkShader(A.replace('//!COMPONENTS 4\n//!WHEN', '//!FOOBAR 1\n//!WHEN'));
    assert.equal(c.ok, false);
    assert.equal(c.errors[0].message, 'Unknown directive //!FOOBAR');
    assert.equal(c.errors[0].line, 8);
    assert.equal(checkShader(A.replace('//!COMPONENTS 4', '//!COMPONENTS 9')).ok, false);
});

test('non-shader content is rejected (.glsl extension is not proof)', () => {
    assert.equal(checkShader('#!/bin/sh\nrm -rf ~\n').ok, false);
    assert.equal(checkShader('void main() { gl_FragColor = vec4(1.0); }').ok, false);
    assert.equal(checkShader('<html><script>alert(1)</script></html>').ok, false);
    assert.throws(() => decodeShaderBytes('x.glsl', new Uint8Array([0x2f, 0x2f, 0, 1])), /binary/);
    assert.throws(() => decodeShaderBytes('x.glsl', new Uint8Array([0xff, 0xfe, 0xfd])), /UTF-8/);
    assert.throws(() => decodeShaderBytes('x.glsl', new Uint8Array(SHADER_LIMITS.maxFileBytes + 1)), /larger/);
    assert.equal(decodeShaderBytes('x.glsl', new TextEncoder().encode('﻿//!HOOK MAIN')), '//!HOOK MAIN');
});

test('chain order matters: B works only after A', () => {
    assert.equal(checkChain([{ name: 'a.glsl', text: A, enabled: true }, { name: 'b.glsl', text: B, enabled: true }]).ok, true);
    const wrong = checkChain([{ name: 'b.glsl', text: B, enabled: true }, { name: 'a.glsl', text: A, enabled: true }]);
    assert.equal(wrong.ok, false);
    assert.match(wrong.error, /^b\.glsl, line 1: Missing dependency/);
    // disabling A breaks B; disabling B is fine
    assert.equal(checkChain([{ name: 'a.glsl', text: A, enabled: false }, { name: 'b.glsl', text: B, enabled: true }]).ok, false);
    const onlyA = checkChain([{ name: 'a.glsl', text: A, enabled: true }, { name: 'b.glsl', text: B, enabled: false }]);
    assert.equal(onlyA.ok, true);
    assert.equal(onlyA.passes, 2);
    assert.equal(checkChain([{ name: 'a.glsl', text: A, enabled: false }]).error, 'The preset has no enabled shader.');
});

test('combined chain keeps order, skips disabled passes and still validates', () => {
    const files = [{ name: 'a.glsl', text: A, enabled: true }, { name: 'skip.glsl', text: TEX, enabled: false }, { name: 'b.glsl', text: B, enabled: true }];
    const text = combineChain(files);
    assert.ok(text.indexOf('Conv-4x3x3x3') < text.indexOf('Depth-to-Space'));
    assert.ok(!text.includes('//!TEXTURE LUT'));
    assert.match(text, /pass file 1: a\.glsl[\s\S]*pass file 2: b\.glsl/);
    const c = checkShader(text);
    assert.equal(c.ok, true, JSON.stringify(c.errors));
    assert.equal(c.passes.length, 3);
});

test('chain limits', () => {
    const many = Array.from({ length: SHADER_LIMITS.maxFiles + 1 }, (_, i) => ({ name: `${i}.glsl`, text: TEX, enabled: true }));
    assert.match(checkChain(many).error, /at most/);
});
