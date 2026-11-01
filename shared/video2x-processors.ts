// v1.0.8: additional Video2X processors – Real-ESRGAN and Real-CUGAN (ncnn + Vulkan) – as a separate adapter.
// The Anime4K / libplacebo path (shared/video2x-cli.ts buildArgs) is not touched.
//
// Verified against Video2X 6.4.0 (tools/video2x/src/argparse.cpp, src/filter_realesrgan.cpp,
// src/filter_realcugan.cpp and the model files of the release):
//   -p realesrgan -s <2|3|4> --realesrgan-model <name>       model file models/realesrgan/<name>-x<scale>.param/.bin
//   -p realcugan  -s <2|3|4> -n <-1..3> --realcugan-model <name>
//                                                            model file models/realcugan/<name>/up<scale>x-<noise>.param/.bin
//                                                            noise -1 = conservative, 0 = no-denoise, n = denoise<n>x
// A model/scale/noise combination without model files fails inside Video2X ("model param file not found"), so
// combinations are checked against this table BEFORE rendering. RIFE (frame interpolation) is deliberately not
// offered in 1.0.8 (see CHANGELOG_1.0.8.md).
import { LOSSLESS_CANDIDATES, type Video2xCapabilities } from './video2x-cli';

export type Video2xProcessor = 'libplacebo' | 'realesrgan' | 'realcugan';
export type Video2xNeuralProcessor = Exclude<Video2xProcessor, 'libplacebo'>;
export const VIDEO2X_PROCESSORS: Video2xProcessor[] = ['libplacebo', 'realesrgan', 'realcugan'];

export const PROCESSOR_LABEL: Record<Video2xProcessor, string> = {
    libplacebo: 'Anime4K v4 (libplacebo)',
    realesrgan: 'Real-ESRGAN (ncnn, Vulkan)',
    realcugan: 'Real-CUGAN (ncnn, Vulkan)'
};

export interface NeuralModelInfo {
    id: string;
    label: string;
    /** scale -> allowed noise levels (Real-ESRGAN: [0], the value is not passed) */
    scales: Record<number, number[]>;
}

/** Model combinations that exist as model files in the pinned Video2X 6.4.0 release. */
export const NEURAL_MODELS: Record<Video2xNeuralProcessor, NeuralModelInfo[]> = {
    realesrgan: [
        { id: 'realesr-animevideov3', label: 'RealESR AnimeVideo v3 (animation, fast)', scales: { 2: [0], 3: [0], 4: [0] } },
        { id: 'realesrgan-plus-anime', label: 'Real-ESRGAN+ Anime (illustrations, 4× only)', scales: { 4: [0] } },
        { id: 'realesrgan-plus', label: 'Real-ESRGAN+ (general content, 4× only)', scales: { 4: [0] } }
    ],
    realcugan: [
        { id: 'models-se', label: 'Real-CUGAN SE (2× / 3× / 4×)', scales: { 2: [-1, 0, 1, 2, 3], 3: [-1, 0, 3], 4: [-1, 0, 3] } },
        { id: 'models-pro', label: 'Real-CUGAN Pro (2× / 3×)', scales: { 2: [-1, 0, 3], 3: [-1, 0, 3] } },
        { id: 'models-nose', label: 'Real-CUGAN No-Denoise (2× only)', scales: { 2: [0] } }
    ]
};

export const DEFAULT_MODEL: Record<Video2xNeuralProcessor, string> = { realesrgan: 'realesr-animevideov3', realcugan: 'models-se' };

export const NOISE_LABEL: Record<number, string> = { [-1]: 'Conservative', 0: 'No denoise', 1: 'Denoise 1×', 2: 'Denoise 2×', 3: 'Denoise 3×' };

export const isNeural = (p: Video2xProcessor | undefined): p is Video2xNeuralProcessor => p === 'realesrgan' || p === 'realcugan';

/** Model file paths (relative to the Video2X folder) a combination needs. */
export function modelFiles(p: Video2xNeuralProcessor, model: string, scale: number, noise: number): string[] {
    const base = p === 'realesrgan'
        ? `models/realesrgan/${model}-x${scale}`
        : `models/realcugan/${model}/up${scale}x-${noise === -1 ? 'conservative' : noise === 0 ? 'no-denoise' : `denoise${noise}x`}`;
    return [`${base}.param`, `${base}.bin`];
}

export interface NeuralProcessorCaps {
    available: boolean;
    /** why the processor cannot be used ('' = usable) */
    problem: string;
    /** models of the table that the binary also lists in --help */
    models: NeuralModelInfo[];
    /** the CLI offers --noise-level (Real-CUGAN) */
    noise: boolean;
}

