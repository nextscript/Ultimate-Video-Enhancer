// Expected timeline shader parameters of BOTH GPU engines (WGSL uniform-block fields of GpuEngine's
// WGSL_TIMELINE_SOURCES, GLSL `u<Name>` uniforms of EffectEngine's GLSL_TIMELINE_SOURCES). Names carry no
// `u` prefix and no padding (`pad0` ...); a mat4x5 is the mat4 `name` plus its vec4 offset `nameOff`.

export type ParameterKind = 'float' | 'int' | 'bool' | 'vec2' | 'vec3' | 'vec4' | 'mat4x5';

export const PARAMETER_KINDS: Record<string, ParameterKind> = {
    // layer: LUT pass
    quant: 'float',
    hasLut: 'bool',
    lutSize: 'int',
    hasLut3d: 'bool',
    lut3dSize: 'int',
    // layer: look pass
    clipMatrix: 'mat4x5',
    layerVeil: 'float',
    layerVeilColor: 'float',
    // composite step
    viewSize: 'vec2',
    mode: 'int',
    axis: 'int',
    edge: 'float',
    highSideIsB: 'bool',
    cx: 'float',
    cy: 'float',
    feather: 'float',
    dxA: 'float',
    dyA: 'float',
    dxB: 'float',
    dyB: 'float',
    weightA: 'float',
    weightB: 'float',
    useAcc: 'bool',
    // composite output
    veil: 'float',
    veilColor: 'float'
};

export const PARAMETER_NAMES: readonly string[] = Object.keys(PARAMETER_KINDS).sort();
