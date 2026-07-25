#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "../Analysis/AnalysisEngine.h"   // AnalysisSnapshot

namespace vf
{
// ============================================================================
//  ModuleIntelligence — the AI Engineer's KNOWLEDGE BASE of the module library.
//  ---------------------------------------------------------------------------
//  Before this existed, the AI could only SUGGEST which module to insert (with a
//  flat hard-coded priority) and LEARN dialled in just four modules with fixed,
//  analysis-independent numbers. Every other module landed at its factory
//  defaults — so "insert a De-Esser" never actually tuned the de-esser to THIS
//  singer's sibilance.
//
//  This table closes that gap. For every implemented module it encodes what a
//  mixing engineer knows:
//    * ROLE      — what the module is for, in plain language.
//    * NEED      — 0..1, how much THIS vocal wants it (derived from the
//                  AnalysisSnapshot, so it is evidence-based, not a constant).
//    * DIAL      — calibrated {paramId, value} settings in NATURAL units,
//                  computed from the analysis (sibilance centre → de-ess
//                  frequency, measured noise floor → gate threshold, crest →
//                  compression, and so on), each with an engineer's rationale.
//
//  Because `dialFor` works for ANY known module id — not just the ones the AI
//  chose to insert — the assistant can also re-tune a module the USER added by
//  hand to the analysed vocal ("dial this in for me").
//
//  Pure + headerless-of-JUCE-GUI: snapshot in, recommendations out. No DSP, no
//  APVTS, no allocation on the audio thread. Everything is unit-testable, and
//  the offline test asserts every id/param here exists in the real registry and
//  that every value lands inside the module's declared range.
// ============================================================================
struct ModuleSetting
{
    juce::String paramId;      // module-local param id, e.g. "threshold"
    float        value = 0.0f; // NATURAL units (dB, Hz, %, :1 …)
};

struct ModuleRecommendation
{
    juce::String moduleId;       // registry type id, e.g. "de_esser"
    juce::String moduleName;     // display name
    juce::String role;           // what it does (plain language)
    juce::String rationale;      // WHY this vocal needs it, with measured evidence
    float        need = 0.0f;    // 0..1 strength of the recommendation
    std::vector<ModuleSetting> settings;   // dialled-in values (may be empty)
    juce::String settingsSummary;          // "Freq 6.8 kHz · Thresh -26 dB"

    /** Legacy priority scale (0..100) used by the older advisor UI. */
    int priority() const noexcept { return (int) juce::jlimit (0.0f, 100.0f, need * 100.0f); }
};

class ModuleIntelligence
{
public:
    /** Every module this vocal would benefit from, dialled in, strongest first.
        Empty when the snapshot is invalid. */
    static std::vector<ModuleRecommendation> recommend (const AnalysisSnapshot& s);

    /** Expert settings for ONE module against this vocal — even if the AI would
        not have inserted it (the user may have added it by hand). `need` still
        reports how much the vocal actually wants it. Returns a recommendation
        with an empty moduleId when the id is unknown. */
    static ModuleRecommendation dialFor (const juce::String& moduleId, const AnalysisSnapshot& s);

    /** True when the knowledge base has an entry for this module type. */
    static bool knows (const juce::String& moduleId);

    /** Plain-language description of what the module is for. */
    static juce::String roleOf (const juce::String& moduleId);

    /** Every module id the knowledge base covers (stable order). */
    static juce::StringArray knownModules();

    /** Modules are only auto-inserted by LEARN above this need. */
    static constexpr float kInsertThreshold = 0.45f;
};
} // namespace vf
