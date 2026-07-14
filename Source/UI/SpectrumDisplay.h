#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Analysis/AnalysisEngine.h"
#include "LookAndFeel.h"

namespace vf
{
// ============================================================================
//  SpectrumDisplay — real-time log-frequency spectrum with peak/LUFS/pitch
//  readouts. Pulls data from the AnalysisEngine's lock-free feed at 30 fps.
// ============================================================================
class SpectrumDisplay : public juce::Component,
                        private juce::Timer
{
public:
    explicit SpectrumDisplay (AnalysisEngine& engine);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    float binToX (int bin, float width) const;

    AnalysisEngine& analysis;
    std::array<float, AnalysisEngine::fftSize / 2> spectrum {};
    std::array<float, AnalysisEngine::fftSize / 2> peakHold {};
    double sampleRate = 44100.0;
};
} // namespace vf
