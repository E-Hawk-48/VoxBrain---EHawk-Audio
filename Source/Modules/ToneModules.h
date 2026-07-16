#pragma once
#include "Module.h"
#include "DynamicsModules.h"   // EnvFollower
#include <juce_dsp/juce_dsp.h>

// ============================================================================
//  Tone rack modules — musical vocal EQ, dynamic EQ, and a high-end exciter.
// ============================================================================
namespace vf::mods
{
    // ---- Parametric Vocal EQ (low shelf / mud bell / presence bell / air) --
    class ParametricEqModule : public Module
    {
    public:
        ParametricEqModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        void updateCoeffs();
        juce::dsp::IIR::Filter<float> band[4][2];   // [band][channel]
        float last[4] { 999, 999, 999, 999 };
    };

    // ---- Dynamic EQ (dynamically ducks a resonant band) -------------------
    class DynamicEqModule : public Module
    {
    public:
        DynamicEqModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::dsp::IIR::Filter<float> bp[2];
        EnvFollower env;
        float lastFreq = -1.0f, lastQ = -1.0f;
    };

    // ---- Exciter (high-frequency harmonic sparkle) ------------------------
    class ExciterModule : public Module
    {
    public:
        ExciterModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::dsp::IIR::Filter<float> hp[2];
        float lastFreq = -1.0f;
    };
} // namespace vf::mods
