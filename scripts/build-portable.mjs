// Builds release/UVE-Manager-<version>-portable.exe: an NSIS self-extractor that asks for a target
// folder and unpacks release/win-unpacked there (see build/portable-extractor.nsi).
// Run after `electron-builder --win dir`.
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const root = process.cwd();
const { version } = JSON.parse(readFileSync(path.join(root, 'package.json'), 'utf8'));
const src = path.join(root, 'release', 'win-unpacked');
if (!existsSync(path.join(src, 'UVE Manager.exe'))) throw new Error('release/win-unpacked is missing – run "electron-builder --win dir" first');

/** makensis.exe anywhere below dir (newer electron-builder versions unpack NSIS into hashed cache folders). */
function findIn(dir, depth = 5) {
    if (depth < 0 || !existsSync(dir)) return null;
    let entries;
    try { entries = readdirSync(dir, { withFileTypes: true }); } catch { return null; }
    const hit = entries.find(e => e.isFile() && e.name.toLowerCase() === 'makensis.exe');
    if (hit) return path.join(dir, hit.name);
    for (const e of entries) {
        if (!e.isDirectory() || /^(linux|mac)$/i.test(e.name)) continue;
        const r = findIn(path.join(dir, e.name), depth - 1);
        if (r) return r;
    }
    return null;
}

const cache = process.env.ELECTRON_BUILDER_CACHE || path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'), 'electron-builder', 'Cache');
const candidates = [
    process.env.MAKENSIS,
    path.join(cache, 'nsis', 'nsis-3.0.4.1', 'Bin', 'makensis.exe'),
    findIn(path.join(cache, 'nsis')),
    findIn(cache),
    path.join(process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)', 'NSIS', 'makensis.exe'),
    path.join(process.env.ProgramFiles || 'C:\\Program Files', 'NSIS', 'makensis.exe'),
    'makensis'
].filter(Boolean);
const makensis = candidates.find(c => c === 'makensis' || existsSync(c));
console.log(`makensis: ${makensis}`);
const out = path.join(root, 'release', `UVE-Manager-${version}-portable.exe`);
rmSync(out, { force: true });

execFileSync(makensis, [
    '/V2', '/INPUTCHARSET', 'UTF8',
    `/DVERSION=${version}`, `/DSRCDIR=${src}`, `/DOUTFILE=${out}`, `/DICON=${path.join(root, 'build', 'icon.ico')}`,
    path.join(root, 'build', 'portable-extractor.nsi')
], { stdio: 'inherit' });
console.log(`Portable self-extractor: ${out}`);
