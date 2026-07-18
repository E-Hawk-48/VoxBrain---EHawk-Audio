#include "VocalIsolation.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace vf
{
juce::AudioBuffer<float> VocalIsolation::isolateCenter (const juce::AudioBuffer<float>& stereo, double)
{
    const int numCh = stereo.getNumChannels();
    const int N     = stereo.getNumSamples();

    // Mono (or empty) → nothing to extract; hand back a copy.
    if (numCh < 2 || N < 1)
    {
        juce::AudioBuffer<float> mono (1, juce::jmax (1, N));
        mono.clear();
        if (numCh >= 1 && N > 0) mono.copyFrom (0, 0, stereo, 0, 0, N);
        return mono;
    }

    constexpr int order = 11;                 // 2048
    constexpr int fftN  = 1 << order;
    const int     hop   = fftN / 4;           // 75% overlap

    juce::dsp::FFT fft (order);

    // sqrt-Hann analysis+synthesis window → COLA with 75% overlap.
    std::vector<float> win ((size_t) fftN);
    for (int i = 0; i < fftN; ++i)
        win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftN));

    const float* L = stereo.getReadPointer (0);
    const float* R = stereo.getReadPointer (1);

    std::vector<float> out ((size_t) N, 0.0f);
    std::vector<float> wsum ((size_t) N, 0.0f);
    std::vector<float> midBuf ((size_t) (fftN * 2), 0.0f);
    std::vector<float> sideBuf ((size_t) (fftN * 2), 0.0f);

    for (int s = 0; s < N; s += hop)
    {
        // windowed mid / side frames (zero-padded past the end)
        std::fill (midBuf.begin(),  midBuf.end(),  0.0f);
        std::fill (sideBuf.begin(), sideBuf.end(), 0.0f);
        const int frameLen = juce::jmin (fftN, N - s);
        for (int i = 0; i < frameLen; ++i)
        {
            const float l = L[s + i], r = R[s + i];
            midBuf[(size_t) i]  = 0.5f * (l + r) * win[(size_t) i];
            sideBuf[(size_t) i] = 0.5f * (l - r) * win[(size_t) i];
        }

        fft.performRealOnlyForwardTransform (midBuf.data(),  true);
        fft.performRealOnlyForwardTransform (sideBuf.data(), true);

        // per-bin centre gain applied to the mid spectrum (phase kept)
        for (int k = 0; k <= fftN / 2; ++k)
        {
            const float mr = midBuf[(size_t) (2 * k)],     mi = midBuf[(size_t) (2 * k + 1)];
            const float sr = sideBuf[(size_t) (2 * k)],    si = sideBuf[(size_t) (2 * k + 1)];
            const float midMag  = std::sqrt (mr * mr + mi * mi);
            const float sideMag = std::sqrt (sr * sr + si * si);
            const float gain    = midMag > 1.0e-9f ? juce::jlimit (0.0f, 1.0f, (midMag - sideMag) / midMag) : 0.0f;
            midBuf[(size_t) (2 * k)]     = mr * gain;
            midBuf[(size_t) (2 * k + 1)] = mi * gain;
        }

        fft.performRealOnlyInverseTransform (midBuf.data());

        // synthesis window + overlap-add
        for (int i = 0; i < frameLen; ++i)
        {
            const float w = win[(size_t) i];
            out[(size_t) (s + i)]  += midBuf[(size_t) i] * w;
            wsum[(size_t) (s + i)] += w * w;
        }
    }

    juce::AudioBuffer<float> mono (1, N);
    float* d = mono.getWritePointer (0);
    for (int n = 0; n < N; ++n)
    {
        const float g = wsum[(size_t) n];
        float v = g > 1.0e-6f ? out[(size_t) n] / g : 0.0f;
        if (! std::isfinite (v)) v = 0.0f;
        d[n] = juce::jlimit (-4.0f, 4.0f, v);
    }
    return mono;
}
} // namespace vf
