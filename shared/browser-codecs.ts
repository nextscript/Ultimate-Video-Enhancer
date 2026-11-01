// Which codecs Chromium (Electron's <video>) can decode itself. Files whose picture plays but whose
// sound is in a codec outside this list (AC3, E-AC3, DTS, TrueHD, …, typical for MKV releases) are
// silent in the preview: no media error is raised, the audio track is just skipped. For those an
// "audio proxy" is built – the video stream copied 1:1, the sound converted to AAC. Export always
// reads the original file.
import type { MediaInfo } from './types';

const BROWSER_AUDIO = new Set(['aac', 'mp3', 'opus', 'vorbis', 'flac', 'alac', 'pcm_s16le', 'pcm_s24le', 'pcm_s32le', 'pcm_f32le', 'pcm_u8', 'pcm_mulaw', 'pcm_alaw']);
/** video codecs Chromium plays that can be stream-copied into an MP4 proxy */
const COPYABLE_VIDEO = new Set(['h264', 'hevc', 'av1', 'vp9']);

export function browserAudioOk(codec: string | undefined): boolean {
    return !codec || BROWSER_AUDIO.has(codec.toLowerCase());
}

/** true when the preview would be silent because the first audio stream cannot be decoded by Chromium */
export function needsAudioProxy(m: MediaInfo): boolean {
    return !!m.audio[0] && !browserAudioOk(m.audio[0].codec);
}

/** true when the audio proxy can keep the original video stream (stream copy instead of re-encode) */
export function videoCopyable(m: MediaInfo): boolean {
    return !m.video || COPYABLE_VIDEO.has(m.video.codec.toLowerCase());
}
