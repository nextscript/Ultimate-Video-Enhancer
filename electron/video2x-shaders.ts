// v1.0.7: Video2X shader library – custom mpv/libplacebo GLSL presets (ordered multi-file chains).
// Layout (user data folder, survives app updates):
//   video2x/shaders/library.json        presets, order, enable flags, hashes, source/author/license
//   video2x/shaders/files/<sha256>.glsl copies of imported files (import mode "copy")
//   video2x/shaders/chains/<sha256>.glsl concatenated enabled passes handed to Video2X at render time
// Files imported as "reference" stay where they are; their hash is checked before every use.
import { createHash } from 'node:crypto';
import { promises as fsp } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { isFilterSafePath } from '../shared/video2x-cli';
import { SHADER_LIMITS, checkChain, checkShader, combineChain, decodeShaderBytes } from '../shared/video2x-shaders';
import type { Video2xFileState, Video2xPreset, Video2xShaderFile } from '../shared/video2x-types';
import { userDir } from './store';
import { readJson, writeJson } from './util';

interface Library { schema: 'uve-video2x-shaders'; version: 1; presets: Video2xPreset[]; tests: Record<string, { ok: boolean; at: number; message: string }> }

export const shaderRoot = () => process.env.UVE_VIDEO2X_SHADER_DIR || path.join(userDir(), 'video2x', 'shaders');
const libFile = () => path.join(shaderRoot(), 'library.json');
const sha = (b: Uint8Array | string) => createHash('sha256').update(b).digest('hex');

export async function loadLibrary(): Promise<Library> {
    const l = await readJson<Partial<Library>>(libFile(), {});
    const presets = Array.isArray(l.presets) ? l.presets.filter(p => p && typeof p.id === 'string' && p.kind === 'custom') : [];
    return { schema: 'uve-video2x-shaders', version: 1, presets, tests: l.tests && typeof l.tests === 'object' ? l.tests : {} };
}
const saveLibrary = (l: Library) => writeJson(libFile(), l);

const newId = () => `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 7)}`;
const cleanName = (n: string, fallback: string) => n.replace(/[\u0000-\u001f]/g, '').trim().slice(0, 80) || fallback;

/** Reads and checks a file the user picked (or a referenced file); throws user-facing errors. */
async function readShader(file: string): Promise<{ text: string; bytes: Buffer }> {
    if (!/\.(glsl|hook)$/i.test(file)) throw new Error(`${path.basename(file)}: only .glsl (or .hook) mpv shader files can be imported.`);
    const st = await fsp.lstat(file);
    if (st.isSymbolicLink()) throw new Error(`${path.basename(file)}: symbolic links are not imported – select the target file.`);
    if (!st.isFile()) throw new Error(`${path.basename(file)} is not a file.`);
    if (st.size > SHADER_LIMITS.maxFileBytes) throw new Error(`${path.basename(file)}: shader file is larger than ${SHADER_LIMITS.maxFileBytes / 1024} KB.`);
    const bytes = await fsp.readFile(file);
    return { text: decodeShaderBytes(path.basename(file), bytes), bytes };
}

/** Text of a library file after checking presence and hash. */
async function fileText(f: Video2xShaderFile): Promise<{ state: Video2xFileState; text: string; message: string }> {
    let bytes: Buffer;
    try { bytes = await fsp.readFile(f.path); } catch { return { state: 'missing', text: '', message: `File not found: ${f.path}` }; }
    if (sha(bytes) !== f.sha256) return { state: 'modified', text: '', message: `${f.name} was modified after import (hash mismatch). Re-import it or accept the change.` };
    try { return { state: 'ok', text: decodeShaderBytes(f.name, bytes), message: '' }; }
    catch (e) { return { state: 'invalid', text: '', message: e instanceof Error ? e.message : String(e) }; }
}

/** Per-file state plus the chain check, used by the status / preset list. */
export async function inspectPreset(p: Video2xPreset): Promise<{ files: { state: Video2xFileState; message: string }[]; problem: string; passes: number }> {
    const texts = await Promise.all(p.shaderFiles.map(fileText));
    const files: { state: Video2xFileState; message: string }[] = texts.map(t => ({ state: t.state, message: t.message }));
    const bad = texts.find((t, i) => p.shaderFiles[i].enabled && t.state !== 'ok');
    if (bad) return { files, problem: bad.message, passes: 0 };
    const chain = checkChain(p.shaderFiles.map((f, i) => ({ name: f.name, text: texts[i].text, enabled: f.enabled })));
    chain.results.forEach((r, i) => { if (r && !r.ok) files[i] = { state: 'invalid', message: `line ${r.errors[0].line}: ${r.errors[0].message}` }; else if (r?.warnings.length) files[i] = { state: 'ok', message: r.warnings[0].message }; });
    return { files, problem: chain.ok ? '' : chain.error, passes: chain.passes };
}

