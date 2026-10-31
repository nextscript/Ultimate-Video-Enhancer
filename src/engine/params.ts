// Parameters both GPU backends share for timeline rendering (`GpuEngine` WGSL struct fields and
// `EffectEngine` GLSL `u<Name>` uniforms of the timeline shaders carry exactly these names), plus the
// host-side inputs that feed them. `tests/engine.test.ts` checks both shader sources against this list.

export type ParamKind = 'float' | 'int' | 'bool' | 'vec2' | 'vec3' | 'vec4' | 'mat4x5';

export interface ParamDescriptor {
    /** WGSL struct field name; the GLSL uniform is `u` + Name. A mat4x5 is a mat4 `name` plus a vec4 `nameOff`. */
    name: string;
    kind: ParamKind;
    /** pass that reads it: graph prims, the per-layer passes (LUT, look) or the composite passes */
    block: 'graph' | 'layer' | 'composite';
    /** 'shader' = uniform of a timeline shader; 'host' = engine input that becomes another uniform or a texture */
    stage: 'shader' | 'host';
    /** where the value comes from */
    source: string;
}

export const ENGINE_PARAMS: readonly ParamDescriptor[] = [
    // ---- graph (host inputs of the existing `matrix` prim shader: uM/uOff, m/off)
    { name: 'matrix', kind: 'mat4x5', block: 'graph', stage: 'host', source: 'matrix prim: autoMatrix when auto, clipMatrix when clip, else prim.m' },
    { name: 'autoMatrix', kind: 'mat4x5', block: 'graph', stage: 'host', source: 'LayerRenderOptions.autoMatrix (auto-levels); identity when null' },
    { name: 'lutHandle', kind: 'int', block: 'layer', stage: 'host', source: 'LayerRenderOptions.lutHandle from uploadLut' },
    { name: 'hwLutHandle', kind: 'int', block: 'layer', stage: 'host', source: 'LayerRenderOptions.hwLutHandle from uploadLut3D' },

    // ---- layer: LUT pass
    { name: 'quant', kind: 'float', block: 'layer', stage: 'shader', source: '255 when quantize8 (export parity mode), else 0' },
    { name: 'hasLut', kind: 'bool', block: 'layer', stage: 'shader', source: 'lutHandle !== null' },
    { name: 'lutSize', kind: 'int', block: 'layer', stage: 'shader', source: 'entries of the 1D LUT' },
    { name: 'hasLut3d', kind: 'bool', block: 'layer', stage: 'shader', source: 'hwLutHandle !== null' },
    { name: 'lut3dSize', kind: 'int', block: 'layer', stage: 'shader', source: 'edge length of the 3D LUT' },
    // ---- layer: look pass
    { name: 'clipMatrix', kind: 'mat4x5', block: 'layer', stage: 'shader', source: 'clipLookMatrix (or the scalar opacity x levelLin law); identity when the graph has a clip prim' },
    { name: 'layerVeil', kind: 'float', block: 'layer', stage: 'shader', source: 'LayerRenderOptions.veil' },
    { name: 'layerVeilColor', kind: 'float', block: 'layer', stage: 'shader', source: 'LayerRenderOptions.veilColor (0 black, 1 white)' },

    // ---- composite: one pass per compositeSteps() entry
    { name: 'viewSize', kind: 'vec2', block: 'composite', stage: 'shader', source: 'CompositeOptions width, height' },
    { name: 'mode', kind: 'int', block: 'composite', stage: 'shader', source: 'TransitionUniforms.mode' },
    { name: 'axis', kind: 'int', block: 'composite', stage: 'shader', source: 'TransitionUniforms.axis' },
    { name: 'edge', kind: 'float', block: 'composite', stage: 'shader', source: 'TransitionUniforms.edge' },
    { name: 'highSideIsB', kind: 'bool', block: 'composite', stage: 'shader', source: 'TransitionUniforms.highSideIsB' },
    { name: 'cx', kind: 'float', block: 'composite', stage: 'shader', source: 'TransitionUniforms.cx' },
    { name: 'cy', kind: 'float', block: 'composite', stage: 'shader', source: 'TransitionUniforms.cy' },
    { name: 'feather', kind: 'float', block: 'composite', stage: 'shader', source: 'TransitionUniforms.feather' },
    { name: 'dxA', kind: 'float', block: 'composite', stage: 'shader', source: 'TransitionUniforms.dxA' },
    { name: 'dyA', kind: 'float', block: 'composite', stage: 'shader', source: 'TransitionUniforms.dyA' },
    { name: 'dxB', kind: 'float', block: 'composite', stage: 'shader', source: 'TransitionUniforms.dxB' },
    { name: 'dyB', kind: 'float', block: 'composite', stage: 'shader', source: 'TransitionUniforms.dyB' },
    { name: 'weightA', kind: 'float', block: 'composite', stage: 'shader', source: 'CompositeStep.weightA (CompositeEntry.weightA)' },
    { name: 'weightB', kind: 'float', block: 'composite', stage: 'shader', source: 'CompositeStep.weightB (CompositeEntry.weightB)' },
    { name: 'useAcc', kind: 'bool', block: 'composite', stage: 'shader', source: 'CompositeStep.over' },
    { name: 'veil', kind: 'float', block: 'composite', stage: 'shader', source: 'CompositeOptions.veil (TransitionUniforms.veil)' },
    { name: 'veilColor', kind: 'float', block: 'composite', stage: 'shader', source: 'CompositeOptions.veilColor (veilColorOf(TransitionUniforms.veilColor))' }
];

/** Names of the shader-stage parameters (what both engines' timeline shaders declare). */
export const SHADER_PARAM_NAMES: readonly string[] = ENGINE_PARAMS.filter(p => p.stage === 'shader').map(p => p.name);
