#include "RetuneEngine.h"
#include <cmath>
#include <algorithm>
#include <initializer_list>

namespace vf
{
namespace
{
    int nextPow2 (int x) { int p = 1; while (p < x) p <<= 1; return p; }

    // Natural major / natural minor pitch-class masks (bit 0 = root)
    constexpr int majorMask = 0b101010110101;   // 0 2 4 5 7 9 11
    constexpr int minorMask = 0b010110101101;   // 0 2 3 5 7 8 10
}

// ============================================================================
//  Setup
// ============================================================================
void RetuneEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    channels = std::max (1, numChannels);

    anaWindow = std::max (512, (int) std::round (fs * 0.030));
    anaHop    = anaWindow / 4;

    // Delay covers two max periods (grain lookback) + scheduling margin
    delaySamples = (int) std::round (fs * 0.032) + 192;

    ringSize = nextPow2 ((int) std::round (fs * 0.12) + maxBlockSize + delaySamples);

    ring.assign   ((size_t) channels, std::vector<float> ((size_t) ringSize, 0.0f));
    olaBuf.assign ((size_t) channels, std::vector<float> ((size_t) ringSize, 0.0f));
    winSum.assign ((size_t) ringSize, 0.0f);

    anaBuf.assign   ((size_t) anaWindow, 0.0f);
    anaFrame.assign ((size_t) anaWindow, 0.0f);
    yinDiff.assign  ((size_t) anaWindow / 2 + 1, 0.0f);

    reset();
}

void RetuneEngine::reset()
{
    for (auto& r : ring)   std::fill (r.begin(), r.end(), 0.0f);
    for (auto& o : olaBuf) std::fill (o.begin(), o.end(), 0.0f);
    std::fill (winSum.begin(), winSum.end(), 0.0f);
    std::fill (anaBuf.begin(), anaBuf.end(), 0.0f);

    writeAbs = 0;
    anaFill = 0;  anaWritePos = 0;
    currentF0 = 0.0f;  f0Confidence = 0.0f;
    smoothedPeriod = (float) fs / 200.0f;
    octaveJumpCount = 0;
    f0Hist[0] = f0Hist[1] = f0Hist[2] = 0.0f;
    f0HistPos = 0;  histPrimed = false;  f0Center = 0.0f;
    voicedState = false;
    unvoicedHops = 0;
    nextGrainOut = 1.0;
    lastSourceCentre = 0.0;
    currentRatio = 1.0f;
    gridValid = false;
}

// ============================================================================
//  Pitch tracking (incremental YIN, runs every anaHop samples)
// ============================================================================
void RetuneEngine::pushAnalysis (float sample)
{
    anaBuf[(size_t) anaWritePos] = sample;
    anaWritePos = (anaWritePos + 1) % anaWindow;

    if (++anaFill < anaHop)
        return;

    anaFill = 0;
    const float f0raw = runYin();

    // Median-of-3 outlier rejection over VOICED estimates only. Unvoiced frames
    // (f0raw == 0) must NOT enter the history — otherwise a single consonant/
    // breath zero pollutes the next two medians and yanks the corrected pitch,
    // which is heard as choppiness/glitches on transitions. On a fresh note
    // onset we prime all three slots so the very first voiced frame is stable.
    float f0;
    if (f0raw > 0.0f)
    {
        if (! histPrimed)
        {
            f0Hist[0] = f0Hist[1] = f0Hist[2] = f0raw;
            histPrimed = true;
        }
        else
        {
            f0Hist[f0HistPos] = f0raw;
            f0HistPos = (f0HistPos + 1) % 3;
        }
        const float a = f0Hist[0], b = f0Hist[1], c = f0Hist[2];
        f0 = std::max (std::min (a, b), std::min (std::max (a, b), c));
    }
    else
    {
        f0 = 0.0f;             // unvoiced this frame — leave the history untouched
        histPrimed = false;    // re-prime cleanly when the next note starts
    }
    currentF0 = f0;
    uiInHz.store (f0);

    // Slow pitch centre (for humanize) — slower than vibrato so it tracks the note.
    if (f0 > 0.0f)
        f0Center = f0Center > 0.0f ? 0.97f * f0Center + 0.03f * f0 : f0;

    if (f0 > 0.0f)
    {
        const float period = (float) fs / f0;
        const bool implausible = smoothedPeriod > 0.0f
                              && (period > smoothedPeriod * 1.6f
                               || period < smoothedPeriod * 0.6f);
        if (implausible)
        {
            if (++octaveJumpCount >= 3)   // accept only persistent changes
            {
                smoothedPeriod = period;
                octaveJumpCount = 0;
            }
        }
        else
        {
            smoothedPeriod = 0.5f * smoothedPeriod + 0.5f * period;
            octaveJumpCount = 0;
        }
    }

    // Voicing hysteresis: enter quickly, leave only after 3 quiet hops.
    if (f0 > 0.0f && f0Confidence > 0.55f)
    {
        voicedState = true;
        unvoicedHops = 0;
    }
    else if (f0 <= 0.0f || f0Confidence < 0.35f)
    {
        if (++unvoicedHops >= 3)
            voicedState = false;
    }
    // between 0.35 and 0.55: keep previous state (no flapping)
}

