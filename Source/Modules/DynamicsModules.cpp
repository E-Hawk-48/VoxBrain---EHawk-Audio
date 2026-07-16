#include "DynamicsModules.h"
#include <cmath>

namespace vf::mods
{
namespace
{
    inline float maxAbs (float a, float b) { return juce::jmax (std::abs (a), std::abs (b)); }
}

// ============================================================================
//  Compressor — feed-forward, stereo-linked, dry/wet.
// ============================================================================
CompressorModule::CompressorModule()
{
    addParam ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, "dB", false, 1.0f, "Level where compression starts.");
    addParam ("ratio",     "Ratio",       1.0f, 20.0f, 3.0f, ":1", false, 0.5f, "How hard it clamps above threshold.");
    addParam ("attack",    "Attack",      0.1f, 100.0f, 8.0f, "ms", false, 0.4f, "How fast it grabs.");
    addParam ("release",   "Release",     10.0f, 500.0f, 120.0f, "ms", false, 0.4f, "How fast it lets go.");
    addParam ("makeup",    "Makeup",       0.0f, 24.0f, 0.0f, "dB", false, 1.0f, "Level added back after compression.");
    addParam ("mix",       "Mix",          0.0f, 100.0f, 100.0f, "%", false, 1.0f, "Dry/wet blend (parallel).");
}
const Descriptor& CompressorModule::desc()
{
    static const Descriptor d { "compressor", "Compressor", Category::Dynamics,
        { "compress", "dynamics", "level", "glue", "punch", "control" },
        "Feed-forward compressor with parallel mix.", true };
    return d;
}
void CompressorModule::process (juce::AudioBuffer<float>& b)
{
    const float thr    = value ("threshold");
    const float ratio  = juce::jmax (1.0f, value ("ratio"));
    const float makeup = juce::Decibels::decibelsToGain (value ("makeup"));
    const float mix    = value ("mix") * 0.01f;
    const float slope  = 1.0f / ratio - 1.0f;
    env.setTimes (value ("attack"), value ("release"), fs);

    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R ? R[i] : l;
        const float e = env.process (maxAbs (l, r));
        const float over = juce::Decibels::gainToDecibels (e, -100.0f) - thr;
        const float g = juce::Decibels::decibelsToGain (over > 0.0f ? over * slope : 0.0f) * makeup;
        L[i] = l * (1.0f - mix) + l * g * mix;
        if (R) R[i] = r * (1.0f - mix) + r * g * mix;
    }
}

// ============================================================================
//  Gate / Downward Expander
// ============================================================================
GateModule::GateModule()
{
    addParam ("threshold", "Threshold", -80.0f, 0.0f, -45.0f, "dB", false, 1.0f, "Signals below this are attenuated.");
    addParam ("ratio",     "Ratio",       1.0f, 20.0f, 4.0f, ":1", false, 0.5f, "Steepness of the downward expansion.");
    addParam ("attack",    "Attack",      0.1f, 50.0f, 2.0f, "ms", false, 0.4f, "How fast the gate opens.");
    addParam ("release",   "Release",     10.0f, 800.0f, 150.0f, "ms", false, 0.4f, "How fast the gate closes.");
    addParam ("range",     "Range",        0.0f, 80.0f, 40.0f, "dB", false, 1.0f, "Maximum attenuation when closed.");
}
const Descriptor& GateModule::desc()
{
    static const Descriptor d { "gate", "Gate / Expander", Category::Dynamics,
        { "gate", "expander", "noise", "clean", "downward", "silence" },
        "Downward expander / noise gate.", true };
    return d;
}
void GateModule::process (juce::AudioBuffer<float>& b)
{
    const float thr   = value ("threshold");
    const float ratio = juce::jmax (1.0f, value ("ratio"));
    const float range = value ("range");
    env.setTimes (value ("attack"), value ("release"), fs);

    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R ? R[i] : l;
        const float e = env.process (maxAbs (l, r));
        const float under = thr - juce::Decibels::gainToDecibels (e, -100.0f);
        float grDb = under > 0.0f ? -under * (ratio - 1.0f) : 0.0f;
        grDb = juce::jmax (grDb, -range);
        const float g = juce::Decibels::decibelsToGain (grDb);
        L[i] = l * g;
        if (R) R[i] = r * g;
    }
}

