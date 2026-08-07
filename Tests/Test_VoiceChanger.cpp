#include "TestFramework.h"
#include "../Source/DSP/RetuneEngine.h"
#include "../Source/DSP/VoiceCharacter.h"
#include <map>

// ============================================================================
//  Voice Changer — transposition, formant shifting, and the character table.
//
//  Two properties matter more than any other here and are tested first:
//
//   1. NO REGRESSION. Transposition is new machinery bolted onto the single
//      most delicate class in the project. With transpose at 0 the engine must
//      behave EXACTLY as it did before it existed — including the byte-identical
//      bypass contract, which is what guarantees that toggling correction never
//      shifts timing in a session.
//
//   2. PITCH AND FORMANT ARE INDEPENDENT. The whole feature is built on the
//      claim that VoxBrain can move the note without moving the body, and the
//      body without moving the note. That claim is measured directly here
//      rather than assumed: transposition must change the measured f0 by the
//      exact ratio, and a formant shift must move the spectrum while leaving
//      f0 alone. If those two ever stop being independent, every character in
//      the table silently degrades into a tape speed-up.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FS = 44100.0;

std::vector<float> run (const std::vector<float>& sig, vf::RetuneParams p)
{
    vf::RetuneEngine e;
    e.prepare (FS, 512, 1);

    std::vector<float> out;
    out.reserve (sig.size());
    juce::AudioBuffer<float> buf (1, 256);

    for (size_t off = 0; off + 256 <= sig.size(); off += 256)
    {
        for (int i = 0; i < 256; ++i) buf.setSample (0, i, sig[off + (size_t) i]);
        e.process (buf, p);
        for (int i = 0; i < 256; ++i) out.push_back (buf.getSample (0, i));
    }
    return out;
}

/** Autocorrelation pitch estimate over a settled window, with parabolic
    refinement. Independent of the engine's own tracker on purpose — measuring
    the output with the same code that produced it would prove nothing. */
double measureHz (const std::vector<float>& v, double fromSec, double lenSec,
                  double loHz = 40.0, double hiHz = 2000.0)
{
    const size_t from = (size_t) (fromSec * FS);
    const size_t n    = (size_t) (lenSec * FS);
    if (from + n >= v.size()) return 0.0;

    const int minLag = (int) (FS / hiHz);
    const int maxLag = (int) (FS / loHz);

    double best = -1e18;
    std::vector<double> corr ((size_t) (maxLag + 2), 0.0);

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0, ea = 0.0, eb = 0.0;
        for (size_t i = 0; i + (size_t) lag < n; ++i)
        {
            const double a = v[from + i], b = v[from + i + (size_t) lag];
            sum += a * b;  ea += a * a;  eb += b * b;
        }
        const double norm = std::sqrt (ea * eb) + 1e-12;
        corr[(size_t) lag] = sum / norm;
        best = std::max (best, corr[(size_t) lag]);
    }

    // OCTAVE-ROBUSTNESS. A periodic signal correlates just as well at 2x and 3x
    // its true period, and on a harmonic-rich tone those longer lags often score
    // marginally HIGHER — so taking the global maximum reports the note an
    // octave (or two) too low. The first version of this helper did exactly
    // that and produced 110 Hz for a 220 Hz input, which would have made the
    // transposition tests meaningless. Take instead the SHORTEST lag that is a
    // clear local peak within 8% of the best score.
    int bestLag = 0;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
        if (corr[(size_t) lag] >= best * 0.92
            && corr[(size_t) lag] >= corr[(size_t) lag - 1]
            && corr[(size_t) lag] >= corr[(size_t) lag + 1])
        { bestLag = lag; break; }

    if (bestLag <= minLag || bestLag >= maxLag) return 0.0;

    const double y0 = corr[(size_t) bestLag - 1];
    const double y1 = corr[(size_t) bestLag];
    const double y2 = corr[(size_t) bestLag + 1];
    const double denom = 2.0 * (2.0 * y1 - y0 - y2);
    const double shift = std::abs (denom) > 1e-12
                       ? juce::jlimit (-1.0, 1.0, (y2 - y0) / denom) : 0.0;
    return FS / ((double) bestLag + shift);
}

/** Energy-weighted mean frequency of a settled window — a direct proxy for
    where the spectral envelope (the "size of the throat") sits. */
