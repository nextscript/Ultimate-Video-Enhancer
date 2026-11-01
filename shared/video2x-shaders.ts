// v1.0.7: static checks for mpv / libplacebo user shaders (.glsl "hook" files) used by Video2X.
// This is NOT a compiler: it rejects files that cannot be mpv hook shaders, unknown directives and passes that
// bind textures nothing provides. Whether libplacebo actually compiles the code is only known from a test run
// with the real Video2X binary (Settings → AI Upscaling → Video2X → Shaders → Test).

export const SHADER_LIMITS = { maxFileBytes: 1024 * 1024, maxChainBytes: 8 * 1024 * 1024, maxFiles: 32, maxFolderFiles: 64 };

/** Directives understood by mpv / libplacebo user shaders. */
const DIRECTIVES = new Set([
    'DESC', 'HOOK', 'BIND', 'SAVE', 'WIDTH', 'HEIGHT', 'OFFSET', 'WHEN', 'COMPONENTS', 'COMPUTE',
    'TEXTURE', 'SIZE', 'FORMAT', 'FILTER', 'BORDER', 'STORAGE', 'PARAM', 'TYPE', 'MINIMUM', 'MAXIMUM', 'BUFFER', 'VAR'
]);

/** Hook points (textures) mpv / libplacebo provide. */
export const HOOK_STAGES = new Set(['RGB', 'LUMA', 'CHROMA', 'ALPHA', 'NATIVE', 'CHROMA_SCALED', 'ALPHA_SCALED', 'MAIN', 'LINEAR', 'SIGMOID',
    'PREKERNEL', 'POSTKERNEL', 'SCALED', 'OUTPUT', 'XYZ']);

export interface ShaderPass { desc: string; hooks: string[]; binds: string[]; save: string; line: number }
export interface ShaderIssue { line: number; message: string }
export interface ShaderCheck {
    ok: boolean;
    errors: ShaderIssue[];
    warnings: ShaderIssue[];
    passes: ShaderPass[];
    textures: string[];
    params: string[];
}

/** Decodes and size-checks a file's bytes; throws a user-facing error for binary / oversized / non-UTF-8 data. */
export function decodeShaderBytes(name: string, data: Uint8Array): string {
    if (data.length > SHADER_LIMITS.maxFileBytes) throw new Error(`${name}: shader file is larger than ${SHADER_LIMITS.maxFileBytes / 1024} KB.`);
    if (data.includes(0)) throw new Error(`${name}: binary file – not a GLSL shader.`);
    try { return new TextDecoder('utf-8', { fatal: true }).decode(data).replace(/^﻿/, ''); }
    catch { throw new Error(`${name}: the file is not UTF-8 text.`); }
}

/**
 * Parses one shader file. `provided` are textures saved by earlier files of the chain (and their textures), so
 * a chain is checked in its execution order.
 */
