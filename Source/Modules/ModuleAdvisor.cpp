#include "ModuleAdvisor.h"
#include "ModuleRegistry.h"

namespace vf::mods
{
namespace
{
    juce::String nameFor (juce::StringRef id, juce::StringRef fallback)
    {
        for (const auto& d : ModuleRegistry::instance().catalog())
            if (d.id == id) return d.name;
        return juce::String (fallback);
    }
}

std::vector<ModuleSuggestion> ModuleAdvisor::suggest (const AnalysisSnapshot& s)
{
    std::vector<ModuleSuggestion> out;
    if (! s.valid)
        return out;

    auto add = [&out] (const char* id, const char* fallback, juce::String why, int prio)
    {
        out.push_back ({ id, nameFor (id, fallback), std::move (why), prio });
    };

    // Low-mid buildup → Dynamic EQ
    if (s.mudDb > -11.0f || s.lowDb > -9.0f)
        add ("dynamic_eq", "Dynamic EQ",
             "Your vocal has excessive low-mid buildup (" + juce::String (juce::jmax (s.mudDb, s.lowDb), 1)
             + " dB rel.). Consider a Dynamic EQ to tame it only when it spikes.", 90);

    // Sibilance → De-Esser
    if (s.sibilanceRatio > 0.15f)
        add ("de_esser", "De-Esser",
             "Sibilance is high (ratio " + juce::String (s.sibilanceRatio, 2)
             + "). A De-Esser would smooth the harsh 's' sounds.", 85);

    // Very dynamic take → Soft Clipper before the Limiter
    if (s.crestDb > 18.0f)
        add ("soft_clipper", "Soft Clipper",
             "Transient peaks are strong (crest " + juce::String (s.crestDb, 1)
             + " dB). A Soft Clipper before the Limiter would catch them cleanly.", 80);

    // Audible noise floor → Noise Reduction
    if (s.rmsDb - s.noiseFloorDb < 40.0f && s.noiseFloorDb > -85.0f)
        add ("noise_reduction", "Noise Reduction",
             "The noise floor is audible (SNR " + juce::String (s.rmsDb - s.noiseFloorDb, 0)
             + " dB). Noise Reduction would clean up the recording.", 78);

    // Sub/plosive energy → De-Plosive
    if (s.subDb > -25.0f)
        add ("de_plosive", "De-Plosive",
             "Strong sub-bass energy suggests plosive pops. A De-Plosive module would improve intelligibility.", 70);

    // Dark tone → Exciter for air
    if (s.brightness < 0.4f)
        add ("exciter", "Exciter",
             "The tone is dark (brightness " + juce::String (s.brightness, 2)
             + "). An Exciter would add air and presence.", 60);

    // Dense / low-crest performance → Parallel Compressor for energy
    if (s.crestDb < 12.0f && s.crestDb > 0.0f)
        add ("parallel_comp", "Parallel Compressor",
             "The performance is fairly compressed already — try a Parallel Compressor for more energy without squashing it.", 55);

    // Melodic/sustained → Stereo Chorus to widen the hook
    if (s.voicedRatio > 0.5f && s.pitchStabilityCents < 60.0f)
        add ("stereo_chorus", "Stereo Chorus",
             "This is a sustained, melodic take — a Stereo Chorus may help widen the hook.", 45);

    // Moderate dynamics → Transient Designer for snap/punch
    if (s.crestDb > 10.0f && s.crestDb < 17.0f)
        add ("transient_designer", "Transient Designer",
             "The delivery could use more snap — a Transient Designer would sharpen the attack for punch.", 58);

    // Not bright → Tape Saturation for analog warmth + glue
    if (s.brightness < 0.55f)
        add ("tape_sat", "Tape Saturation",
             "Some analog warmth would flatter this tone — Tape Saturation adds body and glue.", 50);

    // Dynamic + sustained → Optical Compressor for smooth leveling
    if (s.crestDb > 16.0f && s.voicedRatio > 0.5f)
        add ("optical_comp", "Optical Compressor",
             "A dynamic, sustained take responds beautifully to an Optical Compressor for smooth, invisible leveling.", 62);

    std::stable_sort (out.begin(), out.end(),
                      [] (const auto& a, const auto& b) { return a.priority > b.priority; });
    return out;
}
} // namespace vf::mods
