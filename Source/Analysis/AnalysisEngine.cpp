#include "AnalysisEngine.h"
#include <algorithm>
#include <cmath>

namespace vf
{
namespace
{
    constexpr float kMinDb = -100.0f;

    float toDb (double x) { return x > 0.0 ? (float) (10.0 * std::log10 (x)) : kMinDb; }
    float ampToDb (float x) { return x > 0.0f ? 20.0f * std::log10 (x) : kMinDb; }

    // Band edges in Hz matching AnalysisSnapshot fields
    constexpr std::array<float, 8> bandEdges { 20.0f, 80.0f, 250.0f, 600.0f,
                                               2500.0f, 6000.0f, 10000.0f, 20000.0f };
} // namespace

void AnalysisEngine::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    pitch.prepare (sampleRate);
    loudness.prepare (sampleRate, numChannels);

    pitchFrames.reserve (32768);   // pre-allocate: no audio-thread allocation
    frameRmsDb.reserve (65536);
    reset();
}

void AnalysisEngine::reset()
{
    fifo.fill (0.0f);
    magnitudes.fill (0.0f);
    fifoIndex = 0;
    spectrumReady = false;
    liveBand.fill (-45.0f);                      // neutral until audio arrives
    for (auto& a : uiBandDb) a.store (-45.0f);
    loudness.reset();
    learning = false;
}

void AnalysisEngine::process (const juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || buffer.getNumChannels() == 0)
        return;

    loudness.process (buffer);
    uiLufs.store (loudness.getMomentaryLufs());

    // Mono mix for spectral + pitch analysis
    const float* left  = buffer.getReadPointer (0);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;

    float blockPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float mono = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        blockPeak = std::max (blockPeak, std::abs (mono));

        fifo[(size_t) fifoIndex] = mono;
        if (++fifoIndex >= fftSize)
        {
            fifoIndex = 0;
            runFft();
        }

        if (learning.load (std::memory_order_relaxed))
        {
            sumSquares += (double) mono * (double) mono;
            ++sampleCount;
        }
    }
    uiPeakDb.store (ampToDb (blockPeak));
    if (learning.load (std::memory_order_relaxed))
        learnPeak = std::max (learnPeak, blockPeak);

    // Pitch tracking (mono pointer trick: reuse left channel if already mono)
    if (right == nullptr)
    {
        if (pitch.push (left, numSamples))
            uiPitchHz.store (pitch.getFrequencyHz());
    }
    else
    {
        // Small stack buffer chunks to avoid allocation
        float tmp[512];
        int done = 0;
        while (done < numSamples)
        {
            const int chunk = std::min (512, numSamples - done);
            for (int i = 0; i < chunk; ++i)
                tmp[i] = 0.5f * (left[done + i] + right[done + i]);
            if (pitch.push (tmp, chunk))
                uiPitchHz.store (pitch.getFrequencyHz());
            done += chunk;
        }
    }

    if (learning.load (std::memory_order_relaxed)
        && pitch.getConfidence() > 0.6f && pitch.getFrequencyHz() > 0.0f
        && pitchFrames.size() < pitchFrames.capacity())
    {
        pitchFrames.push_back (pitch.getFrequencyHz());
    }
    if (learning.load (std::memory_order_relaxed))
        ++totalPitchFrames;
}

