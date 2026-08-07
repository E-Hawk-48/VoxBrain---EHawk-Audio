#include "TestFramework.h"
#include "../Source/DSP/PitchTracker.h"
#include <algorithm>

// ============================================================================
//  PitchTracker — the redesigned F0 engine (multi-candidate CMND +
//  sub-harmonic demotion + Viterbi with an explicit unvoiced state).
//
//  These are the measurements that justified replacing the old single-candidate
//  YIN. They exist as a REGRESSION GUARD: the tracker is the foundation the
//  corrector, the musical layer and key detection all stand on, so a change that
//  quietly reintroduces octave errors or jitter must fail loudly here.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FS = 44100.0;

struct Trace
{
    std::vector<float> hz;
    std::vector<int>   voiced;
};

Trace run (const std::vector<float>& sig, float sensitivity = 0.5f, int decodeLag = 4)
{
    vf::PitchTracker t;
    vf::PitchTracker::Config c;
    c.sampleRate  = FS;
    c.hopSize     = 256;
    c.windowSize  = 1024;
    c.decodeLag   = decodeLag;
    c.sensitivity = sensitivity;
    t.prepare (c);

    Trace tr;
    const int block = 128;
    for (size_t off = 0; off + (size_t) block <= sig.size(); off += (size_t) block)
        if (t.process (sig.data() + off, block))
        {
            tr.hz.push_back (t.latestHz());
            tr.voiced.push_back (t.isVoiced() ? 1 : 0);
        }
    return tr;
}

float centsErr (float got, float want)
{
    return got <= 0.0f ? 9999.0f : 1200.0f * std::log2 (got / want);
}

float medianHz (const Trace& t, double skip = 0.35)
{
    std::vector<float> v;
    for (size_t i = (size_t) ((double) t.hz.size() * skip); i < t.hz.size(); ++i)
        if (t.hz[i] > 0.0f) v.push_back (t.hz[i]);
    if (v.empty()) return 0.0f;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

/** Fraction of voiced frames further than `tol` cents from the truth — i.e. the
    octave-error / gross-error rate. */
float errorRate (const Trace& t, float want, double skip = 0.35, float tol = 60.0f)
{
    size_t bad = 0, n = 0;
    for (size_t i = (size_t) ((double) t.hz.size() * skip); i < t.hz.size(); ++i)
        if (t.voiced[i] && t.hz[i] > 0.0f)
        {
            ++n;
            if (std::abs (centsErr (t.hz[i], want)) > tol) ++bad;
        }
    return n > 0 ? (float) bad / (float) n : 1.0f;
}

float jitterCents (const Trace& t, float want, double skip = 0.35)
{
    std::vector<float> c;
    for (size_t i = (size_t) ((double) t.hz.size() * skip); i < t.hz.size(); ++i)
        if (t.voiced[i] && t.hz[i] > 0.0f) c.push_back (centsErr (t.hz[i], want));
    if (c.size() < 4) return 9999.0f;

    double m = 0.0;
    for (float x : c) m += x;
    m /= (double) c.size();
    double s2 = 0.0;
    for (float x : c) s2 += (x - m) * (x - m);
    return (float) std::sqrt (s2 / (double) c.size());
}

float voicedFraction (const Trace& t, double skip = 0.3)
{
    const size_t s = (size_t) ((double) t.voiced.size() * skip);
    if (t.voiced.size() <= s) return 0.0f;
    size_t v = 0;
    for (size_t i = s; i < t.voiced.size(); ++i) v += (size_t) t.voiced[i];
    return (float) v / (float) (t.voiced.size() - s);
}
} // namespace

