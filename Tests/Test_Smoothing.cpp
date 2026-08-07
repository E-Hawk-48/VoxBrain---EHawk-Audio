#include "TestFramework.h"
#include "../Source/DSP/VocalChain.h"
#include "../Source/DSP/RetuneEngine.h"

// ============================================================================
//  Parameter smoothing.
//
//  These tests exist because of a real defect: for most of this project's life
//  there was NO parameter smoothing anywhere in 20,000 lines of DSP. Gains and
//  mixes were applied as a single constant per block, so automating input trim
//  produced a step — a click — at every block boundary. It survived unnoticed
//  because the plugin is normally auditioned on STATIC settings, which is
//  precisely the case where a step is inaudible.
//
//  The assertions below encode both halves of the fix:
//    * a moving parameter must ramp (no discontinuity), and
//    * a STATIC parameter must behave exactly as it always did — smoothing must
//      not add cost, colour, or a startup fade.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FS = 44100.0;
constexpr int    BLOCK = 256;

/** Run the full chain, letting the caller change parameters per block. */
template <typename PerBlockFn>
std::vector<float> runChain (const std::vector<float>& sig, PerBlockFn perBlock)
{
    vf::VocalChain chain;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = FS;
    spec.maximumBlockSize = (juce::uint32) BLOCK;
    spec.numChannels = 1;
    chain.prepare (spec);

    std::vector<float> out;
    out.reserve (sig.size());
    juce::AudioBuffer<float> buf (1, BLOCK);

    int blockIndex = 0;
    for (size_t off = 0; off + (size_t) BLOCK <= sig.size(); off += (size_t) BLOCK, ++blockIndex)
    {
        vf::ChainParams p;                 // all stages default OFF
        perBlock (p, blockIndex);

        for (int i = 0; i < BLOCK; ++i) buf.setSample (0, i, sig[off + (size_t) i]);
        chain.process (buf, p);
        for (int i = 0; i < BLOCK; ++i) out.push_back (buf.getSample (0, i));
    }
    return out;
}

/** The chain ALWAYS applies the retune lookback, even when correction is off —
    bypass deliberately delays the dry signal so toggling never shifts timing.
    Any sample-for-sample comparison against the input must allow for it. */
inline int chainLatency() { return vf::RetuneEngine::latencyForMode (2, FS); }

/** Largest jump attributable to processing rather than to the source itself. */
float excessJump (const std::vector<float>& out, const std::vector<float>& in, size_t from)
{
    return maxJump (out, from) - maxJump (in, from);
}
} // namespace

