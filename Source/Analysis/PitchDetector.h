#pragma once
#include "../DSP/PitchTracker.h"
#include <vector>

namespace vf
{
// ============================================================================
//  PitchDetector — the LEARN / UI pitch read-out.
//
//  This is now a thin adapter over the SAME redesigned engine the real-time
//  corrector uses (DSP/PitchTracker: multi-candidate CMND + sub-harmonic
//  demotion + Viterbi decoding with an explicit unvoiced state). Sharing one
//  tracker matters for more than code size — when analysis and correction used
//  two different estimators they could disagree, so LEARN would report a key or
//  median pitch the corrector never actually tracked.
//
//  The old inline YIN lived here; its failure modes (octave errors on rich
//  voices, note-flipping, one fixed voicing threshold for both belted and
//  whispered takes) are exactly what the new engine was built to remove.
// ============================================================================
class PitchDetector
{
public:
    void prepare (double sampleRate);

    /** Push a block of mono samples. Returns true when a fresh estimate exists. */
    bool push (const float* samples, int numSamples);

    /** Latest fundamental in Hz (0 if unvoiced). */
    float getFrequencyHz() const noexcept   { return currentHz; }

    /** Confidence 0..1 derived from the periodicity of the chosen candidate. */
    float getConfidence() const noexcept    { return confidence; }

    /** True when the tracker considers the current frame pitched. */
    bool isVoiced() const noexcept          { return voiced; }

private:
    PitchTracker tracker;
    float currentHz  = 0.0f;
    float confidence = 0.0f;
    bool  voiced     = false;
};
} // namespace vf
