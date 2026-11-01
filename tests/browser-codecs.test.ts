import assert from 'node:assert/strict';
import { test } from 'node:test';
import { browserAudioOk, needsAudioSidecar } from '../shared/browser-codecs';
import { quickRenderSettings } from '../shared/quick-render';
import { DEFAULT_EXPORT, type MediaInfo } from '../shared/types';

const media = (video: string | null, audio: string[], path = 'C:/v/film.mkv', bitDepth = 8): MediaInfo => ({
    path,
    video: video ? { codec: video, bitDepth } : null,
    audio: audio.map((codec, i) => ({ index: i + 1, codec, channels: 6 }))
} as unknown as MediaInfo);

test('Chromium-decodable audio codecs play from the picture element', () => {
    for (const c of ['aac', 'mp3', 'opus', 'vorbis', 'flac', 'AAC']) assert.equal(browserAudioOk(c), true, c);
    assert.equal(needsAudioSidecar(media('h264', ['aac'])), false);
    assert.equal(needsAudioSidecar(media('hevc', [])), false);
});

test('MKV sound in AC3 / E-AC3 / DTS / TrueHD gets the live sound stream', () => {
    for (const c of ['ac3', 'eac3', 'dts', 'truehd']) assert.equal(needsAudioSidecar(media('hevc', [c])), true, c);
    // only the first stream is previewed (same as the mixer and the export default)
    assert.equal(needsAudioSidecar(media('hevc', ['aac', 'ac3'])), false);
    assert.equal(needsAudioSidecar(media(null, ['dts'])), true);
});

test('Sofort-Render keeps the source: container, codec, bit depth, every audio + subtitle stream', () => {
    const base = { ...DEFAULT_EXPORT, outputDir: 'D:/out', fileName: '{name}_X', resolution: '720' as const, fps: '30', aiUpscale: 'nomos-weak', upscaleProvider: 'rve' as const, quality: 28 };
    const s = quickRenderSettings(media('hevc', ['ac3', 'ac3'], 'C:/v/film.mkv', 10), base);
    assert.equal(s.container, 'mkv');
    assert.equal(s.videoCodec, 'hevc');
    assert.equal(s.tenBit, true);
    assert.equal(s.resolution, 'source');
    assert.equal(s.fps, '');
    assert.equal(s.upscaleProvider, 'off');
    assert.equal(s.aiUpscale, '');
    assert.equal(s.audio, 'copy');
    assert.equal(s.allAudio, true);
    assert.equal(s.subtitles, 'copy');
    assert.equal(s.quality, 16);
    assert.equal(s.useInOut, false);
    // the user's output folder and file-name pattern stay
    assert.equal(s.outputDir, 'D:/out');
    assert.equal(s.fileName, '{name}_X');
});

test('Sofort-Render: other containers and codecs', () => {
    const a = quickRenderSettings(media('h264', ['aac'], 'C:/v/clip.MP4'), DEFAULT_EXPORT);
    assert.deepEqual([a.container, a.videoCodec, a.tenBit], ['mp4', 'h264', false]);
    const b = quickRenderSettings(media('mpeg2video', ['ac3'], 'C:/v/old.vob'), DEFAULT_EXPORT);
    assert.deepEqual([b.container, b.videoCodec], ['mkv', 'hevc']);
    const c = quickRenderSettings(media('h264', ['opus'], 'C:/v/web.webm'), DEFAULT_EXPORT);
    assert.deepEqual([c.container, c.videoCodec], ['webm', 'vp9']);
    const d = quickRenderSettings(media('prores', ['pcm_s24le'], 'C:/v/master.mov', 10), DEFAULT_EXPORT);
    assert.deepEqual([d.container, d.videoCodec], ['mov', 'prores']);
    // 10-bit H.264 is kept 8-bit (hardware encoders and most players do not support it)
    assert.equal(quickRenderSettings(media('h264', ['aac'], 'a.mkv', 10), DEFAULT_EXPORT).tenBit, false);
});
