#include "NoteIntelligence.h"
#include <cmath>
#include <algorithm>

namespace vf
{
namespace
{
    // A new note is confirmed once the pitch has held near a new value for this
    // long. Short enough to feel instant on a real melodic move, long enough that
    // a scoop or an overshoot doesn't get mistaken for a new target.
    constexpr float kConfirmMs = 45.0f;

    // Deviation beyond this is too far to be "the same note being sung expressively".
    constexpr float kNoteBreakCents = 90.0f;

    // Vocal vibrato lives in this band; anything faster is tremor/noise, anything
    // slower is drift or a slide.
    constexpr float kVibMinHz = 3.5f, kVibMaxHz = 8.5f;
}

void NoteIntelligence::prepare (double framesPerSecond)
{
    fps = std::max (20.0, framesPerSecond);
    reset();
}

void NoteIntelligence::reset()
{
    centreHz = pendingHz = 0.0f;
    pendingCount = sustainCount = 0;
    silentCount = 1000;
    std::fill (std::begin (devHist), std::end (devHist), 0.0f);
    devPos = devFilled = 0;
    vibRate = vibDepth = 0.0f;
    vibMeanDev = 0.0f;
    smoothedDev = 0.0f;
    driftAccum = 0.0f;
    prevHz = 0.0f;
    velocityCps = 0.0f;
    lastState = NoteDecision::State::Silent;
}

float NoteIntelligence::centsBetween (float a, float b) const
{
    if (a <= 0.0f || b <= 0.0f) return 0.0f;
    return 1200.0f * std::log2 (a / b);
}

// ---------------------------------------------------------------------------
//  Vibrato estimation from the deviation history.
//  Rate comes from counting sign changes of the (mean-removed) deviation — a
//  robust, cheap proxy for the oscillation frequency that doesn't need an FFT —
//  and depth from its peak-to-peak span. Requiring BOTH a plausible rate and a
//  plausible, reasonably symmetric depth is what stops a slide (one-directional)
//  or a wobble in the tracking from being read as vibrato.
// ---------------------------------------------------------------------------
void NoteIntelligence::updateVibrato()
{
    const int n = devFilled;
    if (n < 12) { vibRate = vibDepth = 0.0f; vibMeanDev = 0.0f; return; }

    float mean = 0.0f;
    for (int i = 0; i < n; ++i) mean += devHist[i];
    mean /= (float) n;

    float lo = 1.0e9f, hi = -1.0e9f;
    int crossings = 0;
    float prev = devHist[0] - mean;
    for (int i = 0; i < n; ++i)
    {
        const int idx = (devPos - n + i + kHist * 2) % kHist;
        const float v = devHist[idx] - mean;
        lo = std::min (lo, devHist[idx]);
        hi = std::max (hi, devHist[idx]);
        if ((v > 0.0f && prev <= 0.0f) || (v < 0.0f && prev >= 0.0f)) ++crossings;
        prev = v;
    }

    vibMeanDev = mean;

    const float seconds = (float) n / (float) fps;
    const float rate = seconds > 0.0f ? (float) crossings / (2.0f * seconds) : 0.0f;
    const float depth = hi - lo;

    if (rate >= kVibMinHz && rate <= kVibMaxHz && depth > 12.0f && depth < 220.0f)
    {
        vibRate = rate;
        vibDepth = depth;
    }
    else
    {
        // decay rather than hard-clear, so one ambiguous frame doesn't drop it
        vibRate *= 0.8f;
        vibDepth *= 0.8f;
        if (vibRate < 1.0f) { vibRate = 0.0f; vibDepth = 0.0f; }
    }
}

// ---------------------------------------------------------------------------
NoteDecision NoteIntelligence::process (float hz, bool voiced)
{
    NoteDecision d;

    if (! voiced || hz <= 0.0f)
    {
        if (++silentCount > (int) (0.08 * fps))
        {
            // A real gap between phrases: forget the note so the next entry is
            // free to start anywhere (an onset shouldn't be dragged toward the
            // previous word's pitch).
            centreHz = pendingHz = 0.0f;
            pendingCount = sustainCount = 0;
            devFilled = devPos = 0;
            vibRate = vibDepth = 0.0f;
            driftAccum = 0.0f;
            prevHz = 0.0f;
            velocityCps = 0.0f;
        }
        d.state = NoteDecision::State::Silent;
        d.correctionScale = 0.0f;
        d.expressionKeep  = 1.0f;
        d.glideScale = 1.0f;
        lastState = d.state;
        return d;
    }

    const bool wasSilent = silentCount > 0;
    silentCount = 0;

    // ---- note onset ---------------------------------------------------
    if (centreHz <= 0.0f)
    {
        centreHz = hz;
        pendingHz = hz;
        pendingCount = 0;
        sustainCount = 0;
        devFilled = devPos = 0;
        driftAccum = 0.0f;
        smoothedDev = 0.0f;
        prevHz = hz;
        velocityCps = 0.0f;

        d.state = NoteDecision::State::Onset;
        d.noteCentreHz = centreHz;
        d.noteChanged = true;
        // Attacks are shaped by the singer; correcting hard on the very first
        // frames is what makes an onset sound clipped/warbly.
        d.correctionScale = 1.0f - 0.5f * params.transitionSmoothing;
        d.expressionKeep  = 1.0f;     // never squash the shape of an attack
        d.glideScale = 1.0f;
        lastState = d.state;
        return d;
    }

    const float dev = centsBetween (hz, centreHz);
    smoothedDev = 0.7f * smoothedDev + 0.3f * dev;

    // Pitch velocity (cents/second), smoothed. This is the evidence for a slide.
    if (prevHz > 0.0f)
    {
        const float instant = centsBetween (hz, prevHz) * (float) fps;
        velocityCps = 0.6f * velocityCps + 0.4f * instant;
    }
    prevHz = hz;

    devHist[devPos] = dev;
    devPos = (devPos + 1) % kHist;
    if (devFilled < kHist) ++devFilled;

    updateVibrato();
    const bool vibratoNow = vibRate > 0.0f && vibDepth > 12.0f;

    // ---- has the singer moved to a different note? --------------------
    // A move is only accepted once the pitch HOLDS near the new value; that is
    // what separates "new note" from "passing through on a slide".
    const int confirmFrames = std::max (2, (int) (kConfirmMs * 0.001 * fps));
    bool changed = false;

    if (std::abs (dev) > kNoteBreakCents && ! vibratoNow)
    {
        if (pendingHz <= 0.0f || std::abs (centsBetween (hz, pendingHz)) > 60.0f)
        {
            pendingHz = hz;          // start (or restart) tracking a new target
            pendingCount = 1;
        }
        else
        {
            pendingHz = 0.85f * pendingHz + 0.15f * hz;
            if (++pendingCount >= confirmFrames)
            {
                centreHz = pendingHz;     // confirmed: lock onto the new note
                pendingCount = 0;
                sustainCount = 0;
                devFilled = devPos = 0;
                driftAccum = 0.0f;
                changed = true;
            }
        }
    }
    else
    {
        pendingCount = 0;
        pendingHz = 0.0f;
        ++sustainCount;

        // While the note is held, let the centre follow only the SLOW component,
        // and only as far as drift correction allows. This is what keeps a
        // sustain locked (no wandering) while still forgiving a singer who leans.
        if (! vibratoNow)
        {
            const float follow = 0.004f * (1.0f - params.driftCorrection);
            centreHz *= std::pow (2.0f, (dev * follow) / 1200.0f);
            driftAccum = 0.98f * driftAccum + 0.02f * dev;
        }
        else
        {
            // VIBRATO: converge the centre on the MEAN of the swing.
            //
            // Following the instantaneous deviation here would make the centre
            // chase the oscillation and defeat the whole point of having one.
            // But FREEZING it — which is what this used to do — leaves the
            // centre wherever the note happened to start, and a note that
            // begins mid-swing starts up to a full vibrato depth out. That
            // error never healed, and because the engine measures expression
            // as deviation from this centre, it leaked straight into the
            // output: a deep-vibrato note measured 11 cents flat even though
            // the centre correction itself was working perfectly.
            //
            // The mean of the deviation history is exactly that error, and
            // averaging over the history spans whole cycles, so the swing
            // itself contributes nothing to it. Converging at roughly a third
            // of a second is slow enough to ignore the oscillation and fast
            // enough that the first sustained note is already centred.
            centreHz *= std::pow (2.0f, (vibMeanDev * 0.02f) / 1200.0f);
        }
    }

    // ---- classify + set the correction policy -------------------------
    const float absDev = std::abs (dev);
    // Travelling steadily through pitch = a slide/bend. Vibrato also moves, but
    // it reverses direction constantly, so it is excluded explicitly above.
    const bool moving = std::abs (velocityCps) > 110.0f && ! vibratoNow;

    if (changed)
    {
        d.state = NoteDecision::State::Onset;
        d.correctionScale = 1.0f;
        d.expressionKeep  = 1.0f;
        d.glideScale = 1.0f - 0.35f * params.transitionSmoothing;   // arrive promptly
        d.noteChanged = true;
    }
    else if (vibratoNow)
    {
        // Correct the CENTRE of the vibrato, not its swing.
        //
        // This branch used to scale the CENTRE pull down by the preservation
        // amount, which is the wrong axis and was the single worst-sounding
        // fault in the engine: at the default 75% preservation a vibrato note
        // could only ever be pulled a third of the way onto pitch, so every
        // expressive singer stayed audibly flat no matter where Correction was
        // set. The centre now gets FULL correction and the swing is preserved
        // by `expressionKeep` instead, which is what the singer actually wants:
        // in tune, still expressive.
        d.state = NoteDecision::State::Vibrato;
        d.correctionScale = 1.0f;
        d.expressionKeep  = params.vibratoPreservation;
        d.glideScale = 1.0f + 2.0f * params.vibratoPreservation;
    }
    else if (moving)
    {
        // A slide/bend in progress. Here the target genuinely IS uncertain —
        // the note centre still holds the note being left, not the one being
        // reached — so easing the centre pull is correct. The gesture itself
        // passes through untouched (keep = 1) rather than being pulled back
        // toward the note it is departing from.
        d.state = NoteDecision::State::Transition;
        d.correctionScale = 1.0f - 0.85f * params.transitionSmoothing;
        d.expressionKeep  = 1.0f;
        d.glideScale = 1.0f + 3.0f * params.transitionSmoothing;
    }
    else if (absDev > 6.0f)
    {
        // Settled but off the note: correct it. Flex Tune no longer throttles
        // the pull here — a note that is settled and off pitch is exactly the
        // case the plugin exists to fix. Flex governs how much micro-variation
        // is kept on the way, via expressionKeep.
        d.state = NoteDecision::State::OffPitch;
        d.correctionScale = 1.0f;
        d.expressionKeep  = params.flexTune;
        d.glideScale = 1.0f;
    }
    else
    {
        d.state = NoteDecision::State::Sustain;
        d.correctionScale = 1.0f;
        d.expressionKeep  = params.flexTune;
        d.glideScale = 1.0f;
    }

    d.noteCentreHz      = centreHz;
    d.deviationCents    = dev;
    d.vibratoRateHz     = vibRate;
    d.vibratoDepthCents = vibDepth;
    d.correctionScale   = juce::jlimit (0.0f, 1.0f, d.correctionScale);
    d.expressionKeep    = juce::jlimit (0.0f, 1.0f, d.expressionKeep);
    d.glideScale        = juce::jlimit (0.25f, 6.0f, d.glideScale);

    juce::ignoreUnused (wasSilent);
    lastState = d.state;
    return d;
}
} // namespace vf
