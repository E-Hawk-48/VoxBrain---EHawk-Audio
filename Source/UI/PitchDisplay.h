#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"
#include "../DSP/RetuneEngine.h"

namespace vf
{
// ============================================================================
//  PitchDisplay — live auto-tune visualizer. Scrolls the detected input pitch
//  against the corrected target pitch on a semitone grid, with note labels and
//  a big current-note read-out. Reads the RetuneEngine's lock-free UI atoms.
// ============================================================================
class PitchDisplay : public juce::Component,
                     private juce::Timer
{
public:
    explicit PitchDisplay (RetuneEngine& engine);
    ~PitchDisplay() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    float hzToY (float hz, juce::Rectangle<float> area) const;
    static juce::String noteName (float hz, int& centsOut);

    RetuneEngine& retune;

    static constexpr int histLen = 260;
    float inHist[histLen]  {};
    float tgtHist[histLen] {};
    int   head = 0;

    // Visible pitch window (MIDI note numbers): G2 … A5, ~3 octaves.
    static constexpr float midiLo = 43.0f;
    static constexpr float midiHi = 81.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchDisplay)
};
} // namespace vf
