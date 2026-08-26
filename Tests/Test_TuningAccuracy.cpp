#include "TestFramework.h"
#include "../Source/DSP/RetuneEngine.h"
#include <algorithm>

// ============================================================================
//  Tuning accuracy — the END-TO-END property a user actually hears.
//
//  WHY THIS SUITE EXISTS. Every other pitch suite in this project tests a
//  COMPONENT (the tracker finds the right f0; the engine snaps 445 to 440 with
//  a deliberately hard-tuned configuration; bypass is byte-identical). All of
//  them passed while the shipping plugin was still leaving audibly out-of-tune
//  notes uncorrected, because none of them ever asked the only question the
//  singer cares about:
//
//      "With the plugin as it comes out of the box, and Correction on 100,
//       how far off pitch is the note that comes out?"
//
//  That question has one honest answer — measured residual error in cents,
//  against a pitch estimator that shares no code with the engine. A control
//  labelled 100% that removes half the error is a broken control, however
//  elegant the machinery behind it.
//
//  REFERENCE POINTS (cents): 5 is inaudible, 10 is the edge of audible on a
//  sustained note, 20 is clearly flat/sharp to a producer, 50 is a quarter tone.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FS = 44100.0;

/** A sustained vowel-like tone: harmonic-rich but band-limited, shaped by three
    formant resonances so it excites the tracker the way a voice does rather
    than the way a synthetic saw does. */
std::vector<float> vowel (double hz, double seconds, float amp)
{
    const size_t n = (size_t) (seconds * FS);
    std::vector<float> v (n, 0.0f);
    const double f[3]  = { 700.0, 1220.0, 2600.0 };
    const double bw[3] = { 130.0, 190.0, 280.0 };

    for (int h = 1; h * hz < 4200.0; ++h)
    {
        const double fh = hz * h;
        double gain = 0.0;
        for (int r = 0; r < 3; ++r)
        {
            const double d = (fh - f[r]) / bw[r];
            gain += 1.0 / (1.0 + d * d);
        }
        gain /= (double) h;                       // natural spectral rolloff
        const double w = 2.0 * juce::MathConstants<double>::pi * fh / FS;
        for (size_t i = 0; i < n; ++i)
            v[i] += (float) (gain * std::sin (w * (double) i));
    }

    float peak = 0.0f;
    for (float sample : v) peak = juce::jmax (peak, std::abs (sample));
    if (peak > 0.0f) for (auto& sample : v) sample *= amp / peak;
    return v;
}

/** Autocorrelation pitch estimate, independent of the engine own tracker.
    Takes the SHORTEST clear local peak rather than the global maximum: a
    periodic signal correlates just as well at 2x and 3x its period, and on a
    harmonic-rich tone the longer lags often score marginally higher. */
double measureHz (const std::vector<float>& v, double fromSec, double lenSec,
                  double loHz = 60.0, double hiHz = 1500.0)
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
        corr[(size_t) lag] = sum / (std::sqrt (ea * eb) + 1e-12);
        best = std::max (best, corr[(size_t) lag]);
    }

    int bestLag = 0;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
        if (corr[(size_t) lag] >= best * 0.92
            && corr[(size_t) lag] >= corr[(size_t) lag - 1]
            && corr[(size_t) lag] >= corr[(size_t) lag + 1])
        { bestLag = lag; break; }

    if (bestLag <= minLag || bestLag >= maxLag) return 0.0;

    const double y0 = corr[(size_t) bestLag - 1], y1 = corr[(size_t) bestLag],
                 y2 = corr[(size_t) bestLag + 1];
    const double denom = 2.0 * (2.0 * y1 - y0 - y2);
    const double shift = std::abs (denom) > 1e-12 ? (y2 - y0) / denom : 0.0;
    return FS / ((double) bestLag + juce::jlimit (-1.0, 1.0, shift));
}

double centsBetween (double a, double b) { return 1200.0 * std::log2 (a / b); }

/** The CENTRE of a note that is moving. A single autocorrelation window over a
    vibrato tone does not measure the centre — the correlation peak is pulled by
    the swing, and the error grows with vibrato depth, which is exactly the
    regime this suite cares about. Sliding short windows and taking the MEDIAN in
    the cents domain is robust to that: half the windows sit above the centre and
    half below, whatever the shape of the modulation. */
double measureCentreHz (const std::vector<float>& v, double fromSec, double lenSec)
{
    std::vector<double> est;
    const double win = 0.075;                       // ~ a third of a vibrato cycle
    for (double t = fromSec; t + win < fromSec + lenSec; t += win * 0.5)
    {
        const double hz = measureHz (v, t, win);
        if (hz > 40.0 && hz < 1500.0) est.push_back (hz);
    }
    if (est.empty()) return 0.0;
    std::sort (est.begin(), est.end());
    return est[est.size() / 2];
}

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

