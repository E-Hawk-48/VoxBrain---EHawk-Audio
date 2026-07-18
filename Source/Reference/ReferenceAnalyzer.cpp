#include "ReferenceAnalyzer.h"
#include "../Analysis/LoudnessMeter.h"
#include "../Analysis/PitchDetector.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <vector>

namespace vf
{
namespace
{
    constexpr int kFftOrder = 11;            // 2048-point analysis
    constexpr int kFftSize  = 1 << kFftOrder;

    inline float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
    inline float norm01 (float x, float a, float b)          // map [a,b] → [0,1]
    { return b > a ? clamp01 ((x - a) / (b - a)) : 0.0f; }
    inline float relDb (double power, double total)
    { return (power > 1e-15 && total > 1e-15) ? (float) (10.0 * std::log10 (power / total)) : -100.0f; }

    struct Band { float lo, hi; };
    // 7 bands: sub, low, mud, mid, presence, sib, air
    const Band kBands[7] = {
        { 20.f, 80.f }, { 80.f, 250.f }, { 250.f, 600.f }, { 600.f, 2500.f },
        { 2500.f, 6000.f }, { 6000.f, 10000.f }, { 10000.f, 20000.f }
    };
}

ReferenceProfile ReferenceAnalyzer::analyze (const juce::AudioBuffer<float>& audio,
                                             double sampleRate,
                                             std::function<void (float)> progress,
                                             const std::atomic<bool>* cancel)
{
    ReferenceProfile p;
    const int numCh = audio.getNumChannels();
    const int N     = audio.getNumSamples();
    if (numCh < 1 || N < kFftSize * 2 || sampleRate < 8000.0)
        return p;   // too short / unusable → valid stays false

    p.sampleRate  = sampleRate;
    p.channels    = numCh;
    p.durationSec = (double) N / sampleRate;

    auto report    = [&progress] (float f) { if (progress) progress (juce::jlimit (0.0f, 1.0f, f)); };
    auto cancelled = [&cancel]   { return cancel != nullptr && cancel->load(); };

    // ---- mono downmix for spectral / dynamics / pitch ----------------------
    std::vector<float> mono ((size_t) N, 0.0f);
    {
        const float g = 1.0f / (float) numCh;
        for (int c = 0; c < numCh; ++c)
        {
            const float* d = audio.getReadPointer (c);
            for (int i = 0; i < N; ++i) mono[(size_t) i] += d[i] * g;
        }
    }
    report (0.05f);
    if (cancelled()) return p;

    // ---- peak / rms / crest ------------------------------------------------
    double sumSq = 0.0; float peak = 0.0f;
    for (int i = 0; i < N; ++i) { const float s = mono[(size_t) i]; sumSq += (double) s * s; peak = std::max (peak, std::fabs (s)); }
    const double rms = std::sqrt (sumSq / (double) N);
    p.peakDb  = peak > 1e-6f ? (float) (20.0 * std::log10 (peak)) : -100.0f;
    p.rmsDb   = rms  > 1e-6  ? (float) (20.0 * std::log10 (rms))  : -100.0f;
    p.crestDb = juce::jlimit (0.0f, 40.0f, p.peakDb - p.rmsDb);

    // ---- stereo image ------------------------------------------------------
    if (numCh >= 2)
    {
        const float* L = audio.getReadPointer (0);
        const float* R = audio.getReadPointer (1);
        double sLL = 0, sRR = 0, sLR = 0, midE = 0, sideE = 0;
        for (int i = 0; i < N; ++i)
        {
            const double l = L[i], r = R[i];
            sLL += l * l; sRR += r * r; sLR += l * r;
            const double m = 0.5 * (l + r), s = 0.5 * (l - r);
            midE += m * m; sideE += s * s;
        }
        const double denom = std::sqrt (sLL * sRR);
        p.stereoCorrelation = denom > 1e-12 ? (float) juce::jlimit (-1.0, 1.0, sLR / denom) : 1.0f;
        const double total  = midE + sideE;
        const float  sideRatio = total > 1e-12 ? (float) (sideE / total) : 0.0f;
        p.stereoWidth = clamp01 (sideRatio * 2.0f);           // side≈half energy → width 1
        p.isMono      = sideE < 1e-9 * (midE + 1e-12);
        p.conf.stereo = 0.9f;
    }
    else { p.isMono = true; p.stereoWidth = 0.0f; p.stereoCorrelation = 1.0f; p.conf.stereo = 0.5f; }
    report (0.15f);

    // ---- Welch-averaged power spectrum + per-frame RMS ---------------------
    juce::dsp::FFT fft (kFftOrder);
    juce::dsp::WindowingFunction<float> win (kFftSize, juce::dsp::WindowingFunction<float>::hann);
    std::vector<double> psd ((size_t) (kFftSize / 2), 0.0);
    std::vector<float>  fftbuf ((size_t) (kFftSize * 2), 0.0f);
    std::vector<float>  frameRmsDb;
    const int hop = kFftSize / 2;
    int frames = 0;
    for (int start = 0; start + kFftSize <= N; start += hop)
    {
        if (cancelled()) return p;
        double fsum = 0; for (int i = 0; i < kFftSize; ++i) { const double s = mono[(size_t) (start + i)]; fsum += s * s; }
        const double frms = std::sqrt (fsum / kFftSize);
        frameRmsDb.push_back (frms > 1e-7 ? (float) (20.0 * std::log10 (frms)) : -120.0f);

        for (int i = 0; i < kFftSize; ++i) fftbuf[(size_t) i] = mono[(size_t) (start + i)];
        std::fill (fftbuf.begin() + kFftSize, fftbuf.end(), 0.0f);
        win.multiplyWithWindowingTable (fftbuf.data(), (size_t) kFftSize);
        fft.performFrequencyOnlyForwardTransform (fftbuf.data());
        for (int b = 0; b < kFftSize / 2; ++b) { const double m = fftbuf[(size_t) b]; psd[(size_t) b] += m * m; }
        ++frames;
    }
    if (frames == 0) return p;
    for (auto& v : psd) v /= (double) frames;
    report (0.55f);

    // ---- band energies + descriptors ---------------------------------------
    const double binHz = sampleRate / (double) kFftSize;
    auto bandPower = [&] (float lo, float hi)
    {
        const int b0 = std::max (1, (int) std::floor (lo / binHz));
        const int b1 = std::min (kFftSize / 2 - 1, (int) std::ceil (hi / binHz));
        double e = 0; for (int b = b0; b <= b1; ++b) e += psd[(size_t) b];
        return e;
    };
    double bandP[7]; double totalP = 0.0, maxBandP = 1e-15;
    for (int i = 0; i < 7; ++i) { bandP[i] = bandPower (kBands[i].lo, std::min (kBands[i].hi, (float) (sampleRate * 0.5))); totalP += bandP[i]; maxBandP = std::max (maxBandP, bandP[i]); }

    p.subDb = relDb (bandP[0], totalP); p.lowDb = relDb (bandP[1], totalP);
    p.mudDb = relDb (bandP[2], totalP); p.midDb = relDb (bandP[3], totalP);
    p.presenceDb = relDb (bandP[4], totalP); p.sibDb = relDb (bandP[5], totalP);
    p.airDb = relDb (bandP[6], totalP);
    for (int i = 0; i < 7; ++i) p.bandNorm[(size_t) i] = clamp01 ((float) (bandP[i] / maxBandP));

    // spectral centroid → brightness (log-mapped 250 Hz..6 kHz)
    double cw = 0, ct = 0;
    for (int b = 1; b < kFftSize / 2; ++b) { const double f = b * binHz; cw += f * psd[(size_t) b]; ct += psd[(size_t) b]; }
    const double centroid = ct > 1e-15 ? cw / ct : 1000.0;
    p.brightness = norm01 ((float) std::log2 ((float) juce::jmax (50.0, centroid)),
                           std::log2 (250.0f), std::log2 (6000.0f));

    // tilt = high thirds vs low thirds (relative dB)
    const double highP = bandP[4] + bandP[5] + bandP[6];
    const double lowP  = bandP[0] + bandP[1] + bandP[2];
    p.spectralTiltDb = juce::jlimit (-24.0f, 24.0f, relDb (highP, totalP) - relDb (lowP, totalP));

    // tone descriptors from relative band levels (neutral ≈ 0.5)
    p.warmth       = norm01 (relDb (bandP[1] + bandP[2], totalP), -14.0f, -3.0f);
    p.presence     = norm01 (p.presenceDb, -20.0f, -6.0f);
    p.air          = norm01 (p.airDb,      -34.0f, -14.0f);
    p.lowEndWeight = norm01 (relDb (bandP[0] + bandP[1], totalP), -16.0f, -3.0f);
    p.conf.spectral = 0.9f;

    // Smooth log-spaced frequency-response curve for the panel graph.
    {
        const double fLo = 20.0, fHi = std::min (20000.0, sampleRate * 0.5);
        const double logLo = std::log10 (fLo), logHi = std::log10 (fHi);
        std::array<float, ReferenceProfile::kSpectrumPoints> curveDb {};
        float mn = 1.0e9f, mx = -1.0e9f;
        for (int i = 0; i < ReferenceProfile::kSpectrumPoints; ++i)
        {
            const double f = std::pow (10.0, logLo + (logHi - logLo) * i / (ReferenceProfile::kSpectrumPoints - 1));
            const int    c = juce::jlimit (1, kFftSize / 2 - 1, (int) std::round (f / binHz));
            const int    w = std::max (1, (int) (0.03 * f / binHz));   // ~1/12 oct average
            double e = 0; int n = 0;
            for (int b = std::max (1, c - w); b <= std::min (kFftSize / 2 - 1, c + w); ++b) { e += psd[(size_t) b]; ++n; }
            const float db = (float) (10.0 * std::log10 ((n > 0 ? e / n : 1e-15) + 1e-15));
            curveDb[(size_t) i] = db; mn = std::min (mn, db); mx = std::max (mx, db);
        }
        const float span = (mx - mn) > 1e-3f ? (mx - mn) : 1.0f;
        for (int i = 0; i < ReferenceProfile::kSpectrumPoints; ++i)
            p.spectrumCurve[(size_t) i] = clamp01 ((curveDb[(size_t) i] - mn) / span);
    }

    // ---- resonances: peaks proud of a 1/3-oct smoothed baseline ------------
    {
        std::vector<float> specDb ((size_t) (kFftSize / 2), -120.0f);
        for (int b = 1; b < kFftSize / 2; ++b) specDb[(size_t) b] = 10.0f * std::log10 ((float) (psd[(size_t) b] + 1e-15));
        auto baselineAt = [&] (int b)
        {
            const double f = b * binHz; const int w = std::max (2, (int) (0.23 * f / binHz)); // ~1/3 oct
            const int lo = std::max (1, b - w), hi = std::min (kFftSize / 2 - 1, b + w);
            double s = 0; int n = 0; for (int k = lo; k <= hi; ++k) { s += specDb[(size_t) k]; ++n; }
            return n > 0 ? (float) (s / n) : -120.0f;
        };
        struct Peak { float hz, strength; };
        std::vector<Peak> peaks;
        const int lob = std::max (1, (int) (200.0 / binHz));
        const int hib = std::min (kFftSize / 2 - 2, (int) (9000.0 / binHz));
        for (int b = lob; b <= hib; ++b)
        {
            if (specDb[(size_t) b] > specDb[(size_t) (b - 1)] && specDb[(size_t) b] >= specDb[(size_t) (b + 1)])
            {
                const float prom = specDb[(size_t) b] - baselineAt (b);
                if (prom > 3.0f) peaks.push_back ({ (float) (b * binHz), prom });
            }
        }
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& c) { return a.strength > c.strength; });
        p.resonanceCount = std::min ((int) peaks.size(), ReferenceProfile::kMaxResonances);
        for (int i = 0; i < p.resonanceCount; ++i) { p.resonanceHz[(size_t) i] = peaks[(size_t) i].hz; p.resonanceStrength[(size_t) i] = peaks[(size_t) i].strength; }
    }

    // ---- sibilance ---------------------------------------------------------
    {
        const double sibP = bandP[5];
        const double voiceP = bandP[3] + bandP[4] + 1e-15;
        p.sibilanceRatio = juce::jlimit (0.0f, 4.0f, (float) (sibP / voiceP));
        // centroid inside 6–10 kHz
        double sw = 0, st = 0;
        const int b0 = (int) (6000.0 / binHz), b1 = std::min (kFftSize / 2 - 1, (int) (10000.0 / binHz));
        for (int b = b0; b <= b1; ++b) { const double f = b * binHz; sw += f * psd[(size_t) b]; st += psd[(size_t) b]; }
        p.sibilanceCenterHz = st > 1e-15 ? (float) (sw / st) : 7500.0f;
        p.conf.sibilance = 0.8f;
    }

    // ---- dynamic range / transient density / compression estimate ----------
    {
        std::vector<float> voiced;
        for (float d : frameRmsDb) if (d > -60.0f) voiced.push_back (d);
        if (voiced.size() > 4)
        {
            std::vector<float> sorted = voiced; std::sort (sorted.begin(), sorted.end());
            auto pct = [&] (float q) { return sorted[(size_t) juce::jlimit (0, (int) sorted.size() - 1, (int) (q * (sorted.size() - 1))) ]; };
            p.dynamicRangeDb = juce::jlimit (0.0f, 40.0f, pct (0.95f) - pct (0.10f));
        }
        int rises = 0;
        for (size_t i = 1; i < frameRmsDb.size(); ++i) if (frameRmsDb[i] - frameRmsDb[i - 1] > 6.0f) ++rises;
        p.transientDensity = (float) rises / (float) juce::jmax (0.001, p.durationSec);
        p.compressionAmount = clamp01 (0.5f * (1.0f - norm01 (p.crestDb, 4.0f, 16.0f))
                                     + 0.5f * (1.0f - norm01 (p.dynamicRangeDb, 3.0f, 14.0f)));
        p.conf.dynamics = 0.8f;
        p.conf.saturation = 0.5f;
    }
    report (0.7f);

    // ---- loudness (real BS.1770) -------------------------------------------
    {
        LoudnessMeter lm; lm.prepare (sampleRate, numCh);
        const int blk = 8192;
        for (int start = 0; start < N; start += blk)
        {
            if (cancelled()) return p;
            const int len = std::min (blk, N - start);
            juce::AudioBuffer<float> slice (const_cast<float* const*> (audio.getArrayOfReadPointers()), numCh, start, len);
            lm.process (slice);
        }
        p.integratedLufs = lm.getIntegratedLufs();
    }
    report (0.8f);

    // ---- ambience / reverb tail estimate -----------------------------------
    {
        // 20 ms RMS envelope (dB)
        const int env = std::max (64, (int) (0.020 * sampleRate));
        std::vector<float> edb;
        for (int s = 0; s + env <= N; s += env)
        {
            double e = 0; for (int i = 0; i < env; ++i) { const double v = mono[(size_t) (s + i)]; e += v * v; }
            const double r = std::sqrt (e / env);
            edb.push_back (r > 1e-7 ? (float) (20.0 * std::log10 (r)) : -120.0f);
        }
        if (edb.size() > 8)
        {
            float mx = -120.0f; for (float v : edb) mx = std::max (mx, v);
            // ambience: energy sitting in the "gaps" (below −18 dB from peak but
            // above −55 dB) as a fraction of frames — a proxy for a wet tail/room.
            int gap = 0, active = 0;
            for (float v : edb) { if (v > mx - 55.0f) { ++active; if (v < mx - 18.0f && v > mx - 55.0f) ++gap; } }
            p.ambience = active > 0 ? clamp01 ((float) gap / (float) active) : 0.0f;

            // decay estimate: after strong peaks, how long to fall 20 dB (T20-ish)
            std::vector<float> decs;
            for (size_t i = 1; i + 1 < edb.size(); ++i)
                if (edb[i] > mx - 8.0f && edb[i] >= edb[i - 1] && edb[i] > edb[i + 1])
                {
                    const float target = edb[i] - 20.0f; size_t j = i + 1;
                    while (j < edb.size() && edb[j] > target && edb[j] < edb[i] + 3.0f) ++j;
                    const float t = (float) (j - i) * (float) env / (float) sampleRate;
                    if (t > 0.02f && t < 4.0f) decs.push_back (t);
                }
            if (! decs.empty()) { std::sort (decs.begin(), decs.end()); p.reverbDecaySec = decs[decs.size() / 2]; }
            p.reverbAmount = clamp01 (0.6f * p.ambience + 0.4f * norm01 (p.reverbDecaySec, 0.15f, 1.6f));
            p.conf.reverb = 0.45f;   // honest: reverb is hard to separate from performance
        }

        // ---- delay: envelope autocorrelation for a rhythmic echo -----------
        if (edb.size() > 16)
        {
            std::vector<float> e (edb.begin(), edb.end());
            float mean = 0; for (float v : e) mean += v; mean /= (float) e.size();
            for (float& v : e) v -= mean;
            double e0 = 0; for (float v : e) e0 += (double) v * v;
            const float fps = (float) sampleRate / (float) env;
            const int lagMin = std::max (1, (int) (0.06f * fps)), lagMax = std::min ((int) e.size() - 1, (int) (0.80f * fps));
            float best = 0; int bestLag = 0;
            for (int lag = lagMin; lag <= lagMax; ++lag)
            {
                double s = 0; for (size_t i = lag; i < e.size(); ++i) s += (double) e[i] * e[i - (size_t) lag];
                const float r = e0 > 1e-9 ? (float) (s / e0) : 0.0f;
                if (r > best) { best = r; bestLag = lag; }
            }
            if (best > 0.3f) { p.delayTimeMs = 1000.0f * (float) bestLag / fps; p.delayAmount = clamp01 (best); }
            p.conf.delay = 0.35f;    // very speculative
        }
    }
    report (0.9f);

    // ---- pitch behaviour (YIN) ---------------------------------------------
    {
        PitchDetector pd; pd.prepare (sampleRate);
        std::vector<float> f0s;
        int voiced = 0, total = 0;
        const int step = 1024;
        for (int s = 0; s + step <= N; s += step)
        {
            if (cancelled()) return p;
            if (pd.push (mono.data() + s, step))
            {
                ++total;
                if (pd.getConfidence() > 0.5f && pd.getFrequencyHz() > 0.0f) { f0s.push_back (pd.getFrequencyHz()); ++voiced; }
            }
        }
        p.voicedRatio = total > 0 ? clamp01 ((float) voiced / (float) total) : 0.0f;
        if (f0s.size() > 4)
        {
            std::vector<float> sorted = f0s; std::sort (sorted.begin(), sorted.end());
            p.pitchMedianHz = sorted[sorted.size() / 2];
            // MAD of cents around the median
            std::vector<float> cents;
            for (float f : f0s) if (f > 0 && p.pitchMedianHz > 0) cents.push_back (1200.0f * std::log2 (f / p.pitchMedianHz));
            std::vector<float> ac; for (float c : cents) ac.push_back (std::fabs (c));
            std::sort (ac.begin(), ac.end());
            p.pitchStabilityCents = ac.empty() ? 0.0f : ac[ac.size() / 2];
            // tight stability + mostly voiced → likely tuned
            p.pitchCorrectionStrength = clamp01 (p.voicedRatio * (1.0f - norm01 (p.pitchStabilityCents, 8.0f, 45.0f)));
            p.conf.pitch = 0.5f;
        }
    }

    report (1.0f);
    p.valid = true;
    return p;
}
} // namespace vf
