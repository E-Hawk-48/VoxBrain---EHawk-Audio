#include "SpectrumDisplay.h"
#include <cmath>

namespace vf
{
namespace
{
    constexpr float minDb = -90.0f, maxDb = 0.0f;
    constexpr float minHz = 20.0f,  maxHz = 20000.0f;

    float hzToNorm (float hz)
    {
        return std::log (hz / minHz) / std::log (maxHz / minHz);
    }
}

SpectrumDisplay::SpectrumDisplay (AnalysisEngine& engine) : analysis (engine)
{
    setOpaque (true);
    startTimerHz (30);
}

void SpectrumDisplay::timerCallback()
{
    if (analysis.getLatestSpectrum (spectrum.data()))
    {
        for (size_t i = 0; i < peakHold.size(); ++i)
            peakHold[i] = std::max (peakHold[i] * 0.985f, spectrum[i]);
        repaint();
    }
}

float SpectrumDisplay::binToX (int bin, float width) const
{
    const float hz = (float) bin * (float) sampleRate / (float) AnalysisEngine::fftSize;
    return hzToNorm (juce::jlimit (minHz, maxHz, hz)) * width;
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (theme::panel);

    // Grid lines at decades + octave marks
    g.setColour (theme::outline.withAlpha (0.5f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = hzToNorm (hz) * bounds.getWidth();
        g.drawVerticalLine ((int) x, 0.0f, bounds.getHeight());
    }
    g.setColour (theme::textDim.withAlpha (0.7f));
    g.setFont (juce::FontOptions (10.0f));
    for (auto [hz, label] : std::initializer_list<std::pair<float, const char*>>
             { { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
        g.drawText (label, (int) (hzToNorm (hz) * bounds.getWidth()) + 3,
                    (int) bounds.getHeight() - 16, 30, 14, juce::Justification::left);

    const int numBins = AnalysisEngine::fftSize / 2;
    const float scale = 1.0f / (float) AnalysisEngine::fftSize;

    auto buildPath = [&] (const std::array<float, AnalysisEngine::fftSize / 2>& mags)
    {
        juce::Path p;
        bool started = false;
        for (int i = 1; i < numBins; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (mags[(size_t) i] * scale, minDb);
            const float x = binToX (i, bounds.getWidth());
            const float y = juce::jmap (juce::jlimit (minDb, maxDb, db),
                                        minDb, maxDb, bounds.getHeight(), 0.0f);
            if (! started) { p.startNewSubPath (x, y); started = true; }
            else             p.lineTo (x, y);
        }
        return p;
    };

    // Peak-hold ghost
    g.setColour (theme::accent.withAlpha (0.18f));
    g.strokePath (buildPath (peakHold), juce::PathStrokeType (1.0f));

    // Live spectrum with gradient fill
    auto live = buildPath (spectrum);
    juce::Path fill (live);
    fill.lineTo (bounds.getWidth(), bounds.getHeight());
    fill.lineTo (0.0f, bounds.getHeight());
    fill.closeSubPath();

    juce::ColourGradient grad (theme::accent.withAlpha (0.30f), 0, 0,
                               theme::accent.withAlpha (0.02f), 0, bounds.getHeight(), false);
    g.setGradientFill (grad);
    g.fillPath (fill);
    g.setColour (theme::accent);
    g.strokePath (live, juce::PathStrokeType (1.6f));

    // Readouts
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    juce::String readout;
    const float pitchHz = analysis.getCurrentPitchHz();
    if (pitchHz > 0.0f)
    {
        const int midi = (int) std::round (69.0 + 12.0 * std::log2 (pitchHz / 440.0));
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        readout << names[((midi % 12) + 12) % 12] << juce::String (midi / 12 - 1)
                << "  " << juce::String (pitchHz, 1) << " Hz    ";
    }
    readout << juce::String (analysis.getMomentaryLufs(), 1) << " LUFS    "
            << juce::String (analysis.getCurrentPeakDb(), 1) << " dB pk";
    g.drawText (readout, getLocalBounds().reduced (10, 8), juce::Justification::topRight);
}
} // namespace vf
