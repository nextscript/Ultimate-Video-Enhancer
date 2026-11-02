// Erzeugt Referenzwerte direkt aus dem Original-UVE-Code (uve.user.js), für tests/unit/tst_uve.cpp.
// Es werden nur die Zeilenbereiche mit Hilfsfunktionen, Standardwerten und Umrechnungsfunktionen
// in einer isolierten vm-Sandbox ausgeführt (kein DOM, kein Netzwerk, keine GM-API).
//   node scripts/uve-reference.mjs <uve.user.js> <ausgabe.json>
import fs from 'node:fs';
import vm from 'node:vm';

const [, , src, out] = process.argv;
const lines = fs.readFileSync(src, 'utf8').split('\n');
const range = (a, b) => lines.slice(a - 1, b).join('\n');

// Zeilen: Helfer 635–637, GVF_*_SETTINGS 4898–5136, Umrechnungsfunktionen 9320–9530 (UVE 1.17.5)
const code = [
  range(635, 637),
  range(4898, 5136),
  'let sl = 0, sr = 0, bl = 0, wl = 0, dn = 0, hdr = 0, edge = 0, u_black = 0, u_white = 0;',
  range(9320, 9530),
  `this.ref = {
     normU, normSL, normDN, normHDR, normEDGE, userGammaValue, userContrastAmount, userSaturationAmount,
     userVibranceAmount, userHueDegrees, userGrainAmount, userHighlightsAmount, userShadowsAmount,
     userBlackOffset, userWhiteOffset, contrastPivotExp, highlightsCurvePeak, shadowsCurvePeak, grainPeakExp,
     GVF_BASE_SETTINGS, GVF_HDR_SETTINGS, GVF_TEAL_SETTINGS, GVF_VIBRANT_SETTINGS, GVF_MOODY_SETTINGS
   };`,
].join('\n');

const sandbox = { Math, Number, console };
vm.createContext(sandbox);
vm.runInContext(code, sandbox, { filename: 'uve-extract.js' });
const r = sandbox.ref;

const inputs = [-10, -7.35, -2, -0.04, 0, 0.05, 1, 4.44, 10, 12];
const fns = ['normU', 'userGammaValue', 'userContrastAmount', 'userSaturationAmount', 'userVibranceAmount',
  'userHueDegrees', 'userGrainAmount', 'userHighlightsAmount', 'userShadowsAmount', 'userBlackOffset', 'userWhiteOffset'];
const table = {};
for (const f of fns) table[f] = inputs.map((v) => [v, r[f](v)]);
const small = [-3, -2, -1.234, -0.004, 0, 0.004, 0.006, 0.5, 1.5, 2, 3];
for (const f of ['normSL', 'normDN', 'normHDR', 'normEDGE']) {
  table[f] = small.map((v) => {
    // normSL & Co. lesen globale Variablen (sl, dn, hdr, edge) → setzen und aufrufen
    const g = { normSL: 'sl', normDN: 'dn', normHDR: 'hdr', normEDGE: 'edge' }[f];
    vm.runInContext(`${g} = ${v};`, sandbox);
    return [v, r[f]()];
  });
}
const constants = {
  contrastPivotExp: r.contrastPivotExp(), highlightsCurvePeak: r.highlightsCurvePeak(),
  shadowsCurvePeak: r.shadowsCurvePeak(), grainPeakExp: r.grainPeakExp(),
  base: r.GVF_BASE_SETTINGS, hdrGpuMaxSingleVideo: r.GVF_HDR_SETTINGS.gpuMaxSingleVideo,
  tealGpuHueShift: r.GVF_TEAL_SETTINGS.gpuHueShift, vibrantSaturation: r.GVF_VIBRANT_SETTINGS.saturation,
  moodyGpuSaturation: r.GVF_MOODY_SETTINGS.gpuSaturation, moodyGpuBrightness: r.GVF_MOODY_SETTINGS.gpuBrightness,
};
fs.writeFileSync(out, JSON.stringify({ source: 'uve.user.js 1.17.5', generated: new Date().toISOString(), constants, table }, null, 1));
console.log('Referenz geschrieben:', out, Object.keys(table).length, 'Funktionen');