float RetuneEngine::runYin()
{
    const int n = anaWindow;
    for (int i = 0; i < n; ++i)
        anaFrame[(size_t) i] = anaBuf[(size_t) ((anaWritePos + i) % n)];

    float energy = 0.0f;
    for (int i = 0; i < n; ++i) energy += anaFrame[(size_t) i] * anaFrame[(size_t) i];
    if (energy / (float) n < 1.0e-7f) { f0Confidence = 0.0f; return 0.0f; }

    const int maxTau = std::min (n / 2 - 1, (int) (fs / minHz));
    const int minTau = std::max (2,         (int) (fs / maxHz));

    for (int tau = 0; tau <= maxTau; ++tau)
    {
        float sum = 0.0f;
        const int limit = n - maxTau;
        for (int i = 0; i < limit; ++i)
        {
            const float d = anaFrame[(size_t) i] - anaFrame[(size_t) (i + tau)];
            sum += d * d;
        }
        yinDiff[(size_t) tau] = sum;
    }

    yinDiff[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        runningSum += yinDiff[(size_t) tau];
        yinDiff[(size_t) tau] = runningSum > 0.0f
                              ? yinDiff[(size_t) tau] * (float) tau / runningSum
                              : 1.0f;
    }

    int tauEstimate = -1;
    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        if (yinDiff[(size_t) tau] < yinThreshold)
        {
            while (tau + 1 <= maxTau && yinDiff[(size_t) (tau + 1)] < yinDiff[(size_t) tau])
                ++tau;
            tauEstimate = tau;
            break;
        }
    }
    if (tauEstimate < 0) { f0Confidence = 0.0f; return 0.0f; }

    f0Confidence = 1.0f - yinDiff[(size_t) tauEstimate];

    float betterTau = (float) tauEstimate;
    if (tauEstimate > 0 && tauEstimate < maxTau)
    {
        const float s0 = yinDiff[(size_t) (tauEstimate - 1)];
        const float s1 = yinDiff[(size_t) tauEstimate];
        const float s2 = yinDiff[(size_t) (tauEstimate + 1)];
        const float denom = 2.0f * s1 - s2 - s0;
        if (std::abs (denom) > 1.0e-12f)
            betterTau += 0.5f * (s2 - s0) / denom;
    }
    return betterTau > 0.0f ? (float) fs / betterTau : 0.0f;
}

// ============================================================================
//  Scale quantizer
// ============================================================================
int RetuneEngine::scaleMaskFor (int scaleType, const RetuneParams& p)
{
    auto build = [] (std::initializer_list<int> pcs)
    { int m = 0; for (int x : pcs) m |= (1 << x); return m; };

    switch (scaleType)
    {
        case 1: return build ({ 0, 2, 4, 5, 7, 9, 11 });   // Major
        case 2: return build ({ 0, 2, 3, 5, 7, 8, 10 });   // Natural minor
        case 3: return build ({ 0, 2, 3, 5, 7, 8, 11 });   // Harmonic minor
        case 4: return build ({ 0, 2, 3, 5, 7, 9, 10 });   // Dorian
        case 5: return build ({ 0, 2, 4, 5, 7, 9, 10 });   // Mixolydian
        case 6: return build ({ 0, 1, 3, 5, 7, 8, 10 });   // Phrygian
        case 7: return build ({ 0, 2, 4, 7, 9 });          // Major pentatonic
        case 8: return build ({ 0, 3, 5, 7, 10 });         // Minor pentatonic
        case 9: return build ({ 0, 3, 5, 6, 7, 10 });      // Blues
        default: break;                                    // 0 → legacy
    }
    if (! p.chromatic && p.keyRoot >= 0)
        return p.majorScale ? majorMask : minorMask;
    return 0xFFF;
}

