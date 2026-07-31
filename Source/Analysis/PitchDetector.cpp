#include "PitchDetector.h"
#include <cmath>

namespace vf
{
void PitchDetector::prepare (double sampleRate)
{
    PitchTracker::Config c;
    c.sampleRate = sampleRate;
    c.minHz      = 55.0f;     // below the lowest male fundamental
    c.maxHz      = 1200.0f;   // above the highest falsetto/soprano
    c.hopSize    = (int) std::round (sampleRate * 0.0058);   // ~5.8 ms
    c.windowSize = (int) std::round (sampleRate * 0.030);    // ~30 ms — LEARN can
                                                             // afford a longer view
    // LEARN is offline-ish (analysis, not monitoring), so it can take the fullest
    // temporal context available for the most stable key/median-pitch statistics.
    c.decodeLag   = 5;
    c.sensitivity = 0.5f;
    tracker.prepare (c);

    currentHz = 0.0f;
    confidence = 0.0f;
    voiced = false;
}

bool PitchDetector::push (const float* samples, int numSamples)
{
    if (! tracker.process (samples, numSamples))
        return false;

    // Report 0 Hz when unvoiced — callers (LEARN's pitch histogram, the UI
    // read-out) treat 0 as "no pitch this frame", and feeding them a stale or
    // noise-derived value is what used to pollute key detection.
    voiced     = tracker.isVoiced();
    confidence = tracker.latestConfidence();
    currentHz  = voiced ? tracker.latestHz() : 0.0f;
    return true;
}
} // namespace vf
