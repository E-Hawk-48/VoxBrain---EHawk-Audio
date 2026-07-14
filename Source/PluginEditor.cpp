#include "PluginEditor.h"
#include "Chat/ChatEngine.h"

namespace vf
{
VocalForgeEditor::VocalForgeEditor (VocalForgeProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      spectrum (p.getAnalysis()),
      presetBar (p.getPresets()),
      moduleStrip (p.apvts)
{
    setLookAndFeel (&lookAndFeel);

    learnButton.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.85f));
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);
    addChildComponent (updateBanner);   // shows itself only when an update is pending
    addAndMakeVisible (spectrum);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (analysisPanel);
    addAndMakeVisible (moduleStrip);

    processor.getUpdater().onStateChanged = [this] (UpdateChecker::Info i)
    {
        updateBanner.setInfo (i);
        resized();
    };
    updateBanner.setInfo (processor.getUpdater().getInfo());

    processor.onAutoMixApplied = [this]
    {
        const auto& r = processor.getLastResult();
        analysisPanel.setResult (r.presetName, r.summary);
        learnButton.setButtonText ("LEARN");
        presetBar.refresh();
    };

    // Refresh the toolbar whenever preset/undo/A-B state changes.
    processor.getPresets().onStateChanged = [this] { presetBar.refresh(); };

    analysisPanel.onChatMessage = [this] (const juce::String& msg)
    {
        return processor.applyChatMessage (msg);
    };

    setResizable (true, true);
    setResizeLimits (900, 700, 2400, 1600);
    setSize (1120, 800);
    startTimerHz (20);
}

VocalForgeEditor::~VocalForgeEditor()
{
    processor.onAutoMixApplied = nullptr;
    processor.getPresets().onStateChanged = nullptr;
    processor.getUpdater().onStateChanged = nullptr;
    setLookAndFeel (nullptr);
}

void VocalForgeEditor::learnClicked()
{
    if (processor.isLearning())
    {
        processor.setLearning (false);
        learnButton.setButtonText ("ANALYZING…");
    }
    else
    {
        processor.setLearning (true);
        learnButton.setButtonText ("LISTENING… (click to finish)");
        analysisPanel.setResult ("", "Learning… play the vocal section you want mixed, "
                                     "then click the button again.");
    }
}

void VocalForgeEditor::timerCallback()
{
    // Pulse the learn button while listening
    if (processor.isLearning())
    {
        pulsePhase += 0.15f;
        const float glow = 0.65f + 0.35f * std::sin (pulsePhase);
        learnButton.setColour (juce::TextButton::buttonColourId,
                               theme::accentWarm.withAlpha (glow));
    }
}

void VocalForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    auto header = getLocalBounds().removeFromTop (52).reduced (16, 0);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("VOCALFORGE", header, juce::Justification::centredLeft);
    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("AI VOCAL ENGINEER", header.translated (170, 5), juce::Justification::centredLeft);
}

void VocalForgeEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (52).reduced (16, 8);
    learnButton.setBounds (header.removeFromRight (240));

    if (updateBanner.isActive())
        updateBanner.setBounds (area.removeFromTop (34).reduced (10, 2));

    area.reduce (10, 0);
    moduleStrip.setBounds (area.removeFromBottom (270));
    analysisPanel.setBounds (area.removeFromBottom (170).reduced (0, 4));
    presetBar.setBounds (area.removeFromBottom (36));
    spectrum.setBounds (area.reduced (3, 4));
}
} // namespace vf
