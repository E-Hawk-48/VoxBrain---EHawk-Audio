#include "TestFramework.h"
#include "../Source/DSP/VocalChain.h"
#include <algorithm>

// ============================================================================
//  Chain modules — does each one actually DO what its control says?
//
//  The pitch engine shipped for months with a control labelled "Correction
//  100%" that removed none of the error on an expressive take, and 119 passing
//  component tests never noticed, because none of them measured the delivered
//  result. This suite asks the same blunt question of every remaining module in
//  the fixed chain:
//
//      * with the module OFF, is the audio genuinely untouched?
//      * when the headline control moves, does the output move the RIGHT WAY,
//        by a measurable amount?
//      * is the result finite, bounded, and free of discontinuities?
//
//  These are deliberately end-to-end, measured in dB / spectral balance rather
//  than by inspecting internal state, so they stay true if the implementation
//  is rewritten.
//
//  NOTE ON LATENCY: VocalChain ALWAYS applies the retune lookback, even with
//  correction off, because bypass deliberately delays the dry signal so that
//  toggling never shifts timing. Any sample-for-sample comparison against the
//  input must offset by RetuneEngine::latencyForMode(2, fs). Three smoothing
//  tests were written without that offset and failed against correct code.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FS = 44100.0;
constexpr int    BLOCK = 256;

int chainLatency() { return vf::RetuneEngine::latencyForMode (2, FS); }

/** Neutral chain parameters: every module off, unity trims. Each test switches
    on exactly the one module it is measuring, so nothing else can contribute. */
vf::ChainParams neutral()
{
    vf::ChainParams p {};
    p.inputGainDb = 0.0f;
    p.outputGainDb = 0.0f;
    p.retune.on = false;
    p.retune.transposeSemis = 0.0f;
    p.gate.on = false;   p.gate.thresholdDb = -60.0f;
    p.eq.on = false;     p.eq.hpfHz = 20.0f; p.eq.lowShelfDb = 0.0f; p.eq.mudDb = 0.0f;
                         p.eq.mudHz = 300.0f; p.eq.presDb = 0.0f; p.eq.presHz = 3000.0f;
                         p.eq.airDb = 0.0f;
    p.dyneq.on = false;  p.dyneq.lowThreshDb = 0.0f; p.dyneq.lowFreqHz = 250.0f;
                         p.dyneq.midThreshDb = 0.0f; p.dyneq.midFreqHz = 1000.0f;
                         p.dyneq.highThreshDb = 0.0f; p.dyneq.highFreqHz = 6000.0f;
                         p.dyneq.rangeDb = 6.0f;
    p.deess.on = false;  p.deess.thresholdDb = -20.0f; p.deess.freqHz = 7000.0f;
    p.comp.on = false;   p.comp.thresholdDb = -18.0f; p.comp.ratio = 3.0f;
                         p.comp.attackMs = 10.0f; p.comp.releaseMs = 120.0f;
                         p.comp.makeupDb = 0.0f; p.comp.mix = 1.0f;
    p.mband.on = false;  p.mband.lowThreshDb = 0.0f; p.mband.midThreshDb = 0.0f;
                         p.mband.highThreshDb = 0.0f; p.mband.lowGainDb = 0.0f;
                         p.mband.midGainDb = 0.0f; p.mband.highGainDb = 0.0f;
                         p.mband.ratio = 2.0f; p.mband.lowXoverHz = 200.0f;
                         p.mband.highXoverHz = 3000.0f;
    p.sat.on = false;    p.sat.type = 0; p.sat.drive = 0.0f; p.sat.tone = 0.5f;
                         p.sat.bias = 0.5f; p.sat.mix = 1.0f; p.sat.hq = false;
    p.delay.on = false;  p.delay.timeMs = 120.0f; p.delay.feedback = 0.2f; p.delay.mix = 0.0f;
    p.verb.on = false;   p.verb.type = 0; p.verb.size = 0.5f; p.verb.decay = 0.5f;
                         p.verb.predelayMs = 10.0f; p.verb.damp = 0.5f;
                         p.verb.diffusion = 0.6f; p.verb.lowCutHz = 200.0f;
                         p.verb.highCutHz = 9000.0f; p.verb.modDepth = 0.2f;
                         p.verb.width = 1.0f; p.verb.mix = 0.0f; p.verb.duck = 0.0f;
                         p.verb.shimmer = 0.0f; p.verb.freeze = false;
    p.limit.on = false;  p.limit.ceilingDb = -0.3f; p.limit.gainDb = 0.0f;
    return p;
}

