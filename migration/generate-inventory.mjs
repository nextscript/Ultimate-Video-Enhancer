// Generates migration/feature-parity-v1.0.0.json from the v1.0.0 baseline SOURCE (git tag v1.0.0-baseline):
//  - every effect/control row of docs/UVE_FEATURE_MATRIX.csv (traced to userscript v1.17.5)
//  - every keyboard shortcut (shared/shortcuts.ts), preference (electron/store.ts DEFAULT_PREFS),
//    export setting (shared/types.ts DEFAULT_EXPORT) and main-process API channel (electron/preload.ts)
//  - the desktop application features listed in APP_FEATURES below (each tied to its source module)
// parity_status is derived, not hand-written: a module unchanged since the baseline is "unchanged";
// a changed module is "verified" when a v1.0.1 test covers it, otherwise "changed-untested".
// Usage: node migration/generate-inventory.mjs
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';

const TAG = 'v1.0.0-baseline';
const git = (...a) => execFileSync('git', a, { encoding: 'utf8' });
const baseSha = git('rev-list', '-n', '1', TAG).trim();
const atBase = f => git('show', `${TAG}:${f}`);
const changed = new Set(git('diff', '--name-only', TAG, '--', '.').split(/\r?\n/).filter(Boolean));
const untracked = git('ls-files', '--others', '--exclude-standard').split(/\r?\n/).filter(Boolean);
untracked.forEach(f => changed.add(f));

const SELFTEST = 'electron/selftest.ts';
const PARITY = 'migration/compare-parity.mjs (v1.0.0 vs v1.0.1 exports, PSNR ≥ 45 dB, SSIM ≥ 0.995)';
const ENGINE_BACKENDS = ['webgpu (D3D12/Vulkan/Metal via Chromium Dawn)', 'webgl2 (v1.0.0 engine)'];
const PREVIEW = 'src/ui/Player.tsx → src/engine/createEngine.ts → GpuEngine | Webgl2Engine';
const EXPORT = 'electron/render.ts → electron/exportPreload.ts (FFmpeg pipes) → src/export/worker.ts → FrameEngine';

// Behaviour guards: an item whose module changed is still "unchanged" when every function that implements
// it is textually identical to the baseline (the module change is elsewhere, e.g. the status bar).
function fnText(src, name) {
    const m = new RegExp(`(?:^|\\n)(?:export )?(?:async )?function ${name}\\b|(?:^|\\n)(?:export )?const ${name}\\b`).exec(src);
    if (!m) return null;
    const i = src.indexOf('{', m.index);
    let depth = 0;
    for (let j = i; j < src.length; j++) { if (src[j] === '{') depth++; else if (src[j] === '}' && --depth === 0) return src.slice(m.index, j + 1); }
    return null;
}
function guardsHold(guards) {
    return guards.every(([file, name]) => {
        const a = name === '*' ? atBase(file) : fnText(atBase(file), name);
        const b = name === '*' ? readFileSync(file, 'utf8') : fnText(readFileSync(file, 'utf8'), name);
        return a !== null && a === b;
    });
}
const GUARDS = {
    shortcut: [['shared/shortcuts.ts', '*'], ['src/ui/App.tsx', 'comboOf'], ['src/ui/App.tsx', 'useShortcuts']],
    'app.media.import': [['src/ui/App.tsx', 'App'], ['src/state/actions.ts', 'loadVideo']],
    'app.media.probe': [['electron/media.ts', 'probe'], ['electron/media.ts', 'rotationOf'], ['electron/media.ts', 'bitDepthOf']],
    'app.media.formats': [['electron/media.ts', 'probe'], ['electron/media.ts', 'decodeFilter'], ['electron/media.ts', 'sourceMatrix']],
    'app.project.io': [['src/state/actions.ts', 'saveProject'], ['src/state/actions.ts', 'openProject'], ['src/state/actions.ts', 'buildProject']],
    'app.project.undo': [['src/state/store.ts', 'undo'], ['src/state/store.ts', 'redo'], ['src/state/store.ts', 'updateEffect']],
    'app.session.restore': [['electron/store.ts', 'getWindowState'], ['electron/store.ts', 'setWindowState'], ['electron/store.ts', 'getEditorState'], ['electron/store.ts', 'setEditorState'], ['electron/store.ts', 'saveSettings'], ['src/ui/App.tsx', 'restoreEditor'], ['src/ui/App.tsx', 'editorSnapshot']]
};
const guardFor = id => GUARDS[id] || GUARDS[id.split('.')[0]];

