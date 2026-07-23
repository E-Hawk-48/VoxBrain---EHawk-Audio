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
    constexpr int kFlowHeight = 78;      // signal-flow ribbon at the top of focus
    constexpr int kFlowCaption = 14;
    constexpr int kFlowBoxH = 26;
    constexpr int kFlowGap  = 9;
    constexpr int kFlowVGap = 6;
    constexpr int kFlowMinW = 56;

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
                r.name       = snap[(size_t) idx].name;
                r.subtitle   = categoryName (snap[(size_t) idx].category);
                r.instanceId = snap[(size_t) idx].instanceId;
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
    // Add the module to the rack (it lands as the last node), then place its row
    // right AFTER the currently selected module and commit the whole arrangement
    // so it inserts at that slot — not just appended to the end of the chain.
    const auto id = rack.addModule (typeId);
    if (id.isEmpty()) return;

    Row nr;
    nr.item       = ChainItem::makeRack();
    nr.instanceId = id;                        // name/subtitle filled by refresh()

    auto newRows = rows;
    const int insertAt = juce::jlimit (0, (int) newRows.size(), selected + 1);
    newRows.insert (newRows.begin() + insertAt, nr);
    commitArrangement (newRows, insertAt);
}

void ChainView::commitArrangement (const std::vector<Row>& newRows, int newSelected)
{
    if (! hooks.applyArrangement) return;

    std::vector<ChainItem> order;
    juce::StringArray rackIds;
    order.reserve (newRows.size());
    for (const auto& r : newRows)
    {
        if (r.item.isStage())
            order.push_back (ChainItem::makeStage (r.item.stage));
        else
        {
            order.push_back (ChainItem::makeRack());
            rackIds.add (r.instanceId);
        }
    }

    selected = juce::jlimit (0, juce::jmax (0, (int) newRows.size() - 1), newSelected);
    hooks.applyArrangement (std::move (order), rackIds);
    refresh();                                 // rebuild from the healed state
}

// ---- signal-flow ribbon layout --------------------------------------------
void ChainView::layoutFlow()
{
    flowBoxes.clear();
    const int n = (int) rows.size();
    if (n == 0 || flowArea.isEmpty()) return;

    auto area = flowArea;
    area.removeFromTop (kFlowCaption);         // caption line

    int perRow = n;
    int boxW   = (area.getWidth() - (n - 1) * kFlowGap) / juce::jmax (1, n);
    int nRows  = 1;
    if (boxW < kFlowMinW)                       // too tight for one row → use two
    {
        nRows  = 2;
        perRow = (n + 1) / 2;
        boxW   = (area.getWidth() - (perRow - 1) * kFlowGap) / juce::jmax (1, perRow);
    }
    boxW = juce::jmax (26, boxW);

    for (int i = 0; i < n; ++i)
    {
        const int row = (nRows == 1) ? 0 : (i / perRow);
        const int col = (nRows == 1) ? i : (i % perRow);
        const int x = area.getX() + col * (boxW + kFlowGap);
        const int y = area.getY() + row * (kFlowBoxH + kFlowVGap);
        flowBoxes.push_back ({ x, y, boxW, kFlowBoxH });
    }
}

int ChainView::flowBoxAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) flowBoxes.size(); ++i)
        if (flowBoxes[(size_t) i].contains (p)) return i;
    return -1;
}

// ---- mouse: select, power, drag-reorder -----------------------------------
int ChainView::rowAtPosition (juce::Point<int> p) const
{
    if (rows.empty() || ! listArea.contains (p)) return -1;
    const int rowH = juce::jmax (1, listArea.getHeight() / (int) rows.size());
    return juce::jlimit (0, (int) rows.size() - 1, (p.y - listArea.getY()) / rowH);
}

void ChainView::mouseMove (const juce::MouseEvent& e)
{
    if (flowBoxAt (e.getPosition()) >= 0)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (rowAtPosition (e.getPosition()) >= 0)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ChainView::mouseDown (const juce::MouseEvent& e)
{
    // Signal-flow box → jump-select that module.
    const int fb = flowBoxAt (e.getPosition());
    if (fb >= 0) { selectIndex (fb); return; }

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
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);

    const int rowH = juce::jmax (1, listArea.getHeight() / (int) juce::jmax (1, (int) rows.size()));
    const int rel  = e.getPosition().y - listArea.getY();
    dragTo = juce::jlimit (0, (int) rows.size() - 1, rel / juce::jmax (1, rowH));
    repaint();
}

