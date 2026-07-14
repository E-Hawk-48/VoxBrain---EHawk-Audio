#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

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
    /** Interprets the message, applies changes, returns the engineer's reply. */
    static juce::String handleMessage (const juce::String& message,
                                       juce::AudioProcessorValueTreeState& apvts);
};
} // namespace vf