void AnalysisEngine::runFft()
{
    std::copy (fifo.begin(), fifo.end(), fftData.begin());
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    // Exponential smoothing for GUI stability
    for (int i = 0; i < fftSize / 2; ++i)
        magnitudes[(size_t) i] = 0.6f * magnitudes[(size_t) i] + 0.4f * fftData[(size_t) i];
    spectrumReady.store (true);

    // ---- per-frame spectral read (ALWAYS: powers the live DNA radar) --------
    const double binHz = fs / (double) fftSize;
    std::array<double, 7> frameBand {};
    double frameTotal = 0.0, wSum = 0.0, frameNasal = 0.0;

    for (int i = 1; i < fftSize / 2; ++i)
    {
        const double f = i * binHz;
        const double e = (double) fftData[(size_t) i] * (double) fftData[(size_t) i];
        frameTotal += e;
        wSum += e * f;
        if (f >= 900.0 && f < 1200.0) frameNasal += e;

        for (int b = 0; b < 7; ++b)
            if (f >= bandEdges[(size_t) b] && f < bandEdges[(size_t) b + 1])
                { frameBand[(size_t) b] += e; break; }
    }

    // Publish live, smoothed spectral state for the always-on radar.
    if (frameTotal > 1.0e-12)
    {
        for (int b = 0; b < 7; ++b)
        {
            const float rel = toDb (frameBand[(size_t) b] / frameTotal);
            liveBand[(size_t) b] = 0.85f * liveBand[(size_t) b] + 0.15f * rel;
            uiBandDb[(size_t) b].store (liveBand[(size_t) b]);
        }
        const float centroidHz = (float) (wSum / frameTotal);
        uiBrightnessLive.store (std::clamp ((std::log2 (std::max (1.0f, centroidHz) / 500.0f)) / 3.0f, 0.0f, 1.0f));
        const double voiceLive = frameBand[2] + frameBand[3];
        uiSibLive.store   (voiceLive > 1.0e-9 ? (float) std::clamp (frameBand[5] / voiceLive, 0.0, 2.0) : 0.0f);
        uiNasalLive.store (voiceLive > 1.0e-9 ? (float) std::clamp (frameNasal   / voiceLive, 0.0, 2.0) : 0.0f);
    }

    if (! learning.load (std::memory_order_relaxed))
        return;

    // ---- learning accumulation (reuse the frame values just computed) -------
    for (int b = 0; b < 7; ++b) bandEnergy[(size_t) b] += frameBand[(size_t) b];
    bandTotal        += frameTotal;
    centroidWeighted += wSum;
    centroidTotal    += frameTotal;
    nasalEnergy      += frameNasal;

    // Sibilance ratio for this frame: 6–10 kHz vs core voice 250–2500 Hz
    const double voiceE = frameBand[2] + frameBand[3];
    if (voiceE > 1.0e-9)
    {
        sibPeakRatioAccum += frameBand[5] / voiceE;
        ++sibFrames;
    }

    // Frame RMS (for noise-floor percentile estimation)
    if (frameRmsDb.size() < frameRmsDb.capacity())
        frameRmsDb.push_back (toDb (frameTotal / (double) (fftSize / 2)));
}

void AnalysisEngine::startLearning()
{
    sumSquares = 0.0;  sampleCount = 0;  learnPeak = 0.0f;
    bandEnergy.fill (0.0);
    bandTotal = 0.0;  centroidWeighted = 0.0;  centroidTotal = 0.0;
    pitchFrames.clear();
    frameRmsDb.clear();
    totalPitchFrames = 0;
    sibPeakRatioAccum = 0.0;  sibFrames = 0;
    nasalEnergy = 0.0;
    loudness.reset();
    learning = true;
}

AnalysisSnapshot AnalysisEngine::finishLearning()
{
    stopLearning();
    return finalizeLearning();
}