void ChainView::mouseUp (const juce::MouseEvent&)
{
    if (dragging && dragFrom >= 0 && dragTo >= 0 && dragTo != dragFrom
        && dragFrom < (int) rows.size())
    {
        // Rebuild the full visual row order and commit it as one arrangement —
        // this reorders rack nodes as well as stage tokens, so dragging ANY row
        // (including one rack module past another) genuinely changes the sound.
        auto newRows = rows;
        const auto moved = newRows[(size_t) dragFrom];
        newRows.erase (newRows.begin() + dragFrom);
        newRows.insert (newRows.begin() + juce::jlimit (0, (int) newRows.size(), dragTo), moved);
        commitArrangement (newRows, dragTo);
    }
    dragFrom = dragTo = -1;
    dragging = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
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

    // The signal-flow ribbon sits at the top of the focus area; the selected
    // module's controls fill the rest below it.
    auto inner = focusArea.reduced (12, 10);
    flowArea = inner.removeFromTop (kFlowHeight);
    layoutFlow();
    inner.removeFromTop (6);
    if (rackPanel != nullptr)
    {
        inner.removeFromTop (22);         // name band (rack modules have no header)
        rackPanel->setBounds (inner);
    }
    else if (stagePanel != nullptr)
    {
        stagePanel->setBounds (inner);
    }

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
                        rb.getWidth() - 66, rowH > 34 ? rowH / 2 : rowH, juce::Justification::centredLeft);

            if (rowH > 34)
            {
                g.setColour (theme::textDim.withAlpha (active ? 0.9f : 0.45f));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (r.subtitle + (r.rackIndex >= 0 ? "  \xc2\xb7  module" : ""),
                            rb.getX() + 44, rb.getCentreY(), rb.getWidth() - 66, rowH / 2 - 2,
                            juce::Justification::centredLeft);
            }

            // drag-handle grip (affordance that the row is draggable)
            {
                const float gx = (float) rb.getRight() - 17.0f;
                const float gy = (float) rb.getCentreY();
                g.setColour (theme::textDim.withAlpha (active ? 0.55f : 0.3f));
                for (int cx = 0; cx < 2; ++cx)
                    for (int cy = -1; cy <= 1; ++cy)
                        g.fillEllipse (gx + (float) cx * 4.5f, gy + (float) cy * 4.5f - 1.1f, 2.2f, 2.2f);
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

    // ---- signal-flow ribbon (top of the focus area) ----
    if (! rows.empty() && ! flowBoxes.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("SIGNAL FLOW", flowArea.getX() + 2, flowArea.getY(), 200, kFlowCaption,
                    juce::Justification::centredLeft);
        g.setColour (theme::textDim.withAlpha (0.55f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("in \xe2\x96\xb8 out")),
                    flowArea.getRight() - 82, flowArea.getY(), 80, kFlowCaption,
                    juce::Justification::centredRight);

        const int nBoxes = juce::jmin ((int) flowBoxes.size(), (int) rows.size());

        // connectors first (behind the boxes): straight arrow between same-row nodes
        for (int i = 0; i + 1 < nBoxes; ++i)
        {
            const auto a = flowBoxes[(size_t) i];
            const auto b = flowBoxes[(size_t) (i + 1)];
            if (a.getY() != b.getY()) continue;    // wrap boundary — reading order carries it
            const float y = (float) a.getCentreY();
            g.setColour (theme::outline);
            g.drawLine ((float) a.getRight(), y, (float) b.getX(), y, 1.3f);
            juce::Path tri;
            const float tx = (float) b.getX();
            tri.addTriangle (tx - 5.0f, y - 3.0f, tx - 5.0f, y + 3.0f, tx, y);
            g.setColour (theme::accent.withAlpha (0.8f));
            g.fillPath (tri);
        }

        for (int i = 0; i < nBoxes; ++i)
        {
            const auto& r = rows[(size_t) i];
            const auto boxF = flowBoxes[(size_t) i].toFloat();
            const bool sel = (i == selected);
            const bool active = isRowActive (r);

            g.setColour ((active ? theme::panelLight : theme::panel).withAlpha (active ? 0.95f : 0.55f));
            g.fillRoundedRectangle (boxF, 5.0f);
            g.setColour (sel ? theme::accent : theme::outline);
            g.drawRoundedRectangle (boxF, 5.0f, sel ? 1.7f : 1.0f);

            g.setColour (active ? theme::accentGreen : theme::outline);
            g.fillEllipse (boxF.getX() + 5.0f, boxF.getCentreY() - 2.0f, 4.0f, 4.0f);

            g.setColour (active ? theme::text : theme::text.withAlpha (0.5f));
            g.setFont (juce::FontOptions (10.0f, sel ? juce::Font::bold : juce::Font::plain));
            g.drawFittedText (r.name, flowBoxes[(size_t) i].reduced (12, 2),
                              juce::Justification::centred, 1);
        }
    }

    if (rows.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("No modules in the chain.", focusArea, juce::Justification::centred);
    }
    else if (rackPanel != nullptr)
    {
        // rack modules have no card header of their own — draw one under the ribbon
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (rows[(size_t) selected].name, flowArea.getX() + 4, flowArea.getBottom() + 6,
                    flowArea.getWidth() - 8, 20, juce::Justification::centredLeft);
    }

    // ---- add bar ----
    g.setColour (theme::panel.withAlpha (0.75f));
    g.fillRoundedRectangle (addArea.toFloat().reduced (6.0f), 10.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (addArea.toFloat().reduced (6.0f), 10.0f, 1.0f);
}
} // namespace vf
