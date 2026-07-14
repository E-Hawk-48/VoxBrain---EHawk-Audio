#include "LoudnessMeter.h"
#include <cmath>
#include <numeric>

namespace vf
{
namespace
{
    constexpr double absoluteGateLufs = -70.0;

    double meanSquareToLufs (double ms)
    {
        return ms > 0.0 ? -0.691 + 10.0 * std::log10 (ms) : -120.0;
    }
} // namespace

void LoudnessMeter::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    channels = std::max (1, numChannels);
    hopSamples = (int) std::round (fs * 0.100);   // 100 ms hop

    shelfFilters.clear();
    hpFilters.clear();

    // BS.1770 stage 1: +4 dB high shelf (~1681 Hz), stage 2: high-pass (~38 Hz).
    // Coefficients derived from the ITU reference biquads, bilinear-scaled to fs.
    auto shelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                     fs, 1681.974450955533, 0.7071752369554196f, juce::Decibels::decibelsToGain (3.999843853973347f));
    auto hp    = juce::dsp::IIR::Coefficients<float>::makeHighPass (
                     fs, 38.13547087602444, 0.5003270373238773f);

    for (int c = 0; c < channels; ++c)
    {
        shelfFilters.emplace_back (shelf);
        hpFilters.emplace_back (hp);
    }
    reset();
}

void LoudnessMeter::reset()
{
    for (auto& f : shelfFilters) f.reset();
    for (auto& f : hpFilters)    f.reset();
    hopCounter = 0;
    sumSquares = 0.0;
    hopHistory.clear();
    blockLoudness.clear();
    momentaryLufs  = -100.0f;
    integratedLufs = -100.0f;
}

void LoudnessMeter::process (const juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min (channels, buffer.getNumChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        double frameSum = 0.0;
        for (int c = 0; c < numCh; ++c)
        {
            float s = buffer.getSample (c, i);
            s = shelfFilters[(size_t) c].processSample (s);
            s = hpFilters[(size_t) c].processSample (s);
            frameSum += (double) s * (double) s;   // channel weights = 1.0 (no surround)
        }
        sumSquares += frameSum;

        if (++hopCounter >= hopSamples)
        {
            pushBlockLoudness (sumSquares / (double) hopSamples);
            hopCounter = 0;
            sumSquares = 0.0;
        }
    }
}

void LoudnessMeter::pushBlockLoudness (double hopMeanSquare)
{
    hopHistory.push_back (hopMeanSquare);
    if (hopHistory.size() > 4)
        hopHistory.erase (hopHistory.begin());

    if (hopHistory.size() < 4)
        return;

    // One 400 ms block = mean of the last four 100 ms hops
    const double blockMs = std::accumulate (hopHistory.begin(), hopHistory.end(), 0.0) / 4.0;
    momentaryLufs = (float) meanSquareToLufs (blockMs);

    // Absolute gate
    if (meanSquareToLufs (blockMs) > absoluteGateLufs)
        blockLoudness.push_back (blockMs);

    // Integrated loudness with relative gate (-10 LU below abs-gated mean)
    if (! blockLoudness.empty())
    {
        const double absMean = std::accumulate (blockLoudness.begin(), blockLoudness.end(), 0.0)
                             / (double) blockLoudness.size();
        const double relGate = meanSquareToLufs (absMean) - 10.0;

        double sum = 0.0; int count = 0;
        for (double ms : blockLoudness)
            if (meanSquareToLufs (ms) > relGate) { sum += ms; ++count; }

        if (count > 0)
            integratedLufs = (float) meanSquareToLufs (sum / (double) count);
    }

    // Keep memory bounded for very long sessions (~2.2 hours of blocks)
    if (blockLoudness.size() > 80000)
        blockLoudness.erase (blockLoudness.begin(), blockLoudness.begin() + 40000);
}
} // namespace vf