std::vector<float> run (const std::vector<float>& sig, const vf::ChainParams& p)
{
    vf::VocalChain chain;
    juce::dsp::ProcessSpec spec { FS, (juce::uint32) BLOCK, 2 };
    chain.prepare (spec);

    std::vector<float> out;
    out.reserve (sig.size());
    juce::AudioBuffer<float> buf (2, BLOCK);

    for (size_t off = 0; off + BLOCK <= sig.size(); off += BLOCK)
    {
        for (int i = 0; i < BLOCK; ++i)
        {
            buf.setSample (0, i, sig[off + (size_t) i]);
            buf.setSample (1, i, sig[off + (size_t) i]);
        }
        chain.process (buf, p);
        for (int i = 0; i < BLOCK; ++i) out.push_back (buf.getSample (0, i));
    }
    return out;
}

double rmsDb (const std::vector<float>& v, size_t from, size_t to)
{
    to = std::min (to, v.size());
    if (from >= to) return -200.0;
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
    return 10.0 * std::log10 (s / (double) (to - from) + 1e-20);
}

/** Energy in a frequency band, via a plain Goertzel-style sum over a set of
    probe tones. Cheap and sufficient: every tonal test here drives the chain
    with a known sum of sinusoids, so band energy is measured at those exact
    frequencies rather than by an FFT. */
double toneLevelDb (const std::vector<float>& v, double hz, size_t from, size_t n)
{
    if (from + n > v.size()) return -200.0;
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double a = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / FS;
        re += v[from + i] * std::cos (a);
        im += v[from + i] * std::sin (a);
    }
    const double mag = 2.0 * std::sqrt (re * re + im * im) / (double) n;
    return 20.0 * std::log10 (mag + 1e-12);
}

std::vector<float> twoTone (double loHz, double hiHz, double seconds, float amp)
{
    const size_t n = (size_t) (seconds * FS);
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / FS;
        v[i] = (float) (amp * (std::sin (2.0 * juce::MathConstants<double>::pi * loHz * t)
                             + std::sin (2.0 * juce::MathConstants<double>::pi * hiHz * t)) * 0.5);
    }
    return v;
}
} // namespace

