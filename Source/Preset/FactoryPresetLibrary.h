#pragma once
#include "PresetMeta.h"
#include <vector>

namespace vf
{
// ============================================================================
//  FactoryPresetLibrary — VoxBrain's built-in professional preset library.
//  Generated programmatically from per-genre templates × variations so it
//  scales to hundreds of presets while every entry carries full metadata.
// ============================================================================
class FactoryPresetLibrary
{
public:
    static std::vector<Preset> build();

    struct Collection
    {
        juce::String name;
        juce::String description;
        juce::StringArray presetNames;
    };
    /** Curated official collections (references presets by name). */
    static std::vector<Collection> officialCollections();
};
} // namespace vf
