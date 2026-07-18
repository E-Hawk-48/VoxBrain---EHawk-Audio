#include "ReferencePanel.h"
#include <cmath>
#include <vector>

namespace vf
{
namespace
{
    bool hasAudioExtension (const juce::String& path)
    {
        const auto e = path.fromLastOccurrenceOf (".", false, true).toLowerCase();
        return e == "wav" || e == "aif" || e == "aiff" || e == "flac"
            || e == "mp3" || e == "ogg" || e == "m4a" || e == "aac" || e == "wma";
    }

    float fracFor (double f, double lo, double hi)
    {
        return (float) juce::jlimit (0.0, 1.0,
            (std::log10 (juce::jmax (1.0, f)) - std::log10 (lo)) / (std::log10 (hi) - std::log10 (lo)));
    }

    juce::Colour confColour (float c)
    {
        if (c >= 0.75f) return juce::Colour (0xff2be0a6);   // green
        if (c >= 0.50f) return juce::Colour (0xffe8c15a);   // amber
        if (c >= 0.30f) return juce::Colour (0xffe8883a);   // orange
        return juce::Colour (0xffe0556b);                   // red
    }

    constexpr int kPad = 16;

    juce::TextLayout layoutText (const juce::String& s, float width, float fontH, juce::Colour col)
    {
        juce::AttributedString as;
        as.setText (s);
        as.setFont (juce::FontOptions (fontH));
        as.setColour (col);
        as.setJustification (juce::Justification::topLeft);
        juce::TextLayout tl;
        tl.createLayout (as, width);
        return tl;
    }
}

// ============================================================================
//  ReferenceReportView — scrollable decisions + rationale + confidence bars
// ============================================================================
void ReferenceReportView::setResult (const ReferenceResult& r)
{
    result = r; have = r.ok;
    decAccepted.assign (r.match.decisions.size(), (char) 0);
    insAccepted.assign (r.match.rackInserts.size(), (char) 0);
    decPill.clear(); insPill.clear();
    repaint();
}

void ReferenceReportView::markAllAccepted()
{
    std::fill (decAccepted.begin(), decAccepted.end(), (char) 1);
    std::fill (insAccepted.begin(), insAccepted.end(), (char) 1);
    repaint();
}

void ReferenceReportView::mouseDown (const juce::MouseEvent& e)
{
    const auto pt = e.getPosition();
    for (size_t i = 0; i < decPill.size(); ++i)
        if (decPill[i].contains (pt))
        {
            if (! decAccepted[i]) { decAccepted[i] = 1; if (onAcceptDecision) onAcceptDecision ((int) i); repaint(); }
            return;
        }
    for (size_t i = 0; i < insPill.size(); ++i)
        if (insPill[i].contains (pt))
        {
            if (! insAccepted[i]) { insAccepted[i] = 1; if (onAcceptInsert) onAcceptInsert ((int) i); repaint(); }
            return;
        }
}

int ReferenceReportView::preferredHeight (int width) const
{
    if (! have) return 200;
    const float tw = (float) (width - 2 * kPad);
    int y = kPad;
    y += (int) layoutText (result.match.summary, tw, 13.0f, theme::textDim).getHeight() + 14;
    y += 22;   // section header
    auto item = [&] (const juce::String& rationale)
    {
        y += 20 + 14;                                                   // title + bar
        y += (int) layoutText (rationale, tw, 12.5f, theme::text).getHeight() + 16;
    };
    for (const auto& d : result.match.decisions) item (d.rationale);
    if (! result.match.rackInserts.empty())
    {
        y += 24;
        for (const auto& ins : result.match.rackInserts) item (ins.rationale);
    }
    return y + kPad;
}

void ReferenceReportView::paint (juce::Graphics& g)
{
    if (! have) return;
    const int   W  = getWidth();
    const float tw = (float) (W - 2 * kPad);
    int y = kPad;

    {
        auto tl = layoutText (result.match.summary, tw, 13.0f, theme::textDim);
        tl.draw (g, juce::Rectangle<float> ((float) kPad, (float) y, tw, tl.getHeight()));
        y += (int) tl.getHeight() + 14;
    }

    auto drawItem = [&] (const juce::String& title, const juce::String& rationale, float conf, bool accepted) -> juce::Rectangle<int>
    {
        const int pillW = 92, pillX = W - kPad - pillW;
        const int cW = 46, cX = pillX - 8 - cW;

        g.setColour (theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (title, kPad, y, cX - kPad - 6, 18, juce::Justification::centredLeft);

        g.setColour (confColour (conf));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String ((int) std::round (conf * 100.0f)) + "%", cX, y, cW, 18, juce::Justification::centredRight);

        auto pill = juce::Rectangle<int> (pillX, y, pillW, 18);
        if (accepted)
        {
            g.setColour (theme::accentGreen.withAlpha (0.20f));
            g.fillRoundedRectangle (pill.toFloat(), 9.0f);
            g.setColour (theme::accentGreen);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 Accepted")), pill, juce::Justification::centred);
        }
        else
        {
            g.setColour (theme::accent.withAlpha (0.16f));
            g.fillRoundedRectangle (pill.toFloat(), 9.0f);
            g.setColour (theme::accent);
            g.drawRoundedRectangle (pill.toFloat(), 9.0f, 1.0f);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("Accept", pill, juce::Justification::centred);
        }
        y += 20;

        auto bar = juce::Rectangle<int> (kPad, y, W - 2 * kPad, 6);
        g.setColour (theme::panelLight);
        g.fillRoundedRectangle (bar.toFloat(), 3.0f);
        g.setColour (confColour (conf));
        g.fillRoundedRectangle (bar.withWidth ((int) (bar.getWidth() * juce::jlimit (0.0f, 1.0f, conf))).toFloat(), 3.0f);
        y += 14;

        auto tl = layoutText (rationale, tw, 12.5f, theme::text);
        tl.draw (g, juce::Rectangle<float> ((float) kPad, (float) y, tw, tl.getHeight()));
        y += (int) tl.getHeight() + 16;
        return pill;
    };

    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("SUGGESTED CHAIN  (click Accept on each, or Accept All)", kPad, y, W - 2 * kPad, 18, juce::Justification::centredLeft);
    y += 22;

    decPill.clear();
    for (size_t i = 0; i < result.match.decisions.size(); ++i)
    {
        const auto& d = result.match.decisions[i];
        decPill.push_back (drawItem (d.area, d.rationale, d.confidence, i < decAccepted.size() && decAccepted[i]));
    }

    if (! result.match.rackInserts.empty())
    {
        g.setColour (theme::accentWarm);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("COMPLEMENTARY MODULES", kPad, y, W - 2 * kPad, 18, juce::Justification::centredLeft);
        y += 24;
        insPill.clear();
        for (size_t i = 0; i < result.match.rackInserts.size(); ++i)
        {
            const auto& ins = result.match.rackInserts[i];
            insPill.push_back (drawItem (ins.name, ins.rationale, ins.confidence, i < insAccepted.size() && insAccepted[i]));
        }
    }
}

// ============================================================================
//  ReferencePanel
// ============================================================================
ReferencePanel::ReferencePanel()
{
    closeButton.onClick     = [this] { if (onClose)  onClose(); };
    browseButton.onClick    = [this] { if (onBrowse) onBrowse(); };
    anotherButton.onClick   = [this] { if (onBrowse) onBrowse(); };
    cancelButton.onClick    = [this] { if (onCancel) onCancel(); };
    acceptAllButton.onClick = [this] { if (onAcceptAll) onAcceptAll(); };
    compareButton.onClick   = [this] { if (onCompare) onCompare(); };
    undoButton.onClick      = [this] { if (onUndo) onUndo(); };
    saveButton.onClick      = [this] { if (onSavePreset) onSavePreset(); };
    shareButton.onClick     = [this] { if (onShare) onShare(); };

    acceptAllButton.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.85f));
    acceptAllButton.setTooltip ("Apply every suggestion at once (one Undo reverts it). Locked modules are left untouched.");
    compareButton.setTooltip ("A/B — flip between your original sound and the applied suggestions.");
    undoButton.setTooltip ("Undo the last applied suggestion.");
    saveButton.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.7f));
    saveButton.setTooltip ("Save this match (chain + rack + analysis metadata) as a preset — find it under PRESETS.");
    shareButton.setTooltip ("Share this match to the community marketplace.");

    addAndMakeVisible (closeButton);
    addChildComponent (browseButton);
    addChildComponent (cancelButton);
    addChildComponent (anotherButton);
    addChildComponent (acceptAllButton);
    addChildComponent (compareButton);
    addChildComponent (undoButton);
    addChildComponent (saveButton);
    addChildComponent (shareButton);

    reportView.onAcceptDecision = [this] (int i) { if (onAcceptDecision) onAcceptDecision (i); };
    reportView.onAcceptInsert   = [this] (int i) { if (onAcceptInsert)   onAcceptInsert (i); };

    reportViewport.setViewedComponent (&reportView, false);
    reportViewport.setScrollBarsShown (true, false);
    addChildComponent (reportViewport);

    applyView();
}

