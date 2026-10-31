// End-to-end self test (run: electron . --selftest). Generates synthetic clips with FFmpeg,
// renders them through the real export worker and verifies the encoded results:
//  1. neutral settings  → output must match the source (catches flips, channel swaps, color shifts)
//  2. UVE default look  → output must differ visibly, keep duration, frame count and audio
//  3. 10-bit HEVC + odd-size + rotated source paths
//  4. timeline jobs: cut, opacity overlay, dissolve and reversed clip on both engines
import { spawn } from 'node:child_process';
import { promises as fsp } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { app } from 'electron';
import {
    addSource, addTrack, buildPlan, defaultTimeline, insertClip, patchClip, setTransition, srcTimeAt, timelineDuration, type OpResult
} from '../shared/timeline';
import type { EffectState, JobSpec, JobState, Timeline } from '../shared/types';
import { DEFAULT_EXPORT } from '../shared/types';
import { expertDefaults } from '../shared/uve/expert';
import { DEFAULT_SETTINGS, NEUTRAL_SETTINGS } from '../shared/uve/settings';
import { previewProxy, probe } from './media';
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

        // 2a. v1.0.1 engine parity: identical job on the v1.0.0 WebGL2 engine and on the WebGPU engine (lossless output)
        {
            const heavy: EffectState = {
                settings: { ...DEFAULT_SETTINGS, dn: -0.6, hdr: 0.5, tealOrange: true, u_contrast: 4, u_sat: 3, u_vib: 4, u_sharp: 5, u_grain: 2, u_hue: 2, cbFilter: 'protanopia' },
                expert: expertDefaults(), customFilters: [], lut: { name: 'test', group: 'selftest', matrix4x5: [1.05, 0.02, 0, 0, -0.01, 0, 1, 0.03, 0, 0, 0.02, 0, 0.97, 0, 0.01, 0, 0, 0, 1, 0], createdAt: 0, updatedAt: 0 }
            };
            heavy.settings.lutProfile = 'selftest||test';
            const outs: Record<string, string> = {};
            const engines: Record<string, string> = {};
            for (const pref of ['webgl2', 'webgpu'] as const) {
                const o = path.join(dir, `engine_${pref}.mkv`);
                const spec = { ...mkSpec(`engine_${pref}`, heavy, o, { container: 'mkv', videoCodec: 'h264', encoder: 'cpu', quality: 2 }), enginePref: pref };
                const jj = await waitJob(enqueue(spec).id);
                check(jj.status === 'done', `${pref} engine render${jj.error ? ' – ' + jj.error : ''} [${jj.engine || '?'}]`);
                outs[pref] = o;
                engines[pref] = jj.engine || '';
                const st = jj.stats;
                if (st) say(`INFO ${pref}: ${(st.frames / (st.totalMs / 1000)).toFixed(1)} fps · per frame: gpu-filter ${(st.gpu / st.frames).toFixed(2)} ms, readback ${(st.readback / st.frames).toFixed(2)} ms, upload ${(st.upload / st.frames).toFixed(2)} ms`);
            }
            check(engines.webgpu.startsWith('webgpu'), `WebGPU engine actually used when requested (${engines.webgpu})`);
            check(engines.webgl2.startsWith('webgl2'), `WebGL2 legacy engine still selectable (${engines.webgl2})`);
            if (outs.webgl2 && outs.webgpu) {
                let worst = 0, sumSq = 0, n = 0, maxMean = 0;
                for (const t of [0.2, 1.0, 1.5, 2.6]) {
                    const a = await frameAt(caps.ffmpegPath, outs.webgl2, t, 640, 360), b = await frameAt(caps.ffmpegPath, outs.webgpu, t, 640, 360);
                    for (let i = 0; i < a.length; i++) { const d = a[i] - b[i]; sumSq += d * d; worst = Math.max(worst, Math.abs(d)); n++; }
                    const ma = [0, 1, 2].map(c => { let s = 0; for (let i = c; i < a.length; i += 3) s += a[i]; return s / (a.length / 3); });
                    const mb = [0, 1, 2].map(c => { let s = 0; for (let i = c; i < b.length; i += 3) s += b[i]; return s / (b.length / 3); });
                    maxMean = Math.max(maxMean, ...ma.map((v, i) => Math.abs(v - mb[i])));
                }
                const psnr = sumSq === 0 ? Infinity : 10 * Math.log10((255 * 255) / (sumSq / n));
                check(psnr >= 45 && maxMean < 0.5, `WebGPU vs v1.0.0 engine export parity (PSNR ${psnr === Infinity ? '∞' : psnr.toFixed(1)} dB, max channel-mean diff ${maxMean.toFixed(3)}, worst pixel ${worst}/255)`);
            }
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

        // 2b. preview proxy (v1.0.1: any verified hardware H.264 encoder, else x264)
        try {
            const proxy = await previewProxy(await probe(src), () => undefined);
            const pm = await probe(proxy);
            check(pm.video?.codec === 'h264' && Math.abs(pm.duration - 3) < 0.1, `preview proxy (${pm.video?.codec}, ${pm.video?.width}x${pm.video?.height}, ${pm.duration.toFixed(2)} s)`);
        } catch (e) { check(false, `preview proxy – ${String(e)}`); }

        // 3a. user-defined output frame rate (v1.0.1): 25 fps source → 30 fps and 29.97 fps output, same duration
        for (const [fpsIn, num, den] of [['30', 30, 1], ['29.97', 30000, 1001]] as const) {
            const outF = path.join(dir, `fps_${fpsIn.replace('.', '_')}.mp4`);
            j = await waitJob(enqueue(mkSpec(`fps${fpsIn}`, look, outF, { videoCodec: 'h264', fps: fpsIn })).id);
            check(j.status === 'done', `render with output frame rate ${fpsIn}${j.error ? ' – ' + j.error : ''}`);
            if (j.status === 'done') {
                const fm = await probe(outF);
                const r = await run(caps.ffprobePath, ['-v', 'error', '-count_frames', '-select_streams', 'v:0', '-show_entries', 'stream=nb_read_frames,avg_frame_rate', '-of', 'csv=p=0', outF]);
                const [rate, nb] = r.out.toString().trim().split(',');
                const expected = Math.round(3 * num / den);
                check(rate === `${num}/${den}` && Math.abs(Number(nb) - expected) <= 1 && Math.abs(fm.duration - 3) < 0.1,
                    `output frame rate ${fpsIn}: ${rate} fps, ${nb} frames (expected ${expected}), ${fm.duration.toFixed(2)} s, audio ${fm.audio.length ? 'kept' : 'missing'}`);
            }
        }

        // 3b. AI upscale pipeline (REAL-Video-Enhancer) with the mock backend from tests/rve-mock
        const sysPython = process.platform === 'win32'
            ? (await run('where.exe', ['python'])).out.toString().split(/\r?\n/).map(s => s.trim()).find(s => s.toLowerCase().endsWith('python.exe') && !s.includes('WindowsApps'))
            : (await run('which', ['python3'])).out.toString().trim() || undefined;
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

        // 3c. timeline jobs (v1.0.3 NLE) on both engines: the timeline compositor shaders, per-clip streams,
        //     the reverse pre-pass and the synthesized audio pass
        {
            const step = (r: OpResult): Timeline => { if (!r.ok) throw new Error(r.error); return r.tl; };
            const base = step(addSource(defaultTimeline(25), src, media));
            const sId = base.sources[0].id;
            const vTrack = (t: Timeline, i: number) => t.tracks.filter(x => x.kind === 'video')[i].id;
            const put = (t: Timeline, track: number, start: number, dur: number, srcIn: number) =>
                step(insertClip(t, { trackId: vTrack(t, track), sourceId: sId, start, dur, srcIn, mode: 'replace' }));
            const mkTl = (name: string, tl: Timeline, out: string, pref: 'webgl2' | 'webgpu'): JobSpec => ({
                ...mkSpec(name, neutral, out, { container: 'mkv', videoCodec: 'h264', encoder: 'cpu', quality: 2, useInOut: false, audio: 'aac' }),
                range: { start: 0, end: timelineDuration(tl) }, timeline: structuredClone(tl), enginePref: pref
            });
            const cut = put(put(base, 0, 0, 1.6, 0), 0, 1.6, 1.4, 1.6);
            const stacked = put(step(addTrack(cut, 'video')), 1, 0, 3, 0);
            const overlay = step(patchClip(stacked, stacked.clips.find(c => c.trackId === vTrack(stacked, 1))!.id, { opacity: 0.5 }));
            const dissolve = step(setTransition(cut, cut.clips.find(c => c.start > 1)!.id, 'dissolve', 0.4));
            const whole = put(base, 0, 0, 3, 0);
            const reversed = step(patchClip(whole, whole.clips[0].id, { reverse: true }));

            // frames are compared by index: in mkv the AAC priming gives the container a negative start time, which shifts -ss seeks by a frame
            const nth = async (file: string, n: number) => {
                const r = await run(caps.ffmpegPath, ['-v', 'error', '-i', file, '-vf', `select=eq(n\\,${n}),scale=320:180:out_range=pc,format=rgb24`, '-frames:v', '1', '-f', 'rawvideo', 'pipe:1']);
                if (r.code !== 0 || r.out.length !== 320 * 180 * 3) throw new Error(`frame ${n} extraction failed: ${r.err}`);
                return r.out;
            };
            const worstOf = async (out: string, pairs: [number, number][]) => {
                let worst = 0;
                for (const [o, sN] of pairs) worst = Math.max(worst, meanAbsDiff(await nth(src, sN), await nth(out, o)));
                return worst;
            };
            const same = (ns: number[]) => ns.map(n => [n, n] as [number, number]);
            const cases: { name: string; tl: Timeline; verify?: (out: string) => Promise<void> }[] = [
                { name: 'cut', tl: cut, verify: async out => {
                    const d = await worstOf(out, same([12, 39, 40, 62]));
                    check(d < 2.5, `timeline cut equals the source across the edit (worst mean abs diff ${d.toFixed(2)} / 255)`);
                } },
                { name: 'overlay', tl: overlay, verify: async out => {
                    const d = await worstOf(out, same([25, 50]));
                    check(d < 2.5, `upper track at opacity 0.5 over the same picture equals the source (worst mean abs diff ${d.toFixed(2)} / 255)`);
                } },
                { name: 'dissolve', tl: dissolve, verify: async out => {
                    // both sides of the dissolve show the same continuous footage, so the blend must equal the source
                    const d = await worstOf(out, same([36, 40, 44]));
                    check(d < 2.5, `dissolve of continuous footage equals the source (worst mean abs diff ${d.toFixed(2)} / 255)`);
                } },
                { name: 'reverse', tl: reversed, verify: async out => {
                    const pc = buildPlan(reversed).clips[0];
                    const pairs = [5, 25, 60].map(f => [f, Math.floor(srcTimeAt(pc, f, 25) * 25 + 1e-6)] as [number, number]);
                    const d = await worstOf(out, pairs);
                    check(d < 3, `reversed clip shows the source frame at srcTimeAt (${pairs.map(([a, b]) => `${a}→${b}`).join(', ')}, worst mean abs diff ${d.toFixed(2)} / 255)`);
                } }
            ];
            for (const c of cases) {
                const outs: Partial<Record<'webgl2' | 'webgpu', string>> = {};
                for (const pref of ['webgpu', 'webgl2'] as const) {
                    const o = path.join(dir, `tl_${c.name}_${pref}.mkv`);
                    const jj = await waitJob(enqueue(mkTl(`tl_${c.name}_${pref}`, c.tl, o, pref)).id);
                    check(jj.status === 'done', `timeline ${c.name} render on ${pref}${jj.error ? ' – ' + jj.error : ''} [${jj.engine || '?'}]`);
                    if (jj.status !== 'done') continue;
                    outs[pref] = o;
                    const om = await probe(o);
                    const fr = await run(caps.ffprobePath, ['-v', 'error', '-count_frames', '-select_streams', 'v:0', '-show_entries', 'stream=nb_read_frames', '-of', 'csv=p=0', o]);
                    const nb = Number(fr.out.toString().trim());
                    check(nb === 75 && om.audio.length === 1 && Math.abs(om.duration - 3) < 0.08, `timeline ${c.name} on ${pref}: ${nb} frames (expected 75), ${om.audio.length} audio, ${om.duration.toFixed(2)} s`);
                    if (pref === 'webgpu' && c.verify) await c.verify(o);
                }
                if (outs.webgl2 && outs.webgpu) {
                    let worst = 0;
                    for (const t of [0.3, 1.4, 1.5, 1.6, 2.7]) worst = Math.max(worst, meanAbsDiff(await frameAt(caps.ffmpegPath, outs.webgl2, t, 320, 180), await frameAt(caps.ffmpegPath, outs.webgpu, t, 320, 180)));
                    check(worst < 1.5, `timeline ${c.name}: WebGPU and WebGL2 agree (worst mean abs diff ${worst.toFixed(2)} / 255)`);
                }
            }
        }

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