void runPitchTrackerTests()
{
    using namespace vftest;
    Suite s ("PitchTracker");

    // ---- accuracy across the full vocal range -------------------------
    struct Reg { const char* name; double hz; int harm; };
    const Reg regs[] = {
        { "bass 82 Hz (E2)",        82.41,  6 },
        { "baritone 110 Hz",       110.00,  6 },
        { "male 147 Hz (D3)",      146.83,  5 },
        { "female 262 Hz (C4)",    261.63,  4 },
        { "alto 330 Hz",           329.63,  4 },
        { "soprano 523 Hz (C5)",   523.25,  3 },
        { "falsetto 784 Hz (G5)",  783.99,  2 },
        { "high falsetto 1047 Hz",1046.50,  2 },
    };
    for (const auto& r : regs)
    {
        const auto tr = run (sineWave (r.hz, 0.8, 0.3f, FS, r.harm));
        const float m = medianHz (tr);
        s.check (r.name, std::abs (centsErr (m, (float) r.hz)) < 25.0f,
                 juce::String (m, 1) + " Hz, err " + juce::String (centsErr (m, (float) r.hz), 1) + " cents");
    }

    // ---- octave errors: the failure the redesign was built to remove ---
    {
        const auto tr = run (sawWave (110.0, 1.0, 0.3f, FS));
        s.check ("110 Hz saw tracks the fundamental", std::abs (centsErr (medianHz (tr), 110.0f)) < 30.0f,
                 juce::String (medianHz (tr), 1) + " Hz");
        s.check ("110 Hz saw octave-error rate < 2%", errorRate (tr, 110.0f) < 0.02f,
                 juce::String (errorRate (tr, 110.0f) * 100.0f, 1) + "%");
    }
    {
        const auto tr = run (sawWave (220.0, 1.0, 0.3f, FS));
        s.check ("220 Hz saw error rate < 2%", errorRate (tr, 220.0f) < 0.02f,
                 juce::String (errorRate (tr, 220.0f) * 100.0f, 1) + "%");
    }
    {
        // Nine harmonics is the classic octave-down trap.
        const auto tr = run (sineWave (150.0, 1.0, 0.3f, FS, 9));
        s.check ("9-harmonic 150 Hz error rate < 2%", errorRate (tr, 150.0f) < 0.02f,
                 juce::String (errorRate (tr, 150.0f) * 100.0f, 1) + "%");
    }

    // ---- stability -----------------------------------------------------
    {
        const auto tr = run (sineWave (220.0, 1.2, 0.3f, FS, 4));
        s.check ("sustained note jitter < 8 cents", jitterCents (tr, 220.0f) < 8.0f,
                 juce::String (jitterCents (tr, 220.0f), 2) + " cents SD");
    }
    {
        const auto tr = run (sawWave (196.0, 1.2, 0.3f, FS));
        size_t flips = 0, n = 0;
        for (size_t i = tr.hz.size() / 3 + 1; i < tr.hz.size(); ++i)
            if (tr.hz[i] > 0.0f && tr.hz[i - 1] > 0.0f)
            {
                ++n;
                if (std::abs (1200.0f * std::log2 (tr.hz[i] / tr.hz[i - 1])) > 50.0f) ++flips;
            }
        s.check ("no note-flipping on a steady saw", n > 0 && flips == 0,
                 juce::String ((int) flips) + " flips / " + juce::String ((int) n) + " frames");
    }

    // ---- expression must survive ---------------------------------------
    {
        // 5 Hz vibrato, ±60 cents.
        std::vector<float> v ((size_t) (1.5 * FS));
        double ph = 0.0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double t = (double) i / FS;
            const double f = 330.0 * std::pow (2.0, (60.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t)) / 1200.0);
            ph += f / FS;
            v[i] = (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * ph));
        }
        const auto tr = run (v);
        float lo = 1.0e9f, hi = 0.0f;
        for (size_t i = tr.hz.size() / 3; i < tr.hz.size(); ++i)
            if (tr.hz[i] > 0.0f) { lo = juce::jmin (lo, tr.hz[i]); hi = juce::jmax (hi, tr.hz[i]); }
        const float depth = (lo < hi) ? 1200.0f * std::log2 (hi / lo) : 0.0f;
        s.check ("vibrato depth tracked, not flattened", depth > 70.0f && depth < 190.0f,
                 juce::String (depth, 0) + " cents pk-pk (true 120)");
    }

    // ---- false detection ------------------------------------------------
    {
        const auto tr = run (whiteNoise (0.8, 0.10f, FS));
        s.check ("white noise is not tracked as pitch", voicedFraction (tr) < 0.10f,
                 juce::String (voicedFraction (tr) * 100.0f, 0) + "% voiced");
    }
    {
        const std::vector<float> silence ((size_t) (0.5 * FS), 0.0f);
        const auto tr = run (silence);
        s.check ("silence is unvoiced", voicedFraction (tr) < 0.02f);
    }
    {
        // A genuinely quiet note must still be found.
        const auto tr = run (sineWave (196.0, 0.9, 0.02f, FS, 4));
        s.check ("quiet real note is still detected", voicedFraction (tr) > 0.5f,
                 juce::String (voicedFraction (tr) * 100.0f, 0) + "% voiced");
    }

    // ---- latency contract ----------------------------------------------
    {
        vf::PitchTracker t;
        vf::PitchTracker::Config c;
        c.sampleRate = FS; c.hopSize = 256; c.decodeLag = 4;
        t.prepare (c);
        s.check ("decode lag reported correctly", t.latencySamples() == 4 * 256,
                 juce::String (t.latencySamples() * 1000.0f / (float) FS, 1) + " ms");

        // Narrowing at runtime must be allocation-free and must shorten the lag.
        t.setRuntimeMode (130.0f, 2);
        s.check ("low-latency mode halves the lag", t.latencySamples() == 2 * 256,
                 juce::String (t.latencySamples() * 1000.0f / (float) FS, 1) + " ms");
    }

}
