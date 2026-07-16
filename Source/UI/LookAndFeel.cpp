#include "LookAndFeel.h"

namespace vf
{
VoxBrainLookAndFeel::VoxBrainLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::Slider::textBoxTextColourId,     theme::text);
    setColour (juce::Slider::textBoxOutlineColourId,  juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,             theme::text);
    setColour (juce::TextEditor::backgroundColourId,  theme::panel);
    setColour (juce::TextEditor::textColourId,        theme::text);
    setColour (juce::TextEditor::outlineColourId,     theme::outline);
    setColour (juce::TextButton::textColourOffId,     theme::text);
    setColour (juce::TextButton::textColourOnId,      theme::bg);
    setColour (juce::PopupMenu::backgroundColourId,   theme::panel);
    setColour (juce::PopupMenu::textColourId,         theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.30f));
    setColour (juce::ComboBox::backgroundColourId,    theme::panel);
    setColour (juce::ComboBox::textColourId,          theme::text);
    setColour (juce::ComboBox::outlineColourId,       theme::outline);
    setColour (juce::ComboBox::arrowColourId,         theme::accent);
}

void VoxBrainLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                              float sliderPos, float startAngle, float endAngle,
                                              juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre  = bounds.getCentre();
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);
    const float arcR   = radius - 3.0f;
    const float lineW  = juce::jmax (2.5f, radius * 0.15f);
    const auto col     = slider.isEnabled() ? theme::accent : theme::textDim;

    // Recessed track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::outline.withAlpha (0.9f));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc — soft glow underlay + gradient stroke
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (col.withAlpha (0.22f));
    g.strokePath (value, juce::PathStrokeType (lineW + 5.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setGradientFill (juce::ColourGradient (col.brighter (0.25f), bounds.getTopLeft(),
                                             col.darker (0.15f),   bounds.getBottomRight(), false));
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Domed centre cap (radial gradient)
    const float capR = radius * 0.62f;
    auto capBounds = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);
    juce::ColourGradient dome (theme::panelLight.brighter (0.10f),
                               centre.x, centre.y - capR * 0.5f,
                               theme::panel.darker (0.25f),
                               centre.x, centre.y + capR, true);
    g.setGradientFill (dome);
    g.fillEllipse (capBounds);
    g.setColour (theme::outline.withAlpha (0.8f));
    g.drawEllipse (capBounds, 1.0f);

    // Pointer + glowing tip
    juce::Path pointer;
    pointer.addRoundedRectangle (-lineW * 0.35f, -radius * 0.52f, lineW * 0.7f, radius * 0.34f, lineW * 0.3f);
    g.setColour (theme::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    const auto tip = centre.getPointOnCircumference (radius * 0.5f, angle);
    g.setColour (col.withAlpha (0.9f));
    g.fillEllipse (juce::Rectangle<float> (lineW * 1.1f, lineW * 1.1f).withCentre (tip));
}

void VoxBrainLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                              bool highlighted, bool)
{
    const auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const float rad = bounds.getHeight() * 0.5f;

    if (on)
    {
        g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.34f), bounds.getTopLeft(),
                                                 theme::accent.withAlpha (0.16f), bounds.getBottomLeft(), false));
        g.fillRoundedRectangle (bounds, rad);
    }
    else
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, rad);
    }
    g.setColour (on ? theme::accent : (highlighted ? theme::textDim : theme::outline));
    g.drawRoundedRectangle (bounds, rad, 1.2f);

    g.setColour (on ? theme::accent : theme::textDim);
    g.setFont (juce::FontOptions (juce::jmin (13.0f, bounds.getHeight() * 0.6f), juce::Font::bold));
    g.drawText (b.getButtonText(), bounds, juce::Justification::centred);
}

void VoxBrainLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                  const juce::Colour& backgroundColour,
                                                  bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);
    auto col = backgroundColour;
    if (down) col = col.brighter (0.18f);
    else if (highlighted) col = col.brighter (0.10f);

    // Glassy vertical gradient + top highlight
    g.setGradientFill (juce::ColourGradient (col.brighter (0.08f), bounds.getTopLeft(),
                                             col.darker (0.14f),   bounds.getBottomLeft(), false));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (bounds.getX() + 6.0f, bounds.getY() + 1.5f,
                bounds.getRight() - 6.0f, bounds.getY() + 1.5f, 1.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

juce::Font VoxBrainLookAndFeel::getLabelFont (juce::Label& l)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) l.getHeight() - 2.0f)));
}
} // namespace vf
