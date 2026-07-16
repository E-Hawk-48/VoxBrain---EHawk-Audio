#include "ToneModules.h"
#include <cmath>

namespace vf::mods
{
// ============================================================================
//  Parametric Vocal EQ — 4 musical bands with fixed voicing frequencies.
// ============================================================================
ParametricEqModule::ParametricEqModule()
{
    addParam ("low",   "Low",      -12.0f, 12.0f, 0.0f, "dB", false, 1.0f, "Low shelf around 120 Hz (weight/warmth).");
    addParam ("mud",   "Mud",      -12.0f, 12.0f, 0.0f, "dB", false, 1.0f, "Bell around 300 Hz (cut to de-mud).");
    addParam ("pres",  "Presence", -12.0f, 12.0f, 0.0f, "dB", false, 1.0f, "Bell around 3.5 kHz (clarity/forward).");
    addParam ("air",   "Air",      -12.0f, 12.0f, 0.0f, "dB", false, 1.0f, "High shelf around 10 kHz (openness).");
}
const Descriptor& ParametricEqModule::desc()
{
    static const Descriptor d { "parametric_eq", "Parametric EQ", Category::EQ,
        { "eq", "tone", "frequency", "bell", "shelf", "presence", "air", "mud" },
        "Musical 4-band vocal EQ.", true };
    return d;
}
void ParametricEqModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    for (auto& bnd : band) for (auto& f : bnd) f.reset();
    for (auto& l : last) l = 999.0f;
}
void ParametricEqModule::reset() { for (auto& bnd : band) for (auto& f : bnd) f.reset(); }
void ParametricEqModule::updateCoeffs()
{
    const float g[4] = { value ("low"), value ("mud"), value ("pres"), value ("air") };
    bool changed = false;
    for (int i = 0; i < 4; ++i) if (g[i] != last[i]) { changed = true; break; }
    if (! changed) return;

    using Co = juce::dsp::IIR::Coefficients<float>;
    auto lowC  = Co::makeLowShelf  (fs, 120.0f,  0.707f, juce::Decibels::decibelsToGain (g[0]));
    auto mudC  = Co::makePeakFilter (fs, 300.0f,  1.0f,   juce::Decibels::decibelsToGain (g[1]));
    auto presC = Co::makePeakFilter (fs, 3500.0f, 1.0f,   juce::Decibels::decibelsToGain (g[2]));
    auto airC  = Co::makeHighShelf (fs, 10000.0f, 0.707f, juce::Decibels::decibelsToGain (g[3]));
    for (int c = 0; c < 2; ++c)
    {
        band[0][c].coefficients = lowC;
        band[1][c].coefficients = mudC;
        band[2][c].coefficients = presC;
        band[3][c].coefficients = airC;
    }
    for (int i = 0; i < 4; ++i) last[i] = g[i];
}
void ParametricEqModule::process (juce::AudioBuffer<float>& b)
{
    updateCoeffs();
    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            float x = d[i];
            x = band[0][c].processSample (x);
            x = band[1][c].processSample (x);
            x = band[2][c].processSample (x);
            x = band[3][c].processSample (x);
            d[i] = x;
        }
    }
}

// ============================================================================
//  Dynamic EQ — ducks a resonant band only when it gets loud.
// ============================================================================
DynamicEqModule::DynamicEqModule()
{
    addParam ("freq",      "Frequency", 80.0f, 12000.0f, 500.0f, "Hz", false, 0.4f, "Centre of the dynamic band.");
    addParam ("q",         "Q",          0.3f, 8.0f, 2.0f, {}, false, 0.5f, "Bandwidth (higher = narrower).");
    addParam ("threshold", "Threshold", -60.0f, 0.0f, -24.0f, "dB", false, 1.0f, "Band level where ducking starts.");
    addParam ("range",     "Range",       0.0f, 18.0f, 6.0f, "dB", false, 1.0f, "Maximum reduction of the band.");
}
const Descriptor& DynamicEqModule::desc()
{
    static const Descriptor d { "dynamic_eq", "Dynamic EQ", Category::EQ,
        { "eq", "dynamic", "resonance", "mud", "harsh", "tame", "boxy", "honk" },
        "Frequency-dependent dynamic cut EQ.", true };
    return d;
}
void DynamicEqModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    for (auto& f : bp) f.reset();
    lastFreq = lastQ = -1.0f;
    env.setTimes (3.0f, 120.0f, fs);
}
void DynamicEqModule::reset() { for (auto& f : bp) f.reset(); env.reset(); }
void DynamicEqModule::process (juce::AudioBuffer<float>& b)
{
    const float freq  = value ("freq");
    const float q     = juce::jmax (0.2f, value ("q"));
    const float thr   = value ("threshold");
    const float range = value ("range");
    if (freq != lastFreq || q != lastQ)
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeBandPass (fs, freq, q);
        bp[0].coefficients = c; bp[1].coefficients = c;
        lastFreq = freq; lastQ = q;
    }
    env.setTimes (3.0f, 120.0f, fs);

    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R ? R[i] : l;
        const float bL = bp[0].processSample (l);
        const float bR = R ? bp[1].processSample (r) : bL;
        const float e = env.process (juce::jmax (std::abs (bL), std::abs (bR)));
        const float over = juce::Decibels::gainToDecibels (e, -100.0f) - thr;
        float grDb = over > 0.0f ? -over : 0.0f;
        grDb = juce::jmax (grDb, -range);
        const float duck = 1.0f - juce::Decibels::decibelsToGain (grDb);
        L[i] = l - bL * duck;
        if (R) R[i] = r - bR * duck;
    }
}

// ============================================================================
//  Exciter — high-passes, generates harmonics, blends the sparkle back in.
// ============================================================================
ExciterModule::ExciterModule()
{
    addParam ("freq",   "Frequency", 1500.0f, 9000.0f, 4000.0f, "Hz", false, 0.4f, "Only content above this is excited.");
    addParam ("amount", "Amount",    0.0f, 100.0f, 30.0f, "%", false, 1.0f, "Drive into the harmonic generator.");
    addParam ("mix",    "Mix",       0.0f, 100.0f, 35.0f, "%", false, 1.0f, "How much sparkle to add.");
}
const Descriptor& ExciterModule::desc()
{
    static const Descriptor d { "exciter", "Exciter", Category::Saturation,
        { "exciter", "airy", "sparkle", "presence", "sheen", "bright", "crisp" },
        "Adds high-end harmonics for air and presence.", true };
    return d;
}
void ExciterModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    for (auto& f : hp) f.reset();
    lastFreq = -1.0f;
}
void ExciterModule::reset() { for (auto& f : hp) f.reset(); }
void ExciterModule::process (juce::AudioBuffer<float>& b)
{
    const float freq   = value ("freq");
    const float amount = 1.0f + value ("amount") * 0.06f;   // 1..7
    const float mix    = value ("mix") * 0.01f;
    if (freq != lastFreq)
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, freq, 0.707f);
        hp[0].coefficients = c; hp[1].coefficients = c;
        lastFreq = freq;
    }
    const int n  = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        auto& f = hp[c];
        for (int i = 0; i < n; ++i)
        {
            const float hb = f.processSample (d[i]);
            const float excited = std::tanh (hb * amount) * 0.5f;
            d[i] = d[i] + excited * mix;
        }
    }
}
} // namespace vf::mods
