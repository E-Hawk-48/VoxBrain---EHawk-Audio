#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include "PitchDetector.h"
#include "LoudnessMeter.h"

namespace vf
{
// ============================================================================
//  VocalDNA — a normalized fingerprint of the voice. Every field is 0..1 so it
//  drops straight onto the radar visualizer, and it gives the AutoMix brain
//  targeted, explainable moves ("cut 400 Hz because it's boxy"). "Quality" axes
//  read higher = better; "problem" axes read higher = more of it = wants fixing.
// ============================================================================
struct VocalDNA
{
    // ---- quality axes (1 = great) ----
    float tuning      = 0.5f;   // 1 = dead in tune, 0 = very pitchy
    float stability   = 0.5f;   // 1 = steady note, 0 = jittery
    float vibrato     = 0.0f;   // amount of natural vibrato present
    float brightness  = 0.5f;   // dark .. bright
    float warmth      = 0.5f;   // low-mid body / fullness
    float presence    = 0.5f;   // clarity / intelligibility (2.5–6 kHz)
    float air         = 0.5f;   // top-end sheen (>10 kHz)
    float dynamics    = 0.5f;   // 1 = dynamic, 0 = squashed flat
    // ---- problem axes (1 = lots of it) ----
    float sibilance   = 0.0f;   // harsh esses (6–10 kHz)
    float harshness   = 0.0f;   // 2–6 kHz ear fatigue
    float muddiness   = 0.0f;   // 200–500 Hz build-up
    float boxiness    = 0.0f;   // 300–600 Hz boxy
    float nasal       = 0.0f;   // 900–1200 Hz honk
    float plosives    = 0.0f;   // low-end pops / rumble
    float breathiness = 0.0f;   // breath + noise between phrases
    float noise       = 0.0f;   // background noise floor
    bool  valid = false;
};

// ============================================================================
//  AnalysisSnapshot — everything the AutoMix brain needs to make decisions.
//  Produced after a LEARN pass; all values are session aggregates.
// ============================================================================
struct AnalysisSnapshot
{
    VocalDNA dna;                      // normalized fingerprint (see above)

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

    // Extended tone / dynamics descriptors (v0.2.7 analyzer)
    float spectralTiltDb = 0.0f;   // + = bright-tilted, - = dark-tilted (highs-lows)
    float dynamicRangeDb = 0.0f;   // loud-vs-quiet spread (LRA proxy, p95-p10 of frame RMS)
    float transientDensity = 0.0f; // onsets per second (syllabic/rhythmic activity)
    float harshnessDb    = 0.0f;   // 2–6 kHz peak build-up over the core (ear-fatigue risk)

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

    /** Live Vocal DNA for the radar — always-on (no LEARN pass required).
        Spectral axes update continuously; pitch/dynamics axes fill in after a
        LEARN pass via setPitchDnaFromSnapshot(). Safe to call on the message thread. */
    VocalDNA getLiveDNA() const;
    /** Seed the live radar's pitch/dynamics axes from the last full analysis so
        they persist between LEARN passes. */
    void setPitchDnaFromSnapshot (const VocalDNA& d) noexcept
    {
        liveTuning.store (d.tuning);   liveStability.store (d.stability);
        liveVibrato.store (d.vibrato); liveDynamics.store (d.dynamics);
        liveNoise.store (d.noise);     liveBreath.store (d.breathiness);
        liveHavePitch.store (d.valid);
    }

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
    double nasalEnergy = 0.0;                // 900–1200 Hz accumulator (honk)

    // ---- live (always-on) spectral state for the UI radar --------------------
    // Written by the audio thread in runFft (smoothed, plain members), then
    // published to atomics the message thread reads. No locks, no allocation.
    std::array<float, 7> liveBand {};        // smoothed relative band dB
    std::array<std::atomic<float>, 7> uiBandDb {};   // published band dB
    std::atomic<float> uiBrightnessLive { 0.5f };
    std::atomic<float> uiSibLive   { 0.0f };
    std::atomic<float> uiNasalLive { 0.0f };
    std::atomic<float> liveTuning { 0.5f }, liveStability { 0.5f },
                       liveVibrato { 0.0f }, liveDynamics { 0.5f },
                       liveNoise { 0.0f }, liveBreath { 0.0f };
    std::atomic<bool>  liveHavePitch { false };

    // ---- UI atomics ----------------------------------------------------------
    std::atomic<float> uiPitchHz { 0.0f }, uiLufs { -100.0f }, uiPeakDb { -100.0f };
};
} // namespace vf
