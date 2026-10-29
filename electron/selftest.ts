// End-to-end self test (run: electron . --selftest). Generates synthetic clips with FFmpeg,
// renders them through the real export worker and verifies the encoded results:
//  1. neutral settings  → output must match the source (catches flips, channel swaps, color shifts)
//  2. UVE default look  → output must differ visibly, keep duration, frame count and audio
//  3. 10-bit HEVC + odd-size + rotated source paths
import { spawn } from 'node:child_process';
import { promises as fsp } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { app } from 'electron';
import type { EffectState, JobSpec, JobState } from '../shared/types';
import { DEFAULT_EXPORT } from '../shared/types';
import { expertDefaults } from '../shared/uve/expert';
import { DEFAULT_SETTINGS, NEUTRAL_SETTINGS } from '../shared/uve/settings';
import { probe } from './media';
import { enqueue, listJobs } from './render';
import { capabilities } from './tools';

function run(file: string, args: string[]): Promise<{ code: number; out: Buffer; err: string }> {
    return new Promise(resolve => {
        const p = spawn(file, args, { windowsHide: true });
        const chunks: Buffer[] = [];
        let err = '';
        p.stdout.on('data', d => chunks.push(d));
        p.stderr.on('data', d => { err += d; });
        p.on('exit', code => resolve({ code: code ?? 1, out: Buffer.concat(chunks), err }));
    });
}

async function frameAt(ffmpeg: string, file: string, t: number, w: number, h: number): Promise<Buffer> {
    const r = await run(ffmpeg, ['-v', 'error', '-ss', String(t), '-i', file, '-frames:v', '1', '-vf', `scale=${w}:${h}:out_range=pc,format=rgb24`, '-f', 'rawvideo', 'pipe:1']);
    if (r.code !== 0 || r.out.length !== w * h * 3) throw new Error(`frame extraction failed: ${r.err}`);
    return r.out;
}

function meanAbsDiff(a: Buffer, b: Buffer) {
    let s = 0;
    for (let i = 0; i < a.length; i++) s += Math.abs(a[i] - b[i]);
    return s / a.length;
}

/** Per-quadrant mean luminance: detects vertical/horizontal flips even when the global mean matches. */
function quadrants(buf: Buffer, w: number, h: number) {
    const q = [0, 0, 0, 0], n = [0, 0, 0, 0];
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const i = (y * w + x) * 3, k = (y < h / 2 ? 0 : 2) + (x < w / 2 ? 0 : 1);
        q[k] += buf[i] * 0.2126 + buf[i + 1] * 0.7152 + buf[i + 2] * 0.0722; n[k]++;
    }
    return q.map((v, i) => v / n[i]);
}

async function waitJob(id: string, timeoutMs = 300000): Promise<JobState> {
    const t0 = Date.now();
    for (;;) {
        const j = listJobs().find(x => x.id === id)!;
        if (['done', 'failed', 'canceled'].includes(j.status)) return j;
        if (Date.now() - t0 > timeoutMs) throw new Error(`job ${id} timed out (${j.status} ${Math.round(j.progress * 100)}%)`);
        await new Promise(r => setTimeout(r, 300));
    }
}