// NOTE: message thread ONLY. This sorts up to ~65k values and allocates; running
// it inside the audio callback (as it used to) overran the real-time deadline by
// orders of magnitude the moment LEARN finished, which can wedge an audio driver.
AnalysisSnapshot AnalysisEngine::finalizeLearning()
{
    AnalysisSnapshot s;

    if (sampleCount < (long long) fs)   // need at least 1 second of audio
        return s;

    s.peakDb = ampToDb (learnPeak);
    s.rmsDb  = toDb (sumSquares / (double) sampleCount);
    s.crestDb = s.peakDb - s.rmsDb;
    s.integratedLufs = loudness.getIntegratedLufs();

    // Spectral balance relative to total energy
    if (bandTotal > 0.0)
    {
        auto rel = [this] (int b) { return toDb (bandEnergy[(size_t) b] / bandTotal); };
        s.subDb = rel (0);  s.lowDb = rel (1);  s.mudDb = rel (2);  s.midDb = rel (3);
        s.presenceDb = rel (4);  s.sibDb = rel (5);  s.airDb = rel (6);

        // Spectral tilt (highs minus lows) + harshness (presence over the core mids)
        s.spectralTiltDb = ((s.presenceDb + s.sibDb + s.airDb) / 3.0f)
                         - ((s.subDb + s.lowDb + s.mudDb) / 3.0f);
        s.harshnessDb    = s.presenceDb - s.midDb;
    }

    if (centroidTotal > 0.0)
    {
        const float centroidHz = (float) (centroidWeighted / centroidTotal);
        // Map ~500 Hz..4 kHz to 0..1 logarithmically
        s.brightness = std::clamp ((std::log2 (centroidHz / 500.0f)) / 3.0f, 0.0f, 1.0f);
    }

    if (sibFrames > 0)
        s.sibilanceRatio = (float) (sibPeakRatioAccum / (double) sibFrames);

    // Noise floor = 10th percentile of frame RMS; also dynamic range + transients
    if (frameRmsDb.size() > 16)
    {
        std::vector<float> sorted (frameRmsDb);
        std::sort (sorted.begin(), sorted.end());
        s.noiseFloorDb = sorted[sorted.size() / 10];

        const float p10 = sorted[(size_t) ((double) sorted.size() * 0.10)];
        const float p95 = sorted[(size_t) juce::jmin ((int) sorted.size() - 1,
                                                       (int) ((double) sorted.size() * 0.95))];
        s.dynamicRangeDb = juce::jlimit (0.0f, 60.0f, p95 - p10);   // LRA-style spread

        int onsets = 0;                                             // > 6 dB frame-to-frame rises
        for (size_t i = 1; i < frameRmsDb.size(); ++i)
            if (frameRmsDb[i] - frameRmsDb[i - 1] > 6.0f) ++onsets;
        const double frameRate = fs / (double) fftSize;
        const double seconds   = (double) frameRmsDb.size() / juce::jmax (1.0, frameRate);
        s.transientDensity = seconds > 0.0 ? (float) ((double) onsets / seconds) : 0.0f;
    }

    // Pitch statistics
    if (pitchFrames.size() > 8)
    {
        std::vector<float> sorted (pitchFrames);
        std::sort (sorted.begin(), sorted.end());
        s.pitchMedianHz = sorted[sorted.size() / 2];

        // Frame-to-frame deviation in cents (median absolute)
        std::vector<float> devs;
        devs.reserve (pitchFrames.size());
        for (size_t i = 1; i < pitchFrames.size(); ++i)
            if (pitchFrames[i] > 0.0f && pitchFrames[i - 1] > 0.0f)
                devs.push_back (std::abs (1200.0f * std::log2 (pitchFrames[i] / pitchFrames[i - 1])));
        if (! devs.empty())
        {
            std::sort (devs.begin(), devs.end());
            s.pitchStabilityCents = devs[devs.size() / 2];
        }
    }
    if (totalPitchFrames > 0)
        s.voicedRatio = (float) pitchFrames.size() / (float) totalPitchFrames;

    // ---- Key / mode / modulation (Analysis/KeyDetector) --------------------
    //  Replaces the old single-histogram, major-or-minor-only correlation. The
    //  detector is confidence-weighted, knows six modes, segments the take so a
    //  modulation is visible, and reports ambiguity instead of inventing
    //  certainty a monophonic melody cannot support.
    {
        // The analysis hop is one FFT frame; pitchFrames holds VOICED frames
        // only, so this is a "sung time" axis (see KeyDetector::analyseFrames).
        const double frameRate = fs / (double) fftSize;
        const auto key = KeyDetector::analyseFrames (pitchFrames, frameRate);

        s.keyChromatic = key.chromatic;
        s.keySummary   = key.summary;

        if (key.isValid())
        {
            s.keyRoot       = key.global.root;
            s.keyConfidence = key.global.confidence;
            s.keyScaleType  = key.global.scaleType();
            s.keyAmbiguous  = key.ambiguous;
            s.keyModulates  = key.modulationDetected;
            s.keyName       = key.global.name();

            // Backward-compatible major/minor flag for existing consumers.
            s.keyIsMajor = key.global.mode == KeyMode::Major
                        || key.global.mode == KeyMode::Mixolydian;
        }
    }

    // ---- Vocal DNA: a normalized 0..1 fingerprint for the radar + brain -----
    // Heuristic mappings (tunable): each axis clamps to 0..1 so it is always
    // safe to draw and to feed the brain. Quality axes read high = good;
    // problem axes read high = more of that issue.
    {
        auto norm = [] (float x, float lo, float hi)
        { return std::clamp ((x - lo) / (hi - lo), 0.0f, 1.0f); };

        VocalDNA& d = s.dna;

        d.brightness = s.brightness;
        d.warmth     = norm (s.lowDb,      -26.0f,  -8.0f);
        d.presence   = norm (s.presenceDb, -28.0f,  -8.0f);
        d.air        = norm (s.airDb,      -40.0f, -14.0f);
        d.muddiness  = norm (s.mudDb,      -24.0f,  -9.0f);
        d.boxiness   = norm (s.mudDb,      -20.0f,  -6.0f) * 0.7f;
        d.harshness  = std::clamp ((s.harshnessDb + 2.0f) / 14.0f, 0.0f, 1.0f);
        d.sibilance  = std::clamp (0.5f * norm (s.sibDb, -34.0f, -12.0f)
                                 + 0.5f * std::clamp (s.sibilanceRatio * 2.5f, 0.0f, 1.0f), 0.0f, 1.0f);
        d.plosives   = norm (s.subDb, -30.0f, -10.0f);

        const float nasalRatio = bandEnergy[3] > 0.0 ? (float) (nasalEnergy / bandEnergy[3]) : 0.0f;
        d.nasal = std::clamp ((nasalRatio - 0.15f) * 3.0f, 0.0f, 1.0f);

        d.dynamics    = std::clamp (s.dynamicRangeDb / 18.0f, 0.0f, 1.0f);
        d.noise       = std::clamp ((s.noiseFloorDb + 75.0f) / 35.0f, 0.0f, 1.0f);
        d.breathiness = std::clamp (0.6f * d.noise + 0.4f * (1.0f - s.voicedRatio), 0.0f, 1.0f);

        d.stability = std::clamp (1.0f - s.pitchStabilityCents / 40.0f, 0.0f, 1.0f);
        if (pitchFrames.size() > 8)
        {
            double centsOffAccum = 0.0; int n = 0;
            std::vector<float> centsFromMed;
            centsFromMed.reserve (pitchFrames.size());
            const float med = s.pitchMedianHz > 0.0f ? s.pitchMedianHz
                                                     : pitchFrames[pitchFrames.size() / 2];
            for (float hz : pitchFrames)
            {
                if (hz <= 0.0f) continue;
                const float midiF = 69.0f + 12.0f * std::log2 (hz / 440.0f);
                const float frac  = midiF - std::round (midiF);
                centsOffAccum += (double) (std::abs (frac) * 100.0f);   // 0..50
                ++n;
                if (med > 0.0f) centsFromMed.push_back (1200.0f * std::log2 (hz / med));
            }
            if (n > 0)
                d.tuning = std::clamp (1.0f - (float) (centsOffAccum / n) / 35.0f, 0.0f, 1.0f);

            if (centsFromMed.size() > 4)
            {
                std::sort (centsFromMed.begin(), centsFromMed.end());
                const float q1 = centsFromMed[centsFromMed.size() / 4];
                const float q3 = centsFromMed[(centsFromMed.size() * 3) / 4];
                d.vibrato = std::clamp ((std::abs (q3 - q1) - 15.0f) / 70.0f, 0.0f, 1.0f);
            }
        }

        d.valid = true;
    }

    s.valid = true;
    return s;
}

