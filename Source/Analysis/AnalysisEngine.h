#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include "PitchDetector.h"
#include "LoudnessMeter.h"

namespace vf
{
// ============================================================================
//  AnalysisSnapshot — everything the AutoMix brain needs to make decisions.
//  Produced after a LEARN pass; all values are session aggregates.
// ============================================================================
struct AnalysisSnapshot
{
    // Level / dynamics
    float peakDb          = -100.0f;
    float rmsDb           = -100.0f;
    float crestDb         = 0.0f;      // peak - rms (dynamics indicator)
    float integratedLufs  = -100.0f;
    float noiseFloorDb    = -100.0f;

    // Spectral balance (energy in dB, relative to total)
    float subDb       = -100.0f;   //   20–80 Hz    rumble / plosives
    float lowDb       = -100.0f;   //   80–250 Hz   body / proximity
    float mudDb       = -100.0f;   //  250–600 Hz   boxiness
    float midDb       = -100.0f;   //  600–2500 Hz  core tone
    float presenceDb  = -100.0f;   // 2500–6000 Hz  clarity
    float sibDb       = -100.0f;   // 6000–10000 Hz sibilance zone
    float airDb       = -100.0f;   //   10k–20k Hz  air

    float sibilanceRatio = 0.0f;   // peak sibilant energy vs voice energy 0..1+
    float brightness     = 0.5f;   // 0 dark .. 1 bright (spectral centroid based)

    // Pitch
    float pitchMedianHz     = 0.0f;
    float pitchStabilityCents = 0.0f;  // median abs deviation between frames
    float voicedRatio       = 0.0f;    // fraction of frames with confident pitch

    // Key detection (Krumhansl-Schmuckler on the pitch-class histogram)
    int   keyRoot       = -1;          // 0=C … 11=B, -1 = undetected
    bool  keyIsMajor    = true;
    float keyConfidence = 0.0f;        // 0..1 correlation margin

    bool  valid = false;
};

// ============================================================================
//  AnalysisEngine — runs on the audio thread. Cheap per-block metrics always;
//  full aggregation only while "learning". Also feeds the GUI spectrum FIFO.
// ============================================================================
class AnalysisEngine
{
public:
    static constexpr int fftOrder = 11;               // 2048
    static constexpr int fftSize  = 1 << fftOrder;

    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Analyse a block (pre-processing input). Real-time safe. */
    void process (const juce::AudioBuffer<float>& buffer);

    void  startLearning();
    /** Stops learning and finalises the snapshot. */
    AnalysisSnapshot finishLearning();
    bool  isLearning() const noexcept { return learning.load(); }

    // ---- GUI feed (lock-free) ---------------------------------------------
    /** Copies latest magnitude spectrum (fftSize/2 bins) if fresh. */
    bool getLatestSpectrum (float* dest);
    float getCurrentPitchHz()  const noexcept { return uiPitchHz.load(); }
    float getMomentaryLufs()   const noexcept { return uiLufs.load(); }
    float getCurrentPeakDb()   const noexcept { return uiPeakDb.load(); }

private:
    void runFft();
    void accumulateLearning (const juce::AudioBuffer<float>& buffer);

    double fs = 44100.0;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize>      fifo {};
    std::array<float, fftSize * 2>  fftData {};
    std::array<float, fftSize / 2>  magnitudes {};   // smoothed, for GUI + learning
    int fifoIndex = 0;
    std::atomic<bool> spectrumReady { false };

    PitchDetector pitch;
    LoudnessMeter loudness;

    // ---- learning accumulators (audio thread only) --------------------------
    std::atomic<bool> learning { false };
    double sumSquares = 0.0;
    long long sampleCount = 0;
    float  learnPeak = 0.0f;
    std::array<double, 7> bandEnergy {};    // spectral bands
    double bandTotal = 0.0;
    double centroidWeighted = 0.0, centroidTotal = 0.0;
    std::vector<float> pitchFrames;         // reserved in prepare(); no RT alloc
    std::vector<float> frameRmsDb;          // per-FFT-frame RMS for noise floor
    int totalPitchFrames = 0;
    double sibPeakRatioAccum = 0.0;
    int    sibFrames = 0;

    // ---- UI atomics ----------------------------------------------------------
    std::atomic<float> uiPitchHz { 0.0f }, uiLufs { -100.0f }, uiPeakDb { -100.0f };
};
} // namespace vf