void runChainModuleTests()
{
    Suite s ("Chain modules (does the control do what it says?)");

    const int lat = chainLatency();
    const auto tone = twoTone (200.0, 6000.0, 2.0, 0.35f);

    // ======================================================================
    //  1. OFF MUST MEAN OFF. A module that colours the sound while switched
    //     off is the single most confusing kind of bug, because every other
    //     control then behaves inconsistently.
    // ======================================================================
    {
        const auto out = run (tone, neutral());
        double worst = 0.0;
        for (size_t i = (size_t) lat + 4096; i + 1 < out.size(); ++i)
            worst = std::max (worst, (double) std::abs (out[i] - tone[i - (size_t) lat]));
        s.check ("every module off = the input, sample for sample", worst < 1.0e-6,
                 "largest deviation " + juce::String (worst, 9));
    }

    // ======================================================================
    //  2. EQ — each band moves its own frequency, in the right direction.
    // ======================================================================
    {
        const size_t from = (size_t) lat + 8192, n = 16384;
        auto flat = neutral(); flat.eq.on = true;
        const auto base = run (tone, flat);
        const double base200 = toneLevelDb (base, 200.0, from, n);
        const double base6k  = toneLevelDb (base, 6000.0, from, n);

        // Air is a shelf at 12 kHz, so it is measured AT 12 kHz. Probing it at
        // 6 kHz reports almost nothing and says more about the probe than the
        // filter. A separate two-tone signal is used so the high partial sits
        // on the shelf rather than an octave below it.
        const auto airTone = twoTone (200.0, 12000.0, 2.0, 0.35f);
        const auto airBase = run (airTone, flat);
        auto boostAir = flat; boostAir.eq.airDb = 6.0f;
        const auto air = run (airTone, boostAir);
        const double dAir = toneLevelDb (air, 12000.0, from, n)
                          - toneLevelDb (airBase, 12000.0, from, n);
        const double dLow = toneLevelDb (air, 200.0, from, n)
                          - toneLevelDb (airBase, 200.0, from, n);
        s.check ("EQ Air lifts the top without touching the low tone",
                 dAir > 3.0 && std::abs (dLow) < 1.0,
                 "12 kHz " + juce::String (dAir, 2) + " dB, 200 Hz "
                 + juce::String (dLow, 2) + " dB");
        juce::ignoreUnused (base6k);

        auto hpf = flat; hpf.eq.hpfHz = 400.0f;
        const auto hp = run (tone, hpf);
        s.check ("EQ high-pass at 400 Hz attenuates a 200 Hz tone",
                 toneLevelDb (hp, 200.0, from, n) - base200 < -6.0,
                 juce::String (toneLevelDb (hp, 200.0, from, n) - base200, 2) + " dB");

        auto cutMud = flat; cutMud.eq.mudHz = 200.0f; cutMud.eq.mudDb = -8.0f;
        const auto mud = run (tone, cutMud);
        s.check ("EQ Mud cut reduces its own band",
                 toneLevelDb (mud, 200.0, from, n) - base200 < -3.0,
                 juce::String (toneLevelDb (mud, 200.0, from, n) - base200, 2) + " dB");
    }

    // ======================================================================
    //  3. GATE — closes below threshold, stays open above it.
    // ======================================================================
    {
        const auto loud  = twoTone (300.0, 3000.0, 1.2, 0.4f);
        const auto quiet = twoTone (300.0, 3000.0, 1.2, 0.004f);   // ~ -48 dB
        auto p = neutral(); p.gate.on = true; p.gate.thresholdDb = -40.0f;

        const auto gl = run (loud, p), gq = run (quiet, p);
        const double loudDrop  = rmsDb (gl, (size_t) lat + 8192, gl.size())
                               - rmsDb (loud, 8192, loud.size());
        const double quietDrop = rmsDb (gq, (size_t) lat + 8192, gq.size())
                               - rmsDb (quiet, 8192, quiet.size());
        s.check ("Gate leaves a loud signal alone", std::abs (loudDrop) < 1.0,
                 juce::String (loudDrop, 2) + " dB");
        s.check ("Gate attenuates a signal below the threshold", quietDrop < -10.0,
                 juce::String (quietDrop, 2) + " dB");
    }

    // ======================================================================
    //  4. COMPRESSOR — reduces dynamic range, and makeup restores level.
    // ======================================================================
    {
        const auto hot = twoTone (300.0, 3000.0, 1.5, 0.7f);
        auto p = neutral(); p.comp.on = true;
        p.comp.thresholdDb = -24.0f; p.comp.ratio = 6.0f; p.comp.makeupDb = 0.0f;

        const double inLvl  = rmsDb (hot, 8192, hot.size());
        const double outLvl = rmsDb (run (hot, p), (size_t) lat + 8192, hot.size());
        s.check ("Compressor above threshold reduces level", outLvl - inLvl < -4.0,
                 juce::String (outLvl - inLvl, 2) + " dB gain reduction");

        auto pm = p; pm.comp.makeupDb = 6.0f;
        const double mkLvl = rmsDb (run (hot, pm), (size_t) lat + 8192, hot.size());
        s.check ("Compressor makeup adds the gain it says it does",
                 std::abs ((mkLvl - outLvl) - 6.0) < 1.0,
                 juce::String (mkLvl - outLvl, 2) + " dB for a 6 dB setting");

        auto pd = p; pd.comp.mix = 0.0f;
        const auto dry = run (hot, pd);
        double worst = 0.0;
        for (size_t i = (size_t) lat + 8192; i + 1 < dry.size(); ++i)
            worst = std::max (worst, (double) std::abs (dry[i] - hot[i - (size_t) lat]));
        s.check ("Compressor at mix 0 is bit-exact dry", worst < 1.0e-6,
                 "largest deviation " + juce::String (worst, 9));
    }

    // ======================================================================
    //  5. DE-ESSER — pulls down the sibilance band, not the body.
    // ======================================================================
    {
        const auto sibilant = twoTone (250.0, 7500.0, 1.6, 0.5f);
        const size_t from = (size_t) lat + 16384, n = 16384;
        auto off = neutral();
        auto on  = neutral(); on.deess.on = true;
        on.deess.thresholdDb = -34.0f; on.deess.freqHz = 7000.0f;

        const auto a = run (sibilant, off), b = run (sibilant, on);
        const double dHi = toneLevelDb (b, 7500.0, from, n) - toneLevelDb (a, 7500.0, from, n);
        const double dLo = toneLevelDb (b,  250.0, from, n) - toneLevelDb (a,  250.0, from, n);
        s.check ("De-esser reduces the sibilance band", dHi < -2.0,
                 juce::String (dHi, 2) + " dB at 7.5 kHz");
        s.check ("De-esser leaves the body of the voice alone", std::abs (dLo) < 1.5,
                 juce::String (dLo, 2) + " dB at 250 Hz");
    }

    // ======================================================================
    //  6. SATURATION — drive adds harmonics; mix 0 stays clean.
    // ======================================================================
    {
        const auto sine = sineWave (400.0, 1.2, 0.35f, FS);
        const size_t from = (size_t) lat + 8192, n = 16384;

        auto p = neutral(); p.sat.on = true; p.sat.drive = 0.8f; p.sat.mix = 1.0f;
        const auto driven = run (sine, p);
        auto clean = neutral();
        const auto ref = run (sine, clean);
        const double h3 = toneLevelDb (driven, 1200.0, from, n)
                        - toneLevelDb (ref,    1200.0, from, n);
        s.check ("Saturation drive actually generates harmonics", h3 > 12.0,
                 juce::String (h3, 1) + " dB more 3rd harmonic");
        s.check ("Saturation output stays bounded", allFinite (driven, 4.0f));

        auto pd = p; pd.sat.mix = 0.0f;
        const auto dry = run (sine, pd);
        double worst = 0.0;
        for (size_t i = from; i + 1 < dry.size(); ++i)
            worst = std::max (worst, (double) std::abs (dry[i] - sine[i - (size_t) lat]));
        s.check ("Saturation at mix 0 is bit-exact dry", worst < 1.0e-6,
                 "largest deviation " + juce::String (worst, 9));
    }

    // ======================================================================
    //  7. REVERB / DELAY — mix 0 is silent-wet; mix up adds a tail that decays.
    // ======================================================================
    {
        auto p = neutral(); p.verb.on = true; p.verb.mix = 0.0f;
        const auto sine = sineWave (500.0, 1.0, 0.3f, FS);
        const auto dry = run (sine, p);
        double worst = 0.0;
        for (size_t i = (size_t) lat + 4096; i + 1 < dry.size(); ++i)
            worst = std::max (worst, (double) std::abs (dry[i] - sine[i - (size_t) lat]));
        s.check ("Reverb at mix 0 is bit-exact dry", worst < 1.0e-6,
                 "largest deviation " + juce::String (worst, 9));

        // A burst followed by silence: the tail must exist, then die away.
        std::vector<float> burst ((size_t) (3.0 * FS), 0.0f);
        for (size_t i = 0; i < (size_t) (0.25 * FS); ++i)
            burst[i] = (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                * 600.0 * (double) i / FS));
        auto pv = neutral(); pv.verb.on = true; pv.verb.mix = 0.6f; pv.verb.decay = 0.6f;
        const auto wet = run (burst, pv);
        const double tail = rmsDb (wet, (size_t) (0.6 * FS), (size_t) (1.0 * FS));
        const double late = rmsDb (wet, (size_t) (2.4 * FS), (size_t) (2.9 * FS));
        s.check ("Reverb produces a tail after the input stops", tail > -60.0,
                 juce::String (tail, 1) + " dB");
        s.check ("Reverb tail decays instead of ringing forever", late < tail - 12.0,
                 juce::String (late, 1) + " dB late vs " + juce::String (tail, 1) + " dB early");
        s.check ("Reverb output stays finite", allFinite (wet, 4.0f));

        auto pdly = neutral(); pdly.delay.on = true; pdly.delay.mix = 0.5f;
        pdly.delay.timeMs = 150.0f; pdly.delay.feedback = 0.4f;
        const auto dl = run (burst, pdly);
        s.check ("Delay produces repeats after the input stops",
                 rmsDb (dl, (size_t) (0.5 * FS), (size_t) (0.8 * FS)) > -60.0,
                 juce::String (rmsDb (dl, (size_t) (0.5 * FS), (size_t) (0.8 * FS)), 1) + " dB");
        s.check ("Delay output stays finite", allFinite (dl, 4.0f));
    }

    // ======================================================================
    //  8. LIMITER — genuinely caps the peak at the ceiling.
    // ======================================================================
    {
        const auto hot = sineWave (300.0, 1.2, 0.95f, FS);
        auto p = neutral(); p.limit.on = true; p.limit.ceilingDb = -6.0f; p.limit.gainDb = 12.0f;
        const auto out = run (hot, p);
        float peak = 0.0f;
        for (size_t i = (size_t) lat + 8192; i < out.size(); ++i) peak = juce::jmax (peak, std::abs (out[i]));
        const double peakDb = 20.0 * std::log10 (peak + 1e-9);
        s.check ("Limiter holds the ceiling even when driven 12 dB hot", peakDb < -5.0,
                 juce::String (peakDb, 2) + " dB peak for a -6 dB ceiling");
    }

    // ======================================================================
    //  9. NOTHING CLICKS. Every module on at once, on real-ish material.
    // ======================================================================
    {
        auto p = neutral();
        p.eq.on = p.gate.on = p.deess.on = p.comp.on = p.sat.on = true;
        p.dyneq.on = p.mband.on = p.delay.on = p.verb.on = p.limit.on = true;
        p.eq.airDb = 4.0f; p.eq.presDb = 3.0f; p.comp.thresholdDb = -20.0f;
        p.sat.drive = 0.4f; p.delay.mix = 0.2f; p.verb.mix = 0.25f;
        const auto out = run (twoTone (220.0, 4400.0, 2.0, 0.4f), p);
        s.check ("the whole chain, everything engaged, stays finite and bounded",
                 allFinite (out, 4.0f));
        // WHAT A CLICK ACTUALLY IS. An absolute slew threshold is the wrong
        // test here: with saturation engaged the signal grows real harmonics,
        // so it legitimately moves fast between samples and any fixed limit
        // just measures how bright the test tone is. A click is an OUTLIER —
        // one sample-to-sample jump far larger than the waveform's own normal
        // slew. Comparing the maximum against the 99.9th percentile catches a
        // discontinuity while staying indifferent to how bright the material is.
        std::vector<float> jumps;
        jumps.reserve (out.size());
        for (size_t i = (size_t) lat + 16384 + 1; i < out.size(); ++i)
            jumps.push_back (std::abs (out[i] - out[i - 1]));
        std::sort (jumps.begin(), jumps.end());
        const float p999 = jumps[(size_t) ((double) jumps.size() * 0.999)];
        const float worst = jumps.back();
        s.check ("the whole chain introduces no clicks", worst < p999 * 2.0f + 0.02f,
                 "largest jump " + juce::String (worst, 4) + " vs 99.9th percentile "
                 + juce::String (p999, 4));
    }
}
