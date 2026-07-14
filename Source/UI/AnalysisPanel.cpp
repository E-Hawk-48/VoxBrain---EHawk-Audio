#include "AnalysisPanel.h"

namespace vf
{
AnalysisPanel::AnalysisPanel()
{
    presetLabel.setText ("Click LEARN, play the vocal, click again.", juce::dontSendNotification);
    presetLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    presetLabel.setColour (juce::Label::textColourId, theme::accentGreen);
    addAndMakeVisible (presetLabel);

    reportView.setMultiLine (true);
    reportView.setReadOnly (true);
    reportView.setScrollbarsShown (true);
    reportView.setCaretVisible (false);
    reportView.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, 0));
    reportView.setText ("The AI engineer's analysis and every decision it makes "
                        "will appear here with full reasoning.\n\n"
                        "You can also just tell me what you want in the box below - "
                        "\"darker\", \"more air\", \"less autotune\", \"hard tune\", "
                        "\"warmer\", \"radio ready\"...");
    addAndMakeVisible (reportView);

    chatInput.setMultiLine (false);
    chatInput.setReturnKeyStartsNewLine (false);
    chatInput.setTextToShowWhenEmpty ("Tell the engineer what you want, then press Enter...",
                                      theme::textDim);
    chatInput.onReturnKey = [this] { sendChat(); };
    addAndMakeVisible (chatInput);
}

void AnalysisPanel::sendChat()
{
    const auto text = chatInput.getText().trim();
    if (text.isEmpty() || onChatMessage == nullptr)
        return;

    const auto reply = onChatMessage (text);
    reportView.moveCaretToEnd();
    reportView.insertTextAtCaret ("\n> " + text + "\n" + reply + "\n");
    reportView.moveCaretToEnd();
    chatInput.clear();
}

void AnalysisPanel::setResult (const juce::String& presetName, const juce::String& report)
{
    presetLabel.setText (presetName.isNotEmpty() ? "Preset: " + presetName : presetName,
                         juce::dontSendNotification);
    reportView.setText (report);
}

void AnalysisPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);
}

void AnalysisPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    presetLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);
    chatInput.setBounds (area.removeFromBottom (26));
    area.removeFromBottom (6);
    reportView.setBounds (area);
}
} // namespace vf
