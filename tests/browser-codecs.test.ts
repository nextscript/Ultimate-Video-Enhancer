import assert from 'node:assert/strict';
import { test } from 'node:test';
import { browserAudioOk, needsAudioProxy, videoCopyable } from '../shared/browser-codecs';
import type { MediaInfo } from '../shared/types';

const media = (video: string | null, audio: string[]): MediaInfo => ({
    video: video ? { codec: video } : null,
    audio: audio.map((codec, i) => ({ index: i + 1, codec, channels: 6 }))
} as unknown as MediaInfo);

test('Chromium-decodable audio codecs need no audio proxy', () => {
    for (const c of ['aac', 'mp3', 'opus', 'vorbis', 'flac', 'AAC']) assert.equal(browserAudioOk(c), true, c);
    assert.equal(needsAudioProxy(media('h264', ['aac'])), false);
    assert.equal(needsAudioProxy(media('hevc', [])), false);
});

test('MKV sound in AC3 / E-AC3 / DTS / TrueHD gets an audio proxy', () => {
    for (const c of ['ac3', 'eac3', 'dts', 'truehd']) assert.equal(needsAudioProxy(media('hevc', [c])), true, c);
    // only the first stream is previewed (same as the mixer and the export default)
    assert.equal(needsAudioProxy(media('hevc', ['aac', 'ac3'])), false);
    assert.equal(needsAudioProxy(media(null, ['dts'])), true);
});

test('audio proxy copies Chromium video codecs, other pictures fall back to the full proxy', () => {
    for (const c of ['h264', 'hevc', 'av1', 'vp9']) assert.equal(videoCopyable(media(c, ['ac3'])), true, c);
    assert.equal(videoCopyable(media('mpeg2video', ['ac3'])), false);
    assert.equal(videoCopyable(media(null, ['ac3'])), true);
});
