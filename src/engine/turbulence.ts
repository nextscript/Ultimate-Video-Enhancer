// feTurbulence lattice setup – direct port of the reference implementation in the
// SVG 1.1 specification (section 15.24). The per-pixel noise runs in GLSL (EffectEngine).
const BSize = 0x100;
const RAND_m = 2147483647, RAND_a = 16807, RAND_q = 127773, RAND_r = 2836;

function setupSeed(lSeed: number) {
    if (lSeed <= 0) lSeed = -(lSeed % (RAND_m - 1)) + 1;
    if (lSeed > RAND_m - 1) lSeed = RAND_m - 1;
    return lSeed;
}

function random(lSeed: number) {
    let result = RAND_a * (lSeed % RAND_q) - RAND_r * Math.trunc(lSeed / RAND_q);
    if (result <= 0) result += RAND_m;
    return result;
}

/**
 * Returns a Float32Array laid out as a (BSize*2+2) x 5 RGBA texture:
 * row 0: R = uLatticeSelector[i]; rows 1..4: RG = fGradient[channel][i].
 */
export function buildTurbulenceLattice(seedIn: number): { data: Float32Array; width: number; height: number } {
    let lSeed = setupSeed(Math.round(seedIn));
    const N = BSize + BSize + 2;
    const lattice = new Int32Array(N);
    const grad: number[][][] = [0, 1, 2, 3].map(() => Array.from({ length: N }, () => [0, 0]));
    let i = 0;
    for (let k = 0; k < 4; k++) {
        for (i = 0; i < BSize; i++) {
            lattice[i] = i;
            for (let j = 0; j < 2; j++) {
                lSeed = random(lSeed);
                grad[k][i][j] = ((lSeed % (BSize + BSize)) - BSize) / BSize;
            }
            const s = Math.sqrt(grad[k][i][0] * grad[k][i][0] + grad[k][i][1] * grad[k][i][1]);
            if (s > 0) { grad[k][i][0] /= s; grad[k][i][1] /= s; }
        }
    }
    while (--i) {
        const k = lattice[i];
        lSeed = random(lSeed);
        const j = lSeed % BSize;
        lattice[i] = lattice[j];
        lattice[j] = k;
    }
    for (i = 0; i < BSize + 2; i++) {
        lattice[BSize + i] = lattice[i];
        for (let k = 0; k < 4; k++) for (let j = 0; j < 2; j++) grad[k][BSize + i][j] = grad[k][i][j];
    }
    const data = new Float32Array(N * 5 * 4);
    for (let x = 0; x < N; x++) {
        data[x * 4] = lattice[x];
        for (let k = 0; k < 4; k++) {
            const o = ((k + 1) * N + x) * 4;
            data[o] = grad[k][x][0];
            data[o + 1] = grad[k][x][1];
        }
    }
    return { data, width: N, height: 5 };
}
