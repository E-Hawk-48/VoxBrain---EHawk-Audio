#pragma once
#include "Module.h"
#include "DynamicsModules.h"   // EnvFollower
#include <juce_dsp/juce_dsp.h>

// ============================================================================
//  More vocal modules — analog colour + transient/tone tools the AI can reach
//  for. All real-time safe, ≤6 params (macro-automatable).
// ============================================================================
namespace vf::mods
{
    // ---- Tape Saturation --------------------------------------------------
    class TapeSatModule : public Module
    {
    public:
        TapeSatModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void reset() override { lp[0] = lp[1] = 0.0f; }
        void process (juce::AudioBuffer<float>&) override;
    private:
        float lp[2] { 0.0f, 0.0f };
    };

    // ---- Console Saturation ----------------------------------------------
    class ConsoleSatModule : public Module
    {
    public:
        ConsoleSatModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void process (juce::AudioBuffer<float>&) override;
    };

    // ---- Transient Designer ----------------------------------------------
    class TransientModule : public Module
    {
    public:
        TransientModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void reset() override { fast.reset(); slow.reset(); }
        void process (juce::AudioBuffer<float>&) override;
    private:
        EnvFollower fast, slow;
    };

    // ---- Telephone / Band-limit ------------------------------------------
    class TelephoneModule : public Module
    {
    public:
        TelephoneModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::dsp::IIR::Filter<float> hp[2], lp[2];
        float lastLo = -1.0f, lastHi = -1.0f;
    };

    // ---- Vintage EQ (broad musical shelves + presence) -------------------
    class VintageEqModule : public Module
    {
    public:
        VintageEqModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        void updateCoeffs();
        juce::dsp::IIR::Filter<float> low[2], pres[2], high[2];
        float last[3] { 999, 999, 999 };
    };

    // ---- Optical Compressor (smooth, program-dependent) ------------------
    class OpticalCompModule : public Module
    {
    public:
        OpticalCompModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void reset() override { env.reset(); }
        void process (juce::AudioBuffer<float>&) override;
    private:
        EnvFollower env;
    };
} // namespace vf::mods