void ReferencePanel::setAnalyzing (const juce::String& fn, const juce::String& status, float p01)
{
    fileName = fn; statusText = status; progress = juce::jlimit (0.0f, 1.0f, p01);
    if (view != View::Analyzing) { view = View::Analyzing; applyView(); }
    repaint();
}

void ReferencePanel::showResult (const ReferenceResult& r)
{
    result = r; haveResult = r.ok;
    if (r.ok) { reportView.setResult (r); view = View::Result; }
    else      { statusText = r.error; view = View::DropZone; }
    applyView();
    repaint();
}

void ReferencePanel::showDropZone() { view = View::DropZone; applyView(); repaint(); }

void ReferencePanel::setCompareShowingOriginal (bool showingOriginal)
{
    compareButton.setButtonText (showingOriginal ? "A/B: Original" : "A/B: Suggested");
}

void ReferencePanel::setSaveStatus (const juce::String& text) { saveStatus = text; repaint(); }

void ReferencePanel::applyView()
{
    browseButton.setVisible   (view == View::DropZone);
    cancelButton.setVisible   (view == View::Analyzing);
    const bool result = (view == View::Result);
    anotherButton.setVisible  (result);
    acceptAllButton.setVisible (result);
    compareButton.setVisible  (result);
    undoButton.setVisible     (result);
    saveButton.setVisible     (result);
    shareButton.setVisible    (result);
    reportViewport.setVisible (result);
    if (! result) saveStatus.clear();
    resized();
}