double centroidHz (const std::vector<float>& v, double fromSec, double lenSec)
{
    const size_t from = (size_t) (fromSec * FS);
    // Largest power-of-two window that fits the requested span (an FFT needs
    // one, and the caller thinks in seconds).
    int order = 10;
    while ((1 << (order + 1)) <= (int) (lenSec * FS) && order < 14) ++order;
    const size_t n = (size_t) 1 << order;
    if (from + n >= v.size()) return 0.0;

    juce::dsp::FFT fft (order);
    std::vector<float> buf (n * 2, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                               * (double) i / (double) (n - 1));
        buf[i] = v[from + i] * (float) w;
    }
    fft.performFrequencyOnlyForwardTransform (buf.data());

    double num = 0.0, den = 0.0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double mag = (double) buf[k];
        const double f = (double) k * FS / (double) n;
        num += mag * f;
        den += mag;
    }
    return den > 1e-9 ? num / den : 0.0;
}

/** A band-limited, vowel-like tone: harmonics of `f0` shaped by two formant
    resonances (roughly an /a/). A NAIVE SAWTOOTH IS THE WRONG TEST SIGNAL here
    and the first version of these tests used one: it carries energy all the way
    to Nyquist, so shifting the formants UP pushes that energy past Nyquist where
    it is lost, and the measured spectral centroid FALLS — the exact opposite of
    the real effect. Band-limiting to 4.2 kHz leaves room for a +12 semitone
    shift to actually land somewhere measurable. */
std::vector<float> vowelTone (double f0, double seconds, float amp, double fs)
{
    std::vector<float> v ((size_t) (seconds * fs));
    struct Res { double hz, q, gain; };
    const Res formants[] = { { 730.0, 90.0, 1.0 }, { 1090.0, 110.0, 0.7 },
                             { 2440.0, 160.0, 0.35 } };

    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = (double) i / fs;
        double sum = 0.0;
        for (int h = 1; h * f0 < 4200.0; ++h)
        {
            const double f = f0 * h;
            double env = 0.02;                     // small floor between formants
            for (const auto& r : formants)
            {
                const double d = (f - r.hz) / r.q;
                env += r.gain * std::exp (-d * d);
            }
            sum += env * std::sin (2.0 * juce::MathConstants<double>::pi * f * t) / std::sqrt ((double) h);
        }
        v[i] = (float) (amp * sum * 0.5);
    }
    return v;
}

/** A character-free params block that runs the grain engine without imposing
    any tuning, i.e. exactly how the Voice Changer uses it. */
vf::RetuneParams shiftOnly (float transpose, float formant)
{
    vf::RetuneParams p;
    p.on = true;
    p.amount = 0.0f;              // no correction — pure shifting
    p.transposeSemis = transpose;
    p.formant = formant;
    p.scaleType = 0;              // chromatic
    return p;
}
} // namespace

