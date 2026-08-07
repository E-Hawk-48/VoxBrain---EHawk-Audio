#include "TestFramework.h"
#include "../Source/DSP/RetuneEngine.h"

// ============================================================================
//  RetuneEngine — the most delicate code in the project.
//
//  These are CONTRACT tests, not feature tests. The byte-identical bypass
//  guarantee and the "hard tune adds no discontinuity beyond the source
//  waveform" measurement are the two properties that have repeatedly caught
//  regressions in this engine (the historical "choppy" and stale-slot crackle
//  bugs). They must never be allowed to fail silently.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FS = 44100.0;

std::vector<float> run (const std::vector<float>& sig, vf::RetuneParams p,
                        float* inHz = nullptr, float* targetHz = nullptr)
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
    if (inHz     != nullptr) *inHz     = e.getInputPitchHz();
    if (targetHz != nullptr) *targetHz = e.getTargetPitchHz();
    return out;
}
} // namespace

void runRetuneEngineTests()
{
    using namespace vftest;
    Suite s ("RetuneEngine");

    // ---- THE contract: bypass is byte-identical delayed dry ------------
    {
        vf::RetuneParams p;   // on == false
        const auto sig = sineWave (330.0, 0.6, 0.4f, FS);
        const auto out = run (sig, p);
        const int d = vf::RetuneEngine::latencyForMode (2, FS);

        bool exact = true;
        int checked = 0;
        for (size_t k = 0; k + (size_t) d < out.size() && checked < 25000; ++k, ++checked)
            if (out[k + (size_t) d] != sig[k]) { exact = false; break; }

        s.check ("bypass is BYTE-IDENTICAL delayed dry", exact,
                 "compared " + juce::String (checked) + " samples");
    }

    // ---- latency modes stay ordered and stable -------------------------
    {
        const int live = vf::RetuneEngine::latencyForMode (0, FS);
        const int bal  = vf::RetuneEngine::latencyForMode (1, FS);
        const int stu  = vf::RetuneEngine::latencyForMode (2, FS);
        s.check ("latency modes ordered Live < Balanced < Studio", live < bal && bal < stu,
                 juce::String (live * 1000.0f / (float) FS, 1) + " / "
                 + juce::String (bal * 1000.0f / (float) FS, 1) + " / "
                 + juce::String (stu * 1000.0f / (float) FS, 1) + " ms");
    }

    // ---- correction actually corrects ----------------------------------
    {
        vf::RetuneParams p;
        p.on = true; p.scaleType = 0; p.chromatic = true; p.amount = 1.0f; p.speedMs = 20.0f;
        float in = 0.0f, target = 0.0f;
        const auto out = run (sineWave (445.0, 1.0, 0.3f, FS, 3), p, &in, &target);

        s.check ("445 Hz input is tracked", std::abs (in - 445.0f) < 10.0f, juce::String (in, 1) + " Hz");
        s.check ("target snaps to 440 Hz (A4)", std::abs (target - 440.0f) < 5.0f, juce::String (target, 1) + " Hz");
        s.check ("corrected output is finite and bounded", allFinite (out, 4.0f));
        s.check ("corrected output is continuous (no clicks)",
                 maxJump (out, (size_t) (0.3 * FS)) < 0.30f,
                 juce::String (maxJump (out, (size_t) (0.3 * FS)), 3) + " max jump");
    }

    // ---- hard tune must stay hard AND clean ----------------------------
    {
        vf::RetuneParams h;
        h.on = true; h.scaleType = 0; h.chromatic = true; h.amount = 1.0f; h.speedMs = 0.0f;
        h.flexTune = 0.0f; h.vibratoPreservation = 0.0f; h.transitionSmoothing = 0.0f;
        h.driftCorrection = 1.0f; h.hardTune = 1.0f;

        float target = 0.0f;
        run (sineWave (445.0, 1.0, 0.3f, FS, 3), h, nullptr, &target);
        s.check ("hard tune still snaps to 440 Hz", std::abs (target - 440.0f) < 5.0f,
                 juce::String (target, 1) + " Hz");

        // A sawtooth's own cycle reset is a large discontinuity BY DESIGN, so the
        // meaningful measure is the output's jump RELATIVE to the input's.
        const auto in  = sawWave (233.0, 1.0, 0.3f, FS);
        const auto out = run (in, h);
        const float mi = maxJump (in,  (size_t) (0.35 * FS));
        const float mo = maxJump (out, (size_t) (0.35 * FS));
        s.check ("hard tune adds no discontinuity beyond the source waveform",
                 mo < mi * 1.35f, juce::String (mo, 3) + " vs source " + juce::String (mi, 3));
    }

    // ---- expressive settings stay clean --------------------------------
    {
        vf::RetuneParams e;
        e.on = true; e.scaleType = 1; e.keyRoot = 0; e.amount = 1.0f; e.speedMs = 60.0f;
        e.flexTune = 0.6f; e.vibratoPreservation = 0.9f;
        e.transitionSmoothing = 0.8f; e.driftCorrection = 0.4f;
        const auto out = run (sineWave (262.0, 1.0, 0.3f, FS, 4), e);
        s.check ("expressive settings finite and continuous",
                 allFinite (out, 4.0f) && maxJump (out, (size_t) (0.35 * FS)) < 0.30f,
                 juce::String (maxJump (out, (size_t) (0.35 * FS)), 3) + " max jump");
    }

    // ---- exhaustive safety sweep ---------------------------------------
    {
        bool ok = true;
        const auto sig = sineWave (250.0, 0.35, 0.3f, FS, 3);
        for (int sc = 0; sc <= 9 && ok; ++sc)
            for (float hm = 0.0f; hm <= 1.0f && ok; hm += 0.5f)
                for (float fm = -5.0f; fm <= 5.0f && ok; fm += 5.0f)
                {
                    vf::RetuneParams p;
                    p.on = true; p.scaleType = sc; p.chromatic = (sc == 0); p.keyRoot = 0;
                    p.amount = 1.0f; p.speedMs = 30.0f; p.humanize = hm; p.formant = fm;
                    if (! allFinite (run (sig, p), 6.0f)) ok = false;
                }
        s.check ("all scales x humanize x formant stay finite", ok, "10 x 3 x 3 combinations");
    }
    {
        bool ok = true;
        const auto sig = sineWave (300.0, 0.3, 0.3f, FS, 3);
        for (float ht = 0.0f; ht <= 1.0f && ok; ht += 0.5f)
            for (float sn = 0.0f; sn <= 100.0f && ok; sn += 50.0f)
                for (float se = 0.0f; se <= 1.0f && ok; se += 0.5f)
                {
                    vf::RetuneParams p;
                    p.on = true; p.amount = 1.0f; p.speedMs = 40.0f;
                    p.hardTune = ht; p.snapThreshold = sn; p.sensitivity = se;
                    if (! allFinite (run (sig, p), 6.0f)) ok = false;
                }
        s.check ("all pro-control combinations stay finite", ok, "hardTune x snap x sensitivity");
    }

    // ---- mid-stream mode switching (RT-safety surrogate) ---------------
    {
        vf::RetuneEngine e;
        e.prepare (FS, 512, 1);
        const auto sig = sineWave (200.0, 0.9, 0.3f, FS, 3);
        juce::AudioBuffer<float> buf (1, 256);
        bool ok = true;
        int blk = 0;

        for (size_t off = 0; off + 256 <= sig.size(); off += 256, ++blk)
        {
            vf::RetuneParams p;
            p.on = true; p.amount = 1.0f; p.speedMs = 25.0f;
            p.latencyMode = (blk / 4) % 3;
            for (int i = 0; i < 256; ++i) buf.setSample (0, i, sig[off + (size_t) i]);
            e.process (buf, p);
            for (int i = 0; i < 256; ++i)
            {
                const float v = buf.getSample (0, i);
                if (! std::isfinite (v) || std::abs (v) > 6.0f) { ok = false; break; }
            }
        }
        s.check ("mid-stream latency-mode switches stay finite", ok);
    }

    // ---- noise must not be "corrected" into something ------------------
    {
        vf::RetuneParams p;
        p.on = true; p.amount = 1.0f; p.speedMs = 20.0f;
        s.check ("noise input stays finite", allFinite (run (whiteNoise (0.6, 0.2f, FS), p), 4.0f));
    }

}