float RetuneEngine::quantizeTargetHz (float inputHz, const RetuneParams& p) const
{
    if (inputHz <= 0.0f)
        return 0.0f;

    const float midi = 69.0f + 12.0f * std::log2 (inputHz / 440.0f);

    const int mask = scaleMaskFor (p.scaleType, p);
    const int root = p.keyRoot >= 0 ? p.keyRoot : 0;

    const int centre = (int) std::round (midi);
    int   bestNote = centre;
    float bestDist = 1.0e9f;
    for (int nOff = -6; nOff <= 6; ++nOff)
    {
        const int note = centre + nOff;
        const int pc = ((note - root) % 12 + 12) % 12;
        if ((mask >> pc) & 1)
        {
            const float dist = std::abs ((float) note - midi);
            if (dist < bestDist) { bestDist = dist; bestNote = note; }
        }
    }
    return 440.0f * std::pow (2.0f, ((float) bestNote - 69.0f) / 12.0f);
}

// ============================================================================
//  Resynthesis helpers
// ============================================================================
float RetuneEngine::readRing (int channel, double absPos) const
{
    const int mask = ringSize - 1;
    const auto ip = (long long) std::floor (absPos);
    const float frac = (float) (absPos - (double) ip);
    const float a = ring[(size_t) channel][(size_t) (ip & mask)];
    const float b = ring[(size_t) channel][(size_t) ((ip + 1) & mask)];
    return a + frac * (b - a);
}

void RetuneEngine::fireGrain (double grainCentreOut, double sourceCentreAbs,
                              int periodSamples, long long nowAbs, double formantRatio)
{
    const int mask = ringSize - 1;
    const double P = (double) periodSamples;
    const float invP = 1.0f / (float) periodSamples;

    // Fractional grain centre: iterate integer OUTPUT positions inside the
    // span, evaluating window and source at exact fractional offsets. This
    // removes per-grain timing quantisation (audible as buzz/roughness).
    auto tStart = (long long) std::ceil  (grainCentreOut - P) ;
    auto tEnd   = (long long) std::floor (grainCentreOut + P);
    if (tStart < nowAbs)          // never write behind the emission point
        tStart = nowAbs;

    for (long long t = tStart; t <= tEnd; ++t)
    {
        const double d = (double) t - grainCentreOut;          // (-P, P)
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi
                                                * ((float) d + (float) P) * invP);
        const auto outIdx = (size_t) (t & mask);
        // Formant shift: resample the grain content around its centre. The
        // window stays in the output domain, so pitch (grain spacing) is
        // untouched while the spectral envelope (formants) scales.
        const double srcOff = d * formantRatio;
        for (int c = 0; c < channels; ++c)
            olaBuf[(size_t) c][outIdx] += w * readRing (c, sourceCentreAbs + srcOff);
        winSum[outIdx] += w;
    }
}

