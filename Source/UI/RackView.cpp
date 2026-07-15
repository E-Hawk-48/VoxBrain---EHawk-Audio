#include "RackView.h"

namespace vf
{
using mods::ModuleRegistry;
using mods::Descriptor;

// ============================================================================
//  CatalogModel
// ============================================================================
void CatalogModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) items.size()) return;
    const auto& d = items[(size_t) row];

    if (selected)
        g.fillAll (theme::accent.withAlpha (0.18f));

    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (10, 0);

    // right-side tag: "+" to add, or "SOON" for roadmap entries
    auto tag = r.removeFromRight (52);
    if (d.implemented)
    {
        g.setColour (theme::accentGreen);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText ("+", tag, juce::Justification::centredRight);
    }
    else
    {
        g.setColour (theme::textDim.withAlpha (0.7f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("SOON", tag, juce::Justification::centredRight);
    }

    g.setColour (d.implemented ? theme::text : theme::textDim);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (d.name, r.removeFromTop (h / 2 + 2), juce::Justification::bottomLeft);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (mods::categoryName (d.category), r, juce::Justification::topLeft);
}

void CatalogModel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) items.size() && items[(size_t) row].implemented && onAdd)
        onAdd (items[(size_t) row].id);
}

void CatalogModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    listBoxItemClicked (row, e);
}

// ============================================================================
//  RackModuleCard
// ============================================================================
RackModuleCard::RackModuleCard (juce::AudioProcessorValueTreeState& a,
                                mods::ModuleRack& r,
                                const mods::ModuleRack::NodeInfo& info,
                                int slotIndex)
    : instanceId (info.instanceId), apvts (a), rack (r), slot (slotIndex),
      title (info.name), subtitle (mods::categoryName (info.category)),
      latency (info.latency)
{
    automatable = slot < mods::ModuleRack::Automation::Slots;

    auto pill = [this] (juce::ToggleButton& b, const juce::String& t, juce::Colour c, bool on)
    {
        b.setButtonText (t);
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::ToggleButton::tickColourId, c);
        addAndMakeVisible (b);
    };
    pill (bypassBtn, "Bypass", theme::textDim,    info.bypass);
    pill (soloBtn,   "Solo",   theme::accentWarm, info.solo);
    pill (lockBtn,   "Lock",   theme::accent,     info.lock);

    bypassBtn.onClick = [this] { rack.setBypass (instanceId, bypassBtn.getToggleState()); if (onChanged) onChanged(); };
    soloBtn.onClick   = [this] { rack.setSolo   (instanceId, soloBtn.getToggleState());   if (onChanged) onChanged(); };
    lockBtn.onClick   = [this] { rack.setLock   (instanceId, lockBtn.getToggleState()); };

    auto small = [this] (juce::TextButton& b, juce::Colour c, std::function<void()>* cb)
    {
        b.setColour (juce::TextButton::buttonColourId, c);
        b.onClick = [cb] { if (cb && *cb) (*cb)(); };
        addAndMakeVisible (b);
    };
    small (upBtn,     theme::panelLight,               &onMoveUp);
    small (downBtn,   theme::panelLight,               &onMoveDown);
    small (dupBtn,    theme::panelLight,               &onDuplicate);
    small (removeBtn, juce::Colour (0xffb2413a),       &onRemove);

    // Macro knobs — bound to the host-automatable pool for this slot.
    if (automatable)
    {
        if (auto* m = rack.find (instanceId))
        {
            auto& ps = m->params();
            const int n = juce::jmin ((int) ps.size(), mods::ModuleRack::Automation::Macros);
            for (int k = 0; k < n; ++k)
            {
                auto knob = std::make_unique<Knob>();
                knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 14);
                knob->slider.setNumDecimalPlacesToDisplay (2);
                knob->slider.setTooltip (ps[(size_t) k].name
                    + (ps[(size_t) k].unit.isNotEmpty() ? " (" + ps[(size_t) k].unit + ")" : juce::String())
                    + (ps[(size_t) k].tooltip.isNotEmpty() ? " — " + ps[(size_t) k].tooltip : juce::String()));
                addAndMakeVisible (knob->slider);

                knob->label.setText (ps[(size_t) k].name, juce::dontSendNotification);
                knob->label.setJustificationType (juce::Justification::centred);
                knob->label.setColour (juce::Label::textColourId, theme::textDim);
                knob->label.setFont (juce::FontOptions (10.0f));
                addAndMakeVisible (knob->label);

                const auto pid = "rack_s" + juce::String (slot) + "_m" + juce::String (k);
                knob->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    apvts, pid, knob->slider);
                knobs.push_back (std::move (knob));
            }
        }
    }
}