/** Real-ESRGAN / Real-CUGAN support of the installed binary, from its --help text (no assumptions about other builds). */
export function parseNeuralCaps(help: string, caps: Video2xCapabilities): Record<Video2xNeuralProcessor, NeuralProcessorCaps> {
    const listed = (opt: string): string[] => {
        const part = help.split(opt)[1]?.split(/\n\s*-{1,2}[a-z]/)[0] || '';
        // boost prints "(=default)" first; the model list is the last other parenthesised group
        const groups = [...part.matchAll(/\(([^)]*)\)/g)].map(m => m[1]).filter(g => !g.trim().startsWith('='));
        return (groups[groups.length - 1] || '').split(/[\s,]+/).filter(x => /^[\w.+-]+$/.test(x));
    };
    const one = (p: Video2xNeuralProcessor, modelOpt: string, needNoise: boolean): NeuralProcessorCaps => {
        const missing: string[] = [];
        if (caps.processors.length && !caps.processors.includes(p)) missing.push(`processor "${p}"`);
        for (const o of [modelOpt, '--scaling-factor', ...(needNoise ? ['--noise-level'] : [])]) if (!caps.options.has(o)) missing.push(o);
        const names = listed(modelOpt);
        const models = NEURAL_MODELS[p].filter(m => names.includes(m.id));
        let problem = '';
        if (!caps.versionSupported) problem = caps.problem || 'Unsupported Video2X version.';
        else if (missing.length) problem = `This Video2X build does not offer ${missing.join(', ')}.`;
        else if (!models.length) problem = `This Video2X build lists no ${p === 'realesrgan' ? 'Real-ESRGAN' : 'Real-CUGAN'} model UVE Manager knows (${names.join(', ') || 'none listed'}).`;
        return { available: !problem, problem, models, noise: caps.options.has('--noise-level') };
    };
    return { realesrgan: one('realesrgan', '--realesrgan-model', false), realcugan: one('realcugan', '--realcugan-model', true) };
}

export interface Video2xNeuralRun {
    input: string;
    output: string;
    processor: Video2xNeuralProcessor;
    modelId: string;
    scale: number;
    noiseLevel?: number;
    device: number;
    lossless: string;
}

/** Why a model/scale/noise combination cannot run ('' = valid). */
export function neuralComboProblem(p: Video2xNeuralProcessor, model: string, scale: number, noise: number | undefined): string {
    const m = NEURAL_MODELS[p].find(x => x.id === model);
    if (!m) return `Unknown ${p === 'realesrgan' ? 'Real-ESRGAN' : 'Real-CUGAN'} model "${model}".`;
    const noises = m.scales[scale];
    if (!noises) return `${m.label} supports only ${Object.keys(m.scales).map(s => `${s}×`).join(' / ')} – choose another scale or model.`;
    if (p === 'realcugan' && !noises.includes(noise ?? 0)) return `${m.label} at ${scale}× supports noise levels ${noises.map(n => NOISE_LABEL[n]).join(', ')} – choose another noise level.`;
    return '';
}

/** argv for one Real-ESRGAN / Real-CUGAN run. Never joined into a shell command line. Throws on invalid input. */
export function buildNeuralArgs(caps: Video2xCapabilities, neural: Record<Video2xNeuralProcessor, NeuralProcessorCaps>, r: Video2xNeuralRun): string[] {
    const pc = neural[r.processor];
    if (!pc) throw new Error(`Unknown Video2X processor "${String(r.processor)}".`);
    if (!pc.available) throw new Error(pc.problem);
    // model ids are matched against the allowlist – a value like "--foo" or "x y" never reaches argv
    if (!pc.models.some(m => m.id === r.modelId)) throw new Error(`Model "${r.modelId}" is not available in this Video2X build (available: ${pc.models.map(m => m.id).join(', ') || 'none'}).`);
    const combo = neuralComboProblem(r.processor, r.modelId, r.scale, r.noiseLevel);
    if (combo) throw new Error(combo);
    for (const [k, v] of [['input', r.input], ['output', r.output]] as const) {
        if (!v || /[\u0000-\u001f]/.test(v)) throw new Error(`Invalid ${k} path.`);
        if (v.startsWith('-')) throw new Error(`The ${k} path must be absolute.`);
    }
    const ll = LOSSLESS_CANDIDATES.find(c => c.id === r.lossless) || LOSSLESS_CANDIDATES[0];
    const a = ['-i', r.input, '-o', r.output, '-p', r.processor, '-s', String(r.scale)];
    if (r.processor === 'realesrgan') a.push('--realesrgan-model', r.modelId);
    else {
        if (!pc.noise) throw new Error('This Video2X build has no --noise-level option for Real-CUGAN.');
        a.push('-n', String(r.noiseLevel ?? 0), '--realcugan-model', r.modelId);
    }
    a.push('-c', ll.codec);
    if (ll.pixFmt) {
        if (!caps.hasPixFmt) throw new Error('This Video2X build has no --pix-fmt option for the lossless intermediate.');
        a.push('--pix-fmt', ll.pixFmt);
    }
    if (ll.extra.length) {
        if (!caps.hasExtraEncoderOption) throw new Error('This Video2X build has no --extra-encoder-option.');
        for (const e of ll.extra) a.push('-e', e);
    }
    a.push('--no-copy-streams');
    if (r.device >= 0) {
        if (!caps.hasDevice) throw new Error('This Video2X build cannot select a GPU (--device missing).');
        a.push('-d', String(r.device));
    }
    if (caps.hasLogLevel) a.push('--log-level', 'info');
    return a;
}

/** Cache key of a verified combination (also keyed by executable fingerprint and GPU in the main process). */
export const comboKey = (p: Video2xNeuralProcessor, model: string, scale: number, noise: number | undefined, device: number) =>
    `${p}|${model}|${scale}|${p === 'realcugan' ? noise ?? 0 : '-'}|${device}`;
