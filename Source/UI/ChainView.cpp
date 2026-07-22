#include "ChainView.h"
#include "../Modules/ModuleRegistry.h"
#include "../ParameterIDs.h"
#include <cmath>

namespace vf
{
using namespace vf::mods;

namespace
{
    constexpr int kStripWidth = 250;
    constexpr int kAddHeight  = 96;
    constexpr int kMaxLibraryButtons = 7;

    juce::String categoryOfStage (VocalChain::Stage s)
    {
        switch (s)
        {
            case VocalChain::Stage::Retune: return "Pitch";
            case VocalChain::Stage::Gate:   return "Dynamics";
            case VocalChain::Stage::Eq:     return "EQ";
            case VocalChain::Stage::DynEq:  return "EQ";
            case VocalChain::Stage::DeEss:  return "Restoration";
            case VocalChain::Stage::Comp:   return "Dynamics";
            case VocalChain::Stage::MBand:  return "Dynamics";
            case VocalChain::Stage::Sat:    return "Saturation";
            case VocalChain::Stage::Delay:  return "Delay";
            case VocalChain::Stage::Verb:   return "Space";
            case VocalChain::Stage::Limit:  return "Master";
        }
        return {};
    }

    size_t hashOrder (const std::vector<ChainItem>& o, int rackSize)
    {
        size_t h = (size_t) rackSize * 1315423911u;
        for (const auto& it : o)
            h = h * 131u + (size_t) ((int) it.kind * 31 + (it.isStage() ? (int) it.stage : 0));
        return h;
    }
}

// ============================================================================
//  RackModulePanel — macro controls for one rack module (bound to the fixed
//  host-automatable slot params, exactly like the old rack cards).
// ============================================================================
class RackModulePanel : public juce::Component
{
public:
    RackModulePanel (juce::AudioProcessorValueTreeState& state, ModuleRack& rackRef,
                     int rackIndex, const juce::String& moduleName)
        : name (moduleName)
    {
        const auto snap = rackRef.snapshot();
        if (rackIndex < 0 || rackIndex >= (int) snap.size()) return;
        auto* m = rackRef.find (snap[(size_t) rackIndex].instanceId);
        if (m == nullptr) return;

        if (rackIndex >= ModuleRack::Automation::Slots)
        {
            note = "This module is past slot " + juce::String (ModuleRack::Automation::Slots)
                 + " so it has no host-automatable macros. Move it earlier in the chain to get knobs.";
            return;
        }

        auto& ps = m->params();
        const int n = juce::jmin ((int) ps.size(), ModuleRack::Automation::Macros);
        for (int k = 0; k < n; ++k)
        {
            auto knob = std::make_unique<Knob>();
            knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
            if (ps[(size_t) k].tooltip.isNotEmpty()) knob->slider.setTooltip (ps[(size_t) k].tooltip);
            addAndMakeVisible (knob->slider);

            knob->label.setText (ps[(size_t) k].name, juce::dontSendNotification);
            knob->label.setJustificationType (juce::Justification::centred);
            knob->label.setColour (juce::Label::textColourId, theme::textDim);
            addAndMakeVisible (knob->label);

            knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, "rack_s" + juce::String (rackIndex) + "_m" + juce::String (k), knob->slider);
            knobs.push_back (std::move (knob));
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (! note.isEmpty())
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (note, getLocalBounds().reduced (30), juce::Justification::centred, 3);
        }
        else if (knobs.empty())
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("This module has no adjustable controls.", getLocalBounds(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        if (knobs.empty()) return;
        auto area = getLocalBounds().reduced (20, 10);
        const int n = (int) knobs.size();
        const int w = juce::jmax (60, area.getWidth() / n);
        for (auto& k : knobs)
        {
            auto cell = area.removeFromLeft (w);
            k->label.setBounds (cell.removeFromBottom (18));
            k->slider.setBounds (cell.reduced (6, 4));
        }
    }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::String name, note;
};

// ============================================================================
//  ChainView
// ============================================================================
ChainView::ChainView (juce::AudioProcessorValueTreeState& state, ModuleRack& rackRef, Hooks h)
    : apvts (state), rack (rackRef), hooks (std::move (h))
{
    searchBox.setTextToShowWhenEmpty ("Search modules to add\xe2\x80\xa6", theme::textDim);
    searchBox.setColour (juce::TextEditor::backgroundColourId, theme::panel);
    searchBox.setColour (juce::TextEditor::outlineColourId, theme::outline);
    searchBox.setColour (juce::TextEditor::textColourId, theme::text);
    searchBox.onTextChange = [this] { refreshLibrary(); };
    addAndMakeVisible (searchBox);

    autoBuildButton.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.75f));
    autoBuildButton.setTooltip ("Let the AI build a full chain from your last LEARN analysis.");
    autoBuildButton.onClick = [this] { if (hooks.autoBuild) { hooks.autoBuild(); refresh(); } };
    addAndMakeVisible (autoBuildButton);