void RackModuleCard::setCpu (float pct) { cpu = pct; repaint (0, 0, getWidth(), 26); }

void RackModuleCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (bypassBtn.getToggleState() ? theme::outline
                                            : theme::accent.withAlpha (0.35f));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    auto head = getLocalBounds().reduced (12, 6).removeFromTop (34);
    // slot number badge
    auto badge = head.removeFromLeft (26);
    g.setColour (theme::accent.withAlpha (0.9f));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (juce::String (slot + 1), badge, juce::Justification::centredLeft);

    g.setColour (theme::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (title, head.removeFromTop (18), juce::Justification::topLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (10.0f));
    juce::String sub = subtitle;
    if (latency > 0) sub << "   •   " << latency << " smp latency";
    if (! automatable) sub << "   •   macros: slots 1–8 only";
    g.drawText (sub, head, juce::Justification::topLeft);

    // CPU meter (top-right)
    auto meter = getLocalBounds().reduced (12, 6).removeFromTop (16).removeFromRight (150);
    meter.removeFromRight (120);   // leave room for the control buttons drawn as components
    g.setColour (cpu > 40.0f ? theme::accentWarm : theme::accentGreen);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (juce::String (cpu, 1) + "%", meter, juce::Justification::centredRight);
}

void RackModuleCard::resized()
{
    auto r = getLocalBounds().reduced (12, 8);

    auto top = r.removeFromTop (22);
    auto ctl = top.removeFromRight (118);
    removeBtn.setBounds (ctl.removeFromRight (26));
    ctl.removeFromRight (4);
    dupBtn.setBounds (ctl.removeFromRight (40));
    ctl.removeFromRight (4);
    downBtn.setBounds (ctl.removeFromRight (22));
    upBtn.setBounds (ctl.removeFromRight (22));

    r.removeFromTop (14);   // space under header/subtitle

    auto toggles = r.removeFromTop (24);
    bypassBtn.setBounds (toggles.removeFromLeft (74));
    toggles.removeFromLeft (6);
    soloBtn.setBounds (toggles.removeFromLeft (60));
    toggles.removeFromLeft (6);
    lockBtn.setBounds (toggles.removeFromLeft (60));

    r.removeFromTop (4);
    if (! knobs.empty())
    {
        const int kw = juce::jmin (74, r.getWidth() / (int) knobs.size());
        auto row = r;
        for (auto& k : knobs)
        {
            auto cell = row.removeFromLeft (kw);
            k->label.setBounds (cell.removeFromBottom (14));
            k->slider.setBounds (cell.reduced (2));
        }
    }
}