export function checkShader(src: string, provided: Iterable<string> = []): ShaderCheck {
    const errors: ShaderIssue[] = [], warnings: ShaderIssue[] = [];
    const passes: ShaderPass[] = [], textures: string[] = [], params: string[] = [];
    if (/^#!/.test(src)) errors.push({ line: 1, message: 'Looks like a script (#!), not a GLSL shader.' });
    if (/<\s*(script|html)\b/i.test(src)) errors.push({ line: 1, message: 'Contains HTML/script markup – not a GLSL shader.' });
    const lines = src.split(/\r?\n/);
    type Block = { kind: 'pass' | 'texture' | 'param' | 'buffer'; pass?: ShaderPass };
    let block: Block | null = null;
    let inHeader = false;
    for (let i = 0; i < lines.length; i++) {
        const raw = lines[i];
        const m = raw.match(/^\s*\/\/!\s*([A-Za-z_]+)\s*(.*?)\s*$/);
        if (!m) { inHeader = false; continue; }
        const dir = m[1].toUpperCase(), arg = m[2];
        const ln = i + 1;
        if (!DIRECTIVES.has(dir)) { errors.push({ line: ln, message: `Unknown directive //!${m[1]}` }); continue; }
        // a new block starts with the first directive after code (or HOOK/TEXTURE/PARAM/BUFFER themselves)
        const starts = dir === 'TEXTURE' ? 'texture' : dir === 'PARAM' ? 'param' : dir === 'BUFFER' ? 'buffer' : null;
        if (starts || !inHeader) {
            if (starts) block = { kind: starts };
            else { const pass: ShaderPass = { desc: '', hooks: [], binds: [], save: '', line: ln }; passes.push(pass); block = { kind: 'pass', pass }; }
            inHeader = true;
        }
        if (dir === 'TEXTURE') { if (!arg) errors.push({ line: ln, message: '//!TEXTURE needs a name' }); else textures.push(arg); continue; }
        if (dir === 'PARAM') { if (!arg) errors.push({ line: ln, message: '//!PARAM needs a name' }); else params.push(arg); continue; }
        const p = block?.kind === 'pass' ? block.pass! : null;
        if (!p) continue;
        if (dir === 'DESC') p.desc = arg;
        else if (dir === 'HOOK') { if (!arg) errors.push({ line: ln, message: '//!HOOK needs a texture name' }); else p.hooks.push(arg); }
        else if (dir === 'BIND') { if (!arg) errors.push({ line: ln, message: '//!BIND needs a texture name' }); else p.binds.push(arg); }
        else if (dir === 'SAVE') p.save = arg;
        else if (dir === 'COMPONENTS' && !/^[1-4]$/.test(arg)) errors.push({ line: ln, message: `//!COMPONENTS must be 1-4 (got "${arg}")` });
    }
    const hookPasses = passes.filter(p => p.hooks.length);
    if (!hookPasses.length) errors.push({ line: 1, message: 'No //!HOOK pass found – this is not an mpv/libplacebo user shader.' });
    for (const p of passes) if (!p.hooks.length) errors.push({ line: p.line, message: `Pass "${p.desc || 'unnamed'}" has no //!HOOK.` });
    // dependencies in execution order: hook stages, HOOKED, textures, and names saved by earlier passes
    const known = new Set<string>([...HOOK_STAGES, 'HOOKED', ...textures, ...provided]);
    for (const p of hookPasses) {
        for (const h of p.hooks) if (!HOOK_STAGES.has(h) && !known.has(h)) warnings.push({ line: p.line, message: `Pass "${p.desc || 'unnamed'}" hooks "${h}", which is not a standard hook point.` });
        for (const b of p.binds) {
            if (!known.has(b)) errors.push({ line: p.line, message: `Missing dependency: pass "${p.desc || 'unnamed'}" binds "${b}", but no earlier pass saves it and no //!TEXTURE provides it.` });
        }
        if (p.save) known.add(p.save);
    }
    if (!/\bvec4\s+hook\s*\(|\bvoid\s+hook\s*\(/.test(src) && hookPasses.length) errors.push({ line: 1, message: 'No hook() function found.' });
    return { ok: errors.length === 0, errors, warnings, passes, textures, params };
}

/** Names the passes of a file make available to later files (saved textures + global textures). */
export function providedBy(c: ShaderCheck): string[] {
    return [...c.passes.map(p => p.save).filter(Boolean), ...c.textures];
}

export interface ChainFile { name: string; text: string; enabled: boolean }

/** Checks the enabled files of a chain in order; returns per-file results and the first error. */
export function checkChain(files: ChainFile[]): { ok: boolean; results: (ShaderCheck | null)[]; error: string; passes: number } {
    const enabled = files.filter(f => f.enabled);
    if (!enabled.length) return { ok: false, results: files.map(() => null), error: 'The preset has no enabled shader.', passes: 0 };
    if (enabled.length > SHADER_LIMITS.maxFiles) return { ok: false, results: files.map(() => null), error: `A chain can have at most ${SHADER_LIMITS.maxFiles} shader files.`, passes: 0 };
    const total = enabled.reduce((a, f) => a + f.text.length, 0);
    if (total > SHADER_LIMITS.maxChainBytes) return { ok: false, results: files.map(() => null), error: 'The shader chain is too large.', passes: 0 };
    const provided: string[] = [];
    let error = '', passes = 0;
    const results = files.map(f => {
        if (!f.enabled) return null;
        const c = checkShader(f.text, provided);
        provided.push(...providedBy(c));
        passes += c.passes.filter(p => p.hooks.length).length;
        if (!c.ok && !error) error = `${f.name}, line ${c.errors[0].line}: ${c.errors[0].message}`;
        return c;
    });
    return { ok: !error, results, error, passes };
}

/** One shader file containing the enabled passes in order (mpv/libplacebo accept several hooks per file). */
export function combineChain(files: ChainFile[]): string {
    return files.filter(f => f.enabled).map((f, i) => `// ===== UVE Manager shader chain – pass file ${i + 1}: ${f.name.replace(/[\r\n]/g, ' ')} =====\n${f.text.replace(/\s*$/, '')}\n`).join('\n');
}
