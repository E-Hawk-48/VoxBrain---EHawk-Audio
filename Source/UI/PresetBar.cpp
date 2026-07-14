#include "PresetBar.h"

namespace vf
{
namespace
{
    void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, theme::panelLight);
        b.setColour (juce::TextButton::textColourOffId, theme::text);
    }
}

PresetBar::PresetBar (PresetManager& pm)
    : presets (pm)
{
    presetBox.setTextWhenNothingSelected ("Load Preset");
    presetBox.setColour (juce::ComboBox::backgroundColourId, theme::panelLight);
    presetBox.setColour (juce::ComboBox::textColourId, theme::text);
    presetBox.setColour (juce::ComboBox::outlineColourId, theme::outline);
    presetBox.onChange = [this] { presetSelected(); };
    addAndMakeVisible (presetBox);

    for (auto* b : { &saveButton, &aButton, &bButton, &copyButton, &undoButton, &redoButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }

    saveButton.onClick = [this] { savePreset(); };
    aButton.onClick    = [this] { if (presets.isSlotB()) presets.toggleAB(); };
    bButton.onClick    = [this] { if (! presets.isSlotB()) presets.toggleAB(); };
    copyButton.onClick = [this] { presets.copyToOther(); };
    undoButton.onClick = [this] { presets.undo(); };
    redoButton.onClick = [this] { presets.redo(); };

    aButton.setTooltip ("A/B compare — slot A");
    bButton.setTooltip ("A/B compare — slot B");
    copyButton.setTooltip ("Copy the current settings into the other A/B slot");

    refresh();
}

void PresetBar::presetSelected()
{
    if (refreshing)
        return;
    const int id = presetBox.getSelectedId();
    if (id <= 0)
        return;

    if (id < 100)
        presets.loadFactory (id - 1);
    else
    {
        const int ui = id - 100;
        if (ui >= 0 && ui < userNames.size())
            presets.loadUserPreset (userNames[ui]);
    }
    // Act like a menu: reset to the "Load Preset" label.
    presetBox.setSelectedId (0, juce::dontSendNotification);
}

void PresetBar::savePreset()
{
    auto* w = new juce::AlertWindow ("Save Preset",
                                     "Name this preset:",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "My Vocal");
    w->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w->enterModalState (true,
        juce::ModalCallbackFunction::create ([this, w] (int result)
        {
            if (result == 1)
            {
                const auto name = w->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                {
                    presets.saveUserPreset (name);
                    refresh();
                }
            }
        }),
        true);   // deleteWhenDismissed
}

void PresetBar::refresh()
{
    refreshing = true;

    presetBox.clear (juce::dontSendNotification);
    const auto facs = presets.factoryNames();
    factoryCount = facs.size();
    for (int i = 0; i < facs.size(); ++i)
        presetBox.addItem (facs[i], i + 1);

    userNames = presets.userPresetNames();
    if (userNames.size() > 0)
    {
        presetBox.addSeparator();
        for (int i = 0; i < userNames.size(); ++i)
            presetBox.addItem (userNames[i], 100 + i);
    }
    presetBox.setSelectedId (0, juce::dontSendNotification);

    const bool onB = presets.isSlotB();
    aButton.setColour (juce::TextButton::buttonColourId, onB ? theme::panelLight : theme::accent);
    bButton.setColour (juce::TextButton::buttonColourId, onB ? theme::accent : theme::panelLight);
    aButton.setColour (juce::TextButton::textColourOffId, onB ? theme::textDim : theme::bg);
    bButton.setColour (juce::TextButton::textColourOffId, onB ? theme::bg : theme::textDim);

    undoButton.setEnabled (presets.canUndo());
    redoButton.setEnabled (presets.canRedo());
    undoButton.setTooltip (presets.canUndo() ? "Undo " + presets.undoLabel() : "Nothing to undo");
    redoButton.setTooltip (presets.canRedo() ? "Redo " + presets.redoLabel() : "Nothing to redo");

    refreshing = false;
}

void PresetBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

void PresetBar::resized()
{
    auto r = getLocalBounds().reduced (8, 5);

    // Right cluster: [A][B]  [Copy]   [Undo][Redo]
    redoButton.setBounds (r.removeFromRight (56)); r.removeFromRight (4);
    undoButton.setBounds (r.removeFromRight (56)); r.removeFromRight (12);
    copyButton.setBounds (r.removeFromRight (56)); r.removeFromRight (10);
    bButton.setBounds    (r.removeFromRight (30)); r.removeFromRight (3);
    aButton.setBounds    (r.removeFromRight (30)); r.removeFromRight (14);

    // Left cluster: preset menu + Save
    saveButton.setBounds (r.removeFromLeft (60)); r.removeFromLeft (6);
    presetBox.setBounds  (r.removeFromLeft (juce::jmin (260, r.getWidth())));
}
} // namespace vf