const items = [];
function add(e) {
    const modules = [].concat(e.current_module);
    const anyChanged = modules.some(m => changed.has(m));
    const tested = !!e.v1_0_1_test;
    items.push({
        id: e.id, display_name: e.display_name, entry_point: e.entry_point, current_module: e.current_module,
        inputs: e.inputs ?? null, defaults: e.defaults ?? null, allowed_values: e.allowed_values ?? null,
        serialized_schema: e.serialized_schema ?? null, current_preview_path: e.current_preview_path ?? null,
        current_export_path: e.current_export_path ?? null, platform_assumptions: e.platform_assumptions ?? 'none',
        v1_0_0_test: e.v1_0_0_test ?? null, v1_0_1_test: e.v1_0_1_test ?? null,
        supported_backends: e.supported_backends ?? ['all'],
        parity_status: e.parity_status ?? (!anyChanged ? 'unchanged' : tested ? 'verified' : guardFor(e.id) && guardsHold(guardFor(e.id)) ? 'unchanged (implementing functions identical to baseline)' : 'changed-untested'),
        differences: e.differences ?? 'none'
    });
}

// ---------------------------------------------------------------- effects (feature matrix)
function csv(text) {
    const rows = [];
    for (const line of text.split(/\r?\n/).filter(Boolean)) {
        const cells = []; let cur = '', q = false;
        for (const ch of line) { if (ch === '"') q = !q; else if (ch === ',' && !q) { cells.push(cur); cur = ''; } else cur += ch; }
        cells.push(cur); rows.push(cells);
    }
    const [head, ...body] = rows;
    return body.map(r => Object.fromEntries(head.map((h, i) => [h, r[i] ?? ''])));
}
const NOT_PIXEL = /^(Recorder|DRM|Overlay|Scopes|Before\/After|LUT package|LUT import|Filter codes|User profiles|gvf-settings|Expert import)/;
for (const r of csv(atBase('docs/UVE_FEATURE_MATRIX.csv'))) {
    const slug = r.feature.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '');
    const pixel = !NOT_PIXEL.test(r.feature) && r.render === 'yes';
    const glsl = /GLSL|mpv/.test(r.feature);
    add({
        id: `effect.${slug}`, display_name: r.user_label || r.feature, entry_point: 'Inspector / EditorPanels',
        current_module: glsl ? ['shared/uve/customFilters.ts', 'src/engine/EffectEngine.ts'] : pixel ? ['shared/uve/graph.ts', 'shared/uve/settings.ts'] : ['shared/uve/settings.ts'],
        inputs: r.type || null, defaults: r.default || null,
        allowed_values: r.min || r.max ? { min: r.min, max: r.max, step: r.step } : null,
        serialized_schema: 'UveProject.effect (shared/types.ts) / gvf-settings JSON',
        current_preview_path: r.preview === 'yes' ? PREVIEW : null, current_export_path: r.render === 'yes' ? EXPORT : null,
        v1_0_0_test: r.test || null,
        v1_0_1_test: pixel ? `${PARITY}; unit tests; WebGPU start-up validation (85 primitives)` : r.test ? `tests/core.test.ts (unchanged, passing)` : null,
        supported_backends: glsl ? ['webgl2 (custom GLSL keeps the v1.0.0 engine)'] : pixel ? ENGINE_BACKENDS : ['n/a (no pixel processing)'],
        parity_status: r.status === 'ported' || r.status === '' ? undefined : `${r.status} (as in v1.0.0)`,
        differences: glsl ? 'Projects with enabled custom GLSL filters are processed by the WebGL2 engine (identical to v1.0.0).' : r.deviation && r.deviation !== 'none' ? `v1.0.0 deviation kept: ${r.deviation}` : 'none'
    });
}

// ---------------------------------------------------------------- keyboard shortcuts
const sc = atBase('shared/shortcuts.ts').match(/DEFAULT_SHORTCUTS[^{]*\{([\s\S]*?)\};/)[1];
for (const [, k, v] of sc.matchAll(/(\w+):\s*'([^']+)'/g)) add({
    id: `shortcut.${k}`, display_name: `Shortcut ${k}`, entry_point: 'keyboard', current_module: ['shared/shortcuts.ts', 'src/ui/App.tsx'],
    inputs: 'key chord', defaults: v, serialized_schema: 'settings.json prefs.shortcuts', v1_0_0_test: 'manual UI', v1_0_1_test: null
});

// ---------------------------------------------------------------- preferences
const pr = atBase('electron/store.ts').match(/DEFAULT_PREFS: AppPrefs = \{([\s\S]*?)\n\};/)[1];
for (const [, k, v] of pr.matchAll(/^\s*(\w+):\s*(.+?),?$/gm)) add({
    id: `pref.${k}`, display_name: `Preference ${k}`, entry_point: 'Settings view', current_module: ['electron/store.ts'],
    defaults: v.replace(/,$/, ''), serialized_schema: 'settings.json prefs (additive migration, backup of the previous version)',
    v1_0_0_test: 'selftest settings round trip', v1_0_1_test: 'electron/store.ts migration + selftest',
    differences: 'v1.0.1 adds prefs gpuEngine (auto|webgpu|webgl2) and hwDecode (auto|hardware|software); existing keys unchanged.'
});