// ============================================================================
//  RackView
// ============================================================================
RackView::RackView (juce::AudioProcessorValueTreeState& a,
                    mods::ModuleRack& r,
                    std::function<std::vector<mods::ModuleSuggestion>()> fn)
    : apvts (a), rack (r), suggestFn (std::move (fn))
{
    titleLabel.setText ("MODULE RACK", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (titleLabel);

    statsLabel.setJustificationType (juce::Justification::centredRight);
    statsLabel.setColour (juce::Label::textColourId, theme::textDim);
    statsLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (statsLabel);

    closeBtn.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);

    paletteLabel.setText ("ADD MODULE", juce::dontSendNotification);
    paletteLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    paletteLabel.setColour (juce::Label::textColourId, theme::accent);
    addAndMakeVisible (paletteLabel);

    searchBox.setTextToShowWhenEmpty ("Search: warm, airy, compress, pitch, reverb…",
                                      theme::textDim);
    searchBox.setColour (juce::TextEditor::backgroundColourId, theme::panel);
    searchBox.setColour (juce::TextEditor::outlineColourId, theme::outline);
    searchBox.setColour (juce::TextEditor::textColourId, theme::text);
    searchBox.onTextChange = [this] { doSearch(); };
    addAndMakeVisible (searchBox);

    catalogModel.onAdd = [this] (const juce::String& id) { addModule (id); };
    catalogList.setModel (&catalogModel);
    catalogList.setRowHeight (38);
    catalogList.setColour (juce::ListBox::backgroundColourId, theme::bg);
    addAndMakeVisible (catalogList);

    advisorLabel.setText ("AI SUGGESTS", juce::dontSendNotification);
    advisorLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    advisorLabel.setColour (juce::Label::textColourId, theme::accentGreen);
    addAndMakeVisible (advisorLabel);
    addAndMakeVisible (advisorStrip);

    emptyHint.setText ("Your rack is empty.\nSearch on the left and click a module to add it.",
                       juce::dontSendNotification);
    emptyHint.setJustificationType (juce::Justification::centred);
    emptyHint.setColour (juce::Label::textColourId, theme::textDim);
    emptyHint.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (emptyHint);

    rackViewport.setViewedComponent (&rackContent, false);
    rackViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (rackViewport);

    doSearch();
    refreshAdvisor();
    rebuildCards();
    startTimerHz (8);
}

RackView::~RackView() { stopTimer(); }

void RackView::doSearch()
{
    catalogModel.items = ModuleRegistry::instance().search (searchBox.getText());
    catalogList.updateContent();
    catalogList.repaint();
}

void RackView::addModule (const juce::String& typeId)
{
    const auto id = rack.addModule (typeId);
    if (id.isEmpty()) return;
    syncMacros();       // seed the new module's slot macros to its defaults
    rebuildCards();
}

void RackView::moveModule (const juce::String& instanceId, int dir)
{
    const int idx = rack.indexOf (instanceId);
    const int to  = idx + dir;
    if (idx < 0 || to < 0 || to >= rack.size()) return;
    rack.moveModule (instanceId, to);
    syncMacros();       // keep each module's sound after the slot shift
    rebuildCards();
}

void RackView::removeModule (const juce::String& instanceId)
{
    rack.removeModule (instanceId);
    syncMacros();
    rebuildCards();
}

void RackView::duplicateModule (const juce::String& instanceId)
{
    rack.duplicateModule (instanceId);
    syncMacros();
    rebuildCards();
}

void RackView::syncMacros()
{
    const auto snap = rack.snapshot();
    const int n = juce::jmin ((int) snap.size(), mods::ModuleRack::Automation::Slots);
    for (int s = 0; s < n; ++s)
    {
        auto* m = rack.find (snap[(size_t) s].instanceId);
        if (m == nullptr) continue;
        auto& ps = m->params();
        const int np = juce::jmin ((int) ps.size(), mods::ModuleRack::Automation::Macros);
        for (int k = 0; k < np; ++k)
        {
            const float mn = ps[(size_t) k].min, mx = ps[(size_t) k].max;
            const float norm = mx > mn ? juce::jlimit (0.0f, 1.0f, (ps[(size_t) k].value - mn) / (mx - mn))
                                       : 0.0f;
            if (auto* p = apvts.getParameter ("rack_s" + juce::String (s) + "_m" + juce::String (k)))
                p->setValueNotifyingHost (norm);
        }
    }
}

void RackView::rebuildCards()
{
    cards.clear();
    rackContent.removeAllChildren();

    const auto snap = rack.snapshot();
    for (int i = 0; i < (int) snap.size(); ++i)
    {
        auto card = std::make_unique<RackModuleCard> (apvts, rack, snap[(size_t) i], i);
        const auto iid = card->instanceId;
        card->onRemove    = [this, iid] { removeModule (iid); };
        card->onDuplicate = [this, iid] { duplicateModule (iid); };
        card->onMoveUp    = [this, iid] { moveModule (iid, -1); };
        card->onMoveDown  = [this, iid] { moveModule (iid, +1); };
        card->onChanged   = [this] { updateStats(); };
        rackContent.addAndMakeVisible (*card);
        cards.push_back (std::move (card));
    }

    emptyHint.setVisible (cards.empty());
    layoutCards();
    updateStats();
}

