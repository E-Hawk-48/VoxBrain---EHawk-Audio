#pragma once
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/SpectrumDisplay.h"
#include "UI/ModuleStrip.h"
#include "UI/AnalysisPanel.h"
#include "UI/PresetBar.h"
#include "UI/UpdateBanner.h"
#include "UI/RackView.h"
#include "UI/PitchDisplay.h"

namespace vf
{
// ============================================================================
//  VoxBrainEditor — main plugin window.
//  Layout:  header (logo + LEARN button)
//           spectrum display
//           analysis panel (AI report)
//           module strip (8 modules)
// ============================================================================
class VoxBrainEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit VoxBrainEditor (VoxBrainProcessor&);
    ~VoxBrainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void learnClicked();
    void toggleRack();

    VoxBrainProcessor& processor;
    VoxBrainLookAndFeel lookAndFeel;

    juce::TextButton learnButton { "LEARN" };
    juce::TextButton modulesButton { "MODULES" };
    UpdateBanner updateBanner;
    SpectrumDisplay spectrum;
    PitchDisplay pitchDisplay;
    PresetBar presetBar;
    AnalysisPanel analysisPanel;
    ModuleStrip moduleStrip;
    RackView rackView;
    bool rackVisible = false;

    float pulsePhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxBrainEditor)
};
} // namespace vf