// ============================================================================
//  Main process
// ============================================================================
void RetuneEngine::process (juce::AudioBuffer<float>& buffer, const RetuneParams& p)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min (channels, buffer.getNumChannels());
    const int mask = ringSize - 1;

    const float glide = p.speedMs <= 0.5f
                      ? 0.0f
                      : std::exp (-1.0f / ((float) fs * p.speedMs * 0.001f));

    const float  humanize     = juce::jlimit (0.0f, 1.0f, p.humanize);
    const float  formantSemis = juce::jlimit (-5.0f, 5.0f, p.formant);
    const double formantRatio = std::pow (2.0, (double) formantSemis / 12.0);
    const double formantSpan  = std::max (1.0, formantRatio);   // for availability clamp

    for (int i = 0; i < numSamples; ++i)
    {
        const auto n = writeAbs;
        const auto wIdx = (size_t) (n & mask);

        // 1. Store input
        float mono = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            const float s = buffer.getSample (c, i);
            ring[(size_t) c][wIdx] = s;
            mono += s;
        }
        mono /= (float) numCh;

        // 2. Track pitch + voicing — ONLY when correcting. The YIN tracker is the
        //    single biggest always-on CPU cost in the whole plugin (an O(n^2) scan
        //    every analysis hop). Running it when auto-tune is OFF (the default!)
        //    wastes headroom and, on smaller DAW buffers, is enough to push the
        //    plugin into dropouts — heard as choppy/glitchy audio. Skip it when
        //    bypassed; it warms back up in a fraction of a second when re-enabled.
        if (p.on)
            pushAnalysis (mono);
        else
            uiInHz.store (0.0f);

        if (p.on)
        {
            // 3. Correction ratio (cents domain, glided)
            //    Humanize: quantize a centre that blends the instantaneous pitch
            //    (hard-tune, snaps the current note) with the slow pitch centre
            //    (musical, note-stable), then re-add the natural deviation around
            //    it scaled by `humanize` so vibrato/scoops survive.
            float targetCents = 0.0f;
            if (voicedState && currentF0 > 0.0f)
            {
                const float centreHz = (f0Center > 0.0f)
                                     ? currentF0 * (1.0f - humanize) + f0Center * humanize
                                     : currentF0;
                const float snapHz = quantizeTargetHz (centreHz, p);
                uiTargetHz.store (snapHz);
                if (snapHz > 0.0f)
                {
                    const float vibRatio = currentF0 / juce::jmax (1.0f, centreHz);
                    const float desiredHz = snapHz * std::pow (vibRatio, humanize);
                    targetCents = juce::jlimit (-700.0f, 700.0f,
                                    1200.0f * std::log2 (desiredHz / currentF0) * p.amount);
                }
            }
            else
            {
                uiTargetHz.store (0.0f);
            }

            const float currentCents = 1200.0f * std::log2 (juce::jmax (0.25f, currentRatio));
            const float newCents = voicedState
                                 ? glide * currentCents + (1.0f - glide) * targetCents
                                 : 0.999f * currentCents;   // ease to unity when unvoiced
            currentRatio = std::pow (2.0f, newCents / 1200.0f);

            // 4. Fire due grains
            const int P = juce::jlimit (32, (delaySamples - 192) / 2,
                                        (int) std::round (smoothedPeriod));

            if (nextGrainOut < (double) n - (double) P)   // scheduler lagged
                nextGrainOut = (double) n;

            while (nextGrainOut - (double) P <= (double) n)
            {
                const double ideal = nextGrainOut - (double) delaySamples;
                double src;

                if (voicedState && gridValid)
                {
                    // Period-aligned source grid keeps waveform coherence
                    const double k = std::round ((ideal - lastSourceCentre) / (double) P);
                    src = lastSourceCentre + k * (double) P;
                    src = juce::jlimit (ideal - (double) P, ideal + (double) P, src);
                }
                else
                {
                    // Unvoiced (or first voiced grain): lock to the ideal
                    // position → transparent reconstruction, and the grid
                    // restarts phase-aligned when voicing begins.
                    src = ideal;
                    gridValid = voicedState;
                }

                // Availability: the grain reads src ± P·formantSpan which must exist
                src = juce::jmin (src, (double) n - (double) P * formantSpan);

                fireGrain (nextGrainOut, src, P, n, formantRatio);
                lastSourceCentre = src;
                nextGrainOut += (double) P / (double) juce::jmax (0.5f, currentRatio);
            }
            if (! voicedState)
                gridValid = false;

            // 5. Emit normalised OLA
            const auto rIdx = (size_t) (n & mask);
            const float norm = 1.0f / juce::jmax (winSum[rIdx], 0.35f);
            for (int c = 0; c < numCh; ++c)
            {
                buffer.setSample (c, i, olaBuf[(size_t) c][rIdx] * norm);
                olaBuf[(size_t) c][rIdx] = 0.0f;
            }
            winSum[rIdx] = 0.0f;
        }
        else
        {
            // Bypass: delayed dry, constant latency
            uiTargetHz.store (0.0f);
            for (int c = 0; c < numCh; ++c)
                buffer.setSample (c, i, ring[(size_t) c]
                                        [(size_t) ((n - delaySamples) & mask)]);

            nextGrainOut = (double) (n + 1);
            lastSourceCentre = (double) (n - delaySamples);
            currentRatio = 1.0f;
            gridValid = false;

            const auto rIdx = (size_t) (n & mask);
            for (int c = 0; c < numCh; ++c) olaBuf[(size_t) c][rIdx] = 0.0f;
            winSum[rIdx] = 0.0f;
        }

        ++writeAbs;
    }
}
} // namespace vf
