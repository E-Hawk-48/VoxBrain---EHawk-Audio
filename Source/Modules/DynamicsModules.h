#pragma once
#include "Module.h"
#include <juce_dsp/juce_dsp.h>

// ============================================================================
//  Dynamics rack modules — compressor family + sibilance/plosive control +
//  clipping/limiting. Every module is real-time safe and exposes ≤6 params so
//  each control maps onto a host-automatable rack macro.
// ============================================================================
namespace vf::mods
{
    // Simple branching envelope follower (attack/release on a rectified detector).
    struct EnvFollower
    {
        float env = 0.0f, aAtt = 0.0f, aRel = 0.0f;
        void setTimes (float attMs, float relMs, double fs)
        {
            aAtt = std::exp (-1.0f / (juce::jmax (0.02f, attMs) * 0.001f * (float) fs));
            aRel = std::exp (-1.0f / (juce::jmax (1.0f,  relMs) * 0.001f * (float) fs));
        }
        float process (float rect)
        {
            const float a = rect > env ? aAtt : aRel;
            env = a * env + (1.0f - a) * rect;
            return env;
        }
        void reset() { env = 0.0f; }
    };

    // ---- Compressor -------------------------------------------------------
    class CompressorModule : public Module
    {
    public:
        CompressorModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void reset() override { env.reset(); }
        void process (juce::AudioBuffer<float>&) override;
    private:
        EnvFollower env;
    };

    // ---- Gate / Downward Expander ----------------------------------------
    class GateModule : public Module
    {
    public:
        GateModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void reset() override { env.reset(); }
        void process (juce::AudioBuffer<float>&) override;
    private:
        EnvFollower env;
    };

    // ---- De-Esser (dynamic sibilance control) -----------------------------
    class DeEsserModule : public Module
    {
    public:
        DeEsserModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::dsp::IIR::Filter<float> hp[2];   // sidechain/band split
        EnvFollower env;
        float lastFreq = -1.0f;
    };

    // ---- De-Plosive (low-frequency pop control) ---------------------------
    class DePlosiveModule : public Module
    {
    public:
        DePlosiveModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::dsp::IIR::Filter<float> lp[2];
        EnvFollower env;
        float lastFreq = -1.0f;
    };

    // ---- Soft Clipper -----------------------------------------------------
    class SoftClipperModule : public Module
    {
    public:
        SoftClipperModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void process (juce::AudioBuffer<float>&) override;
    };

    // ---- Noise Reduction (broadband downward expander / hiss gate) --------
    class NoiseReductionModule : public Module
    {
    public:
        NoiseReductionModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void reset() override { env.reset(); }
        void process (juce::AudioBuffer<float>&) override;
    private:
        EnvFollower env;
    };

    // ---- Parallel Compressor (New York) -----------------------------------
    class ParallelCompModule : public Module
    {
    public:
        ParallelCompModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void reset() override { env.reset(); }
        void process (juce::AudioBuffer<float>&) override;
    private:
        EnvFollower env;
    };

    // ---- Limiter (fast brickwall) -----------------------------------------
    class LimiterModule : public Module
    {
    public:
        LimiterModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void reset() override { env.reset(); }
        void process (juce::AudioBuffer<float>&) override;
    private:
        EnvFollower env;
    };
} // namespace vf::mods
