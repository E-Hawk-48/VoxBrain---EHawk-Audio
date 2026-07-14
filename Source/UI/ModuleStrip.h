#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"

namespace vf
{
// ============================================================================
//  ModuleCard — one processing module: name, bypass pill, up to 3 knobs.
// ============================================================================
class ModuleCard : public juce::Component,
                   private juce::AudioProcessorValueTreeState::Listener,
                   private juce::AsyncUpdater
{
public:
    struct KnobSpec  { juce::String paramId, label; };
    struct ComboSpec { juce::String paramId, label; };

    ModuleCard (juce::AudioProcessorValueTreeState& apvts,
                juce::String title, juce::String bypassParamId, juce::String lockParamId,
                std::vector<KnobSpec> knobs,
                std::vector<ComboSpec> combos = {});
    ~ModuleCard() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void parameterChanged (const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void applyLockState();
    bool isLocked() const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String titleText;
    juce::String lockId;
    std::atomic<float>* lockValue = nullptr;

    juce::ToggleButton bypassButton;
    juce::ToggleButton lockButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::vector<std::unique_ptr<Knob>> knobList;

    struct Combo
    {
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };
    std::vector<std::unique_ptr<Combo>> comboList;
};

// ============================================================================
//  ModuleStrip — all module cards, laid out across two weighted rows.
// ============================================================================
class ModuleStrip : public juce::Component
{
public:
    explicit ModuleStrip (juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    struct Entry { std::unique_ptr<ModuleCard> card; int row; float weight; };
    std::vector<Entry> cards;
};
} // namespace vf