bool ReferencePanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (hasAudioExtension (f)) return true;
    return false;
}

void ReferencePanel::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (hasAudioExtension (f)) { if (onFile) onFile (juce::File (f)); break; }
}

void ReferencePanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (44);
    closeButton.setBounds (header.removeFromRight (74).reduced (6, 8));
    if (view == View::Result)
    {
        header.removeFromRight (6);
        anotherButton.setBounds  (header.removeFromRight (128).reduced (4, 8));
        header.removeFromRight (10);
        acceptAllButton.setBounds (header.removeFromRight (98).reduced (4, 8));
        header.removeFromRight (6);
        compareButton.setBounds  (header.removeFromRight (110).reduced (4, 8));
        header.removeFromRight (6);
        undoButton.setBounds     (header.removeFromRight (62).reduced (4, 8));
    }

    if (view == View::Result)
    {
        auto body = area.reduced (14, 8);
        auto infoRow = body.removeFromTop (24);                    // preset name / confidence / save
        shareButton.setBounds (infoRow.removeFromRight (58).reduced (2, 1));
        infoRow.removeFromRight (6);
        saveButton.setBounds (infoRow.removeFromRight (100).reduced (2, 1));
        auto graphs = body.removeFromTop (juce::jlimit (150, 240, body.getHeight() * 45 / 100));
        graphsBounds = graphs;
        body.removeFromTop (8);
        reportViewport.setBounds (body);
        const int vw = body.getWidth() - 2;
        reportView.setSize (vw, reportView.preferredHeight (vw));
    }
    else if (view == View::Analyzing)
    {
        cancelButton.setBounds (getLocalBounds().withSizeKeepingCentre (120, 32).translated (0, 70));
    }
    else
    {
        browseButton.setBounds (getLocalBounds().withSizeKeepingCentre (150, 38).translated (0, 30));
    }
}