/** The parameters as they arrive from the FACTORY DEFAULTS in Parameters.cpp.
    Kept in one place so the suite tests the shipping configuration and not a
    convenient one invented for the test. */
vf::RetuneParams factoryDefaults()
{
    vf::RetuneParams p;
    p.on        = true;
    p.scaleType = 1;          // Major
    p.keyRoot   = 9;          // A  -> A major contains A440
    p.speedMs   = 60.0f;
    p.amount    = 1.0f;       // "Correction 100%"
    p.humanize  = 0.0f;
    p.flexTune            = 0.35f;
    p.vibratoPreservation = 0.75f;
    p.transitionSmoothing = 0.60f;
    p.driftCorrection     = 0.50f;
    p.sensitivity         = 0.5f;
    p.hardTune            = 0.0f;
    p.snapThreshold       = 0.0f;
    return p;
}
} // namespace

void runTuningAccuracyTests()
{
    Suite s ("Tuning accuracy (factory defaults, Correction 100%)");

    // ======================================================================
    //  1. THE HEADLINE PROPERTY — a settled, sustained note that is simply
    //     off pitch must actually arrive on pitch.
    // ======================================================================
    struct Case { double offCents; const char* what; };
    const Case cases[] = {
        { -45.0, "sustained note 45 cents flat lands within 10 cents of A440"  },
        { -25.0, "sustained note 25 cents flat lands within 10 cents of A440"  },
        { -15.0, "sustained note 15 cents flat lands within 10 cents of A440"  },
        {  18.0, "sustained note 18 cents sharp lands within 10 cents of A440" },
        {  30.0, "sustained note 30 cents sharp lands within 10 cents of A440" },
    };

    for (const auto& c : cases)
    {
        const double sungHz = 440.0 * std::pow (2.0, c.offCents / 1200.0);
        const auto out = run (vowel (sungHz, 2.4, 0.4f), factoryDefaults());
        const double hz = measureHz (out, 1.6, 0.6);
        const double residual = centsBetween (hz, 440.0);

        s.check (c.what,
                 std::abs (residual) <= 10.0,
                 "residual " + juce::String (residual, 1) + " cents  (sang "
                 + juce::String (c.offCents, 0) + ", out " + juce::String (hz, 2) + " Hz)");
    }

    // ======================================================================
    //  2. THE CONTROL MUST MEAN SOMETHING. Correction at 50% should leave
    //     roughly half the error, and at 0% the note must be untouched.
    //     Without this, "Correction" is decoration.
    // ======================================================================
    {
        const double sungHz = 440.0 * std::pow (2.0, -40.0 / 1200.0);
        const auto sig = vowel (sungHz, 2.4, 0.4f);

        auto p0 = factoryDefaults(); p0.amount = 0.0f;
        const double at0 = centsBetween (measureHz (run (sig, p0), 1.6, 0.6), 440.0);
        s.check ("Correction 0% leaves the note alone", std::abs (at0 - (-40.0)) <= 8.0,
                 "residual " + juce::String (at0, 1) + " cents (sang -40)");

        auto p50 = factoryDefaults(); p50.amount = 0.5f;
        const double at50 = centsBetween (measureHz (run (sig, p50), 1.6, 0.6), 440.0);
        s.check ("Correction 50% removes roughly half the error",
                 at50 < -8.0 && at50 > -32.0,
                 "residual " + juce::String (at50, 1) + " cents (sang -40)");
    }

    // ======================================================================
    //  3. VIBRATO — both halves of the contract at once.
    //
    //     This is the regression that mattered most. A vibrato note used to
    //     come out exactly as flat as it went in (35 cents in, 35 cents out),
    //     for two compounding reasons: the centre pull was throttled by the
    //     preservation setting, and the scale quantizer chased the INSTANTANEOUS
    //     pitch, so mid-swing the nearest allowed note flipped between A and G#
    //     and the correction averaged to nothing. A third fault hid behind
    //     those: the note centre was seeded from whatever pitch the note
    //     happened to start on and then frozen, so a note beginning mid-swing
    //     kept a permanent offset.
    //
    //     Both properties are asserted together on purpose. Either one alone is
    //     trivially satisfiable by breaking the other — correction with no
    //     preservation flattens the singer, preservation with no correction is
    //     what shipped.
    // ======================================================================
    {
        auto vibratoSignal = [] (double centreOffsetCents, double depthCents)
        {
            const double centreHz = 440.0 * std::pow (2.0, centreOffsetCents / 1200.0);
            const size_t n = (size_t) (2.6 * FS);
            std::vector<float> sig (n, 0.0f);
            double phase = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                const double t = (double) i / FS;
                const double dev = depthCents * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t);
                const double hz = centreHz * std::pow (2.0, dev / 1200.0);
                phase += 2.0 * juce::MathConstants<double>::pi * hz / FS;
                sig[i] = (float) (0.4 * (std::sin (phase) + 0.5 * std::sin (2.0 * phase)
                                                          + 0.25 * std::sin (3.0 * phase)));
            }
            return sig;
        };

        struct VibCase { double depth; const char* what; };
        const VibCase vibs[] = {
            { 15.0, "a gentle-vibrato note 35 cents flat is centred on A440"  },
            { 25.0, "a medium-vibrato note 35 cents flat is centred on A440"  },
            { 40.0, "a deep-vibrato note 35 cents flat is centred on A440"    },
        };
        for (const auto& v : vibs)
        {
            const auto out = run (vibratoSignal (-35.0, v.depth), factoryDefaults());
            const double residual = centsBetween (measureCentreHz (out, 1.4, 1.0), 440.0);
            s.check (v.what, std::abs (residual) <= 6.0,
                     "centre residual " + juce::String (residual, 1) + " cents (swing +/-"
                     + juce::String (v.depth, 0) + ")");
        }

        // ...and the swing itself must still be there. Preservation is measured
        // as delivered output depth, not as the value of the setting.
        const auto sig = vibratoSignal (-35.0, 40.0);

        // MEASURING SWING DEPTH needs a window materially shorter than the
        // vibrato period: a 75 ms window over a 200 ms cycle averages away more
        // than a third of the modulation, which was enough to invert the
        // comparison below and make a working control look broken. 35 ms still
        // holds ~15 cycles of a 440 Hz tone, so the pitch estimate stays solid.
        auto outputDepth = [&] (float preservation, float amount)
        {
            auto p = factoryDefaults();
            p.vibratoPreservation = preservation;
            p.amount = amount;
            const auto out = run (sig, p);
            std::vector<double> c;
            for (double t = 1.4; t + 0.035 < 2.4; t += 0.01)
            {
                const double hz = measureHz (out, t, 0.035);
                if (hz > 200.0 && hz < 900.0) c.push_back (centsBetween (hz, 440.0));
            }
            if (c.size() < 8) return 0.0;
            std::sort (c.begin(), c.end());
            // 10th..90th percentile span — robust to the odd stray window.
            return c[(size_t) (c.size() * 9 / 10)] - c[c.size() / 10];
        };

        // Baseline: what the measurement reports when nothing is corrected at
        // all. Quoting the other numbers against this rather than against the
        // nominal 80 cents keeps the claim honest about the estimator itself.
        const double untouched = outputDepth (0.75f, 0.0f);
        const double kept      = outputDepth (0.75f, 1.0f);   // shipping default
        const double flatten   = outputDepth (0.0f,  1.0f);

        const double full = outputDepth (1.0f, 1.0f);

        s.check ("preservation 1.0 is transparent to the swing", full > untouched * 0.95,
                 juce::String (full, 1) + " cents delivered vs "
                 + juce::String (untouched, 1) + " uncorrected");
        s.check ("vibrato survives at the default preservation", kept > untouched * 0.7,
                 juce::String (kept, 1) + " cents delivered vs "
                 + juce::String (untouched, 1) + " uncorrected");
        // HOW FAR DOWN THIS GOES IS BOUNDED BY THE TRACKER, not by the control.
        // f0 is estimated over a 23 ms window with a further 23 ms of Viterbi
        // hindsight, so the deviation the engine can see is already a smoothed
        // version of the real swing and cancelling it exactly still leaves
        // roughly a quarter of the modulation behind. Turning preservation down
        // therefore REDUCES vibrato rather than erasing it; erasing it is what
        // Hard Tune is for, and that takes a different path (it quantizes the
        // instantaneous pitch directly). Asserting the achievable behaviour
        // keeps this suite honest — an aspirational threshold here would only
        // tempt someone into over-compensating, which is exactly how the
        // swing-exaggeration artifact got in.
        s.check ("turning preservation down clearly reduces the swing",
                 flatten < untouched * 0.8 && flatten < kept * 0.95,
                 juce::String (flatten, 1) + " cents at preservation 0, vs "
                 + juce::String (kept, 1) + " at default and "
                 + juce::String (untouched, 1) + " uncorrected");
    }

    // ======================================================================
    //  4. HARD TUNE remains absolute — the existing contract, re-checked here
    //     because the fixes for the above must not reach it.
    // ======================================================================
    {
        auto p = factoryDefaults();
        p.hardTune = 1.0f;
        p.speedMs  = 0.0f;
        const auto out = run (vowel (445.0, 2.0, 0.4f), p);
        const double hz = measureHz (out, 1.3, 0.5);
        s.check ("Hard Tune 100% snaps 445 Hz onto A440",
                 std::abs (centsBetween (hz, 440.0)) <= 12.0,
                 juce::String (hz, 2) + " Hz");
    }
}