void runSmoothingTests()
{
    using namespace vftest;
    Suite s ("Parameter smoothing");

    const auto sig = sineWave (220.0, 1.0, 0.3f, FS);

    // ---- THE defect: a step change in trim must not click ---------------
    {
        // Half-way through, jump input trim from 0 dB to +12 dB. Before
        // smoothing this produced a hard step at a block boundary.
        const int flipBlock = (int) (sig.size() / (size_t) BLOCK) / 2;
        const auto out = runChain (sig, [flipBlock] (vf::ChainParams& p, int blk)
        {
            p.inputGainDb = (blk >= flipBlock) ? 12.0f : 0.0f;
        });

        // The boundary sample index in the output.
        const size_t boundary = (size_t) flipBlock * (size_t) BLOCK;

        // Measure the largest single-sample jump right around the change.
        float worst = 0.0f;
        for (size_t i = boundary - 4; i < boundary + 8 && i < out.size(); ++i)
            worst = juce::jmax (worst, std::abs (out[i] - out[i - 1]));

        // A 4x gain step on a 0.3-amplitude sine would jump by up to ~0.9.
        // A correct ramp keeps the step close to the signal's own slope.
        s.check ("a +12 dB trim jump ramps instead of stepping", worst < 0.2f,
                 juce::String (worst, 4) + " max jump at the change");
        s.check ("output stays finite through the change", allFinite (out, 4.0f));
    }

    // ---- and the gain actually arrives -----------------------------------
    {
        const auto out = runChain (sig, [] (vf::ChainParams& p, int) { p.inputGainDb = 6.0f; });
        double sum = 0.0; size_t n = 0;
        for (size_t i = out.size() / 2; i < out.size(); ++i) { sum += (double) out[i] * out[i]; ++n; }
        const double rmsOut = std::sqrt (sum / (double) n);
        // +6 dB on a 0.3 sine: RMS 0.212 -> 0.424
        s.checkNear ("a static +6 dB trim reaches the right level", rmsOut, 0.424, 0.03);
    }

    // ---- static settings must be unchanged (no startup fade) ------------
    {
        // With trim static from the very first block, the first samples must
        // already be at full gain — a smoother that ramped from zero on start
        // would fade the first few milliseconds of every session.
        const auto out = runChain (sig, [] (vf::ChainParams& p, int) { p.inputGainDb = 6.0f; });
        const auto ref = runChain (sig, [] (vf::ChainParams& p, int) { p.inputGainDb = 6.0f; });

        bool identical = true;
        for (size_t i = 0; i < out.size(); ++i)
            if (out[i] != ref[i]) { identical = false; break; }
        s.check ("a static setting is deterministic", identical);

        // No fade-in: the first samples to EMERGE from the chain (i.e. just past
        // the retune lookback) must already be at full gain. A smoother that
        // ramped up from zero on start would fade the beginning of every session.
        const size_t d = (size_t) chainLatency();
        const float expected = std::abs (sig[64]) * juce::Decibels::decibelsToGain (6.0f);
        s.check ("no fade-in on the first audio out of the chain",
                 std::abs (std::abs (out[d + 64]) - expected) < 0.02f,
                 juce::String (std::abs (out[d + 64]), 4) + " vs " + juce::String (expected, 4));
    }

    // ---- unity trim must be bit-exact passthrough ------------------------
    {
        const auto out = runChain (sig, [] (vf::ChainParams& p, int) { p.inputGainDb = 0.0f; });
        const size_t d = (size_t) chainLatency();
        bool exact = true;
        size_t compared = 0;
        for (size_t i = 0; i + d < out.size(); ++i, ++compared)
            if (out[i + d] != sig[i]) { exact = false; break; }
        s.check ("0 dB trim is bit-exact passthrough (all stages off)", exact,
                 "compared " + juce::String ((int) compared) + " samples past the lookback");
    }

    // ---- EQ sweep must not step -----------------------------------------
    {
        // Sweep the presence gain across the take. Instant coefficient swaps
        // made this audibly steppy at every block boundary.
        const auto out = runChain (sig, [] (vf::ChainParams& p, int blk)
        {
            p.eq.on = true;
            p.eq.hpfHz = 80.0f;
            p.eq.mudHz = 350.0f;
            p.eq.presHz = 3500.0f;
            p.eq.presDb = juce::jlimit (0.0f, 12.0f, (float) blk * 0.6f);   // ramp up
        });

        s.check ("EQ sweep output is finite", allFinite (out, 4.0f));
        s.check ("EQ sweep introduces no discontinuity",
                 excessJump (out, sig, (size_t) (0.2 * FS)) < 0.05f,
                 juce::String (excessJump (out, sig, (size_t) (0.2 * FS)), 4) + " excess jump");
    }

    // ---- compressor mix = 0 must stay bit-exact dry ----------------------
    {
        // This is the contract Test_Compressor pins down; smoothing the mix must
        // not break it, which is why the smoother snaps on its first block.
        const auto out = runChain (sig, [] (vf::ChainParams& p, int)
        {
            p.comp.on = true;
            p.comp.thresholdDb = -30.0f;
            p.comp.ratio = 8.0f;
            p.comp.attackMs = 5.0f;
            p.comp.releaseMs = 80.0f;
            p.comp.makeupDb = 0.0f;
            p.comp.mix = 0.0f;
        });
        const size_t d = (size_t) chainLatency();
        bool exact = true;
        for (size_t i = 0; i + d < out.size(); ++i)
            if (out[i + d] != sig[i]) { exact = false; break; }
        s.check ("compressor mix = 0 remains bit-exact dry", exact);
    }

    // ---- a moving mix must not click -------------------------------------
    {
        const int flipBlock = (int) (sig.size() / (size_t) BLOCK) / 2;
        const auto out = runChain (sig, [flipBlock] (vf::ChainParams& p, int blk)
        {
            p.comp.on = true;
            p.comp.thresholdDb = -30.0f;
            p.comp.ratio = 8.0f;
            p.comp.attackMs = 5.0f;
            p.comp.releaseMs = 80.0f;
            p.comp.makeupDb = 0.0f;
            p.comp.mix = (blk >= flipBlock) ? 1.0f : 0.0f;   // dry -> fully wet
        });

        const size_t boundary = (size_t) flipBlock * (size_t) BLOCK;
        float worst = 0.0f;
        for (size_t i = boundary - 4; i < boundary + 8 && i < out.size(); ++i)
            worst = juce::jmax (worst, std::abs (out[i] - out[i - 1]));

        s.check ("a dry->wet mix jump ramps instead of stepping", worst < 0.2f,
                 juce::String (worst, 4) + " max jump at the change");
        s.check ("mix change stays finite", allFinite (out, 4.0f));
    }

    // ---- makeup gain change must not click -------------------------------
    {
        const int flipBlock = (int) (sig.size() / (size_t) BLOCK) / 2;
        const auto out = runChain (sig, [flipBlock] (vf::ChainParams& p, int blk)
        {
            p.comp.on = true;
            p.comp.thresholdDb = -6.0f;     // barely compressing
            p.comp.ratio = 2.0f;
            p.comp.attackMs = 10.0f;
            p.comp.releaseMs = 100.0f;
            p.comp.mix = 1.0f;
            p.comp.makeupDb = (blk >= flipBlock) ? 12.0f : 0.0f;
        });

        const size_t boundary = (size_t) flipBlock * (size_t) BLOCK;
        float worst = 0.0f;
        for (size_t i = boundary - 4; i < boundary + 8 && i < out.size(); ++i)
            worst = juce::jmax (worst, std::abs (out[i] - out[i - 1]));

        s.check ("a +12 dB makeup jump ramps instead of stepping", worst < 0.2f,
                 juce::String (worst, 4) + " max jump at the change");
    }

}
