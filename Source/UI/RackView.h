#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"
#include "../Modules/ModuleRack.h"
#include "../Modules/ModuleRegistry.h"
#include "../Modules/ModuleAdvisor.h"

namespace vf
{
// ============================================================================
//  CatalogModel — backs the palette ListBox with ranked Smart-Search results.
// ============================================================================
class CatalogModel : public juce::ListBoxModel
{
public:
    std::vector<mods::Descriptor> items;
    std::function<void (const juce::String&)> onAdd;   // fired for implemented rows

    int getNumRows() override { return (int) items.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
};

// ============================================================================
//  RackModuleCard — one live module in the rack (message thread view).
//  Reorder / bypass / solo / lock / duplicate / remove + up to 6 macro knobs
//  bound to the host-automatable rack_s{slot}_m{k} parameters.
// ============================================================================
class RackModuleCard : public juce::Component
{
public:
    RackModuleCard (juce::AudioProcessorValueTreeState& apvts,
                    mods::ModuleRack& rack,
                    const mods::ModuleRack::NodeInfo& info,
                    int slotIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void setCpu (float pct);

    static constexpr int cardHeight = 132;

    juce::String instanceId;
    std::function<void()> onRemove, onDuplicate, onMoveUp, onMoveDown, onChanged;

private:
    juce::AudioProcessorValueTreeState& apvts;
    mods::ModuleRack& rack;
    int slot = 0;
    juce::String title, subtitle;
    int   latency = 0;
    float cpu = 0.0f;
    bool  automatable = true;

    juce::TextButton upBtn { "▲" }, downBtn { "▼" }, dupBtn { "Dup" }, removeBtn { "✕" };
    juce::ToggleButton bypassBtn, soloBtn, lockBtn;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    std::vector<std::unique_ptr<Knob>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackModuleCard)
};

// ============================================================================
//  RackView — the modular workstation panel (palette + rack + AI advisor).
// ============================================================================
class RackView : public juce::Component,
                 private juce::Timer
{
public:
    RackView (juce::AudioProcessorValueTreeState& apvts,
              mods::ModuleRack& rack,
              std::function<std::vector<mods::ModuleSuggestion>()> suggestFn);
    ~RackView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Re-read the AI module recommendations (call after a LEARN pass). */
    void refreshAdvisor();

    std::function<void()> onClose;

private:
    void timerCallback() override;
    void doSearch();
    void rebuildCards();
    void layoutCards();
    void addModule (const juce::String& typeId);
    void moveModule (const juce::String& instanceId, int dir);
    void removeModule (const juce::String& instanceId);
    void duplicateModule (const juce::String& instanceId);
    void syncMacros();
    void updateStats();

    juce::AudioProcessorValueTreeState& apvts;
    mods::ModuleRack& rack;
    std::function<std::vector<mods::ModuleSuggestion>()> suggestFn;

    juce::Label      titleLabel, paletteLabel, advisorLabel, statsLabel, emptyHint;
    juce::TextButton closeBtn { "Close ✕" };

    // Palette (left column)
    juce::TextEditor searchBox;
    juce::ListBox    catalogList { "catalog", nullptr };
    CatalogModel     catalogModel;

    // AI advisor chips (top of right column)
    juce::Component  advisorStrip;
    std::vector<std::unique_ptr<juce::TextButton>> advisorChips;

    // Rack (right column, scrollable)
    juce::Viewport   rackViewport;
    juce::Component  rackContent;
    std::vector<std::unique_ptr<RackModuleCard>> cards;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackView)
};
} // namespace vf
