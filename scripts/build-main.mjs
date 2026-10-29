import { build } from 'esbuild';

const common = { bundle: true, platform: 'node', target: 'node22', format: 'cjs', sourcemap: true, external: ['electron'], logLevel: 'info' };

await Promise.all([
    build({ ...common, entryPoints: ['electron/main.ts'], outfile: 'dist-electron/main.js' }),
    build({ ...common, entryPoints: ['electron/preload.ts'], outfile: 'dist-electron/preload.js' }),
    build({ ...common, entryPoints: ['electron/exportPreload.ts'], outfile: 'dist-electron/exportPreload.js' })
]);
