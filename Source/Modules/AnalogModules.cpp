#include "AnalogModules.h"
#include <cmath>

namespace vf::mods
{
namespace { inline float mx (float a, float b) { return juce::jmax (std::abs (a), std::abs (b)); } }

// ============================================================================
//  Tape Saturation — soft tanh drive with a gentle high-frequency roll-off.
// ============================================================================
TapeSatModule::TapeSatModule()
{
    addParam ("drive", "Drive", 0.0f, 100.0f, 30.0f, "%", false, 1.0f, "Tape compression + harmonics.");
    addParam ("tone",  "Tone",  0.0f, 100.0f, 55.0f, "%", false, 1.0f, "High-frequency softening (lower = darker).");
    addParam ("mix",   "Mix",   0.0f, 100.0f, 100.0f, "%", false, 1.0f, "Dry/wet blend.");
}
const Descriptor& TapeSatModule::desc()
{
    static const Descriptor d { "tape_sat", "Tape Saturation", Category::Saturation,
        { "warm", "tape", "analog", "vintage", "saturation", "colour", "smooth" },
        "Smooth, warm tape-style saturation.", true };
    return d;
}
void TapeSatModule::process (juce::AudioBuffer<float>& b)
{
    const float drive = 1.0f + value ("drive") * 0.06f;         // 1..7
    const float mix   = value ("mix") * 0.01f;
    const float tone  = value ("tone") * 0.01f;
    const float a     = std::exp (-2.0f * juce::MathConstants<float>::pi
                                  * (1500.0f + tone * 14000.0f) / (float) fs);   // LP corner
    const int n = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float x = d[i];
            float wet = std::tanh (x * drive) * 0.85f;
            lp[c] = (1.0f - a) * wet + a * lp[c];               // tape HF softening
            wet = lp[c];
            d[i] = x * (1.0f - mix) + wet * mix;
        }
    }
}

// ============================================================================
//  Console Saturation — subtle even+odd harmonics for glue.
// ============================================================================
ConsoleSatModule::ConsoleSatModule()
{
    addParam ("drive", "Drive", 0.0f, 100.0f, 25.0f, "%", false, 1.0f, "Console-desk colour.");
    addParam ("mix",   "Mix",   0.0f, 100.0f, 100.0f, "%", false, 1.0f, "Dry/wet blend.");
}
const Descriptor& ConsoleSatModule::desc()
{
    static const Descriptor d { "console_sat", "Console Saturation", Category::Saturation,
        { "console", "analog", "glue", "colour", "saturation", "desk", "subtle" },
        "Subtle console-desk drive and glue.", true };
    return d;
}
void ConsoleSatModule::process (juce::AudioBuffer<float>& b)
{
    const float drive = 1.0f + value ("drive") * 0.04f;         // 1..5
    const float mix   = value ("mix") * 0.01f;
    const int n = b.getNumSamples();
    const int nc = b.getNumChannels();
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float x  = d[i] * drive;
            const float wet = (x - 0.15f * x * x * x) * 0.92f;  // soft odd + slight even
            d[i] = d[i] * (1.0f - mix) + wet * mix;
        }
    }
}

// ============================================================================
//  Transient Designer — attack/sustain shaping via fast vs slow envelopes.
// ============================================================================
TransientModule::TransientModule()
{
    addParam ("attack",  "Attack",  -100.0f, 100.0f, 0.0f, "%", false, 1.0f, "Boost (+) or soften (-) transients.");
    addParam ("sustain", "Sustain", -100.0f, 100.0f, 0.0f, "%", false, 1.0f, "Add (+) or tighten (-) the tail.");
}
const Descriptor& TransientModule::desc()
{
    static const Descriptor d { "transient_designer", "Transient Designer", Category::Dynamics,
        { "transient", "punch", "attack", "sustain", "snap", "tight" },
        "Shape attack and sustain independently.", true };
    return d;
}
void TransientModule::process (juce::AudioBuffer<float>& b)
{
    const float att = value ("attack") * 0.01f;      // -1..1
    const float sus = value ("sustain") * 0.01f;
    fast.setTimes (0.5f, 30.0f, fs);
    slow.setTimes (20.0f, 180.0f, fs);

    const int n = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        const float det = mx (L[i], R ? R[i] : L[i]);
        const float f = fast.process (det);
        const float s = slow.process (det);
        const float trans = juce::jlimit (-1.0f, 1.0f, (f - s) * 4.0f);   // + on onset, - on decay
        // attack acts on the onset portion, sustain on the decay portion
        float g = 1.0f;
        if (trans > 0.0f) g += att * trans;           // shape the attack
        else              g += sus * (-trans);        // shape the sustain
        g = juce::jlimit (0.25f, 4.0f, g);
        L[i] *= g; if (R) R[i] *= g;
    }
}

