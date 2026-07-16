#include "SpaceModules.h"
#include <cmath>

namespace vf::mods
{
namespace
{
    inline float readInterp (const float* b, int len, float pos)
    {
        while (pos < 0.0f) pos += (float) len;
        const int i0 = (int) pos; const float f = pos - (float) i0;
        return b[i0] + (b[(i0 + 1) % len] - b[i0]) * f;
    }
}

// ============================================================================
//  Reverb
// ============================================================================
ReverbModule::ReverbModule()
{
    addParam ("size",  "Size",  0.0f, 100.0f, 60.0f, "%", false, 1.0f, "Room/hall size.");
    addParam ("damp",  "Damp",  0.0f, 100.0f, 45.0f, "%", false, 1.0f, "High-frequency damping of the tail.");
    addParam ("width", "Width", 0.0f, 100.0f, 100.0f, "%", false, 1.0f, "Stereo width of the reverb.");
    addParam ("mix",   "Mix",   0.0f, 100.0f, 25.0f, "%", false, 1.0f, "Dry/wet blend.");
}
const Descriptor& ReverbModule::desc()
{
    static const Descriptor d { "reverb_hall", "Hall Reverb", Category::Spatial,
        { "reverb", "hall", "space", "lush", "ambience", "wet", "room" },
        "Lush algorithmic reverb.", true };
    return d;
}
void ReverbModule::prepare (const juce::dsp::ProcessSpec& s) { Module::prepare (s); rv.prepare (s); }
void ReverbModule::reset() { rv.reset(); }
void ReverbModule::process (juce::AudioBuffer<float>& b)
{
    const float mix = value ("mix") * 0.01f;
    juce::dsp::Reverb::Parameters p;
    p.roomSize   = value ("size") * 0.01f;
    p.damping    = value ("damp") * 0.01f;
    p.width      = value ("width") * 0.01f;
    p.wetLevel   = mix;
    p.dryLevel   = 1.0f - mix;
    p.freezeMode = 0.0f;
    rv.setParameters (p);

    juce::dsp::AudioBlock<float> block (b);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    rv.process (ctx);
}

// ============================================================================
//  Digital Delay
// ============================================================================
DelayModule::DelayModule()
{
    addParam ("time",     "Time",     1.0f, 1000.0f, 300.0f, "ms", false, 0.5f, "Delay time.");
    addParam ("feedback", "Feedback", 0.0f, 95.0f, 30.0f, "%", false, 1.0f, "Repeats / regeneration.");
    addParam ("tone",     "Tone",     500.0f, 18000.0f, 8000.0f, "Hz", false, 0.4f, "Low-pass on the repeats (darker echoes).");
    addParam ("mix",      "Mix",      0.0f, 100.0f, 25.0f, "%", false, 1.0f, "Dry/wet blend.");
}
const Descriptor& DelayModule::desc()
{
    static const Descriptor d { "digital_delay", "Digital Delay", Category::Delay,
        { "delay", "echo", "clean", "repeat", "throw", "slap" },
        "Clean digital delay with tone + feedback.", true };
    return d;
}
void DelayModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    len = (int) (1.1 * fs) + 4;
    buf.setSize (2, len); buf.clear(); writePos = 0; lp[0] = lp[1] = 0.0f;
}
void DelayModule::reset() { buf.clear(); writePos = 0; lp[0] = lp[1] = 0.0f; }
void DelayModule::process (juce::AudioBuffer<float>& b)
{
    if (len <= 0) return;
    const float dSamp = juce::jlimit (1.0f, (float) len - 2.0f, value ("time") * 0.001f * (float) fs);
    const float fb    = value ("feedback") * 0.01f;
    const float mix   = value ("mix") * 0.01f;
    const float tone  = value ("tone");
    const float a     = std::exp (-2.0f * juce::MathConstants<float>::pi * tone / (float) fs);

    const int n = b.getNumSamples();
    const int nc = juce::jmin (b.getNumChannels(), 2);
    for (int c = 0; c < nc; ++c)
    {
        auto* d = b.getWritePointer (c);
        auto* db = buf.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const int wp = (writePos + i) % len;
            const float wet = readInterp (db, len, (float) wp - dSamp);
            lp[c] = (1.0f - a) * wet + a * lp[c];              // tone LP in the loop
            db[wp] = d[i] + lp[c] * fb;
            d[i] = d[i] * (1.0f - mix) + wet * mix;
        }
    }
    writePos = (writePos + n) % len;
}

