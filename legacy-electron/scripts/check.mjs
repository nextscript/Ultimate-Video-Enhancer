// Syntaxprüfung aller Quelldateien (CommonJS im Main-Prozess, ES-Module im Renderer).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'uve-check-'));
let failed = 0;

function walk(dir, out = []) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out); else if (p.endsWith('.js')) out.push(p);
  }
  return out;
}

function check(file, asModule) {
  let target = file;
  if (asModule) { target = path.join(tmp, path.basename(file, '.js') + '-' + Math.random().toString(36).slice(2) + '.mjs'); fs.copyFileSync(file, target); }
  const r = spawnSync(process.execPath, ['--check', target], { encoding: 'utf8' });
  const rel = path.relative(root, file);
  if (r.status === 0) console.log('ok   ' + rel);
  else { failed++; console.log('FAIL ' + rel + '\n' + r.stderr); }
}

for (const f of walk(path.join(root, 'src', 'main'))) check(f, false);
for (const f of walk(path.join(root, 'src', 'preload'))) check(f, false);
for (const f of walk(path.join(root, 'src', 'renderer'))) check(f, true);
check(path.join(root, 'plugins', 'uve', 'uve.user.js'), false);
fs.rmSync(tmp, { recursive: true, force: true });
console.log(failed ? `\n${failed} Fehler` : '\nAlle Dateien syntaktisch korrekt.');
process.exit(failed ? 1 : 0);