// ============================================================================
//  De-Esser — ducks a sibilant high band when it gets loud.
// ============================================================================
DeEsserModule::DeEsserModule()
{
    addParam ("freq",      "Frequency", 2000.0f, 12000.0f, 6500.0f, "Hz", false, 0.4f, "Centre of the sibilance band.");
    addParam ("threshold", "Threshold", -60.0f, 0.0f, -28.0f, "dB", false, 1.0f, "Sibilance level where ducking begins.");
    addParam ("range",     "Amount",     0.0f, 24.0f, 8.0f, "dB", false, 1.0f, "Maximum sibilance reduction.");
}
const Descriptor& DeEsserModule::desc()
{
    static const Descriptor d { "de_esser", "De-Esser", Category::Restoration,
        { "deess", "sibilance", "harsh", "ess", "clean", "sss" },
        "Dynamic sibilance (ess) reduction.", true };
    return d;
}
void DeEsserModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    for (auto& f : hp) f.reset();
    lastFreq = -1.0f;
    env.setTimes (0.5f, 60.0f, fs);
}
void DeEsserModule::reset() { for (auto& f : hp) f.reset(); env.reset(); }
void DeEsserModule::process (juce::AudioBuffer<float>& b)
{
    const float freq  = value ("freq");
    const float thr   = value ("threshold");
    const float range = value ("range");
    if (freq != lastFreq)
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, freq, 0.707f);
        hp[0].coefficients = c; hp[1].coefficients = c;
        lastFreq = freq;
    }
    env.setTimes (0.5f, 60.0f, fs);

    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R ? R[i] : l;
        const float hbL = hp[0].processSample (l);
        const float hbR = R ? hp[1].processSample (r) : hbL;
        const float e = env.process (maxAbs (hbL, hbR));
        const float over = juce::Decibels::gainToDecibels (e, -100.0f) - thr;
        float grDb = over > 0.0f ? -over * 0.8f : 0.0f;
        grDb = juce::jmax (grDb, -range);
        const float duck = 1.0f - juce::Decibels::decibelsToGain (grDb);   // 0..1 amount removed
        L[i] = l - hbL * duck;
        if (R) R[i] = r - hbR * duck;
    }
}

// ============================================================================
//  De-Plosive — ducks a low band on plosive pops.
// ============================================================================
DePlosiveModule::DePlosiveModule()
{
    addParam ("freq",      "Frequency", 40.0f, 200.0f, 90.0f, "Hz", false, 0.4f, "Below this is treated as plosive energy.");
    addParam ("threshold", "Threshold", -50.0f, 0.0f, -24.0f, "dB", false, 1.0f, "Pop level where ducking begins.");
    addParam ("range",     "Amount",     0.0f, 24.0f, 10.0f, "dB", false, 1.0f, "Maximum low-end reduction on a pop.");
}
const Descriptor& DePlosiveModule::desc()
{
    static const Descriptor d { "de_plosive", "De-Plosive", Category::Restoration,
        { "plosive", "pop", "breath", "clean", "p", "b", "boom" },
        "Tames plosive 'p'/'b' pops.", true };
    return d;
}
void DePlosiveModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    for (auto& f : lp) f.reset();
    lastFreq = -1.0f;
    env.setTimes (1.0f, 90.0f, fs);
}
void DePlosiveModule::reset() { for (auto& f : lp) f.reset(); env.reset(); }
void DePlosiveModule::process (juce::AudioBuffer<float>& b)
{
    const float freq  = value ("freq");
    const float thr   = value ("threshold");
    const float range = value ("range");
    if (freq != lastFreq)
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass (fs, freq, 0.707f);
        lp[0].coefficients = c; lp[1].coefficients = c;
        lastFreq = freq;
    }
    env.setTimes (1.0f, 90.0f, fs);

    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R ? R[i] : l;
        const float lbL = lp[0].processSample (l);
        const float lbR = R ? lp[1].processSample (r) : lbL;
        const float e = env.process (maxAbs (lbL, lbR));
        const float over = juce::Decibels::gainToDecibels (e, -100.0f) - thr;
        float grDb = over > 0.0f ? -over * 0.9f : 0.0f;
        grDb = juce::jmax (grDb, -range);
        const float duck = 1.0f - juce::Decibels::decibelsToGain (grDb);
        L[i] = l - lbL * duck;
        if (R) R[i] = r - lbR * duck;
    }
}

