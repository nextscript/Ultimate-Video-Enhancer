// Renders a benchmark JSON (electron . --benchmark=<file>) as Markdown, so the report never contains
// hand-copied numbers. Usage: node migration/report-benchmark.mjs migration/benchmark/<host>.json > migration/benchmark/<host>.md
import { readFileSync } from 'node:fs';

const d = JSON.parse(readFileSync(process.argv[2], 'utf8'));
const out = [];
out.push(`# Benchmark – UVE Manager ${d.app}`, '');
out.push(`Measured ${d.at} · ${d.platform} · ${d.cpu.trim()} (${d.cores} threads) · ${d.ramGB} GB RAM`);
out.push(`GPU ${d.gpu}, driver ${d.gpuDriver} · FFmpeg ${d.ffmpeg}`, '');
out.push('| Resolution | Workload | Engine | Encoder | Status | FPS | Wall [s] | decode-wait | upload | gpu-filter | readback | encode-wait | CPU % | Peak app RAM [MB] | Peak VRAM total [MB] |');
out.push('|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|');
for (const r of d.results) {
    const p = r.perFrameMs || {};
    const eng = (r.engineUsed || '').split(' · ')[0];
    out.push(`| ${r.resolution} | ${r.workload} | ${eng}${eng !== r.engineRequested ? ` (requested ${r.engineRequested})` : ''} | ${r.encoder} | ${r.status} | ${r.fps ?? '–'} | ${r.wallSeconds} | ${p.decodeWait ?? '–'} | ${p.upload ?? '–'} | ${p.gpuFilter ?? '–'} | ${p.readback ?? '–'} | ${p.encodeWait ?? '–'} | ${r.systemCpuPercent} | ${r.peakAppRamMB} | ${r.peakVramMB ?? '–'} |`);
}
out.push('', 'Per-frame stage times in milliseconds (averages over the job).', '', '## Notes', '');
for (const n of d.notes) out.push(`- ${n}`);
out.push('- "webgl2" in v1.0.1 runs the unchanged v1.0.0 filter engine, so the webgl2 rows are the v1.0.0 baseline for the filter stage on the same machine and FFmpeg build.');
out.push('- Stage times overlap (decoder, GPU and encoder run concurrently); they are not additive.');
console.log(out.join('\n'));
