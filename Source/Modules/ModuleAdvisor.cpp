#include "ModuleAdvisor.h"
#include "ModuleRegistry.h"
#include "../Brain/ModuleIntelligence.h"

namespace vf::mods
{
// ============================================================================
//  ModuleAdvisor — now a thin adapter over the AI Engineer's knowledge base.
//  ---------------------------------------------------------------------------
//  This used to be a flat list of if-statements with hand-written priority
//  numbers, and it could only say WHICH module to add — never how to set it up.
//  All of that expertise (need scoring from the analysis + calibrated settings +
//  rationale) now lives in `vf::ModuleIntelligence`, so the advisor, LEARN's
//  auto-build and the "dial this in for me" action all speak from ONE source of
//  truth and can never disagree with each other.
//
//  The public API is unchanged so the existing UI (ChainView's suggestion chips,
//  RackView's advisor row) keeps working; the rationale it shows is now richer
//  and the ranking is evidence-based rather than a constant.
// ============================================================================
std::vector<ModuleSuggestion> ModuleAdvisor::suggest (const AnalysisSnapshot& s)
{
    std::vector<ModuleSuggestion> out;
    if (! s.valid)
        return out;

    for (const auto& rec : ModuleIntelligence::recommend (s))
    {
        // Only surface modules this vocal genuinely calls for — utility/creative
        // entries in the knowledge base score low on purpose and stay out of the
        // way until the user searches for them.
        if (rec.need < ModuleIntelligence::kInsertThreshold)
            continue;
        if (! ModuleRegistry::instance().isImplemented (rec.moduleId))
            continue;

        juce::String why = rec.rationale;
        if (rec.settingsSummary.isNotEmpty())
            why += "\n\nI'd set it to:  " + rec.settingsSummary;

        out.push_back ({ rec.moduleId, rec.moduleName, why, rec.priority() });
    }
    return out;   // ModuleIntelligence::recommend already ranks strongest-first
}
} // namespace vf::mods
