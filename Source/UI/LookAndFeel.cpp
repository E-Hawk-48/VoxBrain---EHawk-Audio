#include "LookAndFeel.h"

namespace vf
{
namespace theme
{
    namespace { juce::String g_scheme = "Neon Red / Purple"; }

    const std::vector<Scheme>& schemes()
    {
        static const std::vector<Scheme> s = {
            { "Neon Red / Purple", 0xff0a0612, 0xff150d24, 0xff231640, 0xff3d2668, 0xffb14dff, 0xffff1e56, 0xff2be0a6, 0xfff3ecff, 0xff9182b3 },
            { "Ultraviolet",       0xff08061a, 0xff100d2b, 0xff1a1640, 0xff2e2466, 0xff7d4dff, 0xff4d9bff, 0xff2be0a6, 0xffecebff, 0xff8a82b3 },
            { "Vaporwave",         0xff0d0714, 0xff1a0f24, 0xff2b1840, 0xff4d2466, 0xffff4dd2, 0xff4dd2ff, 0xff8dff2b, 0xffffeafc, 0xffb382a8 },
            { "Cyber Blue",        0xff060a14, 0xff0d1524, 0xff162336, 0xff244066, 0xff30c8ff, 0xff5d7bff, 0xff2be0a6, 0xffe6f2ff, 0xff7f93b3 },
            { "Inferno",           0xff140806, 0xff24100d, 0xff3a1a16, 0xff66302a, 0xffff3b1e, 0xffffab2d, 0xffffd24d, 0xffffeae6, 0xffb39182 },
            { "Toxic",             0xff070f08, 0xff0e1a10, 0xff16281a, 0xff244d2c, 0xff8dff2b, 0xff2be0a6, 0xffd0ff4d, 0xffecffe9, 0xff82b394 },
            { "Sunset",            0xff130a08, 0xff241410, 0xff3a2118, 0xff66402a, 0xffff7847, 0xffff2d7a, 0xffffd24d, 0xfffff0e9, 0xffb39a82 },
            { "Mono",              0xff0c0c0f, 0xff16161b, 0xff232329, 0xff3a3a44, 0xffe0e0ea, 0xffff5577, 0xff8fe0b0, 0xfff2f2f7, 0xff8a8a99 },
            // ---- holographic-glass designer gallery ----
            { "Holographic",       0xff060a12, 0xff0d1524, 0xff17233d, 0xff2a3f66, 0xff4de8ff, 0xffff5df0, 0xff5dffc8, 0xffeaf6ff, 0xff8298b3 },
            { "Aurora",            0xff050f0d, 0xff0c1a17, 0xff163028, 0xff245040, 0xff2bffb0, 0xff7d6bff, 0xffb6ff5d, 0xffe9fff6, 0xff82b3a2 },
            { "Sapphire",          0xff060814, 0xff0d1024, 0xff181d40, 0xff283066, 0xff5d7bff, 0xff30c8ff, 0xff2be0a6, 0xffe9ecff, 0xff8288b3 },
            { "Rose Gold",         0xff140f0e, 0xff241a18, 0xff3a2a26, 0xff664a42, 0xffff9d8a, 0xffffcf7a, 0xffe0c8a0, 0xfffff2ec, 0xffb39a90 },
            { "Miami",             0xff0d0714, 0xff1a0f24, 0xff2b1840, 0xff4d2466, 0xff2bd4ff, 0xffff3d9a, 0xff8dff2b, 0xffeafcff, 0xff9a82b3 },
            { "Obsidian",          0xff050506, 0xff0e0e12, 0xff1a1a20, 0xff2c2c36, 0xffb14dff, 0xffff1e56, 0xff2be0a6, 0xfff0f0f5, 0xff80808f }
        };
        return s;
    }

    void applyScheme (const Scheme& s)
    {
        bg = juce::Colour (s.bg);           panel = juce::Colour (s.panel);
        panelLight = juce::Colour (s.panelLight); outline = juce::Colour (s.outline);
        accent = juce::Colour (s.accent);   accentWarm = juce::Colour (s.accentWarm);
        accentGreen = juce::Colour (s.accentGreen);
        text = juce::Colour (s.text);       textDim = juce::Colour (s.textDim);
        g_scheme = s.name;
    }

