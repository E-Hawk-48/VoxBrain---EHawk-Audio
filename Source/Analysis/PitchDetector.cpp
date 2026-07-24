#include "PitchDetector.h"
#include <cmath>
#include <algorithm>

namespace vf
{
namespace
{
    // Weak-voicing fallback ceiling (just past the 0.15 hard threshold).
    constexpr float kPdFallbackThresh = 0.22f;
}

void PitchDetector::prepare (double sampleRate)
{
    fs = sampleRate;
    windowSize = (int) std::round (fs * 0.046);          // ~46 ms
    windowSize = std::clamp (windowSize, 1024, 8192);

    buffer.assign ((size_t) windowSize, 0.0f);
    frame.assign  ((size_t) windowSize, 0.0f);
    diff.assign   ((size_t) windowSize / 2, 0.0f);
    writePos = 0;
    currentHz = 0.0f;
    confidence = 0.0f;
}

bool PitchDetector::push (const float* samples, int numSamples)
{
    bool updated = false;
    for (int i = 0; i < numSamples; ++i)
    {
        buffer[(size_t) writePos] = samples[i];
        if (++writePos >= windowSize)
        {
            writePos = 0;
            currentHz = runYin();
            updated = true;
        }
    }
    return updated;
}

float PitchDetector::runYin()
{
    // Linearise circular buffer (writePos is oldest sample after wrap)
    const int n = windowSize;
    for (int i = 0; i < n; ++i)
        frame[(size_t) i] = buffer[(size_t) ((writePos + i) % n)];

    // Quick silence rejection
    float energy = 0.0f;
    for (int i = 0; i < n; ++i) energy += frame[(size_t) i] * frame[(size_t) i];
    if (energy / (float) n < 1.0e-7f) { confidence = 0.0f; return 0.0f; }

    const int maxTau = std::min (n / 2 - 1, (int) (fs / minHz));
    const int minTau = std::max (2,         (int) (fs / maxHz));

    // Difference function d(tau)
    for (int tau = 0; tau <= maxTau; ++tau)
    {
        float sum = 0.0f;
        const int limit = n - maxTau;
        for (int i = 0; i < limit; ++i)
        {
            const float d = frame[(size_t) i] - frame[(size_t) (i + tau)];
            sum += d * d;
        }
        diff[(size_t) tau] = sum;
    }

    // Cumulative-mean normalised difference d'(tau)
    diff[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        runningSum += diff[(size_t) tau];
        diff[(size_t) tau] = runningSum > 0.0f
                           ? diff[(size_t) tau] * (float) tau / runningSum
                           : 1.0f;
    }

    // Absolute-threshold search
    int  tauEstimate = -1;
    bool fallback    = false;
    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        if (diff[(size_t) tau] < yinThreshold)
        {
            while (tau + 1 <= maxTau && diff[(size_t) (tau + 1)] < diff[(size_t) tau])
                ++tau;
            tauEstimate = tau;
            break;
        }
    }

    // Weak-voicing fallback (parity with the real-time engine): if nothing beat
    // the hard threshold but the global minimum is still fairly periodic and a
    // genuine local dip, take it with reduced confidence. Recovers soft/breathy
    // notes for LEARN + the live pitch read-out without latching onto noise.
    if (tauEstimate < 0)
    {
        int   bestTau = -1;
        float bestVal = 1.0e9f;
        for (int tau = minTau; tau <= maxTau; ++tau)
            if (diff[(size_t) tau] < bestVal) { bestVal = diff[(size_t) tau]; bestTau = tau; }

        const bool isLocalMin = bestTau > minTau && bestTau < maxTau
                             && diff[(size_t) (bestTau - 1)] >= bestVal
                             && diff[(size_t) (bestTau + 1)] >= bestVal;
        if (bestTau > 0 && bestVal < kPdFallbackThresh && isLocalMin)
        {
            tauEstimate = bestTau;
            fallback = true;
        }
    }
    if (tauEstimate < 0) { confidence = 0.0f; return 0.0f; }

    const float periodicity = 1.0f - diff[(size_t) tauEstimate];
    confidence = fallback ? periodicity * 0.7f : periodicity;

    // Parabolic interpolation around the minimum for sub-sample precision
    float betterTau = (float) tauEstimate;
    if (tauEstimate > 0 && tauEstimate < maxTau)
    {
        const float s0 = diff[(size_t) (tauEstimate - 1)];
        const float s1 = diff[(size_t) tauEstimate];
        const float s2 = diff[(size_t) (tauEstimate + 1)];
        const float denom = 2.0f * s1 - s2 - s0;
        if (std::abs (denom) > 1.0e-12f)
        {
            // Clamp the vertex offset to +/-1 bin: a near-flat parabola can throw
            // it far outside, spiking F0. A real minimum's vertex is within a bin.
            const float off = 0.5f * (s2 - s0) / denom;
            if (std::abs (off) < 1.0f)
                betterTau += off;
        }
    }

    return betterTau > 0.0f ? (float) fs / betterTau : 0.0f;
}
} // namespace vf
