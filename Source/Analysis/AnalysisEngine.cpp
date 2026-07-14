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

    if (! learning.load (std::memory_order_relaxed))
        return;

    // ---- learning: band energies, centroid, sibilance, frame RMS ----------
    const double binHz = fs / (double) fftSize;
    std::array<double, 7> frameBand {};
    double frameTotal = 0.0, wSum = 0.0;

    for (int i = 1; i < fftSize / 2; ++i)
    {
        const double f = i * binHz;
        const double e = (double) fftData[(size_t) i] * (double) fftData[(size_t) i];
        frameTotal += e;
        wSum += e * f;

        for (int b = 0; b < 7; ++b)
            if (f >= bandEdges[(size_t) b] && f < bandEdges[(size_t) b + 1])
                { frameBand[(size_t) b] += e; break; }
    }

    for (int b = 0; b < 7; ++b) bandEnergy[(size_t) b] += frameBand[(size_t) b];
    bandTotal        += frameTotal;
    centroidWeighted += wSum;
    centroidTotal    += frameTotal;

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
    loudness.reset();
    learning = true;
}

AnalysisSnapshot AnalysisEngine::finishLearning()
{
    learning = false;
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
    }

    if (centroidTotal > 0.0)
    {
        const float centroidHz = (float) (centroidWeighted / centroidTotal);
        // Map ~500 Hz..4 kHz to 0..1 logarithmically
        s.brightness = std::clamp ((std::log2 (centroidHz / 500.0f)) / 3.0f, 0.0f, 1.0f);
    }

    if (sibFrames > 0)
        s.sibilanceRatio = (float) (sibPeakRatioAccum / (double) sibFrames);

    // Noise floor = 10th percentile of frame RMS values
    if (frameRmsDb.size() > 16)
    {
        std::vector<float> sorted (frameRmsDb);
        std::sort (sorted.begin(), sorted.end());
        s.noiseFloorDb = sorted[sorted.size() / 10];
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

    // ---- Key detection: pitch-class histogram vs Krumhansl-Kessler profiles
    if (pitchFrames.size() > 32)
    {
        std::array<double, 12> hist {};
        for (float hz : pitchFrames)
        {
            const int midi = (int) std::round (69.0f + 12.0f * std::log2 (hz / 440.0f));
            hist[(size_t) (((midi % 12) + 12) % 12)] += 1.0;
        }

        static constexpr std::array<double, 12> majP { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                                       2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static constexpr std::array<double, 12> minP { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                                       2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        auto correlate = [&hist] (const std::array<double, 12>& prof, int rot)
        {
            double hx = 0, px = 0;
            for (int i = 0; i < 12; ++i) { hx += hist[(size_t) i]; px += prof[(size_t) i]; }
            hx /= 12.0; px /= 12.0;
            double num = 0, dh = 0, dp = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double a = hist[(size_t) i] - hx;
                const double b = prof[(size_t) (((i - rot) % 12 + 12) % 12)] - px;
                num += a * b; dh += a * a; dp += b * b;
            }
            return (dh > 0 && dp > 0) ? num / std::sqrt (dh * dp) : 0.0;
        };

        double best = -2.0, second = -2.0;
        for (int root = 0; root < 12; ++root)
            for (int isMaj = 0; isMaj < 2; ++isMaj)
            {
                const double c = correlate (isMaj ? majP : minP, root);
                if (c > best)
                {
                    second = best; best = c;
                    s.keyRoot = root; s.keyIsMajor = (isMaj == 1);
                }
                else if (c > second)
                    second = c;
            }
        s.keyConfidence = (float) juce::jlimit (0.0, 1.0, (best - second) * 4.0 + 0.2);
    }

    s.valid = true;
    return s;
}

bool AnalysisEngine::getLatestSpectrum (float* dest)
{
    if (! spectrumReady.exchange (false))
        return false;
    std::copy (magnitudes.begin(), magnitudes.end(), dest);
    return true;
}
} // namespace vf