// ============================================================================
//  Telephone — band-limit + drive (lo-fi "phone"/megaphone).
// ============================================================================
TelephoneModule::TelephoneModule()
{
    addParam ("low",   "Low Cut",  100.0f, 900.0f, 400.0f, "Hz", false, 0.4f, "Rolls off below this.");
    addParam ("high",  "High Cut", 1800.0f, 6000.0f, 3400.0f, "Hz", false, 0.4f, "Rolls off above this.");
    addParam ("drive", "Drive",    0.0f, 100.0f, 35.0f, "%", false, 1.0f, "Grit inside the band.");
    addParam ("mix",   "Mix",      0.0f, 100.0f, 100.0f, "%", false, 1.0f, "Dry/wet blend.");
}
const Descriptor& TelephoneModule::desc()
{
    static const Descriptor d { "telephone", "Telephone", Category::Filter,
        { "telephone", "phone", "lofi", "band", "megaphone", "radio", "vintage" },
        "Band-limited, driven 'phone' effect.", true };
    return d;
}
void TelephoneModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    for (auto& f : hp) f.reset();
    for (auto& f : lp) f.reset();
    lastLo = lastHi = -1.0f;
}
void TelephoneModule::reset() { for (auto& f : hp) f.reset(); for (auto& f : lp) f.reset(); }
void TelephoneModule::process (juce::AudioBuffer<float>& b)
{
    const float lo = value ("low"), hi = juce::jmax (value ("high"), value ("low") + 200.0f);
    const float drive = 1.0f + value ("drive") * 0.05f;
    const float mix = value ("mix") * 0.01f;
    if (lo != lastLo) { auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, lo, 0.9f); hp[0].coefficients = c; hp[1].coefficients = c; lastLo = lo; }
    if (hi != lastHi) { auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass  (fs, hi, 0.9f); lp[0].coefficients = c; lp[1].coefficients = c; lastHi = hi; }

    const int n = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            float w = lp[c].processSample (hp[c].processSample (d[i]));
            w = std::tanh (w * drive) * 0.9f;
            d[i] = d[i] * (1.0f - mix) + w * mix;
        }
    }
}

// ============================================================================
//  Vintage EQ — broad low shelf, presence bell, high shelf (Pultec-ish).
// ============================================================================
VintageEqModule::VintageEqModule()
{
    addParam ("low",  "Low",      -10.0f, 10.0f, 0.0f, "dB", false, 1.0f, "Low shelf ~100 Hz (weight).");
    addParam ("pres", "Presence", -10.0f, 10.0f, 0.0f, "dB", false, 1.0f, "Broad bell ~4 kHz (forward).");
    addParam ("high", "Air",      -10.0f, 10.0f, 0.0f, "dB", false, 1.0f, "High shelf ~12 kHz (silk).");
}
const Descriptor& VintageEqModule::desc()
{
    static const Descriptor d { "vintage_eq", "Vintage EQ", Category::EQ,
        { "eq", "vintage", "analog", "warm", "musical", "pultec", "broad" },
        "Musical broad-stroke vintage EQ.", true };
    return d;
}
void VintageEqModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    for (auto& f : low)  f.reset();
    for (auto& f : pres) f.reset();
    for (auto& f : high) f.reset();
    for (auto& l : last) l = 999.0f;
}
void VintageEqModule::reset() { for (auto& f : low) f.reset(); for (auto& f : pres) f.reset(); for (auto& f : high) f.reset(); }
void VintageEqModule::updateCoeffs()
{
    const float g[3] = { value ("low"), value ("pres"), value ("high") };
    bool changed = false;
    for (int i = 0; i < 3; ++i) if (g[i] != last[i]) changed = true;
    if (! changed) return;
    using Co = juce::dsp::IIR::Coefficients<float>;
    auto lc = Co::makeLowShelf  (fs, 100.0f,  0.6f, juce::Decibels::decibelsToGain (g[0]));
    auto pc = Co::makePeakFilter (fs, 4000.0f, 0.7f, juce::Decibels::decibelsToGain (g[1]));
    auto hc = Co::makeHighShelf (fs, 12000.0f, 0.6f, juce::Decibels::decibelsToGain (g[2]));
    for (int c = 0; c < 2; ++c) { low[c].coefficients = lc; pres[c].coefficients = pc; high[c].coefficients = hc; }
    for (int i = 0; i < 3; ++i) last[i] = g[i];
}
void VintageEqModule::process (juce::AudioBuffer<float>& b)
{
    updateCoeffs();
    const int n = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            d[i] = high[c].processSample (pres[c].processSample (low[c].processSample (d[i])));
    }
}

// ============================================================================
//  Optical Compressor — smooth, program-dependent (slow) gain reduction.
// ============================================================================
OpticalCompModule::OpticalCompModule()
{
    addParam ("threshold", "Threshold", -50.0f, 0.0f, -20.0f, "dB", false, 1.0f, "Where gentle compression begins.");
    addParam ("ratio",     "Ratio",       1.0f, 8.0f, 2.5f, ":1", false, 0.5f, "Amount of smoothing.");
    addParam ("makeup",    "Makeup",       0.0f, 18.0f, 3.0f, "dB", false, 1.0f, "Level added back.");
    addParam ("mix",       "Mix",          0.0f, 100.0f, 100.0f, "%", false, 1.0f, "Dry/wet (parallel).");
}
const Descriptor& OpticalCompModule::desc()
{
    static const Descriptor d { "optical_comp", "Optical Compressor", Category::Dynamics,
        { "compress", "optical", "smooth", "vintage", "gentle", "la2a", "glue" },
        "Smooth, program-dependent optical compression.", true };
    return d;
}
void OpticalCompModule::process (juce::AudioBuffer<float>& b)
{
    const float thr    = value ("threshold");
    const float ratio  = juce::jmax (1.0f, value ("ratio"));
    const float makeup = juce::Decibels::decibelsToGain (value ("makeup"));
    const float mix    = value ("mix") * 0.01f;
    const float slope  = 1.0f / ratio - 1.0f;
    env.setTimes (12.0f, 220.0f, fs);      // slow, smooth optical character

    const int n = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    float* L = b.getWritePointer (0);
    float* R = nc > 1 ? b.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R ? R[i] : l;
        const float e = env.process (mx (l, r));
        const float over = juce::Decibels::gainToDecibels (e, -100.0f) - thr;
        const float g = juce::Decibels::decibelsToGain (over > 0.0f ? over * slope : 0.0f) * makeup;
        L[i] = l * (1.0f - mix) + l * g * mix;
        if (R) R[i] = r * (1.0f - mix) + r * g * mix;
    }
}
} // namespace vf::mods
