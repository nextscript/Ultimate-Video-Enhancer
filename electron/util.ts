import { createHash } from 'node:crypto';
import { promises as fsp } from 'node:fs';
import path from 'node:path';
import { net } from 'electron';
import { unzipSync } from 'fflate';

export async function readJson<T>(file: string, fallback: T): Promise<T> {
    try { return JSON.parse(await fsp.readFile(file, 'utf8')) as T; } catch { return fallback; }
}

/** Atomic JSON write (temp file + rename). */
export async function writeJson(file: string, data: unknown): Promise<void> {
    await fsp.mkdir(path.dirname(file), { recursive: true });
    const tmp = `${file}.${process.pid}.tmp`;
    await fsp.writeFile(tmp, JSON.stringify(data, null, 2), 'utf8');
    await fsp.rename(tmp, file);
}

export const sha256 = (buf: Uint8Array) => createHash('sha256').update(buf).digest('hex');

export async function fileExists(p: string) {
    try { await fsp.access(p); return true; } catch { return false; }
}

export async function isWritableDir(dir: string): Promise<boolean> {
    try {
        await fsp.mkdir(dir, { recursive: true });
        const probe = path.join(dir, `.write-test-${process.pid}-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`);
        await fsp.writeFile(probe, 'x');
        await fsp.unlink(probe);
        return true;
    } catch { return false; }
}

export interface DownloadOptions {
    maxBytes: number;
    onProgress?: (received: number, total: number) => void;
    signal?: AbortSignal;
    accept?: string;
}

/** Downloads a URL into memory with a hard size limit (redirects followed by Electron net). */
export async function download(url: string, opt: DownloadOptions): Promise<Uint8Array> {
    const res = await net.fetch(url, { headers: { 'User-Agent': 'UVE-Manager/1.0.0', Accept: opt.accept || '*/*' }, signal: opt.signal });
    if (!res.ok) {
        // GitHub API without sign-in: 60 requests per hour and IP; the 403 itself says nothing useful
        if ((res.status === 403 || res.status === 429) && res.headers.get('x-ratelimit-remaining') === '0') {
            const reset = Number(res.headers.get('x-ratelimit-reset')) * 1000;
            const at = reset ? new Date(reset).toLocaleTimeString() : 'about an hour';
            throw new Error(`GitHub request limit reached (60 requests per hour per internet connection without sign-in). Please try again after ${at}.`);
        }
        throw new Error(`HTTP ${res.status} for ${url}`);
    }
    const total = Number(res.headers.get('content-length')) || 0;
    if (total > opt.maxBytes) throw new Error(`Download too large (${total} bytes)`);
    const reader = res.body?.getReader();
    if (!reader) return new Uint8Array(await res.arrayBuffer());
    const chunks: Uint8Array[] = [];
    let received = 0;
    for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        received += value.length;
        if (received > opt.maxBytes) { await reader.cancel(); throw new Error('Download exceeds size limit'); }
        chunks.push(value);
        opt.onProgress?.(received, total);
    }
    const out = new Uint8Array(received);
    let o = 0;
    for (const c of chunks) { out.set(c, o); o += c.length; }
    return out;
}

export async function fetchJson<T>(url: string): Promise<T> {
    const buf = await download(url, { maxBytes: 4 * 1024 * 1024, accept: 'application/vnd.github+json, application/json' });
    return JSON.parse(Buffer.from(buf).toString('utf8')) as T;
}

export interface SafeZipLimits { maxEntries: number; maxEntryBytes: number; maxTotalBytes: number; filter?: (name: string) => boolean }

/**
 * Untrusted ZIP reader: entries are returned in memory only (never written using their
 * archive path), with entry count / size limits and path normalization (zip-slip safe).
 */
export function safeUnzip(data: Uint8Array, lim: SafeZipLimits): { name: string; data: Uint8Array }[] {
    let count = 0, total = 0;
    const files = unzipSync(data, {
        filter: f => {
            count++;
            if (count > lim.maxEntries) throw new Error(`ZIP has too many entries (> ${lim.maxEntries})`);
            const name = f.name.replace(/\\/g, '/');
            if (name.endsWith('/')) return false;
            if (name.split('/').some(s => s === '..') || path.isAbsolute(name)) throw new Error(`Unsafe ZIP entry path: ${f.name}`);
            if (f.originalSize > lim.maxEntryBytes) throw new Error(`ZIP entry too large: ${f.name}`);
            total += f.originalSize;
            if (total > lim.maxTotalBytes) throw new Error('ZIP content exceeds the size limit');
            return lim.filter ? lim.filter(name) : true;
        }
    });
    return Object.entries(files).map(([name, d]) => ({ name: name.replace(/\\/g, '/'), data: d }));
}

/** Compares dotted versions ("1.2.10" > "1.2.9"); non-numeric parts compare as strings. */
export function compareVersions(a: string, b: string): number {
    const pa = a.replace(/^v/i, '').split(/[.-]/), pb = b.replace(/^v/i, '').split(/[.-]/);
    for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
        const x = pa[i] ?? '0', y = pb[i] ?? '0';
        const nx = Number(x), ny = Number(y);
        if (Number.isFinite(nx) && Number.isFinite(ny)) { if (nx !== ny) return nx - ny; }
        else if (x !== y) return x < y ? -1 : 1;
    }
    return 0;
}
