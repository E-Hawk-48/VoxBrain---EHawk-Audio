#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include "RetuneEngine.h"

namespace vf
{
// ============================================================================
//  VocalChain — the complete serial vocal processing chain:
//    Gate → EQ → De-esser → Compressor → Saturation → Delay → Reverb → Limiter
//  All parameters are plain floats set from the processor each block
//  (already smoothed/atomic-read from APVTS). Real-time safe throughout.
// ============================================================================

// ---------------------------------------------------------------------------
struct GateParams     { bool on; float thresholdDb; };
struct EqParams       { bool on; float hpfHz, lowShelfDb, mudDb, mudHz, presDb, presHz, airDb; };
struct DynEqParams    { bool on; float lowThreshDb, lowFreqHz, midThreshDb, midFreqHz,
                                       highThreshDb, highFreqHz, rangeDb; };
struct MultibandParams{ bool on; float lowThreshDb, midThreshDb, highThreshDb,
                                       lowGainDb, midGainDb, highGainDb,
                                       ratio, lowXoverHz, highXoverHz; };
struct DeEssParams    { bool on; float thresholdDb, freqHz; };
struct CompParams     { bool on; float thresholdDb, ratio, attackMs, releaseMs, makeupDb, mix; };
struct SatParams      { bool on; int type; float drive, tone, bias, mix; bool hq; }; // 0..1
struct DelayParams    { bool on; float timeMs, feedback, mix; }; // fb/mix 0..1
struct ReverbParams   { bool on; int type; float size, decay, predelayMs, damp, diffusion,
                                              lowCutHz, highCutHz, modDepth, width, mix,
                                              duck, shimmer; bool freeze; };
struct LimitParams    { bool on; float ceilingDb, gainDb; };

struct ChainParams
{
    float inputGainDb = 0.0f, outputGainDb = 0.0f;
    RetuneParams   retune {};
    GateParams     gate   {};
    EqParams       eq     {};
    DynEqParams    dyneq  {};
    DeEssParams    deess  {};
    CompParams     comp   {};
    MultibandParams mband {};
    SatParams      sat    {};
    DelayParams    delay  {};
    ReverbParams   verb   {};
    LimitParams    limit  {};
};

// ---------------------------------------------------------------------------
//  NoiseGate — downward expander with soft knee & smooth envelope
// ---------------------------------------------------------------------------
class NoiseGate
{
public:
    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& buffer, const GateParams& p);

private:
    double fs = 44100.0;
    float envelope = 0.0f;   // detector envelope (linear)
    float gain = 1.0f;       // smoothed applied gain
};

// ---------------------------------------------------------------------------
//  VocalEq — HPF + low shelf + mud bell + presence bell + air shelf
// ---------------------------------------------------------------------------
class VocalEq
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, const EqParams& p);

private:
    void updateCoefficients (const EqParams& p);

    double fs = 44100.0;
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                  juce::dsp::IIR::Coefficients<float>>;
    Filter hpf, lowShelf, mud, presence, air;
    EqParams lastParams { true, -1.0f, 0, 0, 0, 0, 0, 0 };
};

// ---------------------------------------------------------------------------
//  DynamicEq — three downward (compressive) bands. Each band isolates its
//  frequency with a unity-gain band-pass detector and removes a level-
//  dependent fraction of that band (output = input - f·band), so it is fully
//  transparent when nothing exceeds the threshold and never re-computes IIR
//  coefficients on the audio thread. Used to tame resonances / harshness that
//  only appear on peaks.
// ---------------------------------------------------------------------------
class DynamicEq
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, const DynEqParams& p);
    float getGainReductionDb() const noexcept { return grDb.load(); }

private:
    struct Band
    {
        juce::dsp::IIR::Filter<float> bpL, bpR;   // per-channel band-pass detectors
        float freq  = -1.0f;
        float envDb = -100.0f;
    };
    void updateBand (Band& b, float freq);
    void processBand (juce::AudioBuffer<float>& buffer, Band& b,
                      float threshDb, float rangeDb, float& maxGr);

    double fs = 44100.0;
    Band low, mid, high;
    std::atomic<float> grDb { 0.0f };
};