/** Directory for generated chain files, inside a path Video2X can quote (falls back to the temp folder). */
async function chainDir(): Promise<string> {
    for (const base of [path.join(shaderRoot(), 'chains'), path.join(os.tmpdir(), 'uve-manager-video2x', 'chains')]) {
        if (isFilterSafePath(base)) { await fsp.mkdir(base, { recursive: true }); return base; }
    }
    throw new Error('The UVE Manager data folder and the temp folder contain characters Video2X cannot pass to libplacebo (e.g. a quote). Move the data folder or set TEMP to a simpler path.');
}

/** Writes the combined, verified chain of a custom preset and returns its path and hash. */
export async function materializeChain(presetId: string): Promise<{ file: string; hash: string; preset: Video2xPreset }> {
    const lib = await loadLibrary();
    const p = lib.presets.find(x => x.id === presetId);
    if (!p) throw new Error(`The Video2X shader preset "${presetId}" does not exist any more – choose another preset in Output Settings.`);
    const texts = await Promise.all(p.shaderFiles.map(fileText));
    for (let i = 0; i < texts.length; i++) if (p.shaderFiles[i].enabled && texts[i].state !== 'ok') throw new Error(`Shader preset "${p.displayName}": ${texts[i].message}`);
    const files = p.shaderFiles.map((f, i) => ({ name: f.name, text: texts[i].text, enabled: f.enabled }));
    const chain = checkChain(files);
    if (!chain.ok) throw new Error(`Shader preset "${p.displayName}" is not usable: ${chain.error}`);
    const text = combineChain(files);
    const hash = sha(text);
    const file = path.join(await chainDir(), `${hash}.glsl`);
    if (!(await fsp.stat(file).catch(() => null))) await fsp.writeFile(file, text, 'utf8');
    return { file, hash, preset: p };
}

async function addFiles(p: Video2xPreset, files: string[], mode: 'copy' | 'reference') {
    if (p.shaderFiles.length + files.length > SHADER_LIMITS.maxFiles) throw new Error(`A preset can have at most ${SHADER_LIMITS.maxFiles} shader files.`);
    const filesDir = path.join(shaderRoot(), 'files');
    for (const src of files) {
        const abs = path.resolve(src);
        const { text, bytes } = await readShader(abs);
        const c = checkShader(text);
        if (!c.ok) throw new Error(`${path.basename(abs)}, line ${c.errors[0].line}: ${c.errors[0].message}`);
        const hash = sha(bytes);
        let target = abs;
        if (mode === 'copy') {
            await fsp.mkdir(filesDir, { recursive: true });
            target = path.join(filesDir, `${hash}.glsl`);
            if (!(await fsp.stat(target).catch(() => null))) await fsp.writeFile(target, bytes);
        }
        p.shaderFiles.push({ name: path.basename(abs), path: target, sha256: hash, size: bytes.length, enabled: true, mode, origin: abs });
    }
}

/** Imports files into a preset (new preset when `presetId` is empty). Returns the preset id. */
export async function importShaderFiles(files: string[], o: { presetId?: string; mode: 'copy' | 'reference'; name?: string }): Promise<string> {
    if (!files.length) throw new Error('No shader file selected.');
    const lib = await loadLibrary();
    let p = o.presetId ? lib.presets.find(x => x.id === o.presetId) : undefined;
    if (o.presetId && !p) throw new Error('Preset not found.');
    const fresh = !p;
    if (!p) p = { id: newId(), displayName: cleanName(o.name || path.basename(files[0]).replace(/\.(glsl|hook)$/i, ''), 'Custom shader'), kind: 'custom', shaderFiles: [], createdAt: Date.now(), source: path.dirname(path.resolve(files[0])) };
    await addFiles(p, files, o.mode);
    if (fresh) lib.presets.push(p);
    delete lib.tests[p.id];
    await saveLibrary(lib);
    return p.id;
}