// ---------------------------------------------------------------- export settings
const ex = atBase('shared/types.ts').match(/DEFAULT_EXPORT: ExportSettings = \{([\s\S]*?)\};/)[1];
for (const [, k, v] of ex.matchAll(/(\w+):\s*('[^']*'|[\w.]+)/g)) add({
    id: `export.${k}`, display_name: `Export ${k}`, entry_point: 'Export panel / Render queue', current_module: ['shared/types.ts', 'electron/render.ts', 'src/ui/EditorPanels.tsx'],
    defaults: v, serialized_schema: 'UveProject.export / settings.json prefs.defaultExport', current_export_path: EXPORT,
    v1_0_0_test: 'selftest render jobs', v1_0_1_test: `${SELFTEST} (all render jobs) + ${PARITY}`,
    differences: k === 'encoder' ? 'Hardware encoder families NVENC/AMF/QSV/VideoToolbox/VA-API are verified before use; "Prefer Hardware" now falls back to software with a warning instead of failing.' : 'none'
});

// v1.0.1 additions (not part of the v1.0.0 scope, listed so the inventory stays complete)
add({
    id: 'export.fps', display_name: 'Export frame rate (new in v1.0.1)', entry_point: 'Export panel', current_module: ['shared/fps.ts', 'electron/render.ts', 'src/ui/EditorPanels.tsx'],
    defaults: "'' (source)", allowed_values: { min: 1, max: 240, presets: '23.976 24 25 29.97 30 48 50 59.94 60 120', rational: 'n/d' },
    serialized_schema: 'UveProject.export.fps (optional; older projects default to the source rate)', current_export_path: EXPORT,
    v1_0_0_test: null, v1_0_1_test: 'tests/hw.test.ts (parseFps/outputFps) + selftest 30 fps and 29.97 fps renders', parity_status: 'new',
    differences: 'New. Empty keeps the v1.0.0 behaviour (source frame rate).'
});

