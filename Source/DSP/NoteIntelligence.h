#pragma once
#include <juce_core/juce_core.h>

namespace vf
{
// ============================================================================
//  NoteIntelligence — the MUSICAL layer of the auto-tune engine.
//  ---------------------------------------------------------------------------
//  PitchTracker answers "what frequency is this?" very accurately. That alone is
//  not enough to correct a vocal well, because the same 40-cent deviation can be
//  three completely different things:
//      * the singer scooping INTO a note      → intentional, keep it
//      * vibrato swinging around the note      → intentional, keep it
//      * the singer simply being flat          → unwanted, correct it
//  A corrector that can't tell them apart either flattens the performance (fix
//  everything) or leaves it out of tune (fix nothing). This class makes that
//  judgement, frame by frame, and hands the engine a POLICY rather than a number.
//
//  How it decides:
//    * A NOTE CENTRE is tracked with a slow estimator, so it stays locked while a
//      note is held but re-targets as soon as a move is confirmed — that is what
//      makes sustains stable AND melodic changes instant.
//    * VIBRATO is identified from the oscillation of the deviation around that
//      centre: a periodic swing at a vocal vibrato rate (≈4–8 Hz) with plausible
//      depth. Vibrato is corrected AT ITS CENTRE, never squeezed flat, so it stays
//      natural instead of being either removed or exaggerated.
//    * A TRANSITION (slide / bend / portamento) is a sustained, directional move
//      away from the centre. While one is in progress correction eases off, so
//      the slide is heard as musical rather than being snapped into steps; when
//      the pitch settles, the new note is confirmed and full correction resumes.
//    * Anything else that is settled but off the target is genuine OFF-PITCH and
//      gets corrected fully.
//
//  The output is deliberately expressed as scaling factors (0..1) rather than
//  decisions, so the engine can blend them smoothly and the "hard tune" sound
//  remains reachable by simply turning the musical allowances to zero.
//
//  Frame-rate based, no allocation, no locking — safe to call per analysis hop.
// ============================================================================
struct NoteDecision
{
    enum class State { Silent = 0, Onset, Sustain, Vibrato, Transition, OffPitch };

    State state = State::Silent;

    float noteCentreHz   = 0.0f;   // the note the singer is actually on
    float deviationCents = 0.0f;   // current pitch vs that centre
    float vibratoRateHz  = 0.0f;   // 0 when no vibrato detected
    float vibratoDepthCents = 0.0f;

    /** How much of the configured correction to apply right now (0..1). Reduced
        during intentional expression, full on genuine off-pitch. */
    float correctionScale = 1.0f;

    /** Multiplier on the retune glide time (>1 = ease, <1 = snap). */
    float glideScale = 1.0f;

    /** True on the frame a new note target is confirmed. */
    bool noteChanged = false;
};

class NoteIntelligence
{
public:
    struct Params
    {
        /** 0 = correct everything (classic hard tune), 1 = leave natural
            deviation alone until it is clearly wrong. */
        float flexTune = 0.35f;
        /** 0 = flatten vibrato, 1 = pass vibrato through untouched. */
        float vibratoPreservation = 0.75f;
        /** 0 = snap between notes, 1 = let slides/bends breathe. */
        float transitionSmoothing = 0.6f;
        /** 0 = ignore slow drift, 1 = pull long-term drift back to the note. */
        float driftCorrection = 0.5f;
    };

    void prepare (double framesPerSecond);
    void reset();
    void setParams (const Params& p) noexcept { params = p; }

    /** One analysis frame. `hz` is 0 / `voiced` false when unpitched. */
    NoteDecision process (float hz, bool voiced);

private:
    static constexpr int kHist = 48;      // ~280 ms of deviation history @ 172 fps

    void updateVibrato();
    float centsBetween (float a, float b) const;

    Params params;
    double fps = 172.0;

    float centreHz     = 0.0f;    // locked note centre
    float pendingHz    = 0.0f;    // candidate for a new note
    int   pendingCount = 0;
    int   sustainCount = 0;
    int   silentCount  = 0;

    // deviation history (cents around the centre) for vibrato/transition analysis
    float devHist[kHist] {};
    int   devPos = 0, devFilled = 0;

    float vibRate = 0.0f, vibDepth = 0.0f;
    float smoothedDev = 0.0f;
    float driftAccum = 0.0f;

    // A slide/bend is defined by MOVEMENT, not by distance from the note: the
    // singer travelling steadily through pitch. Tracking velocity (cents per
    // second) is what separates "gliding into the next note" from "sitting on
    // this note but flat", which look identical if you only measure deviation.
    float prevHz = 0.0f;
    float velocityCps = 0.0f;      // smoothed, cents/second

    NoteDecision::State lastState = NoteDecision::State::Silent;
};
} // namespace vf
