#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"
#include "../Reference/ReferenceImport.h"

namespace vf
{
// ============================================================================
//  ReferenceReportView — the scrollable list of match decisions with confidence
//  bars + rationale, plus complementary modules and the coach summary. Lives
//  inside the panel's Viewport; sizes itself to its content.
// ============================================================================
class ReferenceReportView : public juce::Component
{
public:
    void setResult (const ReferenceResult& r);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    /** Height needed to render everything at the given width. */
    int preferredHeight (int width) const;
    /** Mark every row accepted (used by "Accept All"). */
    void markAllAccepted();

    std::function<void (int)> onAcceptDecision;   // index into match.decisions
    std::function<void (int)> onAcceptInsert;     // index into match.rackInserts

private:
    ReferenceResult result;
    bool have = false;
    std::vector<char> decAccepted, insAccepted;             // accepted flags
    std::vector<juce::Rectangle<int>> decPill, insPill;     // hit rects (set in paint)
};

// ============================================================================
//  ReferencePanel — the dedicated AI Reference Mix Analyzer overlay.
//   • DropZone:   dashed target + Browse + supported formats.
//   • Analyzing:  file name, progress bar, Cancel.
//   • Result:     preset name + overall confidence, a frequency-response graph
//                 (band tints + resonance markers), dynamics/loudness/stereo
//                 mini-gauges, and the scrollable decision report.
//  It is itself a FileDragAndDropTarget, so files can be dropped straight on it.
//  All interaction is delegated to the editor via callbacks — the panel never
//  touches the processor directly.
// ============================================================================
class ReferencePanel : public juce::Component,
                       public juce::FileDragAndDropTarget
{
public:
    ReferencePanel();

    void paint (juce::Graphics&) override;
    void resized() override;

    // Driven by the editor's timer:
    void setAnalyzing (const juce::String& fileName, const juce::String& status, float progress01);
    void showResult (const ReferenceResult& r);
    void showDropZone();
    bool hasResult() const noexcept { return haveResult; }

    /** Mark every suggestion accepted (visual) — call after Accept All. */
    void markAllAccepted() { reportView.markAllAccepted(); }
    /** Update the A/B button label to reflect what's currently sounding. */
    void setCompareShowingOriginal (bool showingOriginal);
    /** Show a transient confirmation next to Save/Share (e.g. "✓ Saved"). */
    void setSaveStatus (const juce::String& text);

    // Callbacks (all optional):
    std::function<void()> onClose;
    std::function<void()> onBrowse;
    std::function<void()> onCancel;
    std::function<void (const juce::File&)> onFile;
    std::function<void()> onAcceptAll;
    std::function<void()> onCompare;
    std::function<void()> onUndo;
    std::function<void()> onSavePreset;
    std::function<void()> onShare;
    std::function<void (int)> onAcceptDecision;
    std::function<void (int)> onAcceptInsert;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    enum class View { DropZone, Analyzing, Result };
    void applyView();
    void drawDropZone (juce::Graphics&, juce::Rectangle<int>);
    void drawAnalyzing (juce::Graphics&, juce::Rectangle<int>);
    void drawFrequencyGraph (juce::Graphics&, juce::Rectangle<int>);
    void drawGauges (juce::Graphics&, juce::Rectangle<int>);

    View view = View::DropZone;
    juce::String fileName, statusText;
    float progress = 0.0f;
    ReferenceResult result;
    bool haveResult = false;
    juce::Rectangle<int> graphsBounds;   // set in resized(), used by paint()

    juce::TextButton closeButton     { "Close" };
    juce::TextButton browseButton    { "Browse\xe2\x80\xa6" };
    juce::TextButton cancelButton    { "Cancel" };
    juce::TextButton anotherButton   { "Analyze another" };
    juce::TextButton acceptAllButton { "Accept All" };
    juce::TextButton compareButton   { "A / B" };
    juce::TextButton undoButton      { "Undo" };
    juce::TextButton saveButton      { "Save Preset" };
    juce::TextButton shareButton     { "Share" };
    juce::String     saveStatus;

    juce::Viewport            reportViewport;
    ReferenceReportView       reportView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferencePanel)
};
} // namespace vf
