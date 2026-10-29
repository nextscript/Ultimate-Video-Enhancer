// Hardware encode/decode candidates (v1.0.1). These are CANDIDATES only: every encoder is verified with a
// short scratch encode at start-up and again for the actual job size/pixel format before it is used;
// hardware decoding is verified on the actual input file. Software is always the final fallback.

export type HwFamily = 'nvenc' | 'qsv' | 'amf' | 'videotoolbox' | 'vaapi';
export type HwCodec = 'h264' | 'hevc' | 'av1';
export type GpuVendor = 'nvidia' | 'amd' | 'intel' | 'apple' | 'other';

export const HW_FAMILY_LABEL: Record<HwFamily, string> = {
    nvenc: 'NVIDIA NVENC', qsv: 'Intel Quick Sync', amf: 'AMD AMF', videotoolbox: 'Apple VideoToolbox', vaapi: 'VA-API'
};

export function vendorOf(gpuName: string): GpuVendor {
    if (/nvidia|geforce|quadro|rtx|gtx/i.test(gpuName)) return 'nvidia';
    if (/amd|radeon/i.test(gpuName)) return 'amd';
    if (/intel|arc|iris|uhd graphics/i.test(gpuName)) return 'intel';
    if (/apple|m[1-9]\b/i.test(gpuName)) return 'apple';
    return 'other';
}

/** Encoder families that can exist on this platform, the primary GPU's vendor first. */
export function familyOrder(platform: string, vendor: GpuVendor): HwFamily[] {
    if (platform === 'darwin') return ['videotoolbox'];
    const base: HwFamily[] = platform === 'win32' ? ['nvenc', 'amf', 'qsv'] : ['nvenc', 'vaapi', 'qsv'];
    const first: Partial<Record<GpuVendor, HwFamily>> = { nvidia: 'nvenc', amd: platform === 'win32' ? 'amf' : 'vaapi', intel: 'qsv' };
    const f = first[vendor];
    return f ? [f, ...base.filter(x => x !== f)] : base;
}

/** Hardware decoders (FFmpeg -hwaccel) to try on this platform, in order. */
export function hwaccelOrder(platform: string, vendor: GpuVendor, available: string[]): string[] {
    let order: string[];
    if (platform === 'darwin') order = ['videotoolbox'];
    else if (platform === 'win32') order = vendor === 'nvidia' ? ['cuda', 'd3d11va', 'dxva2'] : vendor === 'intel' ? ['qsv', 'd3d11va', 'dxva2'] : ['d3d11va', 'dxva2'];
    else order = vendor === 'nvidia' ? ['cuda', 'vaapi', 'vdpau'] : ['vaapi', 'vdpau'];
    return order.filter(h => available.includes(h));
}

export const hwEncoderName = (codec: HwCodec, fam: HwFamily) => `${codec}_${fam}`;

export function allHwEncoderNames(): string[] {
    const out: string[] = [];
    for (const c of ['h264', 'hevc', 'av1'] as HwCodec[]) for (const f of ['nvenc', 'qsv', 'amf', 'videotoolbox', 'vaapi'] as HwFamily[]) out.push(hwEncoderName(c, f));
    return out;
}

export interface EncoderChoice { name: string; family: HwFamily | 'software'; args: string[]; pix: string; tenBit: boolean; vaapi: boolean }

const clamp = (n: number, a: number, b: number) => Math.min(b, Math.max(a, n));

/**
 * FFmpeg output arguments for a hardware encoder. `quality` uses the CRF-like 0–51 scale of the UI.
 * Returns null when the combination is not supported (e.g. 10-bit H.264 in hardware).
 */
export function hwEncoderArgs(codec: HwCodec, fam: HwFamily, quality: number, preset: 'fast' | 'medium' | 'slow', tenBit: boolean): EncoderChoice | null {
    const name = hwEncoderName(codec, fam);
    const q = String(Math.round(quality));
    if (tenBit && codec === 'h264') return null;
    switch (fam) {
        case 'nvenc': {
            const p = { fast: 'p3', medium: 'p5', slow: 'p7' }[preset];
            const args = ['-c:v', name, '-preset', p, '-tune', 'hq', '-rc', 'vbr', '-cq', q, '-b:v', '0'];
            if (codec === 'h264') args.push('-profile:v', 'high');
            if (codec === 'hevc') args.push('-profile:v', tenBit ? 'main10' : 'main', '-tag:v', 'hvc1');
            return { name, family: fam, args, pix: tenBit ? 'p010le' : 'yuv420p', tenBit, vaapi: false };
        }
        case 'qsv': {
            const p = { fast: 'veryfast', medium: 'medium', slow: 'veryslow' }[preset];
            const args = ['-c:v', name, '-preset', p, '-global_quality', q];
            if (codec === 'hevc') args.push('-profile:v', tenBit ? 'main10' : 'main', '-tag:v', 'hvc1');
            return { name, family: fam, args, pix: tenBit ? 'p010le' : 'nv12', tenBit, vaapi: false };
        }
        case 'amf': {
            const p = { fast: 'speed', medium: 'balanced', slow: 'quality' }[preset];
            const args = ['-c:v', name, '-quality', p, '-rc', 'cqp', '-qp_i', q, '-qp_p', q];
            if (codec !== 'av1') args.push('-qp_b', q);
            if (codec === 'hevc') args.push('-profile:v', tenBit ? 'main10' : 'main', '-tag:v', 'hvc1');
            return { name, family: fam, args, pix: tenBit ? 'p010le' : 'yuv420p', tenBit, vaapi: false };
        }
        case 'videotoolbox': {
            const args = ['-c:v', name, '-q:v', String(clamp(Math.round(100 - quality * 1.6), 1, 100)), '-allow_sw', '0'];
            if (codec === 'hevc') args.push('-profile:v', tenBit ? 'main10' : 'main', '-tag:v', 'hvc1');
            return { name, family: fam, args, pix: tenBit ? 'p010le' : 'yuv420p', tenBit, vaapi: false };
        }
        case 'vaapi': {
            const args = ['-c:v', name, '-rc_mode', 'CQP', '-qp', q];
            if (codec === 'hevc') args.push('-profile:v', tenBit ? 'main10' : 'main', '-tag:v', 'hvc1');
            return { name, family: fam, args, pix: tenBit ? 'p010' : 'nv12', tenBit, vaapi: true };
        }
    }
}

export const VAAPI_DEVICE = '/dev/dri/renderD128';

/** Short scratch encode used to verify an encoder (start-up probe and per job with the real size). */
export function encoderTestArgs(choice: EncoderChoice, w = 256, h = 144): string[] {
    const pre = choice.vaapi ? ['-vaapi_device', VAAPI_DEVICE] : [];
    const vf = choice.vaapi ? ['-vf', `format=${choice.pix},hwupload`] : ['-pix_fmt', choice.pix];
    const args = choice.args.filter((_a, i, a) => a[i] !== '-tag:v' && a[i - 1] !== '-tag:v');
    return ['-hide_banner', '-loglevel', 'error', ...pre, '-f', 'lavfi', '-i', `testsrc2=s=${w}x${h}:d=0.2:r=10`, ...vf, ...args, '-f', 'null', '-'];
}
