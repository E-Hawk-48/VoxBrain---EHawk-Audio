#include "AiPresetGenerator.h"
#include "../Brain/AutoMixBrain.h"
#include "../Analysis/AnalysisEngine.h"
#include <cmath>

namespace vf
{
Preset AiPresetGenerator::generate (const AnalysisSnapshot& s, const juce::String& creator)
{
    const auto r = AutoMixBrain::computeChain (s);

    Preset p;
    p.meta.presetId     = juce::Uuid().toString();
    p.meta.name         = r.presetName.isNotEmpty() ? r.presetName : "AI Vocal";
    p.meta.creatorName  = creator;
    p.meta.creatorId    = "ai";
    p.meta.source       = "ai";
    p.meta.aiGenerated  = true;
    p.meta.version      = "1.0.0";
    p.meta.minAppVersion = "0.2.0";
    p.meta.genre        = AutoMixBrain::detectedStyle (s);
    p.meta.vocalStyle   = p.meta.genre + " Vocal";
    p.meta.mood         = s.brightness < 0.4f ? "Warm" : (s.brightness > 0.65f ? "Bright" : "Balanced");
    p.meta.energy       = juce::jlimit (1, 10, (int) std::round (s.crestDb / 2.5f));
    p.meta.cpuEstimate  = 8.0f;
    p.meta.latencyMs    = 32.0f;

    float conf = 0.4f;
    if (s.valid) conf += 0.2f;
    conf += juce::jlimit (0.0f, 0.3f, s.voicedRatio * 0.3f);
    conf += juce::jlimit (0.0f, 0.2f, s.keyConfidence * 0.2f);
    p.meta.aiConfidence = juce::jlimit (0.0f, 1.0f, conf);

    p.meta.aiSummary = "Detected " + p.meta.genre + " vocal: brightness "
                     + juce::String (s.brightness, 2) + ", crest " + juce::String (s.crestDb, 1)
                     + " dB, " + juce::String ((int) (s.voicedRatio * 100.0f)) + "% voiced. "
                     + "Chain tuned to match.";

    p.meta.tags.add (p.meta.genre.toLowerCase());
    p.meta.tags.add (p.meta.mood.toLowerCase());
    p.meta.tags.add ("ai");
    p.meta.categories.add (p.meta.genre);

    for (const auto& d : r.decisions)
        p.values[d.parameterId] = d.value;

    p.seal();
    return p;
}
} // namespace vf
