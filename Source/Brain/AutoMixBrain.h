#pragma once
#include <juce_core/juce_core.h>
#include "../Analysis/AnalysisEngine.h"

namespace vf
{
// ============================================================================
//  AutoMixBrain — expert-system that converts an AnalysisSnapshot into a
//  complete vocal-chain setting, the way an experienced mixing engineer would.
//
//  Every rule below encodes a real engineering decision with its rationale.
//  The output is a set of {parameterID, value} pairs applied to the APVTS,
//  plus a human-readable report and a generated preset name.
// ============================================================================
class AutoMixBrain
{
public:
    struct Decision
    {
        juce::String parameterId;
        float        value;          // in the parameter's natural units
        juce::String rationale;      // engineer's reasoning, shown in the UI
    };

    struct Result
    {
        std::vector<Decision> decisions;
        juce::String presetName;     // e.g. "Warm Intimate Vocal"
        juce::String summary;        // multi-line coach report
    };

    /** Pure function: snapshot in, complete chain decision out. */
    static Result computeChain (const AnalysisSnapshot& s);

private:
    static juce::String generatePresetName (const AnalysisSnapshot& s);
    static juce::String buildSummary (const AnalysisSnapshot& s, const std::vector<Decision>& d);
};
} // namespace vf
