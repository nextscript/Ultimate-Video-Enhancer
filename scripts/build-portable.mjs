// Builds release/UVE-Manager-<version>-portable.exe: an NSIS self-extractor that asks for a target
// folder and unpacks release/win-unpacked there (see build/portable-extractor.nsi).
// Run after `electron-builder --win dir`.
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const root = process.cwd();
const { version } = JSON.parse(readFileSync(path.join(root, 'package.json'), 'utf8'));
const src = path.join(root, 'release', 'win-unpacked');
if (!existsSync(path.join(src, 'UVE Manager.exe'))) throw new Error('release/win-unpacked is missing – run "electron-builder --win dir" first');

const candidates = [
    process.env.MAKENSIS,
    path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'), 'electron-builder', 'Cache', 'nsis', 'nsis-3.0.4.1', 'Bin', 'makensis.exe'),
    'makensis'
].filter(Boolean);
const makensis = candidates.find(c => c === 'makensis' || existsSync(c));
const out = path.join(root, 'release', `UVE-Manager-${version}-portable.exe`);
rmSync(out, { force: true });

execFileSync(makensis, [
    '/V2', '/INPUTCHARSET', 'UTF8',
    `/DVERSION=${version}`, `/DSRCDIR=${src}`, `/DOUTFILE=${out}`, `/DICON=${path.join(root, 'build', 'icon.ico')}`,
    path.join(root, 'build', 'portable-extractor.nsi')
], { stdio: 'inherit' });
console.log(`Portable self-extractor: ${out}`);
