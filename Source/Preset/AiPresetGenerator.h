#pragma once
#include "PresetMeta.h"

namespace vf { struct AnalysisSnapshot; }

namespace vf
{
// ============================================================================
//  AiPresetGenerator — turns a LEARN analysis into a complete, shareable AI
//  preset: full chain values from the AutoMixBrain, plus rich metadata
//  (detected genre, confidence, plain-language analysis summary).
// ============================================================================
class AiPresetGenerator
{
public:
    static Preset generate (const AnalysisSnapshot& s, const juce::String& creatorName = "You");
};
} // namespace vf