    bool applyScheme (const juce::String& name)
    {
        for (const auto& s : schemes())
            if (s.name == name) { applyScheme (s); return true; }
        return false;
    }

    juce::String currentSchemeName() { return g_scheme; }

    void setPrimary   (juce::Colour c) { accent = c;     g_scheme = "Custom"; }
    void setSecondary (juce::Colour c) { accentWarm = c; g_scheme = "Custom"; }
    void setGlow      (float a)        { glow = juce::jlimit (0.0f, 1.0f, a); }

    void setBackground (juce::Colour c)
    {
        bg         = c;
        panel      = c.brighter (0.10f).withMultipliedSaturation (1.05f);
        panelLight = c.brighter (0.22f).withMultipliedSaturation (1.05f);
        outline    = c.interpolatedWith (accent, 0.30f).brighter (0.10f);
        g_scheme   = "Custom";
    }

    juce::ColourGradient neonGradient (juce::Rectangle<float> a)
    {
        return juce::ColourGradient (accent, a.getTopLeft(), accentWarm, a.getBottomRight(), false);
    }

    std::unique_ptr<juce::XmlElement> toXml()
    {
        auto e = std::make_unique<juce::XmlElement> ("Theme");
        e->setAttribute ("scheme", g_scheme);
        auto hex = [] (juce::Colour c) { return c.toDisplayString (true); };
        e->setAttribute ("bg", hex (bg));                 e->setAttribute ("panel", hex (panel));
        e->setAttribute ("panelLight", hex (panelLight)); e->setAttribute ("outline", hex (outline));
        e->setAttribute ("accent", hex (accent));         e->setAttribute ("accentWarm", hex (accentWarm));
        e->setAttribute ("accentGreen", hex (accentGreen));
        e->setAttribute ("text", hex (text));             e->setAttribute ("textDim", hex (textDim));
        e->setAttribute ("glow", glow);
        return e;
    }

    void fromXml (const juce::XmlElement* e)
    {
        if (e == nullptr || ! e->hasTagName ("Theme")) return;
        auto col = [&e] (const char* k, juce::Colour def)
        {
            const auto s = e->getStringAttribute (k);
            return s.isNotEmpty() ? juce::Colour::fromString (s) : def;   // "aarrggbb"
        };
        bg = col ("bg", bg);                 panel = col ("panel", panel);
        panelLight = col ("panelLight", panelLight); outline = col ("outline", outline);
        accent = col ("accent", accent);     accentWarm = col ("accentWarm", accentWarm);
        accentGreen = col ("accentGreen", accentGreen);
        text = col ("text", text);           textDim = col ("textDim", textDim);
        glow = (float) e->getDoubleAttribute ("glow", glow);
        g_scheme = e->getStringAttribute ("scheme", g_scheme);
    }

    juce::File settingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("VoxBrain").getChildFile ("theme.xml");
    }
    void load()
    {
        if (auto xml = juce::XmlDocument::parse (settingsFile())) fromXml (xml.get());
    }
    void save()
    {
        auto f = settingsFile();
        f.getParentDirectory().createDirectory();
        if (auto xml = toXml()) xml->writeTo (f, {});
    }
} // namespace theme

// ============================================================================
//  VoxBrainLookAndFeel
// ============================================================================
VoxBrainLookAndFeel::VoxBrainLookAndFeel() { refreshColours(); }

