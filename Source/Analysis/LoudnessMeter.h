#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace vf
{
// ============================================================================
//  LoudnessMeter — EBU R128 / ITU-R BS.1770-4 loudness measurement.
//  K-weighting (shelf + high-pass) → 400 ms gated blocks (75% overlap) →
//  absolute (-70 LUFS) and relative (-10 LU) gated integration.
// ============================================================================
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Process a block (does not modify audio). */
    void process (const juce::AudioBuffer<float>& buffer);

    float getMomentaryLufs()  const noexcept { return momentaryLufs; }
    float getIntegratedLufs() const noexcept { return integratedLufs; }

private:
    void pushBlockLoudness (double meanSquare);

    double fs = 44100.0;
    int    channels = 2;

    // Per-channel K-weighting filters
    std::vector<juce::dsp::IIR::Filter<float>> shelfFilters, hpFilters;

    // 400 ms measurement blocks with 100 ms hop
    int    hopSamples   = 4410;
    int    hopCounter   = 0;
    double sumSquares   = 0.0;      // accumulates K-weighted squared samples (this hop)
    std::vector<double> hopHistory; // last 4 hop mean-squares → one 400 ms block

    std::vector<double> blockLoudness;   // all gated blocks (mean square) for integration
    float momentaryLufs  = -100.0f;
    float integratedLufs = -100.0f;
};
} // namespace vf
