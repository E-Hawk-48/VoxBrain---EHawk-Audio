#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Preset/PresetManager.h"
#include "LookAndFeel.h"

namespace vf
{
// ============================================================================
//  PresetBar — compact toolbar: preset menu + Save, A/B compare + Copy,
//  Undo / Redo. All actions delegate to PresetManager.
// ============================================================================
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (PresetManager& pm);

    /** Re-reads state from the manager (button enables, A/B, preset list). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void presetSelected();
    void savePreset();

    PresetManager& presets;

    juce::ComboBox   presetBox;
    juce::TextButton saveButton { "Save" };
    juce::TextButton aButton    { "A" };
    juce::TextButton bButton    { "B" };
    juce::TextButton copyButton { "Copy" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };

    juce::StringArray userNames;   // parallel to combo user items
    int factoryCount = 0;
    bool refreshing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
} // namespace vf
