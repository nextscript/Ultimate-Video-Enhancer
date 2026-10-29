// Compares parity exports of two versions (electron . --parity-export=<dir>) frame by frame with FFmpeg's
// psnr/ssim filters at matched presentation timestamps, plus stream metadata.
// Usage: node migration/compare-parity.mjs <dirA v1.0.0> <dirB v1.0.1> <ffmpeg> <out.json>
// Tolerances are defined here, before the run (see migration/MIGRATION_NOTES.md):
//   PSNR(Y,U,V average) >= 45 dB, SSIM(All) >= 0.995, identical frame count / codec / pixel format /
//   colour tags / audio stream; duration within one frame.
import { execFileSync, spawnSync } from 'node:child_process';
import { readdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const [dirA, dirB, ffmpeg, outFile] = process.argv.slice(2);
const ffprobe = path.join(path.dirname(ffmpeg), path.basename(ffmpeg).replace(/ffmpeg/i, 'ffprobe'));
const TOL = { psnr: 45, ssim: 0.995 };

function meta(file) {
    const j = JSON.parse(execFileSync(ffprobe, ['-v', 'error', '-count_frames', '-show_streams', '-show_format', '-of', 'json', file]).toString());
    const v = j.streams.find(s => s.codec_type === 'video');
    const a = j.streams.filter(s => s.codec_type === 'audio');
    return {
        frames: Number(v.nb_read_frames), codec: v.codec_name, pix: v.pix_fmt, w: v.width, h: v.height,
        colors: `${v.color_space}/${v.color_primaries}/${v.color_transfer}/${v.color_range}`, fps: v.avg_frame_rate,
        duration: Number(j.format.duration), audio: a.map(x => `${x.codec_name}:${x.sample_rate}:${x.channels}`).join(',')
    };
}

function metrics(a, b) {
    const r = spawnSync(ffmpeg, ['-hide_banner', '-i', a, '-i', b, '-lavfi', '[0:v]setpts=PTS-STARTPTS[x];[1:v]setpts=PTS-STARTPTS[y];[x][y]psnr;[0:v]setpts=PTS-STARTPTS[p];[1:v]setpts=PTS-STARTPTS[q];[p][q]ssim', '-f', 'null', '-'], { encoding: 'utf8' });
    const err = r.stderr || '';
    const psnr = (err.match(/PSNR .*average:([\d.inf]+)/) || [])[1];
    const ssim = (err.match(/SSIM .*All:([\d.]+)/) || [])[1];
    return { psnr: psnr === 'inf' ? Infinity : Number(psnr), ssim: Number(ssim) };
}

const cases = readdirSync(dirA).filter(f => /\.(mkv|mp4)$/.test(f) && !f.startsWith('source'));
const rows = [];
for (const f of cases) {
    const a = path.join(dirA, f), b = path.join(dirB, f);
    const ma = meta(a), mb = meta(b);
    const m = metrics(a, b);
    const metaOk = ma.frames === mb.frames && ma.codec === mb.codec && ma.pix === mb.pix && ma.colors === mb.colors && ma.audio === mb.audio
        && ma.w === mb.w && ma.h === mb.h && Math.abs(ma.duration - mb.duration) < 0.05;
    const ok = metaOk && m.psnr >= TOL.psnr && m.ssim >= TOL.ssim;
    rows.push({ case: f, ok, psnr: m.psnr, ssim: m.ssim, metaOk, v100: ma, v101: mb });
    console.log(`${ok ? 'PASS' : 'FAIL'} ${f}: PSNR ${m.psnr === Infinity ? '∞' : m.psnr.toFixed(2)} dB, SSIM ${m.ssim.toFixed(5)}, frames ${ma.frames}/${mb.frames}, ${ma.codec}/${mb.codec}, ${ma.pix}/${mb.pix}, audio ${ma.audio || '-'} / ${mb.audio || '-'}${metaOk ? '' : ' – METADATA DIFFERS'}`);
}
writeFileSync(outFile, JSON.stringify({ tolerances: TOL, comparedAt: new Date().toISOString(), rows }, (k, v) => v === Infinity ? 'inf' : v, 2));
process.exit(rows.every(r => r.ok) ? 0 : 1);
