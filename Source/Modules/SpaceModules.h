#pragma once
#include "Module.h"
#include <juce_dsp/juce_dsp.h>

// ============================================================================
//  Space / creative rack modules — reverb, delay, ping-pong, doubler.
//  Real-time safe; each exposes ≤6 macro-automatable params.
// ============================================================================
namespace vf::mods
{
    // ---- Algorithmic Reverb (JUCE Freeverb core) -------------------------
    class ReverbModule : public Module
    {
    public:
        ReverbModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::dsp::Reverb rv;
    };

    // ---- Digital Delay (feedback + tone) ---------------------------------
    class DelayModule : public Module
    {
    public:
        DelayModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::AudioBuffer<float> buf;
        int   len = 0, writePos = 0;
        float lp[2] { 0.0f, 0.0f };
    };

    // ---- Ping-Pong Delay (stereo bounce) ---------------------------------
    class PingPongModule : public Module
    {
    public:
        PingPongModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::AudioBuffer<float> buf;
        int len = 0, writePos = 0;
    };

    // ---- Doubler (detuned short delays L/R for width + thickness) ---------
    class DoublerModule : public Module
    {
    public:
        DoublerModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::AudioBuffer<float> buf;   // 2 channels
        int   len = 0, writePos = 0;
        float phase = 0.0f;
    };
} // namespace vf::mods
