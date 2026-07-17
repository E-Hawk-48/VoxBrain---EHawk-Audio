#include "ThemePanel.h"
#include <juce_gui_extra/juce_gui_extra.h>   // juce::ColourSelector

namespace vf
{
ThemePanel::ThemePanel()
{
    titleLabel.setText ("THEME", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (titleLabel);

    closeBtn.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.85f));
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);

    auto label = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, theme::accent);
        addAndMakeVisible (l);
    };
    label (schemeLabel, "COLOUR SCHEMES");
    label (colourLabel, "CUSTOM COLOURS");
    label (glowLabel,   "NEON GLOW");

    for (const auto& s : theme::schemes())
    {
        auto b = std::make_unique<juce::TextButton> (s.name);
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (s.accent).withAlpha (0.85f));
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (s.text));
        const auto name = s.name;
        b->onClick = [this, name] { theme::applyScheme (name); refreshChips(); fireChanged(); };
        addAndMakeVisible (*b);
        schemeButtons.push_back (std::move (b));
    }

    primaryChip.onClick   = [this] { openPicker (theme::accent,     [] (juce::Colour c) { theme::setPrimary (c); },   &primaryChip); };
    secondaryChip.onClick = [this] { openPicker (theme::accentWarm, [] (juce::Colour c) { theme::setSecondary (c); }, &secondaryChip); };
    bgChip.onClick        = [this] { openPicker (theme::bg,         [] (juce::Colour c) { theme::setBackground (c); }, &bgChip); };
    resetBtn.onClick      = [this] { theme::applyScheme ("Neon Red / Purple"); theme::setGlow (1.0f);
                                     glowSlider.setValue (100.0, juce::dontSendNotification); refreshChips(); fireChanged(); };
    for (auto* c : { &primaryChip, &secondaryChip, &bgChip, &resetBtn })
        addAndMakeVisible (*c);

    glowSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    glowSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    glowSlider.setRange (0.0, 100.0, 1.0);
    glowSlider.setValue (theme::glow * 100.0, juce::dontSendNotification);
    glowSlider.setColour (juce::Slider::trackColourId, theme::accent);
    glowSlider.onValueChange = [this] { theme::setGlow ((float) glowSlider.getValue() * 0.01f); fireChanged(); };
    addAndMakeVisible (glowSlider);

    refreshChips();
}

void ThemePanel::fireChanged() { if (onChanged) onChanged(); repaint(); }

void ThemePanel::refreshChips()
{
    auto chip = [] (juce::TextButton& b, juce::Colour c)
    {
        b.setColour (juce::TextButton::buttonColourId, c);
        b.setColour (juce::TextButton::textColourOffId, c.contrasting (0.9f));
    };
    chip (primaryChip,   theme::accent);
    chip (secondaryChip, theme::accentWarm);
    chip (bgChip,        theme::panelLight);
    resetBtn.setColour (juce::TextButton::buttonColourId, theme::panel);
    resetBtn.setColour (juce::TextButton::textColourOffId, theme::textDim);
}

void ThemePanel::openPicker (juce::Colour current, std::function<void (juce::Colour)> apply,
                             juce::Component* anchor)
{
    activeApply = std::move (apply);
    auto sel = std::make_unique<juce::ColourSelector> (
        juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
        | juce::ColourSelector::showColourspace);
    sel->setCurrentColour (current);
    sel->setSize (260, 300);
    sel->addChangeListener (this);
    juce::CallOutBox::launchAsynchronously (std::move (sel),
        anchor->getScreenBounds(), nullptr);
}

void ThemePanel::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    if (auto* cs = dynamic_cast<juce::ColourSelector*> (src))
    {
        if (activeApply) activeApply (cs->getCurrentColour());
        refreshChips();
        fireChanged();
    }
}

void ThemePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg.withAlpha (0.985f));
    g.setColour (theme::outline);
    g.drawRect (getLocalBounds(), 1);

    // header underline
    g.setColour (theme::accent);
    g.fillRect (0, 44, getWidth(), 2);

    // a live preview swatch strip of the current neon gradient
    auto preview = getLocalBounds().reduced (16).removeFromBottom (36);
    g.setGradientFill (theme::neonGradient (preview.toFloat()));
    g.fillRoundedRectangle (preview.toFloat(), 8.0f);
    g.setColour (theme::bg.withAlpha (0.85f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("LIVE PREVIEW", preview, juce::Justification::centred);
}

void ThemePanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (34);
    titleLabel.setBounds (header.removeFromLeft (160));
    closeBtn.setBounds (header.removeFromRight (80));
    r.removeFromTop (16);

    schemeLabel.setBounds (r.removeFromTop (16));
    r.removeFromTop (4);
    // scheme buttons in a 2-column grid
    {
        const int cols = 2, rows = (int) ((schemeButtons.size() + 1) / 2);
        auto grid = r.removeFromTop (rows * 32);
        for (int i = 0; i < (int) schemeButtons.size(); ++i)
        {
            const int row = i / cols, coli = i % cols;
            const int cw = grid.getWidth() / cols;
            schemeButtons[(size_t) i]->setBounds (grid.getX() + coli * cw + 3,
                                                  grid.getY() + row * 32 + 3,
                                                  cw - 6, 26);
        }
    }
    r.removeFromTop (12);

    colourLabel.setBounds (r.removeFromTop (16));
    r.removeFromTop (4);
    {
        auto row = r.removeFromTop (30);
        const int cw = row.getWidth() / 3;
        primaryChip.setBounds   (row.removeFromLeft (cw).reduced (3, 2));
        secondaryChip.setBounds (row.removeFromLeft (cw).reduced (3, 2));
        bgChip.setBounds        (row.reduced (3, 2));
    }
    r.removeFromTop (6);
    resetBtn.setBounds (r.removeFromTop (28).reduced (3, 2));
    r.removeFromTop (12);

    glowLabel.setBounds (r.removeFromTop (16));
    glowSlider.setBounds (r.removeFromTop (28));
}
} // namespace vf
