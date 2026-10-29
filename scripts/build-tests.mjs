import { readdirSync } from 'node:fs';
import { build } from 'esbuild';

const entries = readdirSync('tests').filter(f => f.endsWith('.test.ts')).map(f => `tests/${f}`);
await build({ entryPoints: entries, outdir: 'dist-tests', bundle: true, platform: 'node', target: 'node22', format: 'esm', outExtension: { '.js': '.mjs' }, logLevel: 'warning' });
