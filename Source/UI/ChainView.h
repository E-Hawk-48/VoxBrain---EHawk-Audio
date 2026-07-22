#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"
#include "ModuleSpecs.h"
#include "../DSP/ChainOrder.h"
#include "../Modules/ModuleRack.h"
#include "../Modules/ModuleAdvisor.h"
#include <functional>
#include <memory>
#include <vector>

namespace vf
{
// ============================================================================
//  ChainView — the UNIFIED main page. One chain, one place.
//  ---------------------------------------------------------------------------
//  Left   : MODULE CHAIN strip — the whole signal path in processing order
//           (fixed VocalChain stages AND rack modules together). Click to
//           select, drag to reorder, power dot to bypass.
//  Middle : the SELECTED module's controls, full size — nothing else competing.
//  Bottom : add-module bar (search the library, click to insert) + Auto-Build.
//
//  It owns no DSP: reordering calls back into the processor's chain-order API,
//  everything else is bound to the APVTS / rack exactly as before.
// ============================================================================
class ChainView : public juce::Component,
                  private juce::Timer
{
public:
    // Callbacks into the processor (supplied by the editor).
    struct Hooks
    {
        std::function<std::vector<ChainItem>()>  getOrder;
        std::function<void (int, int)>           moveItem;      // from, to
        std::function<void()>                    autoBuild;     // AI rack build
        std::function<std::vector<mods::ModuleSuggestion>()> suggest;   // AI advisor
    };

    ChainView (juce::AudioProcessorValueTreeState& apvts,
               mods::ModuleRack& rack,
               Hooks hooks);
    ~ChainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Hide advanced controls in the focused panel (Simple mode). */
    void setSimpleMode (bool simple);
    /** Rebuild from the current order (after external changes). */
    void refresh();

private:
    // ---- one row of the chain strip ------------------------------------
    struct Row
    {
        ChainItem item;
        juce::String name, subtitle;
        int  rackIndex = -1;          // >=0 when this row is a rack module
        juce::Rectangle<int> bounds;  // within the strip's list area
    };

    void timerCallback() override;
    void rebuildRows();
    void buildFocusPanel();
    void selectIndex (int index);
    int  rowAtPosition (juce::Point<int> p) const;
    void togglePowerAt (int index);
    bool isRowActive (const Row& r) const;
    void addModuleByType (const juce::String& typeId);
    void refreshLibrary();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    juce::AudioProcessorValueTreeState& apvts;
    mods::ModuleRack& rack;
    Hooks hooks;

    std::vector<Row> rows;
    int selected = 0;
    int dragFrom = -1, dragTo = -1;
    bool dragging = false;
    juce::Point<int> dragStart;
    size_t lastOrderHash = 0;

    // focused module panel (exactly one of these is live)
    std::unique_ptr<ModuleCard> stagePanel;      // fixed stage controls
    std::unique_ptr<juce::Component> rackPanel;  // rack module macro controls

    // add-module bar
    juce::TextEditor  searchBox;
    juce::TextButton  autoBuildButton { "\xe2\x9c\xa8 Auto-Build" };
    std::vector<std::unique_ptr<juce::TextButton>> libraryButtons;

    juce::Rectangle<int> stripArea, listArea, focusArea, addArea;
    bool simpleMode = false;
    bool showingSuggestions = false;   // add bar is showing AI picks vs search hits

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainView)
};
} // namespace vf
