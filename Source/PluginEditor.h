#pragma once
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/SpectrumDisplay.h"
#include "UI/VocalDnaPanel.h"
#include "UI/ModuleStrip.h"
#include "UI/ChainView.h"
#include "UI/AnalysisPanel.h"
#include "UI/PresetBar.h"
#include "UI/UpdateBanner.h"
#include "UI/PitchDisplay.h"
#include "UI/ThemePanel.h"
#include "UI/PresetBrowser.h"
#include "UI/ReferencePanel.h"
#include "UI/UiPrefs.h"

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
                         public  juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit VoxBrainEditor (VoxBrainProcessor&);
    ~VoxBrainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- drag-drop reference import ----------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void learnClicked();
    void toggleTheme();
    void togglePresets();
    void toggleSimpleMode();
    void toggleHelp();
    void applyUiPrefs();          // push simpleMode/tooltipsOn into the UI + button text

    void toggleReference();               // header REFERENCE button → open/close panel
    void openReferenceChooser();          // panel Browse → file picker
    void startReference (const juce::File& file);
    void pollReference();                 // called from the timer; drives the panel

    VoxBrainProcessor& processor;
    VoxBrainLookAndFeel lookAndFeel;

    juce::TextButton learnButton { "LEARN" };
    juce::TextButton presetsButton { "PRESETS" };
    // ONE settings button instead of three toggles. The header carried seven
    // buttons plus a wordmark, which left the plugin looking like a control
    // panel before you had done anything with it, and forced the subtitle to be
    // hidden below 1200 px just to avoid a collision. Theme / Simple / Help are
    // all "set it once" choices, so they belong in a menu, not on the face.
    juce::TextButton setupButton { "SETUP" };
    void showSetupMenu();
    juce::TextButton referenceButton { "REFERENCE" };   // AI Reference Mix Analyzer
    std::unique_ptr<juce::FileChooser> fileChooser;     // kept alive for the async picker
    juce::String refFileName;                           // name of the file being analysed
    ReferenceResult referenceResult;                    // last completed analysis (for apply)
    // Owned so it can be created/destroyed to enable/disable hover help.
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    UpdateBanner updateBanner;
    SpectrumDisplay spectrum;
    VocalDnaPanel dnaPanel;
    PitchDisplay pitchDisplay;
    PresetBar presetBar;
    AnalysisPanel analysisPanel;
    ChainView chainView;          // unified main page (chain strip + focused module)
    ThemePanel themePanel;
    PresetBrowser presetBrowser;
    ReferencePanel referencePanel;
    bool themeVisible = false;
    bool browserVisible = false;
    bool referenceVisible = false;

    float pulsePhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxBrainEditor)
};
} // namespace vf
