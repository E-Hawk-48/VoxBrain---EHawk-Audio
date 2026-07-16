#include "PitchDisplay.h"
#include <cmath>

namespace vf
{
PitchDisplay::PitchDisplay (RetuneEngine& e) : retune (e)
{
    for (int i = 0; i < histLen; ++i) { inHist[i] = 0.0f; tgtHist[i] = 0.0f; }
    startTimerHz (30);
}
PitchDisplay::~PitchDisplay() { stopTimer(); }

juce::String PitchDisplay::noteName (float hz, int& centsOut)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    if (hz <= 0.0f) { centsOut = 0; return "—"; }
    const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    const int   note = (int) std::round (midi);
    centsOut = (int) std::round ((midi - (float) note) * 100.0f);
    const int pc = ((note % 12) + 12) % 12;
    const int oct = note / 12 - 1;
    return juce::String (names[pc]) + juce::String (oct);
}

float PitchDisplay::hzToY (float hz, juce::Rectangle<float> area) const
{
    const float midi = 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, hz) / 440.0f);
    const float t = juce::jlimit (0.0f, 1.0f, (midi - midiLo) / (midiHi - midiLo));
    return area.getBottom() - t * area.getHeight();
}

void PitchDisplay::timerCallback()
{
    inHist[head]  = retune.getInputPitchHz();
    tgtHist[head] = retune.getTargetPitchHz();
    head = (head + 1) % histLen;
    repaint();
}

void PitchDisplay::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (full, 8.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (full.reduced (0.5f), 8.0f, 1.0f);

    auto area = full.reduced (10.0f, 8.0f);
    auto graph = area.withTrimmedRight (86.0f);   // reserve right column for read-out

    // --- semitone grid + note labels (label each C) ---
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    for (int m = (int) midiLo; m <= (int) midiHi; ++m)
    {
        const float y = hzToY (440.0f * std::pow (2.0f, (m - 69) / 12.0f), graph);
        const int pc = ((m % 12) + 12) % 12;
        const bool isC = pc == 0;
        g.setColour (theme::outline.withAlpha (isC ? 0.9f : 0.28f));
        g.drawHorizontalLine ((int) y, graph.getX(), graph.getRight());
        if (isC)
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (juce::String (names[pc]) + juce::String (m / 12 - 1),
                        graph.getX() + 2.0f, y - 10.0f, 26.0f, 10.0f,
                        juce::Justification::topLeft);
        }
    }

    // --- pitch traces (oldest → newest left → right) ---
    const float w = graph.getWidth();
    auto drawTrace = [&] (const float* hist, juce::Colour colour, float thick)
    {
        g.setColour (colour);
        juce::Path p; bool pen = false;
        for (int i = 0; i < histLen; ++i)
        {
            const int idx = (head + i) % histLen;    // chronological
            const float hz = hist[idx];
            const float x = graph.getX() + w * (float) i / (float) (histLen - 1);
            if (hz > 0.0f)
            {
                const float y = hzToY (hz, graph);
                if (! pen) { p.startNewSubPath (x, y); pen = true; }
                else        p.lineTo (x, y);
            }
            else pen = false;   // gap on unvoiced
        }
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved));
    };

    drawTrace (inHist,  theme::textDim.withAlpha (0.85f), 1.4f);   // detected
    drawTrace (tgtHist, theme::accent,                    2.2f);   // corrected

    // --- current-note read-out (right column) ---
    const float curTgt = tgtHist[(head - 1 + histLen) % histLen];
    const float curIn  = inHist[(head - 1 + histLen) % histLen];
    int cents = 0;
    const juce::String note = noteName (curTgt > 0.0f ? curTgt : curIn, cents);

    auto rc = juce::Rectangle<float> (graph.getRight() + 8.0f, area.getY(),
                                      78.0f, area.getHeight());
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText (note, rc.removeFromTop (40.0f), juce::Justification::centred);
    g.setColour (curTgt > 0.0f ? theme::accentGreen : theme::textDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (curTgt > 0.0f ? "TUNED" : "—", rc.removeFromTop (14.0f), juce::Justification::centred);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (curIn > 0.0f ? juce::String (curIn, 1) + " Hz" : "no pitch",
                rc.removeFromTop (14.0f), juce::Justification::centred);

    // Title
    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("PITCH", full.reduced (10.0f, 6.0f).removeFromTop (12.0f),
                juce::Justification::topRight);
}
} // namespace vf
