#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParameterIDs.h"   // the stable string IDs (no-JUCE, shared with headless brains)

// ============================================================================
//  Parameters.h
//  Central definition of every automatable parameter in VoxBrain.
//  The stable IDs live in ParameterIDs.h; this header adds the APVTS layout
//  builder and the lock-mapping helper on top. All IDs are stable strings —
//  never reorder/rename after release.
// ============================================================================
namespace vf::param
{
    /** Maps any parameter ID to its module's lock parameter ID, or nullptr if
        the parameter is not part of a lockable module (e.g. global gain). */
    const char* lockParamFor (const juce::String& paramId);

    /** Builds the full parameter layout used by the AudioProcessorValueTreeState. */
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace vf::param
