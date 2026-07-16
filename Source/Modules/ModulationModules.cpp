#include "ModulationModules.h"
#include <cmath>

namespace vf::mods
{
namespace
{
    inline float readInterp (const float* buf, int len, float pos)
    {
        while (pos < 0.0f) pos += (float) len;
        const int i0 = (int) pos;
        const float frac = pos - (float) i0;
        const int i1 = (i0 + 1) % len;
        return buf[i0] + (buf[i1] - buf[i0]) * frac;
    }
}

StereoChorusModule::StereoChorusModule()
{
    addParam ("rate",  "Rate",  0.05f, 8.0f, 0.6f, "Hz", false, 0.4f, "Sweep speed.");
    addParam ("depth", "Depth", 0.0f, 100.0f, 45.0f, "%", false, 1.0f, "Sweep depth (thickness).");
    addParam ("mix",   "Mix",   0.0f, 100.0f, 40.0f, "%", false, 1.0f, "Dry/wet blend.");
    addParam ("width", "Width", 0.0f, 100.0f, 80.0f, "%", false, 1.0f, "Stereo spread of the effect.");
}
const Descriptor& StereoChorusModule::desc()
{
    static const Descriptor d { "stereo_chorus", "Stereo Chorus", Category::Modulation,
        { "chorus", "stereo", "wide", "modulation", "lush", "thicken", "shimmer" },
        "Wide stereo chorus for lush, thick vocals.", true };
    return d;
}
void StereoChorusModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    dlen = (int) (0.05 * fs) + 4;          // 50 ms line
    delayBuf.setSize (2, dlen);
    delayBuf.clear();
    writePos = 0;
    phaseL = 0.0f;
    phaseR = 0.25f;
}
void StereoChorusModule::reset() { delayBuf.clear(); writePos = 0; phaseL = 0.0f; phaseR = 0.25f; }
void StereoChorusModule::process (juce::AudioBuffer<float>& b)
{
    if (dlen <= 0) return;
    const float rate  = value ("rate");
    const float depth = value ("depth") * 0.01f;
    const float mix   = value ("mix") * 0.01f;
    const float width = value ("width") * 0.015f;   // 0..1.5 side scale

    const float base    = 0.012f * (float) fs;      // 12 ms centre delay
    const float modAmt  = depth * 0.006f * (float) fs;   // up to 6 ms
    const float phaseInc = rate / (float) fs;

    const int n  = b.getNumSamples();
    const bool stereo = b.getNumChannels() > 1;
    float* L = b.getWritePointer (0);
    float* R = stereo ? b.getWritePointer (1) : nullptr;
    float* dL = delayBuf.getWritePointer (0);
    float* dR = delayBuf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i];
        const float inR = R ? R[i] : inL;

        const float lfoL = std::sin (juce::MathConstants<float>::twoPi * phaseL);
        const float lfoR = std::sin (juce::MathConstants<float>::twoPi * phaseR);
        const float dSampL = base + modAmt * (0.5f + 0.5f * lfoL);
        const float dSampR = base + modAmt * (0.5f + 0.5f * lfoR);

        float wetL = readInterp (dL, dlen, (float) writePos - dSampL);
        float wetR = readInterp (dR, dlen, (float) writePos - dSampR);

        // stereo-widen the wet signal
        const float mid  = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * width;
        wetL = mid + side;
        wetR = mid - side;

        dL[writePos] = inL;
        dR[writePos] = inR;

        L[i] = inL * (1.0f - mix) + wetL * mix;
        if (R) R[i] = inR * (1.0f - mix) + wetR * mix;

        phaseL += phaseInc; if (phaseL >= 1.0f) phaseL -= 1.0f;
        phaseR += phaseInc; if (phaseR >= 1.0f) phaseR -= 1.0f;
        if (++writePos >= dlen) writePos = 0;
    }
}
} // namespace vf::mods
