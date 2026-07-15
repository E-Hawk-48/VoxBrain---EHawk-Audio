#include "BuiltinModules.h"
#include <cmath>

namespace vf::mods
{
// ============================================================================
//  Gain / Trim / Phase
// ============================================================================
GainModule::GainModule()
{
    addParam ("gain", "Gain", -24.0f, 24.0f, 0.0f, "dB", false, 1.0f, "Output level.");
}
const Descriptor& GainModule::desc()
{
    static const Descriptor d { "gain", "Gain", Category::Utility,
        { "level", "volume", "trim", "utility", "loud", "quiet" },
        "Clean output gain in decibels.", true };
    return d;
}
void GainModule::process (juce::AudioBuffer<float>& b)
{
    b.applyGain (juce::Decibels::decibelsToGain (value ("gain"), -120.0f));
}

TrimModule::TrimModule()
{
    addParam ("trim", "Trim", -12.0f, 12.0f, 0.0f, "dB", false, 1.0f, "Input trim.");
}
const Descriptor& TrimModule::desc()
{
    static const Descriptor d { "trim", "Trim", Category::Input,
        { "level", "input", "gain", "utility" },
        "Fine input-level trim for gain staging.", true };
    return d;
}
void TrimModule::process (juce::AudioBuffer<float>& b)
{
    b.applyGain (juce::Decibels::decibelsToGain (value ("trim"), -120.0f));
}

PhaseFlipModule::PhaseFlipModule()
{
    addParam ("flip", "Flip", 0.0f, 1.0f, 0.0f, {}, false, 1.0f, "Invert polarity.");
}
const Descriptor& PhaseFlipModule::desc()
{
    static const Descriptor d { "phase_flip", "Phase Flip", Category::Input,
        { "phase", "polarity", "invert", "utility" },
        "Inverts signal polarity (180°).", true };
    return d;
}
void PhaseFlipModule::process (juce::AudioBuffer<float>& b)
{
    if (value ("flip") > 0.5f)
        b.applyGain (-1.0f);
}

// ============================================================================
//  Stereo Width (Mid/Side)
// ============================================================================
StereoWidthModule::StereoWidthModule()
{
    addParam ("width", "Width", 0.0f, 200.0f, 100.0f, "%", false, 1.0f,
              "0 = mono, 100 = unchanged, 200 = extra wide.");
}
const Descriptor& StereoWidthModule::desc()
{
    static const Descriptor d { "stereo_width", "Stereo Width", Category::Imaging,
        { "stereo", "width", "wide", "mid side", "ms", "image", "spread" },
        "Mid/Side width control.", true };
    return d;
}
void StereoWidthModule::process (juce::AudioBuffer<float>& b)
{
    if (b.getNumChannels() < 2) return;
    const float w = value ("width") * 0.01f;
    auto* L = b.getWritePointer (0);
    auto* R = b.getWritePointer (1);
    const int n = b.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const float mid  = 0.5f * (L[i] + R[i]);
        const float side = 0.5f * (L[i] - R[i]) * w;
        L[i] = mid + side;
        R[i] = mid - side;
    }
}

// ============================================================================
//  Mono Maker (mono below a frequency — keeps the low end centred)
// ============================================================================
MonoMakerModule::MonoMakerModule()
{
    addParam ("freq", "Frequency", 20.0f, 500.0f, 120.0f, "Hz", false, 0.4f,
              "Everything below this is summed to mono.");
}
const Descriptor& MonoMakerModule::desc()
{
    static const Descriptor d { "mono_maker", "Mono Maker", Category::Imaging,
        { "mono", "stereo", "low", "bass", "mid side", "image", "center" },
        "Collapses the low end to mono for a tight centre.", true };
    return d;
}
void MonoMakerModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    lowPass.prepare (s);  highPass.prepare (s);
    lowPass.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);
    highPass.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lowBuf.setSize  ((int) s.numChannels, (int) s.maximumBlockSize);
    highBuf.setSize ((int) s.numChannels, (int) s.maximumBlockSize);
    lastFreq = -1.0f;
}
void MonoMakerModule::reset() { lowPass.reset(); highPass.reset(); }
void MonoMakerModule::process (juce::AudioBuffer<float>& b)
{
    if (b.getNumChannels() < 2) return;
    const int n = b.getNumSamples();
    const float f = value ("freq");
    if (f != lastFreq) { lowPass.setCutoffFrequency (f); highPass.setCutoffFrequency (f); lastFreq = f; }

    for (int c = 0; c < 2; ++c)
    {
        lowBuf.copyFrom  (c, 0, b, c, 0, n);
        highBuf.copyFrom (c, 0, b, c, 0, n);
    }
    { juce::dsp::AudioBlock<float> lb (lowBuf);  auto s = lb.getSubBlock (0, (size_t) n);
      juce::dsp::ProcessContextReplacing<float> ctx (s); lowPass.process (ctx); }
    { juce::dsp::AudioBlock<float> hb (highBuf); auto s = hb.getSubBlock (0, (size_t) n);
      juce::dsp::ProcessContextReplacing<float> ctx (s); highPass.process (ctx); }

    auto* L = b.getWritePointer (0);
    auto* R = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float monoLow = 0.5f * (lowBuf.getSample (0, i) + lowBuf.getSample (1, i));
        L[i] = monoLow + highBuf.getSample (0, i);
        R[i] = monoLow + highBuf.getSample (1, i);
    }
}

