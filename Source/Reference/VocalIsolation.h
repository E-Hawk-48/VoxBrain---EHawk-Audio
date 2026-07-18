#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace vf
{
// ============================================================================
//  VocalIsolation — rough vocal isolation for full-mix references.
//  ---------------------------------------------------------------------------
//  Lead vocals sit in the CENTRE of a stereo mix (equal in L and R) while most
//  instruments are spread. An STFT "centre extractor" keeps the content common
//  to both channels and suppresses whatever leans into the sides:
//     per bin:  gain = relu(|mid| - |side|) / |mid|   (mid = ½(L+R), side = ½(L-R))
//  applied to the mid spectrum (phase preserved), then overlap-added back.
//
//  It is NOT a true source separation (an ML model comes later) — it just gives
//  the analyzer a vocal-FORWARD signal so a full song is measured more like its
//  acapella. Offline only; mono input is returned unchanged.
// ============================================================================
class VocalIsolation
{
public:
    /** Return a 1-channel centre-extracted estimate of `stereo`. Mono input is
        copied through. Output length matches the input. */
    static juce::AudioBuffer<float> isolateCenter (const juce::AudioBuffer<float>& stereo, double sampleRate);
};
} // namespace vf