void ReferencePanel::paint (juce::Graphics& g)
{
    g.setColour (theme::bg);
    g.fillAll();

    auto hb = getLocalBounds().removeFromTop (44);
    g.setColour (theme::panel);
    g.fillRect (hb);
    g.setColour (theme::accent);
    g.fillRect (0, 43, getWidth(), 2);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("AI REFERENCE MIX ANALYZER", 16, 0, getWidth() - 220, 44, juce::Justification::centredLeft);

    auto body = getLocalBounds();
    body.removeFromTop (44);

    if (view == View::DropZone)       drawDropZone  (g, body.reduced (26));
    else if (view == View::Analyzing) drawAnalyzing (g, body.reduced (26));
    else
    {
        const int W = getWidth();
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (result.match.presetName, juce::Rectangle<int> (28, 50, W / 2, 22), juce::Justification::centredLeft);

        // middle slot: a transient Save/Share status (green) or the confidence line.
        auto mid = juce::Rectangle<int> (W / 2, 50, W / 2 - 190, 22);
        if (saveStatus.isNotEmpty())
        {
            g.setColour (theme::accentGreen);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (saveStatus, mid, juce::Justification::centredRight);
        }
        else
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText ("Overall confidence " + juce::String ((int) std::round (result.match.overallConfidence * 100.0f))
                        + "%   •   " + result.fileName, mid, juce::Justification::centredRight);
        }

        auto gr = graphsBounds;
        auto freq = gr.removeFromLeft (gr.getWidth() * 58 / 100);
        gr.removeFromLeft (10);
        drawFrequencyGraph (g, freq);
        drawGauges (g, gr);
    }
}

void ReferencePanel::drawDropZone (juce::Graphics& g, juce::Rectangle<int> r)
{
    juce::Path p;
    p.addRoundedRectangle (r.toFloat().reduced (2.0f), 14.0f);
    juce::Path dashed;
    const float dl[] = { 9.0f, 6.0f };
    juce::PathStrokeType (2.0f).createDashedStroke (dashed, p, dl, 2);
    g.setColour (theme::outline);
    g.fillPath (dashed);

    auto centre = r.reduced (30);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText ("Drag an audio reference here",
                centre.removeFromTop (centre.getHeight() / 2), juce::Justification::centredBottom);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText ("A full song, acapella or vocal snippet. The AI reverse-engineers its vocal "
                      "production and suggests an editable chain — your own voice, performance and pitch are never changed.",
                      centre.removeFromTop (48).reduced (20, 0), juce::Justification::centredTop, 3);

    if (statusText.isNotEmpty())
    {
        g.setColour (theme::accentWarm);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (statusText, r.removeFromBottom (56).removeFromTop (22), juce::Justification::centred);
    }
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("WAV  \xc2\xb7  AIFF  \xc2\xb7  FLAC  \xc2\xb7  MP3  \xc2\xb7  OGG  \xc2\xb7  AAC / M4A",
                r.removeFromBottom (26), juce::Justification::centred);
}

void ReferencePanel::drawAnalyzing (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("Analyzing \"" + fileName + "\"", r.removeFromTop (r.getHeight() / 2), juce::Justification::centredBottom);

    auto bar = r.withSizeKeepingCentre (juce::jmin (440, r.getWidth() - 40), 12).translated (0, -10);
    g.setColour (theme::panelLight);
    g.fillRoundedRectangle (bar.toFloat(), 6.0f);
    g.setColour (theme::accent);
    g.fillRoundedRectangle (bar.withWidth ((int) (bar.getWidth() * progress)).toFloat(), 6.0f);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (statusText, bar.translated (0, 26), juce::Justification::centred);
}