// ============================================================================
//  Highpass / Lowpass (12 dB/oct)
// ============================================================================
HighpassModule::HighpassModule()
{
    addParam ("freq", "Frequency", 20.0f, 1000.0f, 80.0f, "Hz", false, 0.4f,
              "Rolls off everything below this.");
}
const Descriptor& HighpassModule::desc()
{
    static const Descriptor d { "highpass", "High Pass", Category::Filter,
        { "filter", "hpf", "high pass", "low cut", "rumble", "clean" },
        "12 dB/oct high-pass filter.", true };
    return d;
}
void HighpassModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    for (auto& f : filt) f.reset();
    lastFreq = -1.0f;
}
void HighpassModule::reset() { for (auto& f : filt) f.reset(); }
void HighpassModule::process (juce::AudioBuffer<float>& b)
{
    const float f = value ("freq");
    if (f != lastFreq)
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, f, 0.707f);
        filt[0].coefficients = c; filt[1].coefficients = c;
        lastFreq = f;
    }
    const int nc = juce::jmin (b.getNumChannels(), 2);
    const int n = b.getNumSamples();
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i) d[i] = filt[c].processSample (d[i]);
    }
}

LowpassModule::LowpassModule()
{
    addParam ("freq", "Frequency", 1000.0f, 20000.0f, 12000.0f, "Hz", false, 0.4f,
              "Rolls off everything above this.");
}
const Descriptor& LowpassModule::desc()
{
    static const Descriptor d { "lowpass", "Low Pass", Category::Filter,
        { "filter", "lpf", "low pass", "high cut", "dark", "smooth" },
        "12 dB/oct low-pass filter.", true };
    return d;
}
void LowpassModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    for (auto& f : filt) f.reset();
    lastFreq = -1.0f;
}
void LowpassModule::reset() { for (auto& f : filt) f.reset(); }
void LowpassModule::process (juce::AudioBuffer<float>& b)
{
    const float f = value ("freq");
    if (f != lastFreq)
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass (fs, f, 0.707f);
        filt[0].coefficients = c; filt[1].coefficients = c;
        lastFreq = f;
    }
    const int nc = juce::jmin (b.getNumChannels(), 2);
    const int n = b.getNumSamples();
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i) d[i] = filt[c].processSample (d[i]);
    }
}

// ============================================================================
//  Tube Saturation
// ============================================================================
TubeSaturationModule::TubeSaturationModule()
{
    addParam ("drive", "Drive", 0.0f, 100.0f, 25.0f, "%", false, 1.0f, "Amount of harmonic drive.");
    addParam ("mix",   "Mix",   0.0f, 100.0f, 100.0f, "%", false, 1.0f, "Dry/wet blend.");
}
const Descriptor& TubeSaturationModule::desc()
{
    static const Descriptor d { "tube_sat", "Tube Saturation", Category::Saturation,
        { "warm", "tube", "valve", "analog", "harmonics", "drive", "colour", "saturation" },
        "Warm asymmetric tube-style saturation.", true };
    return d;
}
void TubeSaturationModule::process (juce::AudioBuffer<float>& b)
{
    const float drive = 1.0f + value ("drive") * 0.09f;      // 1..10
    const float mix   = value ("mix") * 0.01f;
    const float norm  = 0.78f;
    const int nc = b.getNumChannels();
    const int n = b.getNumSamples();
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float x = d[i];
            const float xb = x * drive;
            const float wet = std::tanh (xb + 0.08f * xb * xb * (xb > 0.0f ? 1.0f : 0.6f)) * norm;
            d[i] = x * (1.0f - mix) + wet * mix;
        }
    }
}
} // namespace vf::mods
