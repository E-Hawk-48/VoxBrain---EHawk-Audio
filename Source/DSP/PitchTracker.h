#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace vf
{
// ============================================================================
//  PitchTracker — VoxBrain's redesigned monophonic F0 engine.
//  ---------------------------------------------------------------------------
//  WHY THIS EXISTS. The previous tracker was a single-candidate YIN: it took the
//  FIRST lag whose cumulative-mean-normalised difference (CMND) fell under a
//  fixed threshold, then smoothed the result with a median of three. That design
//  has three failure modes you can hear on a vocal:
//    * OCTAVE ERRORS — on a harmonically rich voice the sub-harmonic dip can beat
//      the true one, so the reading snaps an octave down for a frame or two.
//    * NOTE-FLIPPING / JITTER — with no memory beyond a 3-frame median, competing
//      candidates alternate frame to frame; the correction chatters between notes.
//    * FALSE / MISSED DETECTION — one hard threshold can't serve both a belted
//      note and a whisper, so quiet or breathy singing either drops out or noise
//      gets tracked as pitch.
//
//  THE REDESIGN (pYIN-family, probabilistic + temporal):
//    1. MULTI-CANDIDATE: every local minimum of the CMND function is kept, not
//       just the first crossing — the true F0 is almost always among them even
//       when it isn't the deepest dip.
//    2. OBSERVATION PROBABILITY: each candidate is scored from its CMND depth, so
//       "how periodic" is a continuous quantity instead of a yes/no threshold.
//    3. SUB-HARMONIC TEST: a candidate that is an integer multiple of a shorter,
//       nearly-as-good period is demoted — this is the direct, explicit defence
//       against octave-down errors.
//    4. VITERBI DECODING over a short sliding window, with an explicit UNVOICED
//       state. Transitions are penalised by musical distance (semitones), so a
//       stray octave jump must beat the accumulated evidence of the notes around
//       it. This is what removes flipping and jitter WITHOUT the lag of heavy
//       smoothing — sustained notes lock solid, real melodic moves still pass.
//    5. LEVEL-AWARE VOICING with an adaptive noise floor, so a whisper reads as
//       unvoiced (rather than tracking noise) while a quiet-but-pitched falsetto
//       note is still found.
//
//  LATENCY. Decoding looks `decodeLag` frames into the past, so the reported
//  pitch trails the newest input by decodeLag*hop samples. In VoxBrain this is
//  effectively FREE: RetuneEngine already delays the audio it resynthesises by
//  its own lookback (18–36 ms), and the tracker analyses input as it arrives, so
//  the decode lag is absorbed by a delay that already exists.
//
//  REAL-TIME SAFETY: every buffer is allocated in prepare(); process() does no
//  allocation, no locking and no logging.
// ============================================================================
class PitchTracker
{
public:
    struct Config
    {
        double sampleRate = 44100.0;
        float  minHz      = 55.0f;    // below the lowest male fundamental
        float  maxHz      = 1200.0f;  // above the highest falsetto/soprano
        int    hopSize    = 256;      // analysis rate (~5.8 ms @ 44.1 kHz)
        int    windowSize = 1024;     // must cover ~2 periods of minHz
        int    decodeLag  = 4;        // frames of Viterbi context (stability)
        float  sensitivity = 0.5f;    // 0 = only confident pitch, 1 = chase everything
    };

    struct Frame
    {
        float hz         = 0.0f;   // 0 when unvoiced
        float confidence = 0.0f;   // 0..1 periodicity confidence
        bool  voiced     = false;
    };

    void prepare (const Config& cfg);
    void reset();

    /** Push mono input. Returns true when at least one new decoded frame is
        available. Real-time safe. */
    bool process (const float* samples, int numSamples);

    /** Most recent DECODED frame (trails the input by latencySamples()). */
    const Frame& latest() const noexcept { return decoded; }
    float latestHz()         const noexcept { return decoded.hz; }
    float latestConfidence() const noexcept { return decoded.confidence; }
    bool  isVoiced()         const noexcept { return decoded.voiced; }

    /** Decode lag in samples (decodeLag * hopSize). */
    int latencySamples() const noexcept { return activeLag * cfg.hopSize; }

    /** Runtime tweak of tracking sensitivity (no reallocation). */
    void setSensitivity (float s) noexcept { cfg.sensitivity = juce::jlimit (0.0f, 1.0f, s); }

    /** REAL-TIME SAFE mode change. prepare() always allocates for the WIDEST
        configuration (lowest pitch floor, longest decode window), so raising the
        floor or shortening the decode lag afterwards only changes which part of
        those buffers is used — no allocation, no locking. This is what lets the
        host switch latency modes from the audio thread without a glitch (and
        without the allocation that a re-prepare would perform). */
    void setRuntimeMode (float newMinHz, int newDecodeLag) noexcept;

    const Config& getConfig() const noexcept { return cfg; }

private:
    static constexpr int kMaxCandidates = 12;   // per frame (plus the unvoiced state)
    static constexpr int kMaxLag        = 8;    // max decodeLag supported

    struct Candidate
    {
        float tau  = 0.0f;    // period in samples (fractional)
        float hz   = 0.0f;
        float cmnd = 1.0f;    // normalised difference at that lag (lower = better)
        float logProb = 0.0f; // observation log-probability
    };

    void analyseFrame();                       // one hop: CMND → candidates
    void extractCandidates();
    void decodeViterbi();                      // over the stored window
    float refineTau (int tauInt) const;        // parabolic, clamped

    Config cfg;

    // input ring
    std::vector<float> ring;
    int ringWrite = 0, ringFill = 0, hopCounter = 0;

    // analysis scratch
    std::vector<float> frameBuf, diff, cumulative;
    int minTau = 2, maxTau = 400;

    // per-frame candidate store (circular over the decode window)
    struct FrameSlot
    {
        Candidate cand[kMaxCandidates];
        int   numCands = 0;
        float unvoicedLogProb = 0.0f;
        float rms = 0.0f;
    };
    std::vector<FrameSlot> slots;   // allocated for the MAX decode lag
    int slotWrite = 0, slotsFilled = 0;
    int activeLag = 4;              // <= cfg.decodeLag (runtime, no realloc)
    int activeMaxTau = 400;         // <= maxTau (runtime pitch floor)

    // adaptive level tracking for voicing
    float noiseFloor = 1.0e-4f;
    float levelEnv   = 0.0f;

    // decoded output + continuity
    Frame decoded;
    float lastVoicedHz = 0.0f;

    JUCE_LEAK_DETECTOR (PitchTracker)
};
} // namespace vf