// ---------------------------------------------------------------------------
//  DeEsser — split-band compressor acting only above the crossover
// ---------------------------------------------------------------------------
class DeEsser
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, const DeEssParams& p);
    float getGainReductionDb() const noexcept { return grDb.load(); }

private:
    double fs = 44100.0;
    juce::dsp::LinkwitzRileyFilter<float> lowPass, highPass;
    juce::AudioBuffer<float> highBand;
    float envelope = 0.0f;
    float lastFreq = -1.0f;
    std::atomic<float> grDb { 0.0f };
};

// ---------------------------------------------------------------------------
//  Compressor — feed-forward RMS-ish peak detector, soft knee, parallel mix
// ---------------------------------------------------------------------------
class Compressor
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, const CompParams& p);
    float getGainReductionDb() const noexcept { return grDb.load(); }

private:
    double fs = 44100.0;
    float envelopeDb = -100.0f;
    juce::AudioBuffer<float> dryCopy;
    std::atomic<float> grDb { 0.0f };
};

// ---------------------------------------------------------------------------
//  MultibandCompressor — 3-way Linkwitz-Riley split (low/mid/high), each band
//  independently compressed (soft knee) with its own makeup / tonal-balance
//  gain, then summed. Glue + spectral consistency. Zero added latency.
// ---------------------------------------------------------------------------
class MultibandCompressor
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, const MultibandParams& p);
    float getGainReductionDb() const noexcept { return grDb.load(); }

private:
    double fs = 44100.0;
    juce::dsp::LinkwitzRileyFilter<float> lp1, hp1, lp2, hp2;
    juce::AudioBuffer<float> lowBuf, midBuf, highBuf;
    float envLowDb = -100.0f, envMidDb = -100.0f, envHighDb = -100.0f;
    float lastLowXover = -1.0f, lastHighXover = -1.0f;
    std::atomic<float> grDb { 0.0f };
};

// ---------------------------------------------------------------------------
//  Saturator — multi-model harmonic processor. Eight selectable analog-style
//  waveshapers (Tube/Tape/Console/Transformer/Germanium/Diode/Exciter/Lo-Fi),
//  bias (asymmetry → even harmonics, DC-corrected), a tone tilt, and 2×/4×
//  oversampling (Lo-Fi runs at base rate so its decimation aliases on purpose).
//  Dry/wet blend. Real-time safe; all outputs bounded.
// ---------------------------------------------------------------------------
class Saturator
{
public:
    enum Model { Tube = 0, Tape, Console, Transformer, Germanium, Diode, Exciter, LoFi };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, const SatParams& p);

private:
    void processShaped (juce::AudioBuffer<float>& buffer, const SatParams& p);
    void processExciter (juce::AudioBuffer<float>& buffer, const SatParams& p);
    void processLoFi   (juce::AudioBuffer<float>& buffer, const SatParams& p);
    void applyTone (juce::AudioBuffer<float>& buffer, float tone);

    double fs = 44100.0;
    int maxBlockSize = 0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os2, os4;   // 2× and 4×
    juce::AudioBuffer<float> dryCopy;

    float toneLp[2] { 0.0f, 0.0f };
    float exHp[2]   { 0.0f, 0.0f };
    float lofiHold[2] { 0.0f, 0.0f };
    int   lofiCount[2] { 0, 0 };
};

// ---------------------------------------------------------------------------
//  SlapDelay — filtered feedback delay
// ---------------------------------------------------------------------------
class SlapDelay
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, const DelayParams& p);

private:
    double fs = 44100.0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 * 2 };
    juce::dsp::IIR::Filter<float> fbLowpassL, fbLowpassR, fbHighpassL, fbHighpassR;
    float smoothedDelaySamples = 0.0f;
};

// ---------------------------------------------------------------------------
//  ReverbEngine — feedback-delay-network reverb. Input HPF/LPF tone → pre-delay
//  → 4 series diffusion allpasses → 4-line FDN (Hadamard feedback, per-line
//  modulated fractional delay + in-loop damping) → stereo width. Adds octave-up
//  shimmer regeneration, ducking, freeze/infinite, and a bloom swell. Seven
//  voiced algorithm types (Room/Hall/Plate/Spring/Cathedral/Shimmer/Bloom).
//  Real-time safe: everything preallocated in prepare(). Energy-preserving
//  matrix × decay<1 + damping guarantees a stable, decaying tail.
// ---------------------------------------------------------------------------
class ReverbEngine
{
public:
    enum Type { Room = 0, Hall, Plate, Spring, Cathedral, Shimmer, Bloom };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, const ReverbParams& p);