void ReferencePanel::drawFrequencyGraph (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r.toFloat(), 8.0f, 1.0f);

    auto plot = r.reduced (10, 14);
    plot.removeFromTop (2);
    const double lo = 20.0, hi = juce::jmin (20000.0, result.profile.sampleRate * 0.5);

    // frequency gridlines
    for (double f : { 100.0, 1000.0, 10000.0 })
    {
        const int x = plot.getX() + (int) (fracFor (f, lo, hi) * plot.getWidth());
        g.setColour (theme::outline.withAlpha (0.4f));
        g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                    x - 14, plot.getBottom(), 28, 11, juce::Justification::centred);
    }

    // spectrum curve
    const int NP = ReferenceProfile::kSpectrumPoints;
    juce::Path curve;
    for (int i = 0; i < NP; ++i)
    {
        const float x = plot.getX() + (float) i / (NP - 1) * plot.getWidth();
        const float v = juce::jlimit (0.0f, 1.0f, result.profile.spectrumCurve[(size_t) i]);
        const float y = plot.getBottom() - v * plot.getHeight();
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    juce::Path fill = curve;
    fill.lineTo ((float) plot.getRight(), (float) plot.getBottom());
    fill.lineTo ((float) plot.getX(), (float) plot.getBottom());
    fill.closeSubPath();
    g.setGradientFill (theme::neonGradient (plot.toFloat()));
    g.setOpacity (0.22f);
    g.fillPath (fill);
    g.setOpacity (1.0f);
    g.setColour (theme::accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // resonance markers
    for (int i = 0; i < result.profile.resonanceCount; ++i)
    {
        const int x = plot.getX() + (int) (fracFor (result.profile.resonanceHz[(size_t) i], lo, hi) * plot.getWidth());
        g.setColour (theme::accentWarm.withAlpha (0.85f));
        g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
    }

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("FREQUENCY BALANCE", r.getX() + 10, r.getY() + 4, 200, 12, juce::Justification::centredLeft);
    if (result.profile.resonanceCount > 0)
    {
        g.setColour (theme::accentWarm);
        g.drawText ("red = resonances", r.getRight() - 130, r.getY() + 4, 120, 12, juce::Justification::centredRight);
    }
}

void ReferencePanel::drawGauges (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r.toFloat(), 8.0f, 1.0f);

    const auto& p = result.profile;
    auto nrm = [] (float x, float a, float b) { return juce::jlimit (0.0f, 1.0f, (x - a) / (b - a)); };

    struct Gauge { juce::String label; float v; juce::String value; };
    const std::vector<Gauge> gs = {
        { "Loudness",   nrm (p.integratedLufs, -30.0f, -6.0f), juce::String (p.integratedLufs, 1) + " LUFS" },
        { "Crest",      nrm (p.crestDb, 3.0f, 18.0f),          juce::String (p.crestDb, 1) + " dB" },
        { "Dyn range",  nrm (p.dynamicRangeDb, 2.0f, 16.0f),   juce::String (p.dynamicRangeDb, 1) + " LU" },
        { "Compression",juce::jlimit (0.0f, 1.0f, p.compressionAmount), juce::String ((int) std::round (p.compressionAmount * 100.0f)) + "%" },
        { "Brightness", juce::jlimit (0.0f, 1.0f, p.brightness), juce::String ((int) std::round (p.brightness * 100.0f)) + "%" },
        { "Sibilance",  nrm (p.sibilanceRatio, 0.0f, 1.5f),    juce::String (p.sibilanceRatio, 2) },
        { "Stereo",     juce::jlimit (0.0f, 1.0f, p.stereoWidth), p.isMono ? juce::String ("mono") : juce::String ((int) std::round (p.stereoWidth * 100.0f)) + "%" },
        { "Reverb",     juce::jlimit (0.0f, 1.0f, p.reverbAmount), juce::String ((int) std::round (p.reverbAmount * 100.0f)) + "%" },
    };

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("PRODUCTION SNAPSHOT", r.getX() + 10, r.getY() + 4, 200, 12, juce::Justification::centredLeft);

    auto inner = r.reduced (10, 8);
    inner.removeFromTop (16);
    const int rowH = juce::jmax (14, inner.getHeight() / (int) gs.size());
    for (const auto& gg : gs)
    {
        auto row = inner.removeFromTop (rowH);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (gg.label, row.removeFromLeft (76), juce::Justification::centredLeft);
        auto valR = row.removeFromRight (74);
        g.setColour (theme::text);
        g.drawText (gg.value, valR, juce::Justification::centredRight);
        auto bar = row.reduced (4, rowH / 2 - 3);
        g.setColour (theme::panelLight);
        g.fillRoundedRectangle (bar.toFloat(), 3.0f);
        g.setColour (theme::accent);
        g.fillRoundedRectangle (bar.withWidth ((int) (bar.getWidth() * gg.v)).toFloat(), 3.0f);
    }
}
} // namespace vf