void runVoiceChangerTests()
{
    using namespace vftest;
    Suite s ("VoiceChanger");

    // ======================================================================
    //  1. NO REGRESSION — the engine must be untouched when nothing is asked
    // ======================================================================
    {
        vf::RetuneParams p;                      // everything default, on == false
        const auto sig = sineWave (330.0, 0.8, 0.4f, FS);
        const auto out = run (sig, p);
        const int d = vf::RetuneEngine::latencyForMode (2, FS);

        bool exact = true;
        int checked = 0;
        for (size_t k = 0; k + (size_t) d < out.size() && checked < 25000; ++k, ++checked)
            if (out[k + (size_t) d] != sig[k]) { exact = false; break; }

        s.check ("transpose 0 + pitch off: bypass still BYTE-IDENTICAL delayed dry",
                 exact, "compared " + juce::String (checked) + " samples");
    }

    {
        // Deliberate design decision, asserted so nobody "fixes" it by accident:
        // the formant knob alone does NOT wake the engine. It has always been
        // inert with pitch off, and quietly changing that would alter the sound
        // of any existing session that left a formant value behind.
        vf::RetuneParams p;
        p.on = false;
        p.formant = 7.0f;
        const auto sig = sineWave (330.0, 0.8, 0.4f, FS);
        const auto out = run (sig, p);
        const int d = vf::RetuneEngine::latencyForMode (2, FS);

        bool exact = true;
        int checked = 0;
        for (size_t k = 0; k + (size_t) d < out.size() && checked < 20000; ++k, ++checked)
            if (out[k + (size_t) d] != sig[k]) { exact = false; break; }

        s.check ("formant alone with pitch off stays bypassed (documented choice)",
                 exact, "compared " + juce::String (checked) + " samples");
    }

    // ======================================================================
    //  2. TRANSPOSITION ACCURACY — measured, not assumed
    // ======================================================================
    {
        const auto sig = sineWave (220.0, 2.0, 0.4f, FS, 6);   // harmonic-rich
        const double ref = measureHz (run (sig, shiftOnly (0.0f, 0.0f)), 1.2, 0.5);
        s.check ("transpose 0 leaves the note where it was",
                 std::abs (ref - 220.0) < 3.0, juce::String (ref, 2) + " Hz");

        struct Case { float semis; double factor; const char* what; };
        const Case cases[] = {
            { -12.0f, 0.5,      "one octave down" },
            {  12.0f, 2.0,      "one octave up" },
            {  -7.0f, 0.66742,  "a fifth down" },
            {   5.0f, 1.33484,  "a fourth up" },
            { -17.0f, 0.37393,  "Titan territory (-17)" },
        };

        for (const auto& c : cases)
        {
            const auto out = run (sig, shiftOnly (c.semis, 0.0f));
            const double hz = measureHz (out, 1.2, 0.5);
            const double want = 220.0 * c.factor;
            // 1% is ~17 cents — far tighter than any audible transposition error,
            // and loose enough for autocorrelation on a resynthesised signal.
            s.check (c.what,
                     std::abs (hz - want) / want < 0.01,
                     "lands on " + juce::String (hz, 1) + " Hz, wanted "
                                 + juce::String (want, 1) + " Hz");
            s.check (c.what, allFinite (out, 4.0f), "finite and bounded");
        }
    }

    {
        // Transposition must be INDEPENDENT of correction: dropping an octave
        // must not drag an off-key singer onto a scale as a side effect.
        const auto sig = sineWave (233.0, 2.0, 0.4f, FS, 6);   // deliberately not A
        const auto out = run (sig, shiftOnly (-12.0f, 0.0f));
        const double hz = measureHz (out, 1.2, 0.5);
        s.check ("transposition does not quantize the singer to a scale",
                 std::abs (hz - 116.5) / 116.5 < 0.01,
                 juce::String (hz, 1) + " Hz (half of 233, not half of 220 or 246.9)");
    }

    {
        // A transposed note must still be a continuous waveform. Discontinuity
        // here is the classic grain-scheduling bug that was heard as "choppy".
        const auto sig = sineWave (220.0, 1.6, 0.4f, FS);
        const auto out = run (sig, shiftOnly (-12.0f, 0.0f));
        const float jump = maxJump (out, (size_t) (0.8 * FS));
        s.check ("octave-down output is continuous (no clicks)", jump < 0.08f,
                 "max sample-to-sample jump " + juce::String (jump, 4));
    }

    // ======================================================================
    //  3. FORMANTS MOVE INDEPENDENTLY OF PITCH — the core claim
    // ======================================================================
    {
        const auto sig = vowelTone (150.0, 2.0, 0.35f, FS);

        const auto flat = run (sig, shiftOnly (0.0f, 0.0f));
        const auto up   = run (sig, shiftOnly (0.0f, 7.0f));
        const auto down = run (sig, shiftOnly (0.0f, -7.0f));

        const double f0Flat = measureHz (flat, 1.2, 0.5);
        const double f0Up   = measureHz (up,   1.2, 0.5);
        const double f0Down = measureHz (down, 1.2, 0.5);

        const double cFlat = centroidHz (flat, 1.2, 0.1);
        const double cUp   = centroidHz (up,   1.2, 0.1);
        const double cDown = centroidHz (down, 1.2, 0.1);

        s.check ("formant +7 moves the SPECTRUM up", cUp > cFlat * 1.15,
                 juce::String (cFlat, 0) + " Hz -> " + juce::String (cUp, 0) + " Hz");
        s.check ("formant -7 moves the SPECTRUM down", cDown < cFlat * 0.9,
                 juce::String (cFlat, 0) + " Hz -> " + juce::String (cDown, 0) + " Hz");
        s.check ("formant +7 leaves the NOTE alone",
                 std::abs (f0Up - f0Flat) / f0Flat < 0.02,
                 juce::String (f0Flat, 1) + " Hz -> " + juce::String (f0Up, 1) + " Hz");
        s.check ("formant -7 leaves the NOTE alone",
                 std::abs (f0Down - f0Flat) / f0Flat < 0.02,
                 juce::String (f0Flat, 1) + " Hz -> " + juce::String (f0Down, 1) + " Hz");
    }

    {
        // The widened formant range (+/-5 -> +/-12) has to actually reach, and
        // the grain-availability clamp has to survive the widest read span.
        const auto sig = vowelTone (150.0, 2.0, 0.35f, FS);
        const auto wide = run (sig, shiftOnly (0.0f, 12.0f));
        s.check ("formant +12 (the new range limit) is finite and bounded",
                 allFinite (wide, 4.0f));
        s.check ("formant +12 reaches further than +7",
                 centroidHz (wide, 1.2, 0.1) > centroidHz (run (sig, shiftOnly (0.0f, 7.0f)), 1.2, 0.1),
                 juce::String (centroidHz (wide, 1.2, 0.1), 0) + " Hz");
    }

    {
        // The combination is the actual product: a believable "Child" is a note
        // AND a body moving together.
        const auto sig = vowelTone (180.0, 2.0, 0.35f, FS);
        const auto flat  = run (sig, shiftOnly (0.0f, 0.0f));
        const auto child = run (sig, shiftOnly (5.0f, 6.0f));

        const double f0a = measureHz (flat,  1.2, 0.5);
        const double f0b = measureHz (child, 1.2, 0.5);
        s.check ("Child-style shift raises pitch AND spectrum together",
                 f0b > f0a * 1.25 && centroidHz (child, 1.2, 0.1) > centroidHz (flat, 1.2, 0.1) * 1.15,
                 juce::String (f0a, 1) + " -> " + juce::String (f0b, 1) + " Hz");
    }

    {
        // Full sweep: nothing in the widened ranges may produce NaN/inf, and no
        // combination may run away. This is the cheap insurance against the
        // grain-availability clamp being wrong at some corner.
        const auto sig = vowelTone (140.0, 0.9, 0.35f, FS);
        bool ok = true;
        juce::String failedAt;
        for (float t = -24.0f; t <= 24.0f && ok; t += 6.0f)
            for (float f = -12.0f; f <= 12.0f && ok; f += 4.0f)
                if (! allFinite (run (sig, shiftOnly (t, f)), 6.0f))
                { ok = false; failedAt = "transpose " + juce::String (t) + ", formant " + juce::String (f); }

        s.check ("every transpose x formant combination stays finite", ok,
                 ok ? "45 combinations" : failedAt);
    }

    {
        // Every latency mode must survive transposition — Live has the least
        // scheduling slack, so it is where a grain-fit mistake would show.
        const auto sig = sineWave (220.0, 1.2, 0.4f, FS, 4);
        bool ok = true;
        for (int mode = 0; mode <= 2 && ok; ++mode)
        {
            auto p = shiftOnly (-12.0f, -5.0f);
            p.latencyMode = mode;
            ok = allFinite (run (sig, p), 4.0f);
        }
        s.check ("transposition is safe in Live / Balanced / Studio", ok);
    }

    // ======================================================================
    //  4. THE CHARACTER TABLE
    // ======================================================================
    {
        const auto& all = vf::VoiceCharacters::all();
        s.check ("character table is populated", all.size() >= 12,
                 juce::String ((int) all.size()) + " characters");
        s.check ("index 0 is Off (a stable, never-reordered API)",
                 ! all.empty() && all[0].id == "off" && all[0].targets.empty());
        s.check ("the menu list matches the table",
                 vf::VoiceCharacters::menuNames().size() == (int) all.size());
    }

    {
        std::map<juce::String, int> ids;
        std::map<juce::String, juce::String> aliasOwner;
        bool dupId = false, dupAlias = false;
        juce::String clash;

        for (const auto& p : vf::VoiceCharacters::all())
        {
            if (++ids[p.id] > 1) dupId = true;
            for (const auto& a : p.aliases)
            {
                const auto key = a.toLowerCase();
                if (aliasOwner.count (key) && aliasOwner[key] != p.id)
                { dupAlias = true; clash = key; }
                aliasOwner[key] = p.id;
            }
        }
        s.check ("every character id is unique", ! dupId);
        s.check ("no alias is claimed by two characters", ! dupAlias, clash);
    }

    {
        // Longest-alias resolution — the same rule that makes sub-genres work.
        struct M { const char* text; const char* wantId; };
        const M cases[] = {
            { "make me sound demonic",          "demonic"   },
            { "turn this into a deep demon",    "titan"     },   // beats "demon"
            { "i want a child voice",           "child"     },
            { "give me a robot voice",          "robot"     },
            { "make it sound like an old woman","old_man"   },
            { "chipmunk please",                "chipmunk"  },
            { "big movie trailer voice",        "radio"     },
            { "make it sound like a phone call","telephone" },
            { "husky and sultry",               "smoky"     },
            { "ethereal ghostly vocal",         "ghost"     },
        };
        bool ok = true;
        juce::String bad;
        for (const auto& c : cases)
        {
            const auto* got = vf::VoiceCharacters::match (juce::String (c.text));
            if (got == nullptr || got->id != c.wantId)
            { ok = false; bad = juce::String (c.text) + " -> "
                              + (got ? got->id : juce::String ("(none)")); break; }
        }
        s.check ("longest-alias matching resolves every character", ok, bad);
    }

    {
        // NO FALSE POSITIVES. Ordinary mixing requests must never be hijacked
        // into a costume — this is what broke the old hard-coded genre list.
        const char* plain[] = { "make it brighter", "more reverb", "less autotune",
                                "make it emo trap", "punchier", "tuck it back",
                                "add a de-esser", "warmer please" };
        bool clean = true;
        juce::String bad;
        for (const auto* t : plain)
            if (const auto* g = vf::VoiceCharacters::match (juce::String (t)))
            { clean = false; bad = juce::String (t) + " -> " + g->id; break; }

        s.check ("plain mixing requests match no character", clean, bad);
    }

    {
        bool roundTrips = true, described = true, engaged = true, inRange = true;
        juce::String bad;

        for (const auto& p : vf::VoiceCharacters::all())
        {
            const int idx = vf::VoiceCharacters::indexOf (p.id);
            if (vf::VoiceCharacters::byIndex (idx) != &p) roundTrips = false;
            if (vf::VoiceCharacters::byId (p.id) != &p)   roundTrips = false;

            if (p.id == "off") continue;

            // Every character must EXPLAIN itself — the project's transparency
            // rule applies to costumes as much as to AI mix decisions.
            if (p.sound.isEmpty() || p.why.isEmpty() || p.category.isEmpty())
            { described = false; bad = p.id; }

            bool turnsPitchOn = false;
            for (const auto& t : p.targets)
            {
                const juce::String id (t.paramId);
                if (id == "pitch_on" && t.value > 0.5f) turnsPitchOn = true;

                // Ranges that this feature widened, guarded at the source.
                if (id == "pitch_transpose" && std::abs (t.value) > 24.0f)
                { inRange = false; bad = p.id + " transpose"; }
                if (id == "pitch_formant" && std::abs (t.value) > 12.0f)
                { inRange = false; bad = p.id + " formant"; }
            }
            // Without this the grain engine never runs and the character is silent.
            if (! turnsPitchOn) { engaged = false; bad = p.id; }
        }

        s.check ("byId / byIndex / indexOf all round-trip", roundTrips);
        s.check ("every character explains what it does and why", described, bad);
        s.check ("every character engages the pitch engine (or it would be silent)",
                 engaged, bad);
        s.check ("every transpose/formant target is inside the widened ranges",
                 inRange, bad);
    }

    {
        // Spot-check the craft, not just the plumbing: the three archetypes the
        // whole design rests on must actually be built the way the header claims.
        const auto* demonic = vf::VoiceCharacters::byId ("demonic");
        const auto* child   = vf::VoiceCharacters::byId ("child");
        const auto* robot   = vf::VoiceCharacters::byId ("robot");

        auto valueOf = [] (const vf::VoiceProfile* p, const char* id, float fallback)
        {
            if (p == nullptr) return fallback;
            for (const auto& t : p->targets)
                if (juce::String (t.paramId) == id) return t.value;
            return fallback;
        };

        s.check ("Demonic pushes formants BELOW an already-lowered pitch",
                 valueOf (demonic, "pitch_transpose", 0.0f) <= -12.0f
                 && valueOf (demonic, "pitch_formant", 0.0f) < 0.0f,
                 "transpose " + juce::String (valueOf (demonic, "pitch_transpose", 0.0f))
                 + ", formant " + juce::String (valueOf (demonic, "pitch_formant", 0.0f)));

        s.check ("Child moves pitch and formants the SAME way (a real small person)",
                 valueOf (child, "pitch_transpose", 0.0f) > 0.0f
                 && valueOf (child, "pitch_formant", 0.0f) > 0.0f,
                 "transpose " + juce::String (valueOf (child, "pitch_transpose", 0.0f))
                 + ", formant " + juce::String (valueOf (child, "pitch_formant", 0.0f)));

        s.check ("Robot uses no shifting at all — it is Hard Tune that sells it",
                 std::abs (valueOf (robot, "pitch_transpose", 99.0f)) < 0.01f
                 && std::abs (valueOf (robot, "pitch_formant", 99.0f)) < 0.01f
                 && valueOf (robot, "pitch_hardtune", 0.0f) >= 99.0f);
    }
}
