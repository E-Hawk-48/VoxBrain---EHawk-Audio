#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "../Analysis/AnalysisEngine.h"   // AnalysisSnapshot

namespace vf::mods
{
// ============================================================================
//  ModuleAdvisor — proactive AI module recommendations.
//
//  Given the LEARN analysis, it suggests which modules to INSERT (not just
//  which params to tweak), with a plain-language rationale, e.g.:
//    "Your vocal has excessive low-mid buildup. Consider inserting Dynamic EQ."
//    "Transient peaks suggest a Soft Clipper before the Limiter."
//  The UI/AI can then insert + configure them via the ModuleRack.
// ============================================================================
struct ModuleSuggestion
{
    juce::String moduleId;     // registry id to insert
    juce::String moduleName;   // display name
    juce::String rationale;    // why, in plain language
    int priority = 0;          // higher = more strongly recommended
};

class ModuleAdvisor
{
public:
    /** Returns recommendations ordered strongest-first. Empty if analysis
        is invalid or nothing stands out. */
    static std::vector<ModuleSuggestion> suggest (const AnalysisSnapshot& s);
};
} // namespace vf::mods