export async function runSelftest(): Promise<number> {
    const log: string[] = [];
    const say = (s: string) => { log.push(s); console.log(s); };
    let failures = 0;
    const check = (ok: boolean, msg: string) => { say(`${ok ? 'PASS' : 'FAIL'} ${msg}`); if (!ok) failures++; };
    const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'uve-selftest-'));
    try {
        const caps = await capabilities(true);
        say(`FFmpeg ${caps.ffmpegVersion} (${caps.source}) · GPU ${caps.gpuName} · NVENC ${JSON.stringify(caps.nvencWorking)}`);
        if (!caps.ok) throw new Error(caps.error);
        const src = path.join(dir, 'source.mp4');
        // asymmetric test pattern (testsrc2 has distinct quadrants) + 1 kHz audio
        let r = await run(caps.ffmpegPath, ['-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=25:duration=3', '-f', 'lavfi', '-i', 'sine=frequency=1000:duration=3',
            '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '12', '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-c:a', 'aac', '-shortest', '-y', src]);
        if (r.code !== 0) throw new Error(r.err);
        const media = await probe(src);
        check(!!media.video && media.video.width === 640 && Math.abs(media.duration - 3) < 0.1, `probe source (${media.video?.width}x${media.video?.height}, ${media.duration.toFixed(2)} s, ${media.audio.length} audio)`);

        const mkSpec = (name: string, effect: EffectState, out: string, ex: Partial<typeof DEFAULT_EXPORT> = {}, m = media): JobSpec => ({
            id: `selftest_${name}_${Date.now()}`, name, input: m.path, output: out, media: m, effect, autoKeys: null,
            exportSettings: { ...DEFAULT_EXPORT, quality: 12, ...ex }, range: { start: 0, end: m.duration }, parityMode: false
        });
        const neutral: EffectState = { settings: { ...NEUTRAL_SETTINGS }, expert: expertDefaults(), customFilters: [], lut: null };
        const look: EffectState = { settings: { ...DEFAULT_SETTINGS }, expert: expertDefaults(), customFilters: [], lut: null };

        // 1. neutral
        const outN = path.join(dir, 'neutral.mp4');
        let j = await waitJob(enqueue(mkSpec('neutral', neutral, outN, { videoCodec: 'h264' })).id);
        check(j.status === 'done', `neutral render (${j.encoder}, ${j.fps.toFixed(1)} fps)${j.error ? ' – ' + j.error : ''}`);
        if (j.status === 'done') {
            const [a, b] = [await frameAt(caps.ffmpegPath, src, 1.5, 320, 180), await frameAt(caps.ffmpegPath, outN, 1.5, 320, 180)];
            const d = meanAbsDiff(a, b);
            check(d < 2.5, `neutral output equals source (mean abs diff ${d.toFixed(2)} / 255)`);
            const qa = quadrants(a, 320, 180), qb = quadrants(b, 320, 180);
            check(qa.every((v, i) => Math.abs(v - qb[i]) < 3), `orientation preserved (quadrants ${qa.map(v => v.toFixed(0))} vs ${qb.map(v => v.toFixed(0))})`);
            const om = await probe(outN);
            check(om.video?.frames === 75 || Math.abs((om.duration || 0) - 3) < 0.08, `neutral frame count/duration (${om.video?.frames} frames, ${om.duration.toFixed(2)} s)`);
            check(om.audio.length === 1 && om.audio[0].codec === 'aac', 'audio stream copied');
        }

        // 2. default UVE look (auto-scene-match analysis runs inside the job)
        const outL = path.join(dir, 'look.mp4');
        j = await waitJob(enqueue(mkSpec('look', look, outL, { videoCodec: 'hevc' })).id);
        check(j.status === 'done', `UVE default look render (${j.encoder}, ${j.fps.toFixed(1)} fps)${j.error ? ' – ' + j.error : ''}`);
        if (j.status === 'done') {
            const [a, b] = [await frameAt(caps.ffmpegPath, src, 1.5, 320, 180), await frameAt(caps.ffmpegPath, outL, 1.5, 320, 180)];
            const d = meanAbsDiff(a, b);
            check(d > 3, `effects are baked into every frame (mean abs diff ${d.toFixed(2)} / 255 at 1.5 s)`);
            const a0 = await frameAt(caps.ffmpegPath, src, 0.1, 320, 180), b0 = await frameAt(caps.ffmpegPath, outL, 0.1, 320, 180);
            check(meanAbsDiff(a0, b0) > 3, 'effects present on the first frames');
            const om = await probe(outL);
            check(om.video?.codec === 'hevc' && Math.abs(om.duration - 3) < 0.08, `HEVC output, duration ${om.duration.toFixed(2)} s`);
        }

        // 2b. custom GLSL pass (invert) on top of neutral settings + an SVG custom filter that is a no-op matrix
        const invert: EffectState = {
            ...neutral,
            customFilters: [
                { id: 'inv', label: 'Invert', type: 'webgl', enabled: true, blendMode: 'normal', tags: [], category: '', uniforms: {},
                    code: ['// @uniform float AMOUNT 1.0 0.0 1.0 "Amount"', 'void main(){ vec4 c = texture(u_video, v_uv); fragColor = vec4(mix(c.rgb, 1.0 - c.rgb, AMOUNT), 1.0); }'].join('\n') }
            ]
        };
        const outI = path.join(dir, 'invert.mp4');
        j = await waitJob(enqueue(mkSpec('invert', invert, outI, { videoCodec: 'h264' })).id);
        check(j.status === 'done', `custom GLSL render${j.error ? ' – ' + j.error : ''}`);
        if (j.status === 'done') {
            const a = await frameAt(caps.ffmpegPath, src, 1.5, 320, 180), b = await frameAt(caps.ffmpegPath, outI, 1.5, 320, 180);
            let dsum = 0;
            for (let i = 0; i < a.length; i++) dsum += Math.abs(255 - a[i] - b[i]);
            check(dsum / a.length < 3, `custom GLSL pass applied exactly (|out - (255 - src)| = ${(dsum / a.length).toFixed(2)})`);
        }

        // 3. odd size + rotation + 10-bit output
        const src2 = path.join(dir, 'rotated.mp4');
        const plain = path.join(dir, 'plain.mp4');
        r = await run(caps.ffmpegPath, ['-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=321x181:rate=30:duration=1', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-y', plain]);
        if (r.code === 0) r = await run(caps.ffmpegPath, ['-v', 'error', '-display_rotation', '90', '-i', plain, '-c', 'copy', '-y', src2]);
        if (r.code === 0) {
            const m2 = await probe(src2);
            const out3 = path.join(dir, 'rot10.mkv');
            j = await waitJob(enqueue(mkSpec('rot10', look, out3, { container: 'mkv', videoCodec: 'hevc', tenBit: true }, m2)).id);
            check(j.status === 'done', `rotated odd-size 10-bit render${j.error ? ' – ' + j.error : ''}`);
            if (j.status === 'done') {
                const om = await probe(out3);
                check(!!om.video && om.video.height > om.video.width && om.video.bitDepth >= 10, `rotation applied (${om.video?.width}x${om.video?.height}) and ${om.video?.bitDepth}-bit output`);
            }
        } else say(`SKIP rotation test (${r.err.trim().split('\n')[0]})`);

        // 3b. AI upscale pipeline (REAL-Video-Enhancer) with the mock backend from tests/rve-mock
        const sysPython = (await run('where.exe', ['python'])).out.toString().split(/\r?\n/).map(s => s.trim()).find(s => s.toLowerCase().endsWith('python.exe') && !s.includes('WindowsApps'));
        const mockSrc = path.join(app.getAppPath(), 'tests', 'rve-mock', 'backend', 'rve-backend.py');
        if (sysPython && await fsp.stat(mockSrc).catch(() => null)) {
            const rveRoot = path.join(dir, 'rve');
            await fsp.mkdir(path.join(rveRoot, 'backend'), { recursive: true });
            await fsp.mkdir(path.join(rveRoot, 'models', 'custom'), { recursive: true });
            await fsp.copyFile(mockSrc, path.join(rveRoot, 'backend', 'rve-backend.py'));
            await fsp.writeFile(path.join(rveRoot, 'models', 'custom', '2x_mock.pth'), 'mock');
            process.env.UVE_RVE_DIR = rveRoot;
            process.env.UVE_RVE_PYTHON = sysPython;
            const outU = path.join(dir, 'upscaled.mp4');
            j = await waitJob(enqueue(mkSpec('aiupscale', look, outU, { videoCodec: 'hevc', aiUpscale: 'custom:2x_mock.pth' })).id);
            check(j.status === 'done', `AI upscale two-stage render (${j.encoder})${j.error ? ' – ' + j.error : ''}`);
            if (j.status === 'done') {
                const om = await probe(outU);
                check(om.video?.width === 1280 && om.video?.height === 720, `upscaled resolution ${om.video?.width}x${om.video?.height} (expected 1280x720)`);
                check(om.video?.codec === 'hevc' && om.audio.length === 1 && Math.abs(om.duration - 3) < 0.1, `final encode by UVE settings via RVE custom encoder (${om.video?.codec}, ${om.audio.length} audio, ${om.duration.toFixed(2)} s, color ${om.video?.colorSpace})`);
                // colour fidelity: per-channel means of the upscaled output vs. the non-upscaled export (resampling changes edges, not colours)
                const [a, b] = [await frameAt(caps.ffmpegPath, outL, 1.5, 320, 180), await frameAt(caps.ffmpegPath, outU, 1.5, 320, 180)];
                const means = (x: Buffer) => [0, 1, 2].map(c => { let t = 0; for (let i = c; i < x.length; i += 3) t += x[i]; return t / (x.length / 3); });
                const ma = means(a), mb = means(b);
                const cdiff = Math.max(...ma.map((v, i) => Math.abs(v - mb[i])));
                check(cdiff < 1.5, `upscaled output keeps the UVE colours (channel means ${ma.map(v => v.toFixed(1))} vs ${mb.map(v => v.toFixed(1))}, pixel diff ${meanAbsDiff(a, b).toFixed(2)})`);
                const fr = (await probe(outU)).video?.frames ?? 0;
                check(fr === 75, `upscaled frame count ${fr} (source 75, frames delivered by the upscaler: ${j.frames})`);
                check(!(await fsp.readdir(dir)).some(f => f.includes('uvestage')), 'intermediate files removed');
            }
            delete process.env.UVE_RVE_DIR;
            delete process.env.UVE_RVE_PYTHON;
        } else say('SKIP AI upscale pipeline test (no system Python found)');

        // 4. cancel cleans up
        const outC = path.join(dir, 'cancel.mp4');
        const jc = enqueue(mkSpec('cancel', look, outC));
        await new Promise(res => setTimeout(res, 400));
        const { cancelJob } = await import('./render');
        cancelJob(jc.id);
        j = await waitJob(jc.id);
        const leftovers = (await fsp.readdir(dir)).filter(f => f.includes('cancel'));
        check(j.status === 'canceled' && leftovers.length === 0, `cancel removes partial output (${j.status}, leftovers: ${leftovers.join(', ') || 'none'})`);
    } catch (e) {
        failures++;
        say(`FAIL selftest aborted: ${e instanceof Error ? e.stack : e}`);
    } finally {
        say(failures ? `SELFTEST FAILED (${failures})` : 'SELFTEST PASSED');
        await fsp.writeFile(path.join(app.getPath('userData'), 'selftest.log'), log.join('\n'));
        await fsp.rm(dir, { recursive: true, force: true }).catch(() => undefined);
    }
    return failures;
}
