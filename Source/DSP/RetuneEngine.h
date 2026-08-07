#pragma once
#include <juce_dsp/juce_dsp.h>
#include "PitchTracker.h"
#include "NoteIntelligence.h"
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
    int   latencyMode = 2;       // 0=Live, 1=Balanced, 2=Studio (default = original)

    // ---- musical intelligence (see DSP/NoteIntelligence.h) -----------------
    //  These decide WHAT gets corrected, as opposed to how hard/fast. All four
    //  at 0 (with speedMs 0) gives the classic fully-hard tuned sound; raising
    //  them progressively hands expression back to the singer.
    float flexTune            = 0.35f;  // 0..1 tolerance for natural deviation
    float vibratoPreservation = 0.75f;  // 0..1 keep vibrato instead of flattening
    float transitionSmoothing = 0.60f;  // 0..1 let slides/bends breathe
    float driftCorrection     = 0.50f;  // 0..1 pull long-term drift back to pitch

    /** FREE TRANSPOSITION, in semitones — independent of pitch correction.
        Correction decides WHICH note the singer should be on; this shifts the
        whole voice by a fixed interval on top of that. They multiply, so you can
        hard-tune to a scale AND drop the result an octave (the "demonic" trick).
        0 = off and the engine behaves exactly as it did before this existed. */
    float transposeSemis = 0.0f;  // -24 .. +24

    float sensitivity   = 0.5f;   // 0..1 tracking sensitivity (voicing bias)
    /** ONE-KNOB "modern auto-tune": blends every musical allowance toward zero
        and the glide toward instant. At 1.0 the result is the classic hard-tuned
        sound regardless of how the expressive controls are set. */
    float hardTune      = 0.0f;   // 0..1
    /** Deviations smaller than this are left completely alone — the natural
        micro-variation that makes a voice sound human. 0 = correct everything. */
    float snapThreshold = 0.0f;   // cents
    /** Offline/render mode: spend more analysis hindsight for the steadiest
        possible tracking (latency is irrelevant when not monitoring). */
    bool  hqRender      = false;
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

    // ---- latency modes ------------------------------------------------------
    //  Live/Balanced/Studio trade reported latency against how low a pitch the
    //  engine can track+reshape (a grain needs ~two periods of lookahead, so a
    //  lower floor = more latency). Studio is the original, most accurate path.
    struct LatencyConfig { float minHz; int grainMargin; int delaySamples; };
    static LatencyConfig configForMode (int mode, double sampleRate);
    /** Reported latency for a mode — pure, so the processor can report it to the
        host without waiting for the audio thread to switch. */
    static int latencyForMode (int mode, double sampleRate)
        { return configForMode (mode, sampleRate).delaySamples; }
    /** Set the active mode (message thread, e.g. prepareToPlay). No allocation. */
    void setLatencyMode (int mode) { applyLatencyMode (mode); }

private:
    void applyLatencyMode (int mode);   // recompute active delay/floor; re-seat OLA

    // ---- pitch tracking -----------------------------------------------------
    //  F0 now comes from the redesigned PitchTracker (multi-candidate + Viterbi;
    //  see PitchTracker.h). Its decode lag is chosen per latency mode to stay
    //  INSIDE the lookback delay this engine already imposes, so the extra
    //  stability is free — the audio being resynthesised is already that old.
    void  pushAnalysis (float sample);
    void  configureTracker();          // allocates (prepare only)
    void  applyTrackerMode() noexcept; // RT-safe narrowing per latency mode
    PitchTracker tracker;
    std::vector<float> trackerFeed;     // preallocated hop buffer (RT-safe)
    int   trackerFeedFill = 0;

    // Musical layer: decides how much of the configured correction is actually
    // appropriate right now (vibrato/slide preserved, off-pitch corrected).
    NoteIntelligence notes;
    float noteCorrectionScale = 1.0f;   // updated per analysis hop
    float noteGlideScale      = 1.0f;
    bool  hqMode = false;               // HQ render: maximum tracking hindsight

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
    int delaySamples = 0;                          // ACTIVE lookback (per mode)
    int maxDelaySamples = 0;                       // buffers sized for this (Studio)

    // Active latency mode (see configForMode). Studio by default.
    int   activeMode        = 2;
    float activeMinHz       = 65.0f;               // lowest pitch YIN will chase
    int   activeGrainMargin = 192;                 // scheduling slack in the delay

    // Analysis (F0 comes from `tracker`; see PitchTracker.h)
    float currentF0 = 0.0f, f0Confidence = 0.0f;
    float smoothedPeriod = 0.0f;                   // samples
    int   octaveJumpCount = 0;

    // Slow pitch centre for humanize (the old median-of-3 outlier rejection is
    // gone — the tracker now resolves outliers with full temporal context).
    float f0Center = 0.0f;

    // Voicing decision with hysteresis (updated per hop)
    bool voicedState = false;
    int  unvoicedHops = 0;

    // Grain scheduler state
    double nextGrainOut = 0.0;                     // absolute output position
    double lastSourceCentre = 0.0;
    float  currentRatio = 1.0f;      // correction x transpose (what grains use)
    float  correctionRatio = 1.0f;   // the glided correction component alone
    float  transposeRatio = 1.0f;    // fixed interval, not glided
    bool   gridValid = false;                      // source grid continuity flag

    std::atomic<float> uiInHz { 0.0f }, uiTargetHz { 0.0f };

    // Falsetto/soprano headroom (the old engine stopped at 900 Hz).
    static constexpr float maxHz = 1200.0f;
};
} // namespace vf
