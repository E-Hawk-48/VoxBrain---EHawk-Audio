#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Modules/ModuleRack.h"
#include "../Analysis/AnalysisEngine.h"
#include <functional>

namespace vf
{
// ============================================================================
//  ChatEngine — natural-language control of the vocal chain.
//
//  A data-driven production-vocabulary engine: recognises mixing terms
//  ("darker", "airy", "less autotune", "punchier", "vintage", "radio"...),
//  with intensity modifiers ("slightly", "way more") and negation
//  ("less", "too", "remove"), and converts them into precise, range-clamped
//  parameter edits applied through the APVTS (visible to host automation).
//
//  Runs entirely locally on the message thread — instant and private.
// ============================================================================
class ChatEngine
{
public:
    /** Optional access to the module rack, so requests like "add a de-esser" or
        "set up the compressor for my voice" can insert/remove/re-tune modules —
        dialled from the last LEARN analysis via Brain/ModuleIntelligence. When
        no rack is supplied the engine simply behaves as before (fixed chain only). */
    struct Context
    {
        mods::ModuleRack*       rack     = nullptr;
        const AnalysisSnapshot* snapshot = nullptr;   // last LEARN result (may be invalid)
        std::function<void()>   onRackChanged;        // sync macros + chain order
    };

    /** Interprets the message, applies changes, returns the engineer's reply.
        With a Context the assistant can also add/remove/dial rack modules. */
    static juce::String handleMessage (const juce::String& message,
                                       juce::AudioProcessorValueTreeState& apvts,
                                       Context ctx);

    /** Fixed-chain only (no rack access). */
    static juce::String handleMessage (const juce::String& message,
                                       juce::AudioProcessorValueTreeState& apvts);
};
} // namespace vf
