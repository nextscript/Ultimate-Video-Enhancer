// v1.1.2 Sofort-Render (quick render): the original file 1:1 plus the current look.
import type { Container, ExportSettings, MediaInfo, VideoCodec } from './types';

/**
 * Export settings of the Sofort-Render: the source as it is (resolution, frame rate, codec, bit depth,
 * container, every audio and subtitle stream copied) and only the look on top. No AI upscaling, no
 * timeline edits. Output folder and file-name pattern come from the Output Settings.
 */
export function quickRenderSettings(m: MediaInfo, base: ExportSettings): ExportSettings {
    const v = m.video;
    const srcExt = (m.path.match(/\.([^.\\/]+)$/)?.[1] ?? '').toLowerCase();
    const container: Container = srcExt === 'mp4' || srcExt === 'm4v' ? 'mp4' : srcExt === 'mov' ? 'mov' : srcExt === 'webm' ? 'webm' : 'mkv';
    const src = (v?.codec ?? '').toLowerCase();
    let videoCodec: VideoCodec = src === 'h264' || src === 'hevc' || src === 'av1' || src === 'vp9' ? src : src === 'prores' ? 'prores' : 'hevc';
    if (container === 'webm' && videoCodec !== 'vp9' && videoCodec !== 'av1') videoCodec = 'vp9';
    return {
        ...base,
        container: videoCodec === 'prores' ? 'mov' : container, videoCodec,
        encoder: 'auto', quality: 16, preset: 'medium',
        // 10-bit sources stay 10-bit (H.264 10-bit is CPU-only and rarely playable: keep 8-bit there)
        tenBit: !!v && v.bitDepth > 8 && videoCodec !== 'h264',
        resolution: 'source', fps: '', aiUpscale: '', upscaleProvider: 'off', useInOut: false,
        audio: 'copy', audioTrack: -1, allAudio: true, subtitles: 'copy'
    };
}
