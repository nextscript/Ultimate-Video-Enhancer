// Deterministic stand-in for the Video2X 6.4.0 CLI (tests and the self test). Run with Node:
//   node video2x-mock.mjs --help | --version | --list-devices | -i in -o out -p libplacebo --libplacebo-shader X -w W -h H -c CODEC ...
// Behaviour switches (environment):
//   VIDEO2X_MOCK_FFMPEG  path of ffmpeg: the output is a real Lanczos-scaled video (else a small dummy file)
//   VIDEO2X_MOCK_FAIL    vulkan | shader | crash  – fail like the real binary would
//   VIDEO2X_MOCK_SLEEP   ms per progress step (cancellation tests)
//   VIDEO2X_MOCK_ARGV    file the received argv is written to (argument-passing tests)
//   VIDEO2X_MOCK_FRAMES  frame total to report (default 10)
//   VIDEO2X_MOCK_DROP    1 = write one frame less than the input has (verification tests)
//   VIDEO2X_MOCK_NODEVICES 1 = --list-devices finds no Vulkan device
//   VIDEO2X_MOCK_FAIL=oom   GPU out of memory (VK_ERROR_OUT_OF_DEVICE_MEMORY)
//   VIDEO2X_MOCK_HANG      1 = --version never answers (timeout tests)
//   VIDEO2X_MOCK_NOMODELS  comma list of model files to treat as missing, e.g. "realesrgan/realesr-animevideov3-x2"
// v1.0.8: -p realesrgan / realcugan behave like 6.4.0 (scale 2-4, noise -1..3, model files per release table)
import { spawnSync } from 'node:child_process';
import { existsSync, writeFileSync } from 'node:fs';

const argv = process.argv.slice(2);
const env = process.env;
if (env.VIDEO2X_MOCK_ARGV) writeFileSync(env.VIDEO2X_MOCK_ARGV, JSON.stringify(argv));

const HELP = `General options:
  --help                                Display this help page
  -V [ --version ]                      Print program version and exit
  --log-level arg (=info)               Set verbosity level (trace, debug,
                                        info, warn, error, critical, none)
  --no-progress                         Do not display the progress bar
  -l [ --list-devices ]                 List the available Vulkan devices
                                        (GPUs)
  -i [ --input ] arg                    Input video file path
  -o [ --output ] arg                   Output video file path
  -p [ --processor ] arg                Processor to use (libplacebo,
                                        realesrgan, realcugan, rife)
  -a [ --hwaccel ] arg (=none)          Hardware acceleration method
                                        (decoding)
  -d [ --device ] arg (=0)              Vulkan device index (GPU ID)
  -b [ --benchmark ]                    Discard processed frames and calculate
                                        average FPS; useful for detecting
                                        encoder bottlenecks

Encoder options:
  -c [ --codec ] arg (=libx264)         Output codec
  --no-copy-streams                     Do not copy audio and subtitle streams
  --pix-fmt arg                         Output pixel format
  --bit-rate arg (=0)                   Bitrate in bits per second
  -e [ --extra-encoder-option ] arg     Additional AVOption(s) for the encoder
                                        (format: -e key=value)

Upscaling options:
  -w [ --width ] arg                    Output width
  -h [ --height ] arg                   Output height
  -s [ --scaling-factor ] arg           Scaling factor
  -n [ --noise-level ] arg              Noise level

libplacebo options:
  --libplacebo-shader arg (=anime4k-v4-a)
                                        Name/path of the GLSL shader file to
                                        use (built-in: anime4k-v4-a,
                                        anime4k-v4-a+a, anime4k-v4-b,
                                        anime4k-v4-b+b, anime4k-v4-c,
                                        anime4k-v4-c+a, anime4k-v4.1-gan)

RealESRGAN options:
  --realesrgan-model arg (=realesr-animevideov3)
                                        Name of the RealESRGAN model to use
                                        (realesr-animevideov3,
                                        realesrgan-plus-anime, realesrgan-plus)

RealCUGAN options:
  --realcugan-model arg (=models-se)    Name of the RealCUGAN model to use
                                        (models-nose, models-pro, models-se)
  --realcugan-threads arg (=1)          Number of threads to use for RealCUGAN
  --realcugan-syncgap arg (=3)          Sync gap mode; 0:no sync, 1: accurate
                                        sync: 2 = rough sync, 3: very rough
                                        sync

RIFE options:
  --rife-model arg (=rife-v4.6)         Name of the RIFE model to use (rife,
                                        rife-v4.6, rife-v4.25, rife-v4.26)
  --rife-uhd                            Enable Ultra HD mode
`;
// model files of the Video2X 6.4.0 release
const MODEL_FILES = new Set([
    'realesrgan/realesr-animevideov3-x2', 'realesrgan/realesr-animevideov3-x3', 'realesrgan/realesr-animevideov3-x4',
    'realesrgan/realesrgan-plus-anime-x4', 'realesrgan/realesrgan-plus-x4',
    'realcugan/models-nose/up2x-no-denoise',
    ...['up2x-conservative', 'up2x-denoise3x', 'up2x-no-denoise', 'up3x-conservative', 'up3x-denoise3x', 'up3x-no-denoise'].map(f => `realcugan/models-pro/${f}`),
    ...['up2x-conservative', 'up2x-denoise1x', 'up2x-denoise2x', 'up2x-denoise3x', 'up2x-no-denoise', 'up3x-conservative', 'up3x-denoise3x', 'up3x-no-denoise',
        'up4x-conservative', 'up4x-denoise3x', 'up4x-no-denoise'].map(f => `realcugan/models-se/${f}`)
]);
for (const m of (process.env.VIDEO2X_MOCK_NOMODELS || '').split(',').filter(Boolean)) MODEL_FILES.delete(m);
const BUILTIN = ['anime4k-v4-a', 'anime4k-v4-a+a', 'anime4k-v4-b', 'anime4k-v4-b+b', 'anime4k-v4-c', 'anime4k-v4-c+a', 'anime4k-v4.1-gan'];
const log = (lvl, m) => process.stderr.write(`[2025-01-24 12:00:00.000] [${lvl}] ${m}\n`);

