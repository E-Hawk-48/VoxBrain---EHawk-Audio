#pragma once
#include "Module.h"
#include <juce_dsp/juce_dsp.h>

// ============================================================================
//  Modulation rack modules — stereo chorus (LFO-modulated delay lines).
// ============================================================================
namespace vf::mods
{
    class StereoChorusModule : public Module
    {
    public:
        StereoChorusModule();
        static const Descriptor& desc();
        const Descriptor& descriptor() const override { return desc(); }
        void prepare (const juce::dsp::ProcessSpec&) override;
        void reset() override;
        void process (juce::AudioBuffer<float>&) override;
    private:
        juce::AudioBuffer<float> delayBuf;   // 2 channels
        int   dlen = 0, writePos = 0;
        float phaseL = 0.0f, phaseR = 0.25f;
    };
} // namespace vf::mods