// ============================================================================
//  Soft Clipper
// ============================================================================
SoftClipperModule::SoftClipperModule()
{
    addParam ("drive",   "Drive",   0.0f, 24.0f, 3.0f, "dB", false, 1.0f, "Pushes signal into the clip curve.");
    addParam ("ceiling", "Ceiling", -12.0f, 0.0f, -0.5f, "dB", false, 1.0f, "Output ceiling the curve approaches.");
    addParam ("mix",     "Mix",     0.0f, 100.0f, 100.0f, "%", false, 1.0f, "Dry/wet blend.");
}
const Descriptor& SoftClipperModule::desc()
{
    static const Descriptor d { "soft_clipper", "Soft Clipper", Category::Dynamics,
        { "clip", "soft", "transient", "loud", "saturation", "tame" },
        "Rounds peaks before the limiter.", true };
    return d;
}
void SoftClipperModule::process (juce::AudioBuffer<float>& b)
{
    const float drive = juce::Decibels::decibelsToGain (value ("drive"));
    const float ceil  = juce::jmax (0.05f, juce::Decibels::decibelsToGain (value ("ceiling")));
    const float mix   = value ("mix") * 0.01f;
    const int n  = b.getNumSamples();
    const int nc = b.getNumChannels();
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float x   = d[i];
            const float wet = ceil * std::tanh (x * drive / ceil);
            d[i] = x * (1.0f - mix) + wet * mix;
        }
    }
}

// ============================================================================
//  Noise Reduction — broadband downward expander (hiss/room gate).
// ============================================================================
NoiseReductionModule::NoiseReductionModule()
{
    addParam ("threshold", "Threshold", -80.0f, -20.0f, -55.0f, "dB", false, 1.0f, "Noise floor level to push down below.");
    addParam ("reduction", "Reduction",  0.0f, 40.0f, 14.0f, "dB", false, 1.0f, "How much to attenuate the noise floor.");
    addParam ("attack",    "Attack",     0.5f, 50.0f, 5.0f, "ms", false, 0.4f, "How fast it opens on signal.");
    addParam ("release",   "Release",    20.0f, 800.0f, 250.0f, "ms", false, 0.4f, "How fast it closes on silence.");
}
const Descriptor& NoiseReductionModule::desc()
{
    static const Descriptor d { "noise_reduction", "Noise Reduction", Category::Restoration,
        { "noise", "denoise", "clean", "restore", "hiss", "hum" },
        "Broadband downward expander for hiss/room noise.", true };
    return d;
}
void NoiseReductionModule::process (juce::AudioBuffer<float>& b)
{
    const float thr = value ("threshold");
    const float red = value ("reduction");
    env.setTimes (value ("attack"), value ("release"), fs);

    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R ? R[i] : l;
        const float e = env.process (maxAbs (l, r));
        const float under = thr - juce::Decibels::gainToDecibels (e, -100.0f);
        const float grDb = under > 0.0f ? -juce::jmin (red, under * 2.0f) : 0.0f;
        const float g = juce::Decibels::decibelsToGain (grDb);
        L[i] = l * g;
        if (R) R[i] = r * g;
    }
}

