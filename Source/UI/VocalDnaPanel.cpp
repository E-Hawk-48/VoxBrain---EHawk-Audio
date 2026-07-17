#include "VocalDnaPanel.h"
#include <cmath>

namespace vf
{
namespace
{
    const char* kLabels[VocalDnaPanel::kAxes] =
        { "TUNING", "STABLE", "WARMTH", "PRESENCE", "AIR",
          "BRIGHT", "DYNAMIC", "SIBILANCE", "HARSH", "MUD" };

    // Outward = "more". The last three are problem axes (more = worse) and get
    // the warm/red accent so they stand out from the quality axes.
    const bool kProblem[VocalDnaPanel::kAxes] =
        { false, false, false, false, false, false, false, true, true, true };
}

VocalDnaPanel::VocalDnaPanel (AnalysisEngine& engine)
    : analysis (engine)
{
    setInterceptsMouseClicks (false, false);
    shown = extract (analysis.getLiveDNA());
    startTimerHz (24);
}

VocalDnaPanel::~VocalDnaPanel() { stopTimer(); }

std::array<float, VocalDnaPanel::kAxes> VocalDnaPanel::extract (const VocalDNA& d)
{
    return { d.tuning, d.stability, d.warmth, d.presence, d.air,
             d.brightness, d.dynamics, d.sibilance, d.harshness, d.muddiness };
}

void VocalDnaPanel::timerCallback()
{
    const auto target = extract (analysis.getLiveDNA());
    for (int i = 0; i < kAxes; ++i)
        shown[(size_t) i] += 0.2f * (target[(size_t) i] - shown[(size_t) i]);
    repaint();
}

void VocalDnaPanel::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // header
    auto header = area.removeFromTop (22.0f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("VOCAL DNA", header.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("live", header.reduced (4.0f, 0.0f), juce::Justification::centredRight);

    const float cx = area.getCentreX();
    const float cy = area.getCentreY() + 4.0f;
    const float radius = juce::jmax (12.0f, juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 30.0f);

    auto pt = [cx, cy] (int i, float rr)
    {
        const float ang = -juce::MathConstants<float>::halfPi
                        + (float) i * juce::MathConstants<float>::twoPi / (float) kAxes;
        return juce::Point<float> (cx + std::cos (ang) * rr, cy + std::sin (ang) * rr);
    };

    // concentric grid decagons
    for (int ring = 1; ring <= 4; ++ring)
    {
        const float rr = radius * (float) ring / 4.0f;
        juce::Path p;
        for (int i = 0; i < kAxes; ++i)
        {
            const auto q = pt (i, rr);
            if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
        }
        p.closeSubPath();
        g.setColour (theme::outline.withAlpha (ring == 4 ? 0.55f : 0.28f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // spokes + axis labels
    for (int i = 0; i < kAxes; ++i)
    {
        const auto outer = pt (i, radius);
        g.setColour (theme::outline.withAlpha (0.35f));
        g.drawLine (cx, cy, outer.x, outer.y, 1.0f);

        const auto lab = pt (i, radius + 15.0f);
        g.setColour (kProblem[i] ? theme::accentWarm.withAlpha (0.9f) : theme::textDim);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText (kLabels[i], juce::Rectangle<float> (lab.x - 42.0f, lab.y - 8.0f, 84.0f, 16.0f),
                    juce::Justification::centred);
    }

    // the live DNA shape
    juce::Path shape;
    for (int i = 0; i < kAxes; ++i)
    {
        const auto q = pt (i, radius * juce::jlimit (0.02f, 1.0f, shown[(size_t) i]));
        if (i == 0) shape.startNewSubPath (q); else shape.lineTo (q);
    }
    shape.closeSubPath();

    const juce::Rectangle<float> gbox (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    g.setGradientFill (theme::neonGradient (gbox));
    g.setOpacity (0.20f);
    g.fillPath (shape);
    g.setOpacity (1.0f);

    // neon glow: stack a few translucent strokes, scaled by the theme glow amount
    const float glow = juce::jlimit (0.15f, 1.0f, theme::glow);
    for (int pass = 0; pass < 3; ++pass)
    {
        const float w = 1.5f + (float) pass * 2.6f;
        const float a = (0.5f - (float) pass * 0.14f) * glow;
        g.setColour (theme::accent.withAlpha (a));
        g.strokePath (shape, juce::PathStrokeType (w));
    }
    g.setColour (theme::accentWarm);
    g.strokePath (shape, juce::PathStrokeType (1.4f));

    // vertices
    for (int i = 0; i < kAxes; ++i)
    {
        const auto q = pt (i, radius * juce::jlimit (0.02f, 1.0f, shown[(size_t) i]));
        const bool hot = kProblem[i] && shown[(size_t) i] > 0.55f;
        g.setColour (hot ? theme::accentWarm : theme::accent);
        g.fillEllipse (q.x - 2.6f, q.y - 2.6f, 5.2f, 5.2f);
    }
}
} // namespace vf
