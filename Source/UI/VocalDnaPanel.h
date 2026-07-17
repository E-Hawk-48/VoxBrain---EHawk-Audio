#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "LookAndFeel.h"
#include "../Analysis/AnalysisEngine.h"

namespace vf
{
// ============================================================================
//  VocalDnaPanel — a live, glowing radar (spider chart) of the Vocal DNA.
//  Always-on: it reads AnalysisEngine::getLiveDNA() on a timer and animates
//  toward it, so the shape breathes with the voice even without a LEARN pass.
//  Quality axes (tuning, warmth, air …) read outward = better; "watch" axes
//  (sibilance, harshness, mud) read outward = more of that issue and are
//  labelled in the warm/red accent so problems pop out at a glance.
// ============================================================================
class VocalDnaPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit VocalDnaPanel (AnalysisEngine& engine);
    ~VocalDnaPanel() override;

    void paint (juce::Graphics&) override;

    static constexpr int kAxes = 10;

private:
    void timerCallback() override;
    static std::array<float, kAxes> extract (const VocalDNA& d);

    AnalysisEngine& analysis;
    std::array<float, kAxes> shown {};   // smoothed, animated values

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalDnaPanel)
};
} // namespace vf
