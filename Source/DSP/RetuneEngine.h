#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>

namespace vf
{
// ============================================================================
//  RetuneEngine — real-time pitch correction for monophonic vocals.
//
//  Architecture (TD-PSOLA family, formant-preserving by construction):
//   1. Incremental YIN tracker estimates F0 every hop, with octave-jump
//      rejection and hysteresis on the voiced/unvoiced decision.
//   2. A scale quantizer picks the nearest allowed note; the correction
//      ratio glides toward it at the user's retune speed.
//   3. Pitch-synchronous overlap-add resynthesis with FRACTIONAL grain
//      placement: Hann grains two periods long are re-spaced at
//      period/ratio intervals. Grain waveforms are unmodified, so vocal
//      formants are preserved. A window-sum buffer normalises the OLA.
//
//  Unvoiced audio (consonants, breaths, sibilance) passes through the SAME
//  grain pipeline at ratio 1 with the source grid locked to the ideal read
//  position — a transparent reconstruction. Nothing is ever crossfaded
//  against a phase-misaligned dry path, which would comb/chop.
//
//  The engine imposes a fixed lookback delay (getLatencySamples()) so
//  grains can be read ahead of the output position. Bypass still delays
//  the dry signal so toggling correction never shifts timing.
// ============================================================================
struct RetuneParams
{
    bool  on          = false;
    int   keyRoot     = -1;      // 0=C … 11=B, -1 = chromatic (no key)
    bool  majorScale  = true;    // legacy: used only when scaleType < 0
    bool  chromatic   = true;    // legacy: quantize to all 12 semitones
    int   scaleType   = 0;       // 0=Chromatic,1=Major,2=NatMinor,3=HarmMinor,
                                 // 4=Dorian,5=Mixolydian,6=Phrygian,7=MajPentatonic,
                                 // 8=MinPentatonic,9=Blues (overrides chromatic/major)
    float speedMs     = 60.0f;   // retune glide time (0 = hard tune)
    float amount      = 1.0f;    // 0..1 correction depth
    float humanize    = 0.0f;    // 0..1 — preserve natural vibrato/expression
    float formant     = 0.0f;    // semitones, formant shift (0 = natural/preserved)
};

class RetuneEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Process in place. Always applies the fixed delay; corrects when p.on. */
    void process (juce::AudioBuffer<float>& buffer, const RetuneParams& p);

    int getLatencySamples() const noexcept { return delaySamples; }

    float getInputPitchHz()  const noexcept { return uiInHz.load(); }
    float getTargetPitchHz() const noexcept { return uiTargetHz.load(); }

private:
    // ---- pitch tracking -----------------------------------------------------
    void  pushAnalysis (float sample);
    float runYin();

    // ---- retune logic -------------------------------------------------------
    float quantizeTargetHz (float inputHz, const RetuneParams& p) const;
    static int scaleMaskFor (int scaleType, const RetuneParams& p);

    // ---- resynthesis --------------------------------------------------------
    void fireGrain (double grainCentreOut, double sourceCentreAbs,
                    int periodSamples, long long nowAbs, double formantRatio);
    float readRing (int channel, double absPos) const;   // linear interp

    double fs = 44100.0;
    int    channels = 2;

    // Ring buffers indexed by absolute sample position & (ringSize-1)
    int ringSize = 0;
    std::vector<std::vector<float>> ring;          // [channel][pos]
    long long writeAbs = 0;                        // absolute input position

    // OLA output accumulation
    std::vector<std::vector<float>> olaBuf;        // [channel][pos]
    std::vector<float> winSum;                     // shared window sum
    int delaySamples = 0;

    // Analysis (incremental YIN)
    int  anaWindow = 1536, anaHop = 384;
    std::vector<float> anaBuf;
    std::vector<float> anaFrame, yinDiff;
    int  anaFill = 0, anaWritePos = 0;
    float currentF0 = 0.0f, f0Confidence = 0.0f;
    float smoothedPeriod = 0.0f;                   // samples
    int   octaveJumpCount = 0;

    // F0 median smoothing (outlier rejection) + slow centre for humanize
    float f0Hist[3] { 0, 0, 0 };
    int   f0HistPos = 0;
    bool  histPrimed = false;                      // seed median on note onset
    float f0Center = 0.0f;                         // slow-moving pitch centre

    // Voicing decision with hysteresis (updated per hop)
    bool voicedState = false;
    int  unvoicedHops = 0;

    // Grain scheduler state
    double nextGrainOut = 0.0;                     // absolute output position
    double lastSourceCentre = 0.0;
    float  currentRatio = 1.0f;
    bool   gridValid = false;                      // source grid continuity flag

    std::atomic<float> uiInHz { 0.0f }, uiTargetHz { 0.0f };

    static constexpr float minHz = 65.0f, maxHz = 900.0f;
    static constexpr float yinThreshold = 0.18f;
};
} // namespace vf
