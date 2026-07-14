#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"

namespace vf
{
// ============================================================================
//  AnalysisPanel — shows the generated preset name and the AI engineer's
//  full decision report after an auto-mix pass.
// ============================================================================
class AnalysisPanel : public juce::Component
{
public:
    AnalysisPanel();

    void setResult (const juce::String& presetName, const juce::String& report);
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Handler for chat messages; returns the engineer's reply. */
    std::function<juce::String (const juce::String&)> onChatMessage;

private:
    void sendChat();

    juce::Label presetLabel;
    juce::TextEditor reportView;
    juce::TextEditor chatInput;
};
} // namespace vf
