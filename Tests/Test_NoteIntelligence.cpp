#include "TestFramework.h"
#include "../Source/DSP/NoteIntelligence.h"

// ============================================================================
//  NoteIntelligence — the musical layer that decides what a pitch deviation
//  MEANS (scoop, vibrato, slide, or genuinely off-pitch).
//
//  The slide test below is the important one historically: the first
//  implementation defined a transition by DISTANCE from the note and classified
//  0% of a real portamento, because a slide and a flat note look identical if
//  you only measure deviation. Detection is velocity-based for that reason, and
//  this test is what would catch a regression back to the wrong definition.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FPS = 172.0;   // ~5.8 ms analysis hop

float centsTo (float base, float cents) { return base * std::pow (2.0f, cents / 1200.0f); }

std::vector<vf::NoteDecision> run (const std::vector<float>& hz, vf::NoteIntelligence::Params p)
{
    vf::NoteIntelligence ni;
    ni.prepare (FPS);
    ni.setParams (p);
    std::vector<vf::NoteDecision> out;
    out.reserve (hz.size());
    for (float f : hz) out.push_back (ni.process (f, f > 0.0f));
    return out;
}

float fracState (const std::vector<vf::NoteDecision>& d, vf::NoteDecision::State st, double from = 0.4)
{
    const size_t a = (size_t) ((double) d.size() * from);
    size_t n = 0, m = 0;
    for (size_t i = a; i < d.size(); ++i) { ++n; if (d[i].state == st) ++m; }
    return n > 0 ? (float) m / (float) n : 0.0f;
}

float meanCorrection (const std::vector<vf::NoteDecision>& d, double from = 0.4)
{
    const size_t a = (size_t) ((double) d.size() * from);
    double s = 0.0; size_t n = 0;
    for (size_t i = a; i < d.size(); ++i) { s += d[i].correctionScale; ++n; }
    return n > 0 ? (float) (s / (double) n) : 0.0f;
}

std::vector<float> vibratoTrace (float baseHz, double seconds, double rateHz, double depthCents)
{
    std::vector<float> v ((size_t) (seconds * FPS));
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = (double) i / FPS;
        v[i] = centsTo (baseHz, (float) (depthCents * std::sin (2.0 * juce::MathConstants<double>::pi * rateHz * t)));
    }
    return v;
}
} // namespace

