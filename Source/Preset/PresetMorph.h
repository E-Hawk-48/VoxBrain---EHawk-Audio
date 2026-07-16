#pragma once
#include "PresetMeta.h"
#include <vector>

namespace vf
{
// ============================================================================
//  PresetMorph — blend between presets in natural parameter space. Choice/bool
//  params interpolate too; the APVTS rounds them to the nearest legal value on
//  apply, so a morph slider is smooth and always lands on a valid state.
// ============================================================================
class PresetMorph
{
public:
    /** Linear blend A→B at t in [0,1]. Params present in only one preset are
        carried through unchanged. Metadata is merged into a new "Morph" preset. */
    static Preset blend (const Preset& a, const Preset& b, float t);

    /** Weighted blend of many presets (weights are normalised internally). */
    static Preset blendMany (const std::vector<std::pair<const Preset*, float>>& weighted);
};
} // namespace vf