VocalDNA AnalysisEngine::getLiveDNA() const
{
    auto norm = [] (float x, float lo, float hi)
    { return std::clamp ((x - lo) / (hi - lo), 0.0f, 1.0f); };

    VocalDNA d;
    const float b0 = uiBandDb[0].load(), b1 = uiBandDb[1].load(), b2 = uiBandDb[2].load(),
                b3 = uiBandDb[3].load(), b4 = uiBandDb[4].load(), b5 = uiBandDb[5].load(),
                b6 = uiBandDb[6].load();

    d.brightness = uiBrightnessLive.load();
    d.warmth     = norm (b1, -26.0f,  -8.0f);
    d.presence   = norm (b4, -28.0f,  -8.0f);
    d.air        = norm (b6, -40.0f, -14.0f);
    d.muddiness  = norm (b2, -24.0f,  -9.0f);
    d.boxiness   = norm (b2, -20.0f,  -6.0f) * 0.7f;
    d.harshness  = std::clamp ((b4 - b3 + 2.0f) / 14.0f, 0.0f, 1.0f);
    d.sibilance  = std::clamp (0.5f * norm (b5, -34.0f, -12.0f)
                             + 0.5f * std::clamp (uiSibLive.load() * 2.5f, 0.0f, 1.0f), 0.0f, 1.0f);
    d.plosives   = norm (b0, -30.0f, -10.0f);
    d.nasal      = std::clamp ((uiNasalLive.load() - 0.15f) * 3.0f, 0.0f, 1.0f);

    // slow axes seeded from the last LEARN pass (persist between passes)
    d.tuning      = liveTuning.load();
    d.stability   = liveStability.load();
    d.vibrato     = liveVibrato.load();
    d.dynamics    = liveDynamics.load();
    d.noise       = liveNoise.load();
    d.breathiness = liveBreath.load();

    d.valid = true;
    return d;
}

bool AnalysisEngine::getLatestSpectrum (float* dest)
{
    if (! spectrumReady.exchange (false))
        return false;
    std::copy (magnitudes.begin(), magnitudes.end(), dest);
    return true;
}
} // namespace vf
