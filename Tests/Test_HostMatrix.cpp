#include "TestFramework.h"
#include "../Source/DSP/RetuneEngine.h"
#include "../Source/DSP/VocalChain.h"
#include <algorithm>

// ============================================================================
//  Host matrix — sample rates and buffer sizes other than the one we develop at.
//
//  WHY THIS IS THE MOST IMPORTANT GAP TO CLOSE. Every DSP suite in this project
//  runs at 44100 Hz with a 256-sample block. That is not what the plugin runs
//  at in the field: 48 kHz is the professional default, plenty of interfaces sit
//  at 96 kHz, and a host is free to hand us 64, 1024, or a ragged 100-sample
//  block. Everything here is full of rate-derived constants — grain sizes,
//  analysis hops, delay lengths, filter coefficients, envelope times — and a
//  mistake in any of them is invisible at the development rate and audible
//  everywhere else. "Works on my machine, sounds off in my DAW" is precisely
//  the shape of bug this suite exists to catch.
//
//  The properties asserted are the same ones the 44.1 kHz suites assert, so a
//  rate-dependent regression shows up as the SAME failure text at a different
//  rate, which makes it obvious what happened.
// ============================================================================
namespace
{
using namespace vftest;

std::vector<float> vowelAt (double hz, double seconds, float amp, double fs)
{
    const size_t n = (size_t) (seconds * fs);
    std::vector<float> v (n, 0.0f);
    for (int h = 1; h * hz < std::min (4200.0, fs * 0.45); ++h)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz * h / fs;
        const double g = 1.0 / (double) h;
        for (size_t i = 0; i < n; ++i) v[i] += (float) (g * std::sin (w * (double) i));
    }
    float peak = 0.0f;
    for (float x : v) peak = juce::jmax (peak, std::abs (x));
    if (peak > 0.0f) for (auto& x : v) x *= amp / peak;
    return v;
}

double measureHzAt (const std::vector<float>& v, double fromSec, double lenSec, double fs)
{
    const size_t from = (size_t) (fromSec * fs);
    const size_t n    = (size_t) (lenSec * fs);
    if (from + n >= v.size()) return 0.0;
    const int minLag = (int) (fs / 1500.0), maxLag = (int) (fs / 60.0);

    std::vector<double> corr ((size_t) (maxLag + 2), 0.0);
    double best = -1e18;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0, ea = 0.0, eb = 0.0;
        for (size_t i = 0; i + (size_t) lag < n; ++i)
        {
            const double a = v[from + i], b = v[from + i + (size_t) lag];
            sum += a * b; ea += a * a; eb += b * b;
        }
        corr[(size_t) lag] = sum / (std::sqrt (ea * eb) + 1e-12);
        best = std::max (best, corr[(size_t) lag]);
    }
    int bestLag = 0;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
        if (corr[(size_t) lag] >= best * 0.92
            && corr[(size_t) lag] >= corr[(size_t) lag - 1]
            && corr[(size_t) lag] >= corr[(size_t) lag + 1]) { bestLag = lag; break; }
    if (bestLag <= minLag || bestLag >= maxLag) return 0.0;

    const double y0 = corr[(size_t) bestLag - 1], y1 = corr[(size_t) bestLag],
                 y2 = corr[(size_t) bestLag + 1];
    const double den = 2.0 * (2.0 * y1 - y0 - y2);
    const double sh = std::abs (den) > 1e-12 ? (y2 - y0) / den : 0.0;
    return fs / ((double) bestLag + juce::jlimit (-1.0, 1.0, sh));
}

/** Run the retune engine at an arbitrary rate and block size. `block` is the
    MAXIMUM the engine is prepared for; `actual` is what is really handed over,
    so a host that sends short or ragged blocks is modelled honestly. */
std::vector<float> runRetune (const std::vector<float>& sig, vf::RetuneParams p,
                              double fs, int prepareBlock, int actualBlock)
{
    vf::RetuneEngine e;
    e.prepare (fs, prepareBlock, 1);
    std::vector<float> out;
    out.reserve (sig.size());
    juce::AudioBuffer<float> buf (1, prepareBlock);

    for (size_t off = 0; off + (size_t) actualBlock <= sig.size(); off += (size_t) actualBlock)
    {
        buf.setSize (1, actualBlock, false, false, true);
        for (int i = 0; i < actualBlock; ++i) buf.setSample (0, i, sig[off + (size_t) i]);
        e.process (buf, p);
        for (int i = 0; i < actualBlock; ++i) out.push_back (buf.getSample (0, i));
    }
    return out;
}