    rebuildRows();
    buildFocusPanel();
    refreshLibrary();
    startTimerHz (8);
}

ChainView::~ChainView() = default;

void ChainView::setSimpleMode (bool simple)
{
    simpleMode = simple;
    if (stagePanel != nullptr) stagePanel->setSimpleMode (simple);
}

void ChainView::timerCallback()
{
    // Pick up changes made elsewhere (LEARN auto-build, reference accepts, undo).
    if (! hooks.getOrder) return;
    const auto h = hashOrder (hooks.getOrder(), rack.size());
    if (h != lastOrderHash)
        refresh();
}

void ChainView::refresh()
{
    const int prevSelected = selected;
    rebuildRows();
    selected = juce::jlimit (0, juce::jmax (0, (int) rows.size() - 1), prevSelected);
    buildFocusPanel();
    refreshLibrary();
    resized();
    repaint();
}

void ChainView::rebuildRows()
{
    rows.clear();
    if (! hooks.getOrder) return;

    const auto order = hooks.getOrder();
    const auto snap  = rack.snapshot();
    lastOrderHash = hashOrder (order, rack.size());

    int rackCursor = 0;
    for (const auto& item : order)
    {
        Row r;
        r.item = item;
        if (item.isStage())
        {
            r.name     = VocalChain::stageName (item.stage);
            r.subtitle = categoryOfStage (item.stage);
        }
        else
        {
            const int idx = rackCursor++;
            r.rackIndex = idx;
            if (idx < (int) snap.size())
            {
                r.name     = snap[(size_t) idx].name;
                r.subtitle = categoryName (snap[(size_t) idx].category);
            }
            else { r.name = "Module"; r.subtitle = "Rack"; }
        }
        rows.push_back (std::move (r));
    }
}

bool ChainView::isRowActive (const Row& r) const
{
    if (r.item.isStage())
    {
        const auto& spec = specForStage (r.item.stage);
        if (spec.bypassId != nullptr)
            if (auto* v = apvts.getRawParameterValue (spec.bypassId))
                return v->load() > 0.5f;
        return true;
    }
    const auto snap = rack.snapshot();
    if (r.rackIndex >= 0 && r.rackIndex < (int) snap.size())
        return ! snap[(size_t) r.rackIndex].bypass;
    return true;
}

void ChainView::togglePowerAt (int index)
{
    if (index < 0 || index >= (int) rows.size()) return;
    const auto& r = rows[(size_t) index];

    if (r.item.isStage())
    {
        const auto& spec = specForStage (r.item.stage);
        if (spec.bypassId == nullptr) return;
        if (auto* p = apvts.getParameter (spec.bypassId))
        {
            const bool on = p->getValue() > 0.5f;
            p->setValueNotifyingHost (on ? 0.0f : 1.0f);
        }
    }
    else
    {
        const auto snap = rack.snapshot();
        if (r.rackIndex >= 0 && r.rackIndex < (int) snap.size())
            rack.setBypass (snap[(size_t) r.rackIndex].instanceId, ! snap[(size_t) r.rackIndex].bypass);
    }
    repaint();
}

void ChainView::selectIndex (int index)
{
    if (index < 0 || index >= (int) rows.size() || index == selected) return;
    selected = index;
    buildFocusPanel();
    resized();
    repaint();
}

void ChainView::buildFocusPanel()
{
    stagePanel.reset();
    rackPanel.reset();
    if (selected < 0 || selected >= (int) rows.size()) return;

    const auto& r = rows[(size_t) selected];
    if (r.item.isStage())
    {
        const auto& spec = specForStage (r.item.stage);
        stagePanel = std::make_unique<ModuleCard> (apvts, spec.title, spec.bypassId, spec.lockId,
                                                   spec.knobs, spec.combos);
        stagePanel->setSimpleMode (simpleMode);
        addAndMakeVisible (*stagePanel);
    }
    else
    {
        rackPanel = std::make_unique<RackModulePanel> (apvts, rack, r.rackIndex, r.name);
        addAndMakeVisible (*rackPanel);
    }
}

