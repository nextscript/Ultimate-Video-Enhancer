'use strict';
// uve:// – eigenes, privilegiertes Schema. Oberfläche, Plugins und Medien teilen sich
// den Ursprung uve://app. Dadurch sind Video-Frames für Canvas/WebGL (UVE GPU-Modus,
// Scopes, Snapshots) nicht "tainted", was bei file:// der Fall wäre.
const { protocol, net } = require('electron');
const fs = require('fs');
const path = require('path');
const registry = require('./media-registry');
const pluginHost = require('./plugin-host');

const RENDERER_ROOT = path.join(__dirname, '..', 'renderer');

const STATIC_MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json; charset=utf-8', '.svg': 'image/svg+xml',
  '.png': 'image/png', '.ico': 'image/x-icon', '.woff2': 'font/woff2', '.cube': 'text/plain; charset=utf-8', '.txt': 'text/plain; charset=utf-8'
};

function registerPrivileged() {
  protocol.registerSchemesAsPrivileged([{
    scheme: 'uve',
    privileges: { standard: true, secure: true, supportFetchAPI: true, stream: true, corsEnabled: true, codeCache: true }
  }]);
}

// Node-Stream -> Web-Stream mit sauberem Abbruch (Video-Element bricht Range-Anfragen ständig ab).
function toWeb(rs) {
  return new ReadableStream({
    start(ctrl) {
      rs.on('data', (chunk) => {
        try {
          ctrl.enqueue(new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength));
          if (ctrl.desiredSize !== null && ctrl.desiredSize <= 0) rs.pause();
        } catch (_) { rs.destroy(); }
      });
      rs.on('end', () => { try { ctrl.close(); } catch (_) { } });
      rs.on('error', (e) => { try { ctrl.error(e); } catch (_) { } });
    },
    pull() { rs.resume(); },
    cancel() { rs.destroy(); }
  }, { highWaterMark: 4 });
}

async function serveFile(filePath, request, mime) {
  let stat;
  try { stat = await fs.promises.stat(filePath); } catch (_) { return new Response('Nicht gefunden', { status: 404 }); }
  if (!stat.isFile()) return new Response('Nicht gefunden', { status: 404 });
  const size = stat.size;
  const type = mime || registry.mimeOf(filePath);
  const base = { 'Content-Type': type, 'Accept-Ranges': 'bytes', 'Cache-Control': 'no-store' };
  const range = request.headers.get('range');
  if (range && size > 0) {
    const m = /bytes=(\d*)-(\d*)/.exec(range);
    if (m) {
      let start, end;
      if (m[1] === '') { start = Math.max(0, size - Number(m[2] || 0)); end = size - 1; }
      else { start = Number(m[1]); end = m[2] === '' ? size - 1 : Math.min(Number(m[2]), size - 1); }
      if (start >= size || start > end) {
        return new Response(null, { status: 416, headers: { ...base, 'Content-Range': `bytes */${size}` } });
      }
      return new Response(toWeb(fs.createReadStream(filePath, { start, end, highWaterMark: 1 << 20 })), {
        status: 206,
        headers: { ...base, 'Content-Length': String(end - start + 1), 'Content-Range': `bytes ${start}-${end}/${size}` }
      });
    }
  }
  return new Response(toWeb(fs.createReadStream(filePath, { highWaterMark: 1 << 20 })), {
    status: 200, headers: { ...base, 'Content-Length': String(size) }
  });
}

function safeJoin(root, rel) {
  const full = path.normalize(path.join(root, rel));
  const r = path.normalize(root + path.sep);
  return full.toLowerCase().startsWith(r.toLowerCase()) ? full : null;
}

async function proxyRemote(target, request) {
  if (!registry.isAllowedRemote(target)) return new Response('Nicht freigegeben', { status: 403 });
  const headers = {};
  const range = request.headers.get('range');
  if (range) headers.Range = range;
  try {
    const res = await net.fetch(target, { headers, bypassCustomProtocolHandlers: true });
    const out = new Headers();
    for (const h of ['content-type', 'content-length', 'content-range', 'accept-ranges']) {
      const v = res.headers.get(h);
      if (v) out.set(h, v);
    }
    out.set('Cache-Control', 'no-store');
    return new Response(res.body, { status: res.status, statusText: res.statusText, headers: out });
  } catch (e) {
    return new Response('Netzwerkfehler: ' + e.message, { status: 502 });
  }
}

function handle() {
  protocol.handle('uve', async (request) => {
    let url;
    try { url = new URL(request.url); } catch (_) { return new Response('Ungültig', { status: 400 }); }
    if (url.hostname !== 'app') return new Response('Unbekannter Host', { status: 404 });
    const pathname = decodeURIComponent(url.pathname);

    if (pathname === '/media') {
      const p = url.searchParams.get('p') || '';
      if (!registry.isAllowed(p)) return new Response('Nicht freigegeben', { status: 403 });
      return serveFile(p, request);
    }
    if (pathname === '/live') {
      return require('./stream').serve(url.searchParams.get('id') || '', Math.max(0, Number(url.searchParams.get('ss')) || 0));
    }
    if (pathname === '/remote') {
      return proxyRemote(url.searchParams.get('u') || '', request);
    }
    if (pathname.startsWith('/plugins/')) {
      const [, , id, ...rest] = pathname.split('/');
      const file = pluginHost.resolveFile(id, rest.join('/'));
      if (!file) return new Response('Nicht gefunden', { status: 404 });
      return serveFile(file, request, STATIC_MIME[path.extname(file).toLowerCase()]);
    }
    const rel = pathname === '/' ? 'index.html' : pathname.replace(/^\/+/, '');
    const file = safeJoin(RENDERER_ROOT, rel);
    if (!file) return new Response('Verboten', { status: 403 });
    return serveFile(file, request, STATIC_MIME[path.extname(file).toLowerCase()]);
  });
}

module.exports = { registerPrivileged, handle };
