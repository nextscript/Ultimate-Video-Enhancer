import type { EffectState } from '../../shared/types';
import { buildUveGraph, type UveGraph } from '../../shared/uve/graph';
import { lutKeyOf } from '../../shared/uve/lut';

/** Single entry point that turns an EffectState into the executable graph (preview and export). */
export function graphFromEffect(effect: EffectState): UveGraph {
    const lut = effect.lut && effect.settings.lutProfile !== 'none' && lutKeyOf(effect.lut) === effect.settings.lutProfile
        ? effect.lut.matrix4x5 : null;
    return buildUveGraph({ settings: effect.settings, expert: effect.expert, lut, customFilters: effect.customFilters });
}
