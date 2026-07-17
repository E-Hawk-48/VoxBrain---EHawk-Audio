#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"

namespace vf
{
// ============================================================================
//  ThemePanel — live UI customization: colour-scheme presets, full colour
//  pickers for Primary / Secondary / Background, and a neon-glow amount.
//  Every change recolours the whole plugin immediately and is persisted.
// ============================================================================
class ThemePanel : public juce::Component,
                   private juce::ChangeListener
{
public:
    ThemePanel();

    std::function<void()> onChanged;   // editor: refresh LnF + repaint + save
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void openPicker (juce::Colour current, std::function<void (juce::Colour)> apply,
                     juce::Component* anchor);
    void refreshChips();
    void fireChanged();

    juce::Label titleLabel, schemeLabel, colourLabel, glowLabel;
    juce::TextButton closeBtn { "Done" };
    std::vector<std::unique_ptr<juce::TextButton>> schemeButtons;
    juce::TextButton primaryChip { "Primary" }, secondaryChip { "Secondary" },
                     bgChip { "Background" }, resetBtn { "Reset Default" };
    juce::Slider glowSlider;

    std::function<void (juce::Colour)> activeApply;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemePanel)
};
} // namespace vf