// ---- add-module library ---------------------------------------------------
void ChainView::refreshLibrary()
{
    libraryButtons.clear();
    showingSuggestions = false;

    auto addButton = [this] (const juce::String& text, const juce::String& tip,
                             const juce::String& typeId, bool highlight)
    {
        auto b = std::make_unique<juce::TextButton> (text);
        b->setTooltip (tip);
        b->setColour (juce::TextButton::buttonColourId,
                      highlight ? theme::accentGreen.withAlpha (0.22f) : theme::panelLight);
        b->setColour (juce::TextButton::textColourOffId, theme::text);
        b->onClick = [this, typeId] { addModuleByType (typeId); };
        addAndMakeVisible (*b);
        libraryButtons.push_back (std::move (b));
    };

    // With an empty search box the bar shows the AI advisor's picks for the last
    // LEARN analysis (so the suggestions that used to live on the MODULES page
    // are still one click away); typing switches it to a library search.
    if (searchBox.getText().isEmpty() && hooks.suggest)
    {
        int shown = 0;
        for (const auto& s : hooks.suggest())
        {
            if (shown >= kMaxLibraryButtons) break;
            if (! ModuleRegistry::instance().isImplemented (s.moduleId)) continue;
            addButton (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\xa8 ")) + s.moduleName,
                       s.rationale, s.moduleId, true);
            ++shown;
        }
        showingSuggestions = shown > 0;
    }

    if (! showingSuggestions)
    {
        int shown = 0;
        for (const auto& d : ModuleRegistry::instance().search (searchBox.getText()))
        {
            if (shown >= kMaxLibraryButtons) break;
            if (! ModuleRegistry::instance().isImplemented (d.id)) continue;
            addButton ("+ " + d.name, d.description, d.id, false);
            ++shown;
        }
    }
    resized();
}

void ChainView::addModuleByType (const juce::String& typeId)
{
    if (rack.addModule (typeId).isEmpty()) return;
    // The processor's timer heals the chain order (adds the new token); refresh
    // here so the strip updates immediately rather than up to 100 ms later.
    refresh();
}

// ---- mouse: select, power, drag-reorder -----------------------------------
int ChainView::rowAtPosition (juce::Point<int> p) const
{
    if (rows.empty() || ! listArea.contains (p)) return -1;
    const int rowH = juce::jmax (1, listArea.getHeight() / (int) rows.size());
    return juce::jlimit (0, (int) rows.size() - 1, (p.y - listArea.getY()) / rowH);
}

void ChainView::mouseDown (const juce::MouseEvent& e)
{
    const int idx = rowAtPosition (e.getPosition());
    if (idx < 0) return;

    // The power dot lives at the left of each row.
    const int rowH = juce::jmax (1, listArea.getHeight() / (int) rows.size());
    const int rowY = listArea.getY() + idx * rowH;
    const juce::Rectangle<int> powerHit (listArea.getX() + 6, rowY + rowH / 2 - 11, 22, 22);
    if (powerHit.contains (e.getPosition())) { togglePowerAt (idx); return; }

    selectIndex (idx);
    dragFrom  = idx;
    dragTo    = idx;
    dragStart = e.getPosition();
    dragging  = false;
}

void ChainView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    if (! dragging && e.getPosition().getDistanceFrom (dragStart) < 5) return;
    dragging = true;

    const int rowH = juce::jmax (1, listArea.getHeight() / (int) juce::jmax (1, (int) rows.size()));
    const int rel  = e.getPosition().y - listArea.getY();
    dragTo = juce::jlimit (0, (int) rows.size() - 1, rel / juce::jmax (1, rowH));
    repaint();
}

void ChainView::mouseUp (const juce::MouseEvent&)
{
    if (dragging && dragFrom >= 0 && dragTo >= 0 && dragTo != dragFrom && hooks.moveItem)
    {
        hooks.moveItem (dragFrom, dragTo);
        selected = dragTo;
        refresh();
    }
    dragFrom = dragTo = -1;
    dragging = false;
    repaint();
}

