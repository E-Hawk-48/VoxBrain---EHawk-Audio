#pragma once
#include "../Preset/PresetMeta.h"
#include "ReferenceProfile.h"
#include "ReferenceMatchBrain.h"
#include <cmath>

namespace vf::refpreset
{
// ============================================================================
//  ReferencePreset — derive rich preset METADATA from a reference match.
//  ---------------------------------------------------------------------------
//  Pure (juce_core only): turns the measured profile + the match plan into a
//  PresetMeta (mood/energy/tags/AI confidence + a plain-language summary) so a
//  saved "reference match" preset carries its analysis with it. The processor
//  adds the parameter values + rack + seals it; the marketplace can share it.
//  Kept separate + pure so the derivation is unit-testable.
// ============================================================================
inline PresetMeta deriveMeta (const ReferenceProfile& pr,
                              const ReferenceMatchBrain::Result& m,
                              const juce::String& fileName)
{
    PresetMeta meta;
    meta.name          = m.presetName.isNotEmpty() ? m.presetName : "Reference Match";
    meta.creatorName   = "You";
    meta.creatorId     = "reference";
    meta.source        = "ai";
    meta.aiGenerated   = true;
    meta.version       = "1.0.0";
    meta.minAppVersion = "0.2.0";

    meta.genre       = "Reference";
    meta.vocalStyle  = "Reference Match";
    meta.mood        = pr.brightness < 0.40f ? "Warm"
                     : pr.brightness > 0.65f ? "Bright" : "Balanced";
    meta.energy      = juce::jlimit (1, 10, (int) std::round (pr.crestDb / 2.5f + pr.compressionAmount * 3.0f));
    meta.cpuEstimate = 8.0f;
    meta.latencyMs   = 32.0f;

    meta.aiConfidence = juce::jlimit (0.0f, 1.0f, m.overallConfidence);
    meta.description  = "Chain matched to the vocal production of a reference recording "
                        "(your own voice, performance and pitch unchanged).";
    meta.aiSummary    = "Matched from \"" + fileName + "\".\n\n" + m.summary;

    meta.tags.add ("reference");
    meta.tags.add (meta.mood.toLowerCase());
    meta.tags.add ("ai");
    if (! pr.isMono && pr.stereoWidth > 0.25f) meta.tags.add ("wide");
    if (pr.compressionAmount > 0.60f)          meta.tags.add ("compressed");
    if (pr.reverbAmount > 0.30f)               meta.tags.add ("spacious");
    if (pr.air > 0.60f)                        meta.tags.add ("airy");
    meta.categories.add ("Reference");

    return meta;
}
} // namespace vf::refpreset
