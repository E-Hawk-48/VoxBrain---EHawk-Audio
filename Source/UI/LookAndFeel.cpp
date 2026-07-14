#include "LookAndFeel.h"

namespace vf
{
VocalForgeLookAndFeel::VocalForgeLookAndFeel()
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
}

void VocalForgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                              float sliderPos, float startAngle, float endAngle,
                                              juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre  = bounds.getCentre();
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);
    const float arcR   = radius - 3.0f;
    const float lineW  = juce::jmax (2.0f, radius * 0.14f);

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::outline);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc with subtle glow
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    const auto col = slider.isEnabled() ? theme::accent : theme::textDim;
    g.setColour (col.withAlpha (0.35f));
    g.strokePath (value, juce::PathStrokeType (lineW + 3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (col);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Centre cap
    g.setColour (theme::panelLight);
    g.fillEllipse (juce::Rectangle<float> (radius * 0.9f, radius * 0.9f).withCentre (centre));

    // Pointer
    juce::Path pointer;
    pointer.addRoundedRectangle (-lineW * 0.4f, -radius * 0.45f, lineW * 0.8f, radius * 0.32f, lineW * 0.3f);
    g.setColour (theme::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void VocalForgeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                              bool highlighted, bool)
{
    const auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    g.setColour (on ? theme::accent.withAlpha (0.22f) : theme::panel);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    g.setColour (on ? theme::accent : (highlighted ? theme::textDim : theme::outline));
    g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, 1.2f);

    g.setColour (on ? theme::accent : theme::textDim);
    g.setFont (juce::FontOptions (juce::jmin (13.0f, bounds.getHeight() * 0.6f),
                                  juce::Font::bold));
    g.drawText (b.getButtonText(), bounds, juce::Justification::centred);
}

void VocalForgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                  const juce::Colour& backgroundColour,
                                                  bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);
    auto col = backgroundColour;
    if (down) col = col.brighter (0.2f);
    else if (highlighted) col = col.brighter (0.1f);

    g.setColour (col);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

juce::Font VocalForgeLookAndFeel::getLabelFont (juce::Label& l)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) l.getHeight() - 2.0f)));
}
} // namespace vf