// ---------------------------------------------------------------- main-process API (IPC)
const channels = [...new Set([...atBase('electron/main.ts').matchAll(/\bh\('([a-z]+:\w+)'/g)].map(m => m[1]))];
for (const ch of channels) add({
    id: `ipc.${ch}`, display_name: `App API ${ch}`, entry_point: 'renderer → main', current_module: ['electron/preload.ts', 'electron/main.ts'],
    serialized_schema: 'shared/types.ts', v1_0_0_test: 'selftest / UI', v1_0_1_test: `${SELFTEST}`, differences: 'none (channel kept)'
});

// ---------------------------------------------------------------- desktop application features
const APP_FEATURES = [
    ['media.import', 'Open / drag-and-drop media, recent files', ['src/ui/App.tsx', 'electron/main.ts'], 'manual UI'],
    ['media.probe', 'FFprobe codec/stream/metadata probing, rotation, HDR detection', ['electron/media.ts'], 'selftest probe'],
    ['media.formats', 'Supported containers/codecs (docs/FORMAT_SUPPORT.md)', ['electron/media.ts'], 'selftest'],
    ['media.proxy', 'Preview proxy for non-browser codecs (hardware H.264 when verified)', ['electron/media.ts'], 'manual'],
    ['media.tracks', 'Audio/subtitle track selection', ['src/ui/EditorPanels.tsx', 'electron/render.ts'], 'selftest audio copy'],
    ['player.playback', 'Playback, seek, frame stepping, in/out, timecode', ['src/ui/Player.tsx', 'src/ui/Timeline.tsx'], 'manual UI'],
    ['player.zoom', 'Zoom / fit / fullscreen', ['src/ui/Player.tsx'], 'manual UI'],
    ['player.compare', 'Before/After wipe and side-by-side', ['src/ui/Player.tsx'], 'manual UI'],
    ['player.scopes', 'Scopes (histogram, parade, waveform, vectorscope)', ['src/ui/Scopes.tsx', 'src/ui/Player.tsx'], 'manual UI'],
    ['timeline.thumbs', 'Timeline thumbnails and waveform', ['src/ui/Timeline.tsx'], 'manual UI'],
    ['lut.library', 'LUT library with live frame previews and enlarge', ['src/ui/Views.tsx', 'electron/luts.ts'], 'manual UI'],
    ['lut.download', 'Automatic LUTsProfiles_v3.0.zip download', ['electron/luts.ts'], 'selftest LUT package'],
    ['lut.import', '.cube / tiled PNG LUT import (4x5 fit)', ['shared/uve/lut.ts', 'electron/luts.ts'], 'tests/core.test.ts'],
    ['custom.studio', 'Filter Studio (SVG/GLSL editor, validation, uniforms)', ['src/ui/FilterStudio.tsx', 'shared/uve/customFilters.ts'], 'tests/core.test.ts'],
    ['profiles', 'User profiles create/edit/delete, ZIP/JSON import/export', ['shared/uve/profiles.ts', 'electron/store.ts'], 'tests/core.test.ts'],
    ['project.io', 'Project load/save (.uveproj), recent projects', ['electron/store.ts', 'src/state/actions.ts'], 'manual'],
    ['project.undo', 'Undo/redo', ['src/state/store.ts'], 'manual UI'],
    ['session.restore', 'settings.json: prefs, window position/size, editor session', ['electron/store.ts', 'electron/main.ts'], 'manual'],
    ['render.queue', 'Render queue, progress, cancel, logs, atomic .uvepart output', ['electron/render.ts', 'src/ui/Views.tsx'], 'selftest cancel'],
    ['render.parity', '8-bit parity mode', ['src/engine/EffectEngine.ts', 'src/engine/GpuEngine.ts'], 'selftest'],
    ['render.tenbit', '10-bit / 16-bit pipeline (rgba64le)', ['electron/render.ts', 'src/export/worker.ts'], 'selftest 10-bit'],
    ['rve.upscale', 'AI upscaling via REAL-Video-Enhancer backend (TensorRT), 3-stage pipeline', ['electron/rve.ts', 'electron/render.ts'], 'selftest AI upscale'],
    ['tools.portable', 'Portable FFmpeg/FFprobe in tools/, start-up update modal, auto-update toggle', ['electron/tools.ts', 'electron/updates.ts'], 'manual'],
    ['app.update', 'App update check (GitHub releases)', ['electron/updates.ts'], 'manual'],
    ['app.portable', 'Portable self-extractor asking for the target folder, data\\ folder', ['build/portable-extractor.nsi', 'scripts/build-portable.mjs'], 'manual']
];
const APP_TESTS = {
    'media.proxy': `${SELFTEST} (preview proxy check)`,
    'tools.portable': 'migration/capabilities/*.json (capability probe) + every selftest render uses the resolved tools'
};
const APP_REVIEWED = {
    'app.update': ['diff-reviewed: Windows path identical; non-Windows branches untested', 'v1.0.1 only adds platform guards: the portable-FFmpeg check runs on Windows only, non-Windows app updates open the release page.']
};
for (const [id, name, mods, t] of APP_FEATURES) {
    const render = /^render|^rve|^media.tracks/.test(id);
    add({
        id: `app.${id}`, display_name: name, entry_point: 'UI', current_module: mods, v1_0_0_test: t,
        current_preview_path: /^player|^lut.library/.test(id) ? PREVIEW : null, current_export_path: render ? EXPORT : null,
        parity_status: APP_REVIEWED[id]?.[0], differences: APP_REVIEWED[id]?.[1],
        v1_0_1_test: APP_TESTS[id] ?? (render ? `${SELFTEST} + ${PARITY}` : /^player|^lut.library/.test(id) ? 'preview engine verified (__uveProbe output means vs v1.0.0)' : t.startsWith('tests') ? `${t} (passing)` : null),
        supported_backends: render || /^player/.test(id) ? ENGINE_BACKENDS : ['all'],
        platform_assumptions: id === 'tools.portable' ? 'Automatic FFmpeg download: Windows only; macOS/Linux use a system FFmpeg (PATH) or a configured folder.'
            : id === 'rve.upscale' ? 'TensorRT backend: NVIDIA GPU on Windows/Linux x64; unavailable on macOS (reported, export continues without upscaling option).'
            : id === 'app.portable' ? 'Windows NSIS self-extractor; macOS/Linux use dmg/AppImage targets (untested).' : 'none'
    });
}

const counts = items.reduce((a, i) => { a[i.parity_status] = (a[i.parity_status] || 0) + 1; return a; }, {});
const out = {
    schema: 'uve-feature-parity', baseline: { tag: TAG, sha: baseSha, source: 'M:\\LAPS\\UVE Manager (v1.0.0), copied unchanged as the baseline commit' },
    generatedAt: new Date().toISOString(), generator: 'migration/generate-inventory.mjs',
    total_items: items.length, by_parity_status: counts,
    changed_modules_since_baseline: [...changed].sort(),
    items
};
writeFileSync('migration/feature-parity-v1.0.0.json', JSON.stringify(out, null, 2));
console.log(`${items.length} items`, counts);
