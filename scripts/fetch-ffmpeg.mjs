// Downloads the portable FFmpeg build (BtbN FFmpeg-Builds, win64 GPL) into tools/ffmpeg.
// Used at build time so installers and the portable exe ship with the tools folder.
import { mkdirSync, existsSync, writeFileSync, readFileSync, renameSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { unzipSync } from 'fflate';

const dir = path.resolve('tools', 'ffmpeg');
const force = process.argv.includes('--force');
if (!force && existsSync(path.join(dir, 'ffmpeg.exe')) && existsSync(path.join(dir, 'ffprobe.exe'))) {
    console.log('tools/ffmpeg already present (use --force to refresh)');
    process.exit(0);
}
const api = 'https://api.github.com/repos/BtbN/FFmpeg-Builds/releases/latest';
const rel = await (await fetch(api, { headers: { 'User-Agent': 'UVE-Manager-build' } })).json();
const asset = rel.assets?.find(a => /^ffmpeg-master-latest-win64-gpl\.zip$/.test(a.name));
const url = asset?.browser_download_url || 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip';
console.log('Downloading', url);
const res = await fetch(url);
if (!res.ok) throw new Error(`HTTP ${res.status}`);
const zip = new Uint8Array(await res.arrayBuffer());
console.log(`Downloaded ${(zip.length / 1048576).toFixed(1)} MB, extracting…`);
const wanted = new Set(['ffmpeg.exe', 'ffprobe.exe', 'LICENSE.txt']);
const files = unzipSync(zip, { filter: f => wanted.has(path.posix.basename(f.name)) && !f.name.split('/').includes('..') });
mkdirSync(dir, { recursive: true });
for (const [name, data] of Object.entries(files)) {
    const target = path.join(dir, path.posix.basename(name));
    writeFileSync(target + '.new', data);
    rmSync(target, { force: true });
    renameSync(target + '.new', target);
}
const ver = execFileSync(path.join(dir, 'ffmpeg.exe'), ['-hide_banner', '-version']).toString().match(/ffmpeg version (\S+)/)?.[1] || 'unknown';
writeFileSync(path.join(dir, 'version.json'), JSON.stringify({ build: asset?.updated_at || rel.published_at || 'latest', version: ver, publishedAt: rel.published_at || '', installedAt: Date.now(), source: url }, null, 2));
console.log('Installed FFmpeg', ver, 'into', dir);
void readFileSync;