private:
    static constexpr int N = 4;

    struct Allpass
    {
        std::vector<float> buf;
        int idx = 0, len = 0;
        float g = 0.5f;
        float process (float x)
        {
            const float d = buf[(size_t) idx];
            const float y = -g * x + d;
            buf[(size_t) idx] = x + g * y;
            if (++idx >= len) idx = 0;
            return y;
        }
    };

    double fs = 44100.0;

    std::vector<float> preBuf;                 // pre-delay
    int preCap = 0, preWrite = 0;

    juce::dsp::IIR::Filter<float> lowCut, highCut;   // send tone
    float lastLowCut = -1.0f, lastHighCut = -1.0f;

    Allpass diff[N];                           // input diffusion

    std::vector<float> fdn[N];                  // FDN delay lines
    int   fdnCap[N] { 0, 0, 0, 0 };
    int   fdnBaseLen[N] { 0, 0, 0, 0 };
    int   fdnWrite[N] { 0, 0, 0, 0 };
    float dampState[N] { 0, 0, 0, 0 };
    float lfoPhase[N] { 0, 0, 0, 0 };
    float lfoInc[N] { 0, 0, 0, 0 };

    std::vector<float> shBuf;                   // shimmer pitch-shifter
    int   shCap = 0, shWrite = 0;
    float shRamp = 0.0f;

    float duckEnv = 0.0f, bloomEnv = 0.0f;
};

// ---------------------------------------------------------------------------
//  Limiter — wraps juce::dsp::Limiter (lookahead brickwall-ish)
// ---------------------------------------------------------------------------
class OutputLimiter
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, const LimitParams& p);

private:
    juce::dsp::Limiter<float> limiter;
};

// ---------------------------------------------------------------------------
class VocalChain
{
public:
    // ------------------------------------------------------------------
    //  The reorderable processing stages. The chain order is DATA-DRIVEN
    //  (see DSP/ChainOrder.h) so the UI can rearrange them; the enum order
    //  below is the historical/default order, so looping 0..kStageCount-1
    //  reproduces the original hard-wired chain exactly.
    // ------------------------------------------------------------------
    enum class Stage { Retune = 0, Gate, Eq, DynEq, DeEss, Comp, MBand, Sat, Delay, Verb, Limit };
    static constexpr int kStageCount = 11;

    /** Stable id used in saved state (never rename). */
    static const char* stageId (Stage s) noexcept;
    /** Human-readable name for the UI. */
    static const char* stageName (Stage s) noexcept;
    /** Parse a stable id; returns false if unknown. */
    static bool stageFromId (const juce::String& id, Stage& out) noexcept;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const ChainParams& p);

    /** Run ONE stage — the unified (reorderable) dispatcher calls this. */
    void processStage (Stage s, juce::AudioBuffer<float>& buffer, const ChainParams& p);
    /** Input / output trim that bookend the chain (never reordered). */
    void applyInputGain  (juce::AudioBuffer<float>& buffer, const ChainParams& p);
    void applyOutputGain (juce::AudioBuffer<float>& buffer, const ChainParams& p);

    float getCompGainReductionDb()  const noexcept { return comp.getGainReductionDb(); }
    float getDeEssGainReductionDb() const noexcept { return deess.getGainReductionDb(); }
    float getDynEqGainReductionDb() const noexcept { return dyneq.getGainReductionDb(); }
    float getMultibandGainReductionDb() const noexcept { return mband.getGainReductionDb(); }
    int   getLatencySamples()       const noexcept { return retune.getLatencySamples(); }
    RetuneEngine& getRetune()             noexcept { return retune; }

private:
    RetuneEngine  retune;
    NoiseGate     gate;
    VocalEq       eq;
    DynamicEq     dyneq;
    DeEsser       deess;
    Compressor    comp;
    MultibandCompressor mband;
    Saturator     sat;
    SlapDelay     delay;
    ReverbEngine  verb;
    OutputLimiter limiter;
    juce::dsp::ProcessSpec currentSpec {};
};
} // namespace vf
