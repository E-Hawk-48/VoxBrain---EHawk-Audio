#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace vf
{
// ============================================================================
//  ReferenceProfile
//  ---------------------------------------------------------------------------
//  A structured, engineering-level description of how a reference vocal appears
//  to have been PRODUCED — never a description of the singer's identity, timbre
//  or performance. It is pure data (juce_core only) so it is trivially unit-
//  testable, serialisable (later: stored with a preset), and decoupled from any
//  audio-thread or UI concern.
//
//  Every measurable field is filled by ReferenceAnalyzer from a decoded buffer.
//  The `conf` block carries a 0..1 confidence for each analysis AREA so the UI
//  (and the match brain) can flag which estimates are speculative — reverb and
//  delay are inherently harder to reverse-engineer than the spectral balance.
//
//  Future-proofing: this is a plain aggregate. New descriptors can be appended
//  without breaking existing readers; the match brain only reads the fields it
//  understands. Genre-aware targets / multi-reference comparison layer on top.
// ============================================================================
struct ReferenceProfile
{
    // ---- source meta --------------------------------------------------------
    double sampleRate   = 44100.0;
    int    channels     = 1;
    double durationSec  = 0.0;
    bool   valid        = false;
    bool   vocalIsolated = false;   // true = analysed a centre-extracted vocal

    // ---- level / dynamics / loudness ---------------------------------------
    float peakDb           = -100.0f;
    float rmsDb            = -100.0f;
    float crestDb          = 0.0f;     // peak - rms (high = dynamic, low = squashed)
    float integratedLufs   = -100.0f;  // EBU R128 / BS.1770 gated
    float dynamicRangeDb   = 0.0f;     // p95-p10 of frame RMS (LRA proxy)
    float transientDensity = 0.0f;     // onsets per second
    float compressionAmount = 0.0f;    // 0..1 estimate (1 = heavily levelled)

    // ---- spectral balance (energy in dB relative to total) -----------------
    float subDb      = -100.0f;   //   20–80 Hz    rumble / weight
    float lowDb      = -100.0f;   //   80–250 Hz   body / proximity
    float mudDb      = -100.0f;   //  250–600 Hz   boxiness
    float midDb      = -100.0f;   //  600–2500 Hz  core tone
    float presenceDb = -100.0f;   // 2500–6000 Hz  clarity
    float sibDb      = -100.0f;   // 6000–10000 Hz sibilance zone
    float airDb      = -100.0f;   //   10k–20k Hz  air
    float spectralTiltDb = 0.0f;  // + = bright-tilted, - = dark-tilted

    // ---- tone descriptors (0..1, 0.5 = neutral) ----------------------------
    float brightness   = 0.5f;
    float warmth       = 0.5f;    // low-mid body
    float presence     = 0.5f;    // 2.5–6 kHz clarity
    float air          = 0.5f;    // >10 kHz sheen
    float lowEndWeight = 0.5f;    // sub/low fullness

    // ---- resonances (peaks that stand proud of the local average) ----------
    static constexpr int kMaxResonances = 4;
    int   resonanceCount = 0;
    std::array<float, kMaxResonances> resonanceHz       {};   // Hz
    std::array<float, kMaxResonances> resonanceStrength {};   // dB over local avg

    // ---- sibilance ----------------------------------------------------------
    float sibilanceRatio    = 0.0f;   // 0..1+ (sib peak vs voice energy)
    float sibilanceCenterHz = 0.0f;

    // ---- stereo / space -----------------------------------------------------
    bool  isMono            = true;
    float stereoWidth       = 0.0f;   // 0 = mono, 1 = very wide
    float stereoCorrelation = 1.0f;   // -1..1
    float reverbAmount      = 0.0f;   // 0..1 estimate
    float reverbDecaySec    = 0.0f;   // rough RT (seconds)
    float ambience          = 0.0f;   // 0..1 tail/room energy
    float preDelayMs        = 0.0f;   // rough estimate (often ~unknown)
    float delayTimeMs       = 0.0f;   // detected rhythmic echo (0 = none found)
    float delayAmount       = 0.0f;   // 0..1 strength of the detected echo

    // ---- pitch --------------------------------------------------------------
    float pitchMedianHz          = 0.0f;
    float pitchStabilityCents    = 0.0f;   // MAD of cents (low = steady/tuned)
    float voicedRatio            = 0.0f;
    float pitchCorrectionStrength = 0.0f;  // 0..1 estimate of tuning intensity

    // ---- per-area confidence (0..1) ----------------------------------------
    struct Confidence
    {
        float spectral   = 0.0f;
        float dynamics   = 0.0f;
        float sibilance  = 0.0f;
        float stereo     = 0.0f;
        float reverb     = 0.0f;
        float delay      = 0.0f;
        float pitch      = 0.0f;
        float saturation = 0.0f;
    } conf;

    // ---- normalized 7-band vector (0..1) for the comparison overlay --------
    std::array<float, 7> bandNorm {};

    // ---- smooth frequency-response curve for the panel graph ----------------
    //  64 log-spaced points from 20 Hz → min(20 kHz, Nyquist), each a magnitude
    //  in dB normalised to 0..1 across the curve (min→0, max→1). Display only.
    static constexpr int kSpectrumPoints = 64;
    std::array<float, kSpectrumPoints> spectrumCurve {};
};
} // namespace vf