vf::RetuneParams tuned()
{
    vf::RetuneParams p;
    p.on = true; p.scaleType = 1; p.keyRoot = 9;      // A major
    p.speedMs = 60.0f; p.amount = 1.0f;
    p.flexTune = 0.35f; p.vibratoPreservation = 0.75f;
    p.transitionSmoothing = 0.60f; p.driftCorrection = 0.50f;
    return p;
}
} // namespace

void runHostMatrixTests()
{
    Suite s ("Host matrix (sample rates and buffer sizes)");

    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };

    // ======================================================================
    //  1. TUNING ACCURACY AT EVERY SUPPORTED RATE.
    //     A note 40 cents flat must arrive on A440 whatever the host runs at.
    // ======================================================================
    for (double fs : rates)
    {
        const double sung = 440.0 * std::pow (2.0, -40.0 / 1200.0);
        const auto out = runRetune (vowelAt (sung, 2.2, 0.4f, fs), tuned(), fs, 256, 256);
        const double hz = measureHzAt (out, 1.5, 0.5, fs);
        const double residual = 1200.0 * std::log2 (hz / 440.0);
        s.check ((juce::String ("a note 40 cents flat is corrected at ")
                  + juce::String (fs / 1000.0, 1) + " kHz").toRawUTF8(),
                 std::abs (residual) <= 12.0,
                 "residual " + juce::String (residual, 1) + " cents (out "
                 + juce::String (hz, 1) + " Hz)");
    }

    // ======================================================================
    //  2. THE BYPASS CONTRACT AT EVERY RATE.
    //     Correction off must be the dry signal, delayed by exactly the
    //     reported latency — that is what makes toggling safe in a session.
    // ======================================================================
    for (double fs : rates)
    {
        vf::RetuneParams off;                       // on = false, transpose = 0
        const auto sig = vowelAt (220.0, 1.2, 0.4f, fs);
        const auto out = runRetune (sig, off, fs, 256, 256);
        const int lat = vf::RetuneEngine::latencyForMode (2, fs);

        double worst = 0.0;
        for (size_t i = (size_t) lat + 2048; i < out.size(); ++i)
            worst = std::max (worst, (double) std::abs (out[i] - sig[i - (size_t) lat]));
        s.check ((juce::String ("bypass is byte-identical delayed dry at ")
                  + juce::String (fs / 1000.0, 1) + " kHz").toRawUTF8(),
                 worst < 1.0e-6, "largest deviation " + juce::String (worst, 9));
    }

    // ======================================================================
    //  3. BUFFER SIZES, INCLUDING RAGGED ONES.
    //     Hosts are not obliged to send power-of-two blocks, and a scheduler
    //     that assumes they do fails only on the machines that do not.
    // ======================================================================
    for (int block : { 16, 32, 64, 100, 128, 333, 512, 1024, 2048 })
    {
        const double fs = 48000.0;
        const double sung = 440.0 * std::pow (2.0, -40.0 / 1200.0);
        const auto out = runRetune (vowelAt (sung, 2.0, 0.4f, fs), tuned(), fs, 2048, block);
        const double hz = measureHzAt (out, 1.3, 0.5, fs);
        const double residual = 1200.0 * std::log2 (hz / 440.0);
        s.check ((juce::String ("block size ") + juce::String (block)
                  + " tunes correctly and stays finite").toRawUTF8(),
                 allFinite (out, 4.0f) && std::abs (residual) <= 12.0,
                 "residual " + juce::String (residual, 1) + " cents");
    }

    // ======================================================================
    //  4. REPORTED LATENCY MUST MATCH DELIVERED LATENCY AT EVERY RATE.
    //     The host uses this number for delay compensation; if it is wrong the
    //     vocal sits late against the whole mix and nothing in the plugin
    //     sounds broken — which is the worst kind of wrong.
    // ======================================================================
    for (double fs : rates)
        for (int mode : { 0, 1, 2 })
        {
            vf::RetuneEngine e;
            e.prepare (fs, 512, 1);
            e.setLatencyMode (mode);
            s.check ((juce::String ("reported latency matches actual, mode ")
                      + juce::String (mode) + " at " + juce::String (fs / 1000.0, 1)
                      + " kHz").toRawUTF8(),
                     e.getLatencySamples() == vf::RetuneEngine::latencyForMode (mode, fs),
                     juce::String (e.getLatencySamples()) + " vs "
                     + juce::String (vf::RetuneEngine::latencyForMode (mode, fs)));
        }

    // ======================================================================
    //  5. THE WHOLE CHAIN AT EVERY RATE — finite, bounded, click-free.
    // ======================================================================
    for (double fs : rates)
    {
        vf::VocalChain chain;
        juce::dsp::ProcessSpec spec { fs, 256, 2 };
        chain.prepare (spec);

        vf::ChainParams p {};
        p.eq.on = p.gate.on = p.deess.on = p.comp.on = p.sat.on = true;
        p.dyneq.on = p.mband.on = p.delay.on = p.verb.on = p.limit.on = true;
        p.eq.hpfHz = 80.0f; p.eq.mudHz = 300.0f; p.eq.presHz = 3000.0f;
        p.eq.airDb = 4.0f; p.eq.presDb = 3.0f;
        p.gate.thresholdDb = -55.0f;
        p.dyneq.lowFreqHz = 250.0f; p.dyneq.midFreqHz = 1000.0f;
        p.dyneq.highFreqHz = 6000.0f; p.dyneq.rangeDb = 6.0f;
        p.deess.thresholdDb = -28.0f; p.deess.freqHz = 7000.0f;
        p.comp.thresholdDb = -20.0f; p.comp.ratio = 3.0f; p.comp.attackMs = 10.0f;
        p.comp.releaseMs = 120.0f; p.comp.mix = 1.0f;
        p.mband.ratio = 2.0f; p.mband.lowXoverHz = 200.0f; p.mband.highXoverHz = 3000.0f;
        p.sat.drive = 0.4f; p.sat.tone = 0.5f; p.sat.bias = 0.5f; p.sat.mix = 1.0f;
        p.delay.timeMs = 140.0f; p.delay.feedback = 0.3f; p.delay.mix = 0.2f;
        p.verb.size = 0.5f; p.verb.decay = 0.5f; p.verb.predelayMs = 10.0f;
        p.verb.damp = 0.5f; p.verb.diffusion = 0.6f; p.verb.lowCutHz = 200.0f;
        p.verb.highCutHz = 9000.0f; p.verb.width = 1.0f; p.verb.mix = 0.25f;
        p.limit.ceilingDb = -0.3f;

        const auto sig = vowelAt (200.0, 1.5, 0.4f, fs);
        std::vector<float> out;
        out.reserve (sig.size());
        juce::AudioBuffer<float> buf (2, 256);
        for (size_t off = 0; off + 256 <= sig.size(); off += 256)
        {
            for (int i = 0; i < 256; ++i)
            {
                buf.setSample (0, i, sig[off + (size_t) i]);
                buf.setSample (1, i, sig[off + (size_t) i]);
            }
            chain.process (buf, p);
            for (int i = 0; i < 256; ++i) out.push_back (buf.getSample (0, i));
        }

        const size_t from = (size_t) (fs * 0.6);
        std::vector<float> jumps;
        for (size_t i = from + 1; i < out.size(); ++i)
            jumps.push_back (std::abs (out[i] - out[i - 1]));
        std::sort (jumps.begin(), jumps.end());
        const float p999 = jumps.empty() ? 0.0f : jumps[(size_t) ((double) jumps.size() * 0.999)];
        const float worst = jumps.empty() ? 0.0f : jumps.back();

        s.check ((juce::String ("the full chain is clean at ")
                  + juce::String (fs / 1000.0, 1) + " kHz").toRawUTF8(),
                 allFinite (out, 4.0f) && worst < p999 * 2.0f + 0.02f,
                 "largest jump " + juce::String (worst, 4) + " vs 99.9th pct "
                 + juce::String (p999, 4));
    }
}