// ============================================================================
//  Ping-Pong Delay
// ============================================================================
PingPongModule::PingPongModule()
{
    addParam ("time",     "Time",     1.0f, 1000.0f, 350.0f, "ms", false, 0.5f, "Delay time per side.");
    addParam ("feedback", "Feedback", 0.0f, 95.0f, 40.0f, "%", false, 1.0f, "How long the bounce sustains.");
    addParam ("mix",      "Mix",      0.0f, 100.0f, 28.0f, "%", false, 1.0f, "Dry/wet blend.");
}
const Descriptor& PingPongModule::desc()
{
    static const Descriptor d { "pingpong_delay", "Ping Pong Delay", Category::Delay,
        { "delay", "ping pong", "stereo", "bounce", "echo" },
        "Bouncing stereo delay.", true };
    return d;
}
void PingPongModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    len = (int) (1.1 * fs) + 4;
    buf.setSize (2, len); buf.clear(); writePos = 0;
}
void PingPongModule::reset() { buf.clear(); writePos = 0; }
void PingPongModule::process (juce::AudioBuffer<float>& b)
{
    if (len <= 0 || b.getNumChannels() < 2) return;
    const float dSamp = juce::jlimit (1.0f, (float) len - 2.0f, value ("time") * 0.001f * (float) fs);
    const float fb    = value ("feedback") * 0.01f;
    const float mix   = value ("mix") * 0.01f;

    const int n = b.getNumSamples();
    auto* L = b.getWritePointer (0);
    auto* R = b.getWritePointer (1);
    auto* dL = buf.getWritePointer (0);
    auto* dR = buf.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const int wp = (writePos + i) % len;
        const float wetL = readInterp (dL, len, (float) wp - dSamp);
        const float wetR = readInterp (dR, len, (float) wp - dSamp);
        // cross-feed: left taps into right and vice versa → the bounce
        dL[wp] = R[i] + wetR * fb;
        dR[wp] = L[i] + wetL * fb;
        L[i] = L[i] * (1.0f - mix) + wetL * mix;
        R[i] = R[i] * (1.0f - mix) + wetR * mix;
    }
    writePos = (writePos + n) % len;
}

// ============================================================================
//  Doubler
// ============================================================================
DoublerModule::DoublerModule()
{
    addParam ("delay", "Delay", 8.0f, 40.0f, 22.0f, "ms", false, 1.0f, "Base offset of the doubled voices.");
    addParam ("detune","Detune",0.0f, 100.0f, 40.0f, "%", false, 1.0f, "Pitch wobble between the voices.");
    addParam ("width", "Width", 0.0f, 100.0f, 85.0f, "%", false, 1.0f, "Stereo spread of the doubles.");
    addParam ("mix",   "Mix",   0.0f, 100.0f, 45.0f, "%", false, 1.0f, "How much doubling to blend in.");
}
const Descriptor& DoublerModule::desc()
{
    static const Descriptor d { "double_tracker", "Doubler", Category::Modulation,
        { "double", "thicken", "wide", "stack", "adt", "vocal" },
        "Realistic vocal doubling (detuned short delays).", true };
    return d;
}
void DoublerModule::prepare (const juce::dsp::ProcessSpec& s)
{
    Module::prepare (s);
    len = (int) (0.08 * fs) + 4;
    buf.setSize (2, len); buf.clear(); writePos = 0; phase = 0.0f;
}
void DoublerModule::reset() { buf.clear(); writePos = 0; phase = 0.0f; }
void DoublerModule::process (juce::AudioBuffer<float>& b)
{
    if (len <= 0) return;
    const float baseMs = value ("delay");
    const float detune = value ("detune") * 0.01f;
    const float width  = value ("width") * 0.015f;   // 0..1.5 side scale
    const float mix    = value ("mix") * 0.01f;

    const float base   = baseMs * 0.001f * (float) fs;
    const float mod    = detune * 0.004f * (float) fs;    // up to 4 ms wobble
    const float inc    = 0.9f / (float) fs;               // ~0.9 Hz drift

    const int n = b.getNumSamples();
    const bool stereo = b.getNumChannels() > 1;
    float* L = b.getWritePointer (0);
    float* R = stereo ? b.getWritePointer (1) : nullptr;
    float* dL = buf.getWritePointer (0);
    float* dR = buf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        const int wp = (writePos + i) % len;
        const float inL = L[i];
        const float inR = R ? R[i] : inL;
        dL[wp] = inL; dR[wp] = inR;

        const float lfoA = std::sin (juce::MathConstants<float>::twoPi * phase);
        const float lfoB = std::sin (juce::MathConstants<float>::twoPi * (phase + 0.5f));
        float wetL = readInterp (dL, len, (float) wp - (base + mod * (0.5f + 0.5f * lfoA)));
        float wetR = readInterp (dR, len, (float) wp - (base * 1.3f + mod * (0.5f + 0.5f * lfoB)));

        const float mid  = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * width;
        wetL = mid + side; wetR = mid - side;

        L[i] = inL * (1.0f - mix * 0.5f) + wetL * mix;
        if (R) R[i] = inR * (1.0f - mix * 0.5f) + wetR * mix;

        phase += inc; if (phase >= 1.0f) phase -= 1.0f;
    }
    writePos = (writePos + n) % len;
}
} // namespace vf::mods
