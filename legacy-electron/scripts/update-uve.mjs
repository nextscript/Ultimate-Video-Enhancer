// Aktualisiert das mitgelieferte UVE-Userscript in plugins/uve/ auf die neueste GitHub-Version.
//   npm run update-uve
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const dir = path.join(root, 'plugins', 'uve');
const manifest = JSON.parse(fs.readFileSync(path.join(dir, 'plugin.json'), 'utf8'));
const target = path.join(dir, manifest.main);

const ver = (t) => (/@version\s+(\S+)/.exec(t) || [])[1] || '?';
const before = fs.existsSync(target) ? ver(fs.readFileSync(target, 'utf8')) : '–';

const res = await fetch(manifest.updateUrl, { cache: 'no-store' });
if (!res.ok) { console.error('Download fehlgeschlagen: HTTP ' + res.status); process.exit(1); }
const text = await res.text();
if (!/==UserScript==/.test(text)) { console.error('Antwort ist kein Userscript.'); process.exit(1); }
fs.writeFileSync(target, text, 'utf8');
console.log(`Ultimate Video Enhancer: ${before} → ${ver(text)}`);