void runNoteIntelligenceTests()
{
    using namespace vftest;
    Suite s ("NoteIntelligence");

    vf::NoteIntelligence::Params def;   // shipping defaults

    // ---- vibrato ------------------------------------------------------
    {
        const auto v = vibratoTrace (220.0f, 1.6, 5.5, 45.0);
        const auto d = run (v, def);

        s.check ("vibrato is recognised as vibrato",
                 fracState (d, vf::NoteDecision::State::Vibrato) > 0.6f,
                 juce::String (fracState (d, vf::NoteDecision::State::Vibrato) * 100.0f, 0) + "% of frames");

        float rate = 0.0f, depth = 0.0f; size_t n = 0;
        for (size_t i = d.size() / 2; i < d.size(); ++i)
            if (d[i].vibratoRateHz > 0.0f) { rate += d[i].vibratoRateHz; depth += d[i].vibratoDepthCents; ++n; }
        if (n > 0) { rate /= (float) n; depth /= (float) n; }

        s.check ("vibrato rate estimated near 5.5 Hz", std::abs (rate - 5.5f) < 1.5f,
                 juce::String (rate, 2) + " Hz");
        s.check ("vibrato correction is eased, not flattened", meanCorrection (d) < 0.35f,
                 juce::String (meanCorrection (d), 2) + " mean correction");

        float lo = 1.0e9f, hi = 0.0f;
        for (size_t i = d.size() / 2; i < d.size(); ++i)
        {
            lo = juce::jmin (lo, d[i].noteCentreHz);
            hi = juce::jmax (hi, d[i].noteCentreHz);
        }
        s.check ("note centre stays locked through vibrato",
                 lo > 0.0f && 1200.0f * std::log2 (hi / lo) < 25.0f,
                 juce::String (1200.0f * std::log2 (hi / juce::jmax (lo, 1.0f)), 1) + " cents of drift");
    }

    // ---- the preservation control actually controls something ----------
    {
        const auto v = vibratoTrace (220.0f, 1.6, 5.5, 45.0);
        auto off = def; off.vibratoPreservation = 0.0f;
        auto on  = def; on.vibratoPreservation  = 1.0f;
        s.check ("preservation = 0 corrects vibrato hard", meanCorrection (run (v, off)) > 0.8f,
                 juce::String (meanCorrection (run (v, off)), 2));
        s.check ("preservation = 1 leaves vibrato alone", meanCorrection (run (v, on)) < 0.15f,
                 juce::String (meanCorrection (run (v, on)), 2));
    }

    // ---- slide vs off-pitch (the historically wrong one) ---------------
    {
        // A real portamento: 220 -> 262 over 500 ms.
        std::vector<float> v ((size_t) (0.5 * FPS));
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double u = (double) i / (double) v.size();
            v[i] = (float) (220.0 * std::pow (262.0 / 220.0, u));
        }
        const auto d = run (v, def);
        const float trans = fracState (d, vf::NoteDecision::State::Transition, 0.15);
        s.check ("a slide is classified as a transition, not off-pitch", trans > 0.15f,
                 juce::String (trans * 100.0f, 0) + "% transition frames");
        s.check ("correction eases during the slide", meanCorrection (d, 0.15) < 0.75f,
                 juce::String (meanCorrection (d, 0.15), 2));
    }
    {
        // Settle on pitch, then go 35 cents flat and stay there: genuine error.
        std::vector<float> v ((size_t) (0.5 * FPS), 220.0f);
        const std::vector<float> flat ((size_t) (1.2 * FPS), centsTo (220.0f, -35.0f));
        v.insert (v.end(), flat.begin(), flat.end());
        const auto d = run (v, def);
        s.check ("a settled flat note is corrected", meanCorrection (d, 0.75) > 0.25f,
                 juce::String (meanCorrection (d, 0.75), 2) + " mean correction");
    }

    // ---- note locking + re-targeting -----------------------------------
    {
        const std::vector<float> v ((size_t) (1.5 * FPS), 330.0f);
        const auto d = run (v, def);
        int changes = 0;
        for (size_t i = 5; i < d.size(); ++i) if (d[i].noteChanged) ++changes;
        s.check ("no spurious note changes on a steady note", changes == 0,
                 juce::String (changes) + " changes");
        s.check ("sustain is classified as sustain",
                 fracState (d, vf::NoteDecision::State::Sustain) > 0.7f,
                 juce::String (fracState (d, vf::NoteDecision::State::Sustain) * 100.0f, 0) + "%");
    }
    {
        // A -> C# step must re-target promptly.
        std::vector<float> v ((size_t) (0.6 * FPS), 220.0f);
        const std::vector<float> b ((size_t) (0.6 * FPS), 277.18f);
        v.insert (v.end(), b.begin(), b.end());
        const auto d = run (v, def);

        const size_t bound = (size_t) (0.6 * FPS);
        size_t settle = 0; bool ok = false;
        for (size_t i = bound; i < d.size(); ++i)
            if (std::abs (1200.0f * std::log2 (d[i].noteCentreHz / 277.18f)) < 25.0f)
            { settle = i - bound; ok = true; break; }
        const float ms = (float) settle / (float) FPS * 1000.0f;
        s.check ("re-targets to a new note quickly", ok && ms < 90.0f, juce::String (ms, 0) + " ms");
    }

    // ---- hard tune must remain reachable -------------------------------
    {
        const auto v = vibratoTrace (220.0f, 1.2, 5.5, 45.0);
        vf::NoteIntelligence::Params hard;
        hard.flexTune = 0.0f; hard.vibratoPreservation = 0.0f;
        hard.transitionSmoothing = 0.0f; hard.driftCorrection = 1.0f;
        s.check ("all allowances at zero gives full correction", meanCorrection (run (v, hard)) > 0.9f,
                 juce::String (meanCorrection (run (v, hard)), 2));
    }

    // ---- phrase gaps ----------------------------------------------------
    {
        std::vector<float> v ((size_t) (0.4 * FPS), 220.0f);
        const std::vector<float> gap ((size_t) (0.3 * FPS), 0.0f);
        const std::vector<float> next ((size_t) (0.4 * FPS), 392.0f);
        v.insert (v.end(), gap.begin(), gap.end());
        v.insert (v.end(), next.begin(), next.end());
        const auto d = run (v, def);

        bool adopted = false;
        for (size_t i = (size_t) (0.75 * FPS); i < d.size(); ++i)
            if (d[i].noteCentreHz > 0.0f
                && std::abs (1200.0f * std::log2 (d[i].noteCentreHz / 392.0f)) < 25.0f)
            { adopted = true; break; }
        s.check ("a new phrase starts on its own note", adopted);
    }

}
