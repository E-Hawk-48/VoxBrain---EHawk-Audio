#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"
#include "../Preset/PresetLibrary.h"

namespace vf
{
// ============================================================================
//  PresetBrowser — the full preset ecosystem UI: search, filters, discovery
//  views, and a scrollable list of rich preset cards over a PresetLibrary.
//  Load a preset, capture the current chain, or generate an AI preset.
// ============================================================================
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel
{
public:
    PresetBrowser (PresetLibrary& library,
                   std::function<void (const Preset&)> onLoad,
                   std::function<Preset()>             onCaptureCurrent,
                   std::function<Preset()>             onGenerateAi);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();               // re-run the current query

private:
    // ListBoxModel
    int  getNumRows() override { return (int) results.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void runQuery();
    void loadSelected();
    const Preset* selectedPreset() const;

    enum class View { Browse, Trending, New, AiRecommended, Favorites };

    PresetLibrary& library;
    std::function<void (const Preset&)> loadFn;
    std::function<Preset()>             captureFn;
    std::function<Preset()>             genAiFn;

    std::vector<const Preset*> results;
    View view = View::Browse;

    juce::Label      titleLabel, statsLabel;
    juce::TextButton closeBtn { "Close ✕" };
    juce::TextEditor searchBox;
    juce::ComboBox   genreBox, sortBox;
    juce::TextButton browseTab { "All" }, trendingTab { "Trending" },
                     newTab { "New" }, aiTab { "AI Picks" }, favTab { "Favorites" };
    juce::ListBox    list { "presets", nullptr };   // model set in the ctor body (order-safe)
    juce::TextButton loadBtn { "Load" }, captureBtn { "Save Current" },
                     genAiBtn { "✨ AI Preset" }, favBtn { "☆ Favorite" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};
} // namespace vf