// ============================================================================
//  Parallel Compressor — heavy compressed layer added under the dry.
// ============================================================================
ParallelCompModule::ParallelCompModule()
{
    addParam ("threshold", "Threshold", -60.0f, 0.0f, -30.0f, "dB", false, 1.0f, "Level where the parallel layer clamps.");
    addParam ("ratio",     "Ratio",       1.0f, 20.0f, 8.0f, ":1", false, 0.5f, "Compression on the parallel layer.");
    addParam ("attack",    "Attack",      0.1f, 100.0f, 15.0f, "ms", false, 0.4f, "Attack of the parallel layer.");
    addParam ("release",   "Release",     10.0f, 500.0f, 180.0f, "ms", false, 0.4f, "Release of the parallel layer.");
    addParam ("blend",     "Blend",        0.0f, 100.0f, 40.0f, "%", false, 1.0f, "How much compressed layer to add.");
}
const Descriptor& ParallelCompModule::desc()
{
    static const Descriptor d { "parallel_comp", "Parallel Compressor", Category::Dynamics,
        { "compress", "parallel", "energy", "punch", "thick", "new york" },
        "New-York parallel compression.", true };
    return d;
}
void ParallelCompModule::process (juce::AudioBuffer<float>& b)
{
    const float thr    = value ("threshold");
    const float ratio  = juce::jmax (1.0f, value ("ratio"));
    const float blend  = value ("blend") * 0.01f;
    const float slope  = 1.0f / ratio - 1.0f;
    const float makeup = juce::Decibels::decibelsToGain (-thr * 0.35f);   // auto lift the layer
    env.setTimes (value ("attack"), value ("release"), fs);

    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R ? R[i] : l;
        const float e = env.process (maxAbs (l, r));
        const float over = juce::Decibels::gainToDecibels (e, -100.0f) - thr;
        const float g = juce::Decibels::decibelsToGain (over > 0.0f ? over * slope : 0.0f) * makeup;
        L[i] = l + l * g * blend;
        if (R) R[i] = r + r * g * blend;
    }
}

// ============================================================================
//  Limiter — fast peak limiter with a hard safety ceiling (zero latency).
// ============================================================================
LimiterModule::LimiterModule()
{
    addParam ("gain",    "Input Gain", 0.0f, 24.0f, 0.0f, "dB", false, 1.0f, "Drive into the limiter.");
    addParam ("ceiling", "Ceiling",   -12.0f, 0.0f, -0.3f, "dB", false, 1.0f, "Absolute output ceiling.");
    addParam ("release", "Release",    10.0f, 500.0f, 80.0f, "ms", false, 0.4f, "How fast gain recovers.");
}
const Descriptor& LimiterModule::desc()
{
    static const Descriptor d { "limiter", "Limiter", Category::Dynamics,
        { "limit", "ceiling", "loud", "brickwall", "peak", "safe" },
        "Fast peak limiter with a brickwall ceiling.", true };
    return d;
}
void LimiterModule::process (juce::AudioBuffer<float>& b)
{
    const float inGain = juce::Decibels::decibelsToGain (value ("gain"));
    const float ceil   = juce::jmax (0.05f, juce::Decibels::decibelsToGain (value ("ceiling")));
    env.setTimes (0.1f, value ("release"), fs);

    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i] * inGain;
        const float r = (R ? R[i] : l) * inGain;
        const float e = env.process (maxAbs (l, r));
        const float g = e > ceil ? ceil / e : 1.0f;
        L[i] = juce::jlimit (-ceil, ceil, l * g);
        if (R) R[i] = juce::jlimit (-ceil, ceil, r * g);
    }
}
} // namespace vf::mods