void VoxBrainLookAndFeel::refreshColours()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::Slider::textBoxTextColourId,     theme::text);
    setColour (juce::Slider::textBoxOutlineColourId,  juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,             theme::text);
    setColour (juce::TextEditor::backgroundColourId,  theme::panel);
    setColour (juce::TextEditor::textColourId,        theme::text);
    setColour (juce::TextEditor::outlineColourId,     theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    setColour (juce::TextButton::textColourOffId,     theme::text);
    setColour (juce::TextButton::textColourOnId,      theme::bg);
    setColour (juce::PopupMenu::backgroundColourId,   theme::panel);
    setColour (juce::PopupMenu::textColourId,         theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.35f));
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
    const float lineW  = juce::jmax (2.5f, radius * 0.16f);
    const bool  en     = slider.isEnabled();

    // Recessed track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::outline.withAlpha (0.9f));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc — neon glow (layered) + purple→red gradient stroke
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    if (en && theme::glow > 0.0f)
    {
        g.setColour (theme::accent.withAlpha (0.16f * theme::glow));
        g.strokePath (value, juce::PathStrokeType (lineW + 9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::accentWarm.withAlpha (0.14f * theme::glow));
        g.strokePath (value, juce::PathStrokeType (lineW + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setGradientFill (theme::neonGradient (bounds));
    if (! en) g.setColour (theme::textDim);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Domed centre cap (radial gradient)
    const float capR = radius * 0.62f;
    auto capBounds = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);
    juce::ColourGradient dome (theme::panelLight.brighter (0.10f), centre.x, centre.y - capR * 0.5f,
                               theme::bg.darker (0.20f), centre.x, centre.y + capR, true);
    g.setGradientFill (dome);
    g.fillEllipse (capBounds);
    g.setColour (theme::outline.withAlpha (0.85f));
    g.drawEllipse (capBounds, 1.0f);

    // Pointer + glowing tip
    juce::Path pointer;
    pointer.addRoundedRectangle (-lineW * 0.32f, -radius * 0.54f, lineW * 0.64f, radius * 0.34f, lineW * 0.3f);
    g.setColour (theme::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    const auto tip = centre.getPointOnCircumference (radius * 0.5f, angle);
    if (en && theme::glow > 0.0f)
    {
        g.setColour (theme::accent.withAlpha (0.5f * theme::glow));
        g.fillEllipse (juce::Rectangle<float> (lineW * 2.4f, lineW * 2.4f).withCentre (tip));
    }
    g.setColour (en ? theme::accentWarm : theme::textDim);
    g.fillEllipse (juce::Rectangle<float> (lineW * 1.1f, lineW * 1.1f).withCentre (tip));
}

void VoxBrainLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                              bool highlighted, bool)
{
    const auto bounds = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    const float rad = bounds.getHeight() * 0.5f;

    if (on)
    {
        if (theme::glow > 0.0f)
        {
            g.setColour (theme::accent.withAlpha (0.28f * theme::glow));
            g.fillRoundedRectangle (bounds.expanded (2.5f), rad + 2.5f);
        }
        g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.42f), bounds.getTopLeft(),
                                                 theme::accentWarm.withAlpha (0.30f), bounds.getBottomRight(), false));
        g.fillRoundedRectangle (bounds, rad);
    }
    else
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, rad);
    }
    g.setColour (on ? theme::accent : (highlighted ? theme::textDim : theme::outline));
    g.drawRoundedRectangle (bounds, rad, on ? 1.4f : 1.1f);

    g.setColour (on ? theme::text : theme::textDim);
    g.setFont (juce::FontOptions (juce::jmin (13.0f, bounds.getHeight() * 0.58f), juce::Font::bold));
    g.drawText (b.getButtonText(), bounds, juce::Justification::centred);
}

void VoxBrainLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                  const juce::Colour& backgroundColour,
                                                  bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (1.5f);
    auto col = backgroundColour;
    if (down) col = col.brighter (0.20f);
    else if (highlighted) col = col.brighter (0.12f);

    // Neon glow when the button is an accent colour and hovered
    if (highlighted && theme::glow > 0.0f)
    {
        g.setColour (col.withAlpha (0.35f * theme::glow));
        g.fillRoundedRectangle (bounds.expanded (2.5f), 9.0f);
    }

    g.setGradientFill (juce::ColourGradient (col.brighter (0.10f), bounds.getTopLeft(),
                                             col.darker (0.16f),   bounds.getBottomLeft(), false));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawLine (bounds.getX() + 6.0f, bounds.getY() + 1.5f, bounds.getRight() - 6.0f, bounds.getY() + 1.5f, 1.0f);
    g.setColour (col.brighter (0.3f).withAlpha (0.5f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

juce::Font VoxBrainLookAndFeel::getLabelFont (juce::Label& l)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) l.getHeight() - 2.0f)));
}
} // namespace vf
