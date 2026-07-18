#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "ReferenceProfile.h"
#include <functional>
#include <atomic>

namespace vf
{
// ============================================================================
//  ReferenceAnalyzer
//  ---------------------------------------------------------------------------
//  Turns a decoded audio buffer (the reference vocal / snippet / acapella) into
//  a ReferenceProfile by MEASURING its production characteristics — spectral
//  balance, dynamics, loudness, sibilance, stereo image, ambience/reverb, and
//  pitch behaviour. It never touches the user's own audio and never runs on the
//  audio thread: it is a pure, offline computation the caller schedules on a
//  background thread (progress + cancel hooks are provided for that).
//
//  It reuses VoxBrain's existing measurement blocks where possible (the real
//  BS.1770 LoudnessMeter and the YIN PitchDetector) so the numbers are
//  consistent with the rest of the plugin.
//
//  Everything it writes is finite and bounded by construction; on invalid or
//  too-short input it returns a profile with `valid == false`.
// ============================================================================
class ReferenceAnalyzer
{
public:
    /** Analyse `audio` (any channel count, sampled at `sampleRate`).
        @param progress optional 0..1 progress callback (background-thread safe).
        @param cancel   optional flag; if it becomes true the analysis bails and
                        returns whatever it has (with valid=false if it stopped
                        before finishing). */
    static ReferenceProfile analyze (const juce::AudioBuffer<float>& audio,
                                     double sampleRate,
                                     std::function<void (float)> progress = {},
                                     const std::atomic<bool>* cancel = nullptr);
};
} // namespace vf