if (argv.includes('--help') || !argv.length) { process.stdout.write(HELP); process.exit(1); }
if (argv.includes('--version') || argv.includes('-V')) {
    if (env.VIDEO2X_MOCK_HANG === '1') Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 60000); process.stdout.write('Video2X version 6.4.0\n'); process.exit(1); }
if (argv.includes('--list-devices') || argv.includes('-l')) {
    if (env.VIDEO2X_MOCK_NODEVICES === '1') { log('error', 'Failed to enumerate Vulkan physical devices.'); process.exit(255); }
    process.stdout.write('0. Mock Vulkan GPU\n\tType: Discrete GPU\n\tVulkan API Version: 1.3.280\n\tDriver Version: 560.0.0\n\tDevice ID: 0x2206\n');
    process.exit(1);
}

const opt = (...names) => { for (const n of names) { const i = argv.indexOf(n); if (i >= 0) return argv[i + 1]; } return undefined; };
const input = opt('-i', '--input'), output = opt('-o', '--output'), shader = opt('--libplacebo-shader') || 'anime4k-v4-a';
const w = Number(opt('-w', '--width')), h = Number(opt('-h', '--height')), codec = opt('-c', '--codec') || 'libx264', pixFmt = opt('--pix-fmt');
const proc = opt('-p', '--processor');
if (!input || !output || !['libplacebo', 'realesrgan', 'realcugan'].includes(proc)) { log('critical', 'Invalid arguments.'); process.exit(255); }
const sf = Number(opt('-s', '--scaling-factor'));
let modelFile = '';
if (proc === 'realesrgan') {
    const m = opt('--realesrgan-model') || 'realesr-animevideov3';
    if (!['realesr-animevideov3', 'realesrgan-plus-anime', 'realesrgan-plus'].includes(m)) { process.stderr.write("Error: the argument for option '--realesrgan-model' is invalid\n"); process.exit(255); }
    if (!(sf >= 2 && sf <= 4)) { log('critical', 'Scaling factor must be set to 2, 3, or 4 for RealESRGAN.'); process.exit(255); }
    modelFile = `realesrgan/${m}-x${sf}`;
}
if (proc === 'realcugan') {
    const m = opt('--realcugan-model') || 'models-se';
    const n = Number(opt('-n', '--noise-level') ?? 0);
    if (!['models-nose', 'models-pro', 'models-se'].includes(m)) { process.stderr.write("Error: the argument for option '--realcugan-model' is invalid\n"); process.exit(255); }
    if (!(sf >= 2 && sf <= 4)) { log('critical', 'Scaling factor must be set to 2, 3, or 4 for RealCUGAN.'); process.exit(255); }
    if (!(n >= -1 && n <= 3)) { log('critical', 'Noise level must be set to -1, 0, 1, 2, or 3 for RealCUGAN.'); process.exit(255); }
    modelFile = `realcugan/${m}/up${sf}x-${n === -1 ? 'conservative' : n === 0 ? 'no-denoise' : `denoise${n}x`}`;
}
if (!BUILTIN.includes(shader) && !existsSync(shader)) { process.stderr.write('Error: the argument for option \'--libplacebo-shader\' is invalid\n'); process.exit(255); }
log('info', 'Video2X version 6.4.0');
log('info', `Processing file: ${input}`);
if (env.VIDEO2X_MOCK_FAIL === 'vulkan') { log('error', 'Failed to create Vulkan instance: VK_ERROR_INCOMPATIBLE_DRIVER'); log('critical', 'Video processing failed with error code -1'); process.exit(1); }
if (env.VIDEO2X_MOCK_FAIL === 'shader') { log('error', 'libplacebo: Failed to parse custom shader: unexpected token'); log('critical', 'Video processing failed with error code -22'); process.exit(1); }
if (env.VIDEO2X_MOCK_FAIL === 'crash') process.exit(3);
if (env.VIDEO2X_MOCK_FAIL === 'oom') { log('error', 'vkAllocateMemory failed: VK_ERROR_OUT_OF_DEVICE_MEMORY'); log('critical', 'Video processing failed with error code -12'); process.exit(1); }
if (modelFile && !MODEL_FILES.has(modelFile)) {
    log('error', `${proc === 'realesrgan' ? 'Real-ESRGAN' : 'Real-CUGAN'} model param file not found: models/${modelFile}.param`);
    log('critical', 'Video processing failed with error code -1');
    process.exit(1);
}