// ---- layout / paint -------------------------------------------------------
void ChainView::resized()
{
    auto area = getLocalBounds();
    stripArea = area.removeFromLeft (kStripWidth);
    addArea   = area.removeFromBottom (kAddHeight);
    focusArea = area;

    listArea = stripArea.reduced (8, 8);
    listArea.removeFromTop (22);          // "MODULE CHAIN" heading

    if (stagePanel != nullptr) stagePanel->setBounds (focusArea.reduced (12, 10));
    if (rackPanel  != nullptr) rackPanel->setBounds  (focusArea.reduced (12, 10));

    auto add = addArea.reduced (12, 10);
    auto top = add.removeFromTop (28);
    searchBox.setBounds (top.removeFromLeft (240));
    top.removeFromLeft (10);
    autoBuildButton.setBounds (top.removeFromLeft (130));

    add.removeFromTop (6);
    if (! libraryButtons.empty())
    {
        const int w = juce::jmax (90, add.getWidth() / (int) libraryButtons.size());
        for (auto& b : libraryButtons)
            b->setBounds (add.removeFromLeft (w).reduced (3, 2));
    }
}

void ChainView::paint (juce::Graphics& g)
{
    // ---- chain strip ----
    g.setColour (theme::panel.withAlpha (0.75f));
    g.fillRoundedRectangle (stripArea.toFloat().reduced (4.0f), 10.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (stripArea.toFloat().reduced (4.0f), 10.0f, 1.0f);

    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("MODULE CHAIN", stripArea.getX() + 14, stripArea.getY() + 10, 200, 16,
                juce::Justification::centredLeft);

    if (! rows.empty())
    {
        const int rowH = juce::jmax (1, listArea.getHeight() / (int) rows.size());
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto& r = rows[(size_t) i];
            juce::Rectangle<int> rb (listArea.getX(), listArea.getY() + i * rowH, listArea.getWidth(), rowH);
            const bool isSel = (i == selected);
            const bool active = isRowActive (r);

            if (isSel)
            {
                g.setColour (theme::accent.withAlpha (0.18f));
                g.fillRoundedRectangle (rb.toFloat().reduced (2.0f), 6.0f);
                g.setColour (theme::accent);
                g.drawRoundedRectangle (rb.toFloat().reduced (2.0f), 6.0f, 1.2f);
            }

            // power dot
            const auto dot = juce::Rectangle<float> ((float) rb.getX() + 10.0f,
                                                     (float) rb.getCentreY() - 5.0f, 10.0f, 10.0f);
            g.setColour (active ? theme::accentGreen : theme::outline);
            g.fillEllipse (dot);

            // index + name
            g.setColour (active ? theme::textDim : theme::textDim.withAlpha (0.5f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (i + 1), rb.getX() + 24, rb.getY(), 18, rowH, juce::Justification::centredLeft);

            g.setColour (active ? theme::text : theme::text.withAlpha (0.45f));
            g.setFont (juce::FontOptions (rowH > 34 ? 13.0f : 12.0f, juce::Font::bold));
            g.drawText (r.name, rb.getX() + 44, rb.getY() + (rowH > 34 ? 4 : 0),
                        rb.getWidth() - 54, rowH > 34 ? rowH / 2 : rowH, juce::Justification::centredLeft);

            if (rowH > 34)
            {
                g.setColour (theme::textDim.withAlpha (active ? 0.9f : 0.45f));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (r.subtitle + (r.rackIndex >= 0 ? "  \xc2\xb7  module" : ""),
                            rb.getX() + 44, rb.getCentreY(), rb.getWidth() - 54, rowH / 2 - 2,
                            juce::Justification::centredLeft);
            }
        }

        // drag insertion line
        if (dragging && dragTo >= 0)
        {
            const int y = listArea.getY() + dragTo * rowH;
            g.setColour (theme::accentWarm);
            g.fillRect (listArea.getX() + 4, y - 1, listArea.getWidth() - 8, 2);
        }
    }

    // ---- focused module panel backdrop ----
    g.setColour (theme::panel.withAlpha (0.55f));
    g.fillRoundedRectangle (focusArea.toFloat().reduced (6.0f), 10.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (focusArea.toFloat().reduced (6.0f), 10.0f, 1.0f);

    if (rows.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("No modules in the chain.", focusArea, juce::Justification::centred);
    }
    else if (rackPanel != nullptr)
    {
        // rack modules have no card header of their own — draw one
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (rows[(size_t) selected].name, focusArea.getX() + 22, focusArea.getY() + 14,
                    focusArea.getWidth() - 40, 18, juce::Justification::centredLeft);
    }

    // ---- add bar ----
    g.setColour (theme::panel.withAlpha (0.75f));
    g.fillRoundedRectangle (addArea.toFloat().reduced (6.0f), 10.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (addArea.toFloat().reduced (6.0f), 10.0f, 1.0f);
}
} // namespace vf
