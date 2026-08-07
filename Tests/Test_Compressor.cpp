#include "TestFramework.h"
#include "../Source/DSP/VocalChain.h"

// ============================================================================
//  Compressor — the program-dependent upgrade (blended peak/RMS detection and
//  release that slows as gain reduction deepens).
//
//  The value of these tests is that the upgrade shipped WITHOUT new parameters,
//  which means a future change could alter the sound of every existing preset
//  without any obvious signal that something moved. These assertions pin the
//  behaviour down.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FS = 44100.0;

std::vector<float> run (const std::vector<float>& sig, vf::CompParams p, float* grOut = nullptr)
{
    vf::Compressor comp;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = FS;
    spec.maximumBlockSize = 256;
    spec.numChannels = 1;
    comp.prepare (spec);

    std::vector<float> out;
    out.reserve (sig.size());
    juce::AudioBuffer<float> buf (1, 256);

    for (size_t off = 0; off + 256 <= sig.size(); off += 256)
    {
        for (int i = 0; i < 256; ++i) buf.setSample (0, i, sig[off + (size_t) i]);
        comp.process (buf, p);
        for (int i = 0; i < 256; ++i) out.push_back (buf.getSample (0, i));
    }
    if (grOut != nullptr) *grOut = comp.getGainReductionDb();
    return out;
}

double rms (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0.0;
    size_t n = 0;
    for (size_t i = a; i < b && i < v.size(); ++i) { s += (double) v[i] * v[i]; ++n; }
    return n > 0 ? std::sqrt (s / (double) n) : 0.0;
}

vf::CompParams makeParams (float threshold, float ratio, float attack, float release,
                           float makeup, float mix)
{
    vf::CompParams p;
    p.on = true;
    p.thresholdDb = threshold;
    p.ratio = ratio;
    p.attackMs = attack;
    p.releaseMs = release;
    p.makeupDb = makeup;
    p.mix = mix;
    return p;
}
} // namespace

void runCompressorTests()
{
    using namespace vftest;
    Suite s ("Compressor");

    // ---- below threshold: effectively transparent ----------------------
    {
        const auto p = makeParams (-12.0f, 4.0f, 5.0f, 80.0f, 0.0f, 1.0f);
        const auto in = sineWave (200.0, 0.5, 0.03f, FS);
        float gr = 0.0f;
        const auto out = run (in, p, &gr);
        const double ri = rms (in, 4096, in.size());
        const double ro = rms (out, 4096, out.size());
        s.check ("below threshold is transparent (GR ~ 0)",
                 gr < 0.5f && std::abs (ro - ri) / ri < 0.05,
                 "GR " + juce::String (gr, 2) + " dB");
    }

    // ---- above threshold: real, bounded gain reduction ------------------
    {
        const auto p = makeParams (-20.0f, 4.0f, 5.0f, 80.0f, 0.0f, 1.0f);
        const auto in = sineWave (200.0, 0.6, 0.9f, FS);
        float gr = 0.0f;
        const auto out = run (in, p, &gr);
        s.check ("above threshold applies 8-20 dB GR", gr > 8.0f && gr < 20.0f,
                 juce::String (gr, 1) + " dB");
        s.check ("output level is reduced", rms (out, 8192, out.size()) < rms (in, 8192, in.size()) * 0.6);
    }

    // ---- mix = 0 must be bit-exact dry ---------------------------------
    {
        const auto p = makeParams (-30.0f, 8.0f, 5.0f, 80.0f, 0.0f, 0.0f);
        const auto in = sineWave (300.0, 0.4, 0.8f, FS);
        const auto out = run (in, p);
        bool identical = true;
        for (size_t i = 0; i < out.size(); ++i)
            if (out[i] != in[i]) { identical = false; break; }
        s.check ("mix = 0 is byte-identical dry", identical);
    }

    // ---- makeup is exact -----------------------------------------------
    {
        const auto p = makeParams (-20.0f, 1.0f, 5.0f, 80.0f, 6.0f, 1.0f);
        const auto in = sineWave (200.0, 0.4, 0.2f, FS);
        const auto out = run (in, p);
        const double gain = rms (out, 4096, out.size()) / rms (in, 4096, in.size());
        s.checkNear ("ratio 1:1 + 6 dB makeup gives +6 dB", gain, 1.995, 0.15);
    }

    // ---- program-dependent release recovers smoothly -------------------
    {
        const auto p = makeParams (-24.0f, 4.0f, 3.0f, 50.0f, 0.0f, 1.0f);
        const auto loud  = sineWave (220.0, 0.25, 0.9f, FS);
        const auto quiet = sineWave (220.0, 0.35, 0.05f, FS);

        std::vector<float> in (loud);
        in.insert (in.end(), quiet.begin(), quiet.end());

        float grEnd = 0.0f;
        const auto out = run (in, p, &grEnd);

        // No click at the loud -> quiet boundary.
        const size_t bound = loud.size();
        float mx = 0.0f;
        for (size_t i = bound; i < bound + 2000 && i < out.size(); ++i)
            mx = juce::jmax (mx, std::abs (out[i] - out[i - 1]));

        s.check ("adaptive release recovers without a click", mx < 0.15f,
                 juce::String (mx, 4) + " max jump at the boundary");
        s.check ("gain reduction relaxes by the end", grEnd < 3.0f,
                 juce::String (grEnd, 2) + " dB");
    }

    // ---- safety ---------------------------------------------------------
    {
        const auto p = makeParams (-25.0f, 6.0f, 2.0f, 60.0f, 3.0f, 0.7f);
        s.check ("hot signal stays finite and bounded",
                 allFinite (run (sineWave (150.0, 0.5, 1.2f, FS), p), 8.0f));
    }
    {
        const auto p = makeParams (-22.0f, 5.0f, 120.0f, 5.0f, 0.0f, 1.0f);
        s.check ("sustained tone under a very fast release stays finite",
                 allFinite (run (sineWave (180.0, 0.6, 0.7f, FS), p), 6.0f));
    }

}
