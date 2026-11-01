// Which audio codecs Chromium (Electron's <video>) can decode itself. Files whose picture plays but whose
// sound is in a codec outside this list (AC3, E-AC3, DTS, TrueHD, …, typical for MKV releases) are
// silent in the preview: no media error is raised, the audio track is just skipped. For those the sound
// is streamed live as PCM next to the picture (electron/audio-stream.ts). Export always reads the original file.
import type { MediaInfo } from './types';

const BROWSER_AUDIO = new Set(['aac', 'mp3', 'opus', 'vorbis', 'flac', 'alac', 'pcm_s16le', 'pcm_s24le', 'pcm_s32le', 'pcm_f32le', 'pcm_u8', 'pcm_mulaw', 'pcm_alaw']);

export function browserAudioOk(codec: string | undefined): boolean {
    return !codec || BROWSER_AUDIO.has(codec.toLowerCase());
}

/** true when the preview would be silent because the first audio stream cannot be decoded by Chromium */
export function needsAudioSidecar(m: MediaInfo): boolean {
    return !!m.audio[0] && !browserAudioOk(m.audio[0].codec);
}
