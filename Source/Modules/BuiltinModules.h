#pragma once
#include "Module.h"

// ============================================================================
//  Built-in starter modules. Each is a small, correct, self-contained DSP block
//  that proves the framework end-to-end; the library grows by adding more of
//  these (see the roadmap / catalog). Real-time safe, zero-latency unless noted.
// ============================================================================
namespace vf::mods
{
    // ---- Utility / Input --------------------------------------------------
    class GainModule : public Module
    {
    public:
        GainModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void process (juce::AudioBuffer<float>&) override;
    };

    class TrimModule : public Module
    {
    public:
        TrimModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void process (juce::AudioBuffer<float>&) override;
    };

    class PhaseFlipModule : public Module
    {
    public:
        PhaseFlipModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void process (juce::AudioBuffer<float>&) override;
    };

    // ---- Imaging ----------------------------------------------------------
    class StereoWidthModule : public Module
    {
    public:
        StereoWidthModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void process (juce::AudioBuffer<float>&) override;
    };

    class MonoMakerModule : public Module
    {
    public:
        MonoMakerModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::dsp::LinkwitzRileyFilter<float> lowPass, highPass;
        juce::AudioBuffer<float> lowBuf, highBuf;
        float lastFreq = -1.0f;
    };

    // ---- Filter -----------------------------------------------------------
    class HighpassModule : public Module
    {
    public:
        HighpassModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::dsp::IIR::Filter<float> filt[2];
        float lastFreq = -1.0f;
    };

    class LowpassModule : public Module
    {
    public:
        LowpassModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::dsp::IIR::Filter<float> filt[2];
        float lastFreq = -1.0f;
    };

    // ---- Saturation -------------------------------------------------------
    class TubeSaturationModule : public Module
    {
    public:
        TubeSaturationModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void process (juce::AudioBuffer<float>&) override;
    };
} // namespace vf::mods