void RackView::layoutCards()
{
    const int w = juce::jmax (10, rackViewport.getWidth() - 12);
    const int h = RackModuleCard::cardHeight;
    rackContent.setSize (w, juce::jmax (h, (int) cards.size() * (h + 8)));
    int y = 0;
    for (auto& c : cards) { c->setBounds (0, y, w, h); y += h + 8; }
}

void RackView::updateStats()
{
    statsLabel.setText (juce::String (rack.size()) + " modules   •   "
                        + juce::String (rack.latencySamples()) + " smp added latency",
                        juce::dontSendNotification);
}

void RackView::refreshAdvisor()
{
    advisorChips.clear();
    advisorStrip.removeAllChildren();

    std::vector<mods::ModuleSuggestion> sug;
    if (suggestFn) sug = suggestFn();

    int shown = 0;
    for (const auto& s : sug)
    {
        if (shown >= 4) break;
        const bool canAdd = ModuleRegistry::instance().isImplemented (s.moduleId);
        auto chip = std::make_unique<juce::TextButton> (
            juce::String (canAdd ? "+ " : "") + s.moduleName + (canAdd ? "" : " (soon)"));
        chip->setTooltip (s.rationale);
        chip->setColour (juce::TextButton::buttonColourId,
                         canAdd ? theme::accentGreen.withAlpha (0.22f)
                                : theme::panelLight);
        chip->setColour (juce::TextButton::textColourOffId,
                         canAdd ? theme::text : theme::textDim);
        chip->setEnabled (canAdd);
        if (canAdd)
        {
            const auto id = s.moduleId;
            chip->onClick = [this, id] { addModule (id); };
        }
        advisorStrip.addAndMakeVisible (*chip);
        advisorChips.push_back (std::move (chip));
        ++shown;
    }

    if (advisorChips.empty())
    {
        auto chip = std::make_unique<juce::TextButton> ("Run LEARN for module suggestions");
        chip->setEnabled (false);
        chip->setColour (juce::TextButton::buttonColourId, theme::panelLight);
        chip->setColour (juce::TextButton::textColourOffId, theme::textDim);
        advisorStrip.addAndMakeVisible (*chip);
        advisorChips.push_back (std::move (chip));
    }

    resized();
}

void RackView::timerCallback()
{
    if (cards.empty()) return;
    const auto snap = rack.snapshot();
    for (auto& c : cards)
        for (const auto& n : snap)
            if (n.instanceId == c->instanceId) { c->setCpu (n.cpu); break; }
}

void RackView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg.withAlpha (0.985f));
    g.setColour (theme::outline);
    g.drawRect (getLocalBounds(), 1);

    // column separator
    auto b = getLocalBounds().reduced (16);
    b.removeFromTop (44);
    const int paletteW = juce::jmin (320, b.getWidth() / 3);
    g.setColour (theme::outline);
    g.drawVerticalLine (b.getX() + paletteW + 8, (float) b.getY(), (float) b.getBottom());
}

void RackView::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (36);
    titleLabel.setBounds (header.removeFromLeft (200));
    closeBtn.setBounds (header.removeFromRight (90));
    statsLabel.setBounds (header);
    area.removeFromTop (8);

    const int paletteW = juce::jmin (320, area.getWidth() / 3);
    auto palette = area.removeFromLeft (paletteW);
    paletteLabel.setBounds (palette.removeFromTop (18));
    searchBox.setBounds (palette.removeFromTop (30));
    palette.removeFromTop (6);
    catalogList.setBounds (palette);

    area.removeFromLeft (16);   // gap past the separator

    advisorLabel.setBounds (area.removeFromTop (18));
    auto strip = area.removeFromTop (34);
    advisorStrip.setBounds (strip);
    // lay chips left→right within the strip
    {
        int x = 0;
        const int h = strip.getHeight();
        for (auto& c : advisorChips)
        {
            const int tw = juce::jlimit (120, 240,
                c->getButtonText().length() * 8 + 24);
            c->setBounds (x, 0, tw, h);
            x += tw + 8;
        }
    }
    area.removeFromTop (6);

    rackViewport.setBounds (area);
    emptyHint.setBounds (area);
    layoutCards();
}
} // namespace vf