/** Lists the .glsl files of a folder (not recursive, name order) for "Import folder". */
export async function shaderFilesInFolder(dir: string): Promise<string[]> {
    const entries = await fsp.readdir(dir, { withFileTypes: true });
    const files = entries.filter(e => e.isFile() && /\.(glsl|hook)$/i.test(e.name)).map(e => path.join(dir, e.name)).sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
    if (!files.length) throw new Error('The folder contains no .glsl shader files.');
    if (files.length > SHADER_LIMITS.maxFolderFiles) throw new Error(`The folder contains more than ${SHADER_LIMITS.maxFolderFiles} shader files.`);
    return files;
}

export type PresetEdit =
    | { op: 'create'; name: string }
    | { op: 'rename'; id: string; name: string }
    | { op: 'duplicate'; id: string }
    | { op: 'remove'; id: string }
    | { op: 'meta'; id: string; source?: string; author?: string; license?: string }
    | { op: 'move'; id: string; index: number; dir: -1 | 1 }
    | { op: 'toggle'; id: string; index: number }
    | { op: 'removeFile'; id: string; index: number }
    | { op: 'acceptChange'; id: string; index: number };

export async function editPreset(e: PresetEdit): Promise<string> {
    const lib = await loadLibrary();
    if (e.op === 'create') {
        const p: Video2xPreset = { id: newId(), displayName: cleanName(e.name, 'Custom preset'), kind: 'custom', shaderFiles: [], createdAt: Date.now() };
        lib.presets.push(p);
        await saveLibrary(lib);
        return p.id;
    }
    const p = lib.presets.find(x => x.id === e.id);
    if (!p) throw new Error('Preset not found.');
    const at = (i: number) => { if (!(i >= 0 && i < p.shaderFiles.length)) throw new Error('Shader not found.'); return p.shaderFiles[i]; };
    switch (e.op) {
        case 'rename': p.displayName = cleanName(e.name, p.displayName); break;
        case 'duplicate': {
            const copy: Video2xPreset = { ...structuredClone(p), id: newId(), displayName: `${p.displayName} (copy)`, createdAt: Date.now() };
            lib.presets.push(copy);
            await saveLibrary(lib);
            return copy.id;
        }
        case 'remove': {
            lib.presets = lib.presets.filter(x => x !== p);
            delete lib.tests[p.id];
            // copies no other preset uses are deleted; referenced (external) files are never touched
            const used = new Set(lib.presets.flatMap(x => x.shaderFiles.map(f => f.path)));
            for (const f of p.shaderFiles) if (f.mode === 'copy' && !used.has(f.path) && path.dirname(f.path) === path.join(shaderRoot(), 'files')) await fsp.rm(f.path, { force: true });
            break;
        }
        case 'meta':
            if (e.source !== undefined) p.source = e.source.slice(0, 500);
            if (e.author !== undefined) p.author = e.author.slice(0, 200);
            if (e.license !== undefined) p.license = e.license.slice(0, 200);
            break;
        case 'move': {
            const j = e.index + e.dir;
            at(e.index);
            if (j < 0 || j >= p.shaderFiles.length) break;
            [p.shaderFiles[e.index], p.shaderFiles[j]] = [p.shaderFiles[j], p.shaderFiles[e.index]];
            break;
        }
        case 'toggle': at(e.index).enabled = !at(e.index).enabled; break;
        case 'removeFile': at(e.index); p.shaderFiles.splice(e.index, 1); break;
        case 'acceptChange': {
            const f = at(e.index);
            const { text, bytes } = await readShader(f.path);
            const c = checkShader(text);
            if (!c.ok) throw new Error(`${f.name}, line ${c.errors[0].line}: ${c.errors[0].message}`);
            f.sha256 = sha(bytes); f.size = bytes.length;
            break;
        }
    }
    if (e.op !== 'remove') delete lib.tests[p.id];
    await saveLibrary(lib);
    return p.id;
}

export async function recordTest(presetId: string, r: { ok: boolean; message: string }) {
    const lib = await loadLibrary();
    lib.tests[presetId] = { ok: r.ok, at: Date.now(), message: r.message.slice(0, 2000) };
    await saveLibrary(lib);
}

/** Content hash of a preset's enabled files (preview cache key). */
export async function presetHash(presetId: string): Promise<string> {
    const lib = await loadLibrary();
    const p = lib.presets.find(x => x.id === presetId);
    return p ? sha(JSON.stringify(p.shaderFiles.filter(f => f.enabled).map(f => f.sha256))) : presetId;
}