const total = Number(env.VIDEO2X_MOCK_FRAMES || 10);
const sleep = Number(env.VIDEO2X_MOCK_SLEEP || 0);
const wait = ms => { if (ms > 0) Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms); };
for (let f = 1; f <= total; f++) {
    wait(sleep);
    process.stdout.write(`\r\u001b[Kframe=${f}/${total} (${(f * 100 / total).toFixed(2)}%); fps=25.00; elapsed=00:00:0${Math.min(9, f)}; remaining=00:00:00`);
}
process.stdout.write('\n');
if (env.VIDEO2X_MOCK_FFMPEG) {
    const vf = [proc === 'libplacebo' ? `scale=${w}:${h}:flags=lanczos` : `scale=iw*${sf}:ih*${sf}:flags=lanczos`];
    if (env.VIDEO2X_MOCK_DROP === '1') vf.push('trim=start_frame=1');
    const args = ['-v', 'error', '-nostdin', '-y', '-i', input, '-vf', vf.join(','), '-c:v', codec, ...(pixFmt ? ['-pix_fmt', pixFmt] : []),
        ...(codec === 'libx264rgb' ? ['-qp', '0'] : []), '-an', '-sn', output];
    const r = spawnSync(env.VIDEO2X_MOCK_FFMPEG, args, { stdio: ['ignore', 'ignore', 'pipe'] });
    if (r.status !== 0) { log('error', `encoder: ${String(r.stderr).slice(0, 300)}`); log('critical', 'Video processing failed with error code -1'); process.exit(1); }
} else writeFileSync(output, proc === 'libplacebo' ? `mock ${w}x${h} ${codec} ${shader}` : `mock ${proc} ${modelFile} ${codec}`);
log('info', 'Video processed successfully');
process.stdout.write(`====== Video2X Processing summary ======\nVideo file processed: ${input}\nTotal frames processed: ${total}\nOutput written to: ${output}\n`);
process.exit(0);
