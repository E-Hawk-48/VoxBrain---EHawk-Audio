#include "RetuneEngine.h"
#include <cmath>
#include <algorithm>
#include <initializer_list>

namespace vf
{
namespace
{
    int nextPow2 (int x) { int p = 1; while (p < x) p <<= 1; return p; }

    // Natural major / natural minor pitch-class masks (bit 0 = root)
    constexpr int majorMask = 0b101010110101;   // 0 2 4 5 7 9 11
    constexpr int minorMask = 0b010110101101;   // 0 2 3 5 7 8 10

}

// ============================================================================
//  Setup
// ============================================================================
void RetuneEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    channels = std::max (1, numChannels);

    // Buffers are always sized for the LARGEST configuration (Studio) so a
    // runtime latency-mode switch needs no reallocation — it just uses a shorter
    // active lookback into the same ring. Studio is the original engine.
    const auto studio = configForMode (2, fs);
    maxDelaySamples   = studio.delaySamples;
    activeMode        = 2;
    activeMinHz       = studio.minHz;
    activeGrainMargin = studio.grainMargin;
    delaySamples      = studio.delaySamples;

    ringSize = nextPow2 ((int) std::round (fs * 0.12) + maxBlockSize + maxDelaySamples);

    ring.assign   ((size_t) channels, std::vector<float> ((size_t) ringSize, 0.0f));
    olaBuf.assign ((size_t) channels, std::vector<float> ((size_t) ringSize, 0.0f));
    winSum.assign ((size_t) ringSize, 0.0f);

    configureTracker();

    // ~8 ms one-pole for the expression term (see process()).
    exprSmoothCoeff = std::exp (-1.0f / (float) (fs * 0.008));

    reset();
}

// ---------------------------------------------------------------------------
//  Tracker configuration.
//  The decode lag buys stability (octave/flip resolution) at the cost of group
//  delay — but this engine ALREADY delays the audio it resynthesises by
//  `delaySamples`, and the tracker analyses input as it arrives. So as long as
//  the tracker's total group delay stays within that existing lookback, the
//  stability is genuinely free. Studio has the most room; Live has the least,
//  so it trades a little hindsight for its lower latency.
// ---------------------------------------------------------------------------
void RetuneEngine::configureTracker()
{
    // ALLOCATE ONCE for the widest configuration — the Studio pitch floor and the
    // longest decode window. A latency-mode change then only narrows what is
    // used (PitchTracker::setRuntimeMode), which is allocation-free. This mirrors
    // how this engine already sizes its own ring/OLA buffers for the maximum
    // delay, and it is what keeps a runtime mode switch off the allocator on the
    // audio thread.
    const auto widest = configForMode (2, fs);          // Studio = lowest floor

    PitchTracker::Config tc;
    tc.sampleRate  = fs;
    tc.minHz       = widest.minHz;
    tc.maxHz       = maxHz;
    tc.hopSize     = std::max (64, (int) std::round (fs * 0.0058));   // ~5.8 ms
    tc.windowSize  = (int) std::round (fs * 0.023);                   // ~23 ms
    tc.decodeLag   = 4;                                               // max hindsight
    tc.sensitivity = 0.5f;
    tracker.prepare (tc);

    trackerFeed.assign ((size_t) tracker.getConfig().hopSize, 0.0f);
    trackerFeedFill = 0;

    // The musical layer runs once per tracker frame, so its notion of time is
    // the analysis frame rate.
    notes.prepare (fs / (double) juce::jmax (1, tracker.getConfig().hopSize));

    applyTrackerMode();
}

void RetuneEngine::applyTrackerMode() noexcept
{
    // Real-time safe: narrows the pitch floor + decode lag to the active mode.
    // Lower-latency modes take less hindsight so the tracker's group delay keeps
    // fitting inside this engine's (shorter) lookback. HQ render mode is for
    // bouncing, where nothing is being monitored, so it takes the maximum
    // hindsight the tracker was allocated for — the steadiest possible decode.
    const int lag = hqMode ? 4
                           : (activeMode == 0 ? 2 : (activeMode == 1 ? 3 : 4));
    tracker.setRuntimeMode (activeMinHz, lag);
}

void RetuneEngine::reset()
{
    for (auto& r : ring)   std::fill (r.begin(), r.end(), 0.0f);
    for (auto& o : olaBuf) std::fill (o.begin(), o.end(), 0.0f);
    std::fill (winSum.begin(), winSum.end(), 0.0f);

    tracker.reset();
    trackerFeedFill = 0;
    notes.reset();
    noteCorrectionScale = 1.0f;
    noteGlideScale = 1.0f;
    noteExpressionKeep = 1.0f;
    noteCentreHz = 0.0f;

    writeAbs = 0;
    currentF0 = 0.0f;  f0Confidence = 0.0f;
    smoothedPeriod = (float) fs / 200.0f;
    octaveJumpCount = 0;
    f0Center = 0.0f;
    voicedState = false;
    unvoicedHops = 0;
    nextGrainOut = 1.0;
    lastSourceCentre = 0.0;
    currentRatio = 1.0f;
    correctionRatio = 1.0f;
    transposeRatio = 1.0f;
    exprSmoothed = 0.0f;
    gridValid = false;
}

// ============================================================================
//  Latency modes
// ============================================================================
//  A grain is two pitch periods long and must be read ~one period ahead of the
//  output, so the fixed lookback ≈ 2·(period of the lowest supported pitch) plus
//  a small scheduling margin. Raising the low-pitch floor therefore lowers the
//  latency. maxP = (delaySamples − grainMargin)/2 must stay ≥ ceil(fs/minHz).
RetuneEngine::LatencyConfig RetuneEngine::configForMode (int mode, double sampleRate)
{
    switch (mode)
    {
        case 0: // Live — lowest latency (floor ≈ C3); great for tracking/perform
        {
            const int P = (int) std::ceil (sampleRate / 130.0);
            return { 130.0f, 128, 2 * P + 128 };
        }
        case 1: // Balanced — middle ground (floor ≈ F#2)
        {
            const int P = (int) std::ceil (sampleRate / 90.0);
            return { 90.0f, 128, 2 * P + 128 };
        }
        default: // Studio (2) — original engine, most accurate on low notes
            return { 65.0f, 192, (int) std::round (sampleRate * 0.032) + 192 };
    }
}

void RetuneEngine::applyLatencyMode (int mode)
{
    const auto cfg = configForMode (mode, fs);
    activeMode        = mode < 0 || mode > 2 ? 2 : mode;
    activeMinHz       = cfg.minHz;
    activeGrainMargin = cfg.grainMargin;
    // Never exceed the allocated buffers (Studio is the max, but clamp anyway).
    delaySamples      = std::min (cfg.delaySamples, std::max (1, maxDelaySamples));

    // Narrow the tracker to the new mode. ALLOCATION-FREE (prepare() already
    // sized it for the widest config), so this is safe even though process()
    // applies a mode change on the audio thread.
    applyTrackerMode();

    // Re-seat only the OLA/scheduler so grains scheduled under the OLD delay do
    // not linger. The input ring is left intact — the delayed read is valid at
    // every supported delay, so audio keeps flowing (no dropout on the switch).
    for (auto& o : olaBuf) std::fill (o.begin(), o.end(), 0.0f);
    std::fill (winSum.begin(), winSum.end(), 0.0f);
    nextGrainOut     = (double) (writeAbs + 1);
    lastSourceCentre = (double) (writeAbs - delaySamples);
    gridValid        = false;
}

// ============================================================================
//  Pitch tracking — delegated to the redesigned PitchTracker
// ============================================================================
void RetuneEngine::pushAnalysis (float sample)
{
    // Feed the tracker in hop-sized chunks (it does its own windowing/ringing).
    trackerFeed[(size_t) trackerFeedFill++] = sample;
    if (trackerFeedFill < (int) trackerFeed.size())
        return;

    tracker.process (trackerFeed.data(), trackerFeedFill);
    trackerFeedFill = 0;

    // The tracker already resolves octave errors, note-flips and voicing with
    // full temporal context (Viterbi over its decode window), so the old
    // median-of-3 + threshold heuristics that used to live here are gone —
    // they can only blur a decision that has now been made properly.
    const float f0 = tracker.isVoiced() ? tracker.latestHz() : 0.0f;
    f0Confidence = tracker.latestConfidence();

    // Musical judgement on this frame: is the deviation expression (vibrato, a
    // slide, a scoop) or is it simply off-pitch? The result scales how much of
    // the user's correction actually gets applied, which is what lets the engine
    // be transparent on an expressive take and still ruthless when asked.
    const auto nd = notes.process (f0, tracker.isVoiced());
    noteCorrectionScale = nd.correctionScale;
    noteGlideScale      = nd.glideScale;
    noteExpressionKeep  = nd.expressionKeep;
    noteCentreHz        = nd.noteCentreHz;

    currentF0 = f0;
    uiInHz.store (f0);

    // Slow pitch centre (for humanize) — slower than vibrato so it tracks the note.
    if (f0 > 0.0f)
        f0Center = f0Center > 0.0f ? 0.97f * f0Center + 0.03f * f0 : f0;

    if (f0 > 0.0f)
    {
        // The grain scheduler needs a period; the tracker's estimate is already
        // temporally coherent, so this only has to follow it (a light smoother
        // keeps grain spacing from stepping on a fractional-Hz change).
        const float period = (float) fs / f0;
        smoothedPeriod = smoothedPeriod > 0.0f ? 0.6f * smoothedPeriod + 0.4f * period
                                               : period;
        octaveJumpCount = 0;
    }

    // Voicing comes from the tracker, which decides it with temporal context
    // (an explicit unvoiced state inside the Viterbi decode) rather than a bare
    // per-frame threshold. A short leave-hysteresis is still useful so a single
    // consonant inside a held word doesn't unlock the note.
    if (tracker.isVoiced())
    {
        voicedState = true;
        unvoicedHops = 0;
    }
    else if (++unvoicedHops >= 2)
    {
        voicedState = false;
    }
}

// ============================================================================
//  Scale quantizer
// ============================================================================
int RetuneEngine::scaleMaskFor (int scaleType, const RetuneParams& p)
{
    auto build = [] (std::initializer_list<int> pcs)
    { int m = 0; for (int x : pcs) m |= (1 << x); return m; };

    switch (scaleType)
    {
        case 1: return build ({ 0, 2, 4, 5, 7, 9, 11 });   // Major
        case 2: return build ({ 0, 2, 3, 5, 7, 8, 10 });   // Natural minor
        case 3: return build ({ 0, 2, 3, 5, 7, 8, 11 });   // Harmonic minor
        case 4: return build ({ 0, 2, 3, 5, 7, 9, 10 });   // Dorian
        case 5: return build ({ 0, 2, 4, 5, 7, 9, 10 });   // Mixolydian
        case 6: return build ({ 0, 1, 3, 5, 7, 8, 10 });   // Phrygian
        case 7: return build ({ 0, 2, 4, 7, 9 });          // Major pentatonic
        case 8: return build ({ 0, 3, 5, 7, 10 });         // Minor pentatonic
        case 9: return build ({ 0, 3, 5, 6, 7, 10 });      // Blues
        default: break;                                    // 0 → legacy
    }
    if (! p.chromatic && p.keyRoot >= 0)
        return p.majorScale ? majorMask : minorMask;
    return 0xFFF;
}

float RetuneEngine::quantizeTargetHz (float inputHz, const RetuneParams& p) const
{
    if (inputHz <= 0.0f)
        return 0.0f;

    const float midi = 69.0f + 12.0f * std::log2 (inputHz / 440.0f);

    const int mask = scaleMaskFor (p.scaleType, p);
    const int root = p.keyRoot >= 0 ? p.keyRoot : 0;

    const int centre = (int) std::round (midi);
    int   bestNote = centre;
    float bestDist = 1.0e9f;
    for (int nOff = -6; nOff <= 6; ++nOff)
    {
        const int note = centre + nOff;
        const int pc = ((note - root) % 12 + 12) % 12;
        if ((mask >> pc) & 1)
        {
            const float dist = std::abs ((float) note - midi);
            if (dist < bestDist) { bestDist = dist; bestNote = note; }
        }
    }
    return 440.0f * std::pow (2.0f, ((float) bestNote - 69.0f) / 12.0f);
}

// ============================================================================
//  Resynthesis helpers
// ============================================================================
float RetuneEngine::readRing (int channel, double absPos) const
{
    // 4-point Catmull-Rom cubic interpolation. Linear interpolation of the ring
    // colours every fractional grain read with high-frequency loss + a little
    // aliasing (audible as a slightly gritty/edgy retune); cubic reconstructs the
    // waveform far more faithfully at the same cost class. Bounded: the output is
    // a fixed weighted combination of four neighbours (mild, energy-preserving
    // overshoot only), and the ring is power-of-two masked so all indices are
    // valid. The window taper drives grain-edge weights to ~0, so the one extra
    // look-ahead sample (ip+2) at a grain's trailing edge is negligible.
    const int mask = ringSize - 1;
    const auto ip = (long long) std::floor (absPos);
    const float t = (float) (absPos - (double) ip);
    const auto& r = ring[(size_t) channel];
    const float xm1 = r[(size_t) ((ip - 1) & mask)];
    const float x0  = r[(size_t) ( ip      & mask)];
    const float x1  = r[(size_t) ((ip + 1) & mask)];
    const float x2  = r[(size_t) ((ip + 2) & mask)];

    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ( (2.0f * x0)
                  + (-xm1 + x1) * t
                  + (2.0f * xm1 - 5.0f * x0 + 4.0f * x1 - x2) * t2
                  + (-xm1 + 3.0f * x0 - 3.0f * x1 + x2) * t3 );
}

void RetuneEngine::fireGrain (double grainCentreOut, double sourceCentreAbs,
                              int periodSamples, long long nowAbs, double formantRatio)
{
    const int mask = ringSize - 1;
    const double P = (double) periodSamples;
    const float invP = 1.0f / (float) periodSamples;

    // Fractional grain centre: iterate integer OUTPUT positions inside the
    // span, evaluating window and source at exact fractional offsets. This
    // removes per-grain timing quantisation (audible as buzz/roughness).
    auto tStart = (long long) std::ceil  (grainCentreOut - P) ;
    auto tEnd   = (long long) std::floor (grainCentreOut + P);
    if (tStart < nowAbs)          // never write behind the emission point
        tStart = nowAbs;

    for (long long t = tStart; t <= tEnd; ++t)
    {
        const double d = (double) t - grainCentreOut;          // (-P, P)
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi
                                                * ((float) d + (float) P) * invP);
        const auto outIdx = (size_t) (t & mask);
        // Formant shift: resample the grain content around its centre. The
        // window stays in the output domain, so pitch (grain spacing) is
        // untouched while the spectral envelope (formants) scales.
        const double srcOff = d * formantRatio;
        for (int c = 0; c < channels; ++c)
            olaBuf[(size_t) c][outIdx] += w * readRing (c, sourceCentreAbs + srcOff);
        winSum[outIdx] += w;
    }
}

// ============================================================================
//  Main process
// ============================================================================
void RetuneEngine::process (juce::AudioBuffer<float>& buffer, const RetuneParams& p)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min (channels, buffer.getNumChannels());
    const int mask = ringSize - 1;

    // Apply a latency-mode switch once, at a block boundary (no allocation). The
    // host is told about the new latency separately, on the message thread.
    if (p.latencyMode != activeMode)
        applyLatencyMode (p.latencyMode);

    if (p.hqRender != hqMode)
    {
        hqMode = p.hqRender;      // allocation-free (setRuntimeMode only narrows)
        applyTrackerMode();
    }

    // HARD TUNE is a one-knob macro: it pulls every musical allowance toward
    // zero and the glide toward instant, so a single control takes the engine
    // from "transparent and natural" to the modern hard-tuned sound without the
    // user having to understand five interacting parameters.
    const float ht = juce::jlimit (0.0f, 1.0f, p.hardTune);
    const float blend = 1.0f - ht;

    // Push the musical-intelligence settings down (cheap, no allocation).
    {
        NoteIntelligence::Params np;
        np.flexTune            = juce::jlimit (0.0f, 1.0f, p.flexTune)            * blend;
        np.vibratoPreservation = juce::jlimit (0.0f, 1.0f, p.vibratoPreservation) * blend;
        np.transitionSmoothing = juce::jlimit (0.0f, 1.0f, p.transitionSmoothing) * blend;
        // Drift correction goes the OTHER way: hard tuning wants maximum pull.
        np.driftCorrection     = juce::jlimit (0.0f, 1.0f,
                                     juce::jlimit (0.0f, 1.0f, p.driftCorrection) * blend + ht);
        notes.setParams (np);
    }

    tracker.setSensitivity (juce::jlimit (0.0f, 1.0f, p.sensitivity));

    // Fully hard: the musical layer is bypassed entirely, so the classic instant
    // snap is exactly as immediate as it ever was.
    const float effSpeedMs = p.speedMs * blend;
    const bool hardTune = effSpeedMs <= 0.5f
                       && p.flexTune * blend <= 0.001f
                       && p.vibratoPreservation * blend <= 0.001f
                       && p.transitionSmoothing * blend <= 0.001f;

    const float effGlideMs = hardTune ? 0.0f : effSpeedMs * noteGlideScale;
    const float glide = effGlideMs <= 0.5f
                      ? 0.0f
                      : std::exp (-1.0f / ((float) fs * effGlideMs * 0.001f));

    const float  humanize     = juce::jlimit (0.0f, 1.0f, p.humanize);

    // Free transposition. Range is +/-24 semitones so a voice can be dropped two
    // octaves ("demonic") or lifted two ("chipmunk"); the grain scheduler's own
    // clamp below is widened to match, since it previously stopped at one octave.
    transposeRatio = std::pow (2.0f, juce::jlimit (-24.0f, 24.0f, p.transposeSemis) / 12.0f);

    // Formant range widened from +/-5 to +/-12 semitones: a convincing child or
    // giant needs the spectral envelope moved much further than a corrective
    // formant nudge ever did.
    const float  formantSemis = juce::jlimit (-12.0f, 12.0f, p.formant);
    const double formantRatio = std::pow (2.0, (double) formantSemis / 12.0);
    const double formantSpan  = std::max (1.0, formantRatio);   // for availability clamp

    // THE GRAIN ENGINE RUNS FOR TRANSPOSITION TOO, not just correction.
    //   `correcting`    — quantize to a scale (the auto-tune half).
    //   `engineActive`  — resynthesise at all. Transposition is a pure grain
    //                     respacing, so it needs the pipeline but NOT the
    //                     quantizer; a voice changer should not force the singer
    //                     onto a scale just to drop them an octave.
    // Deliberately NOT gated on `formant`: with pitch off, the formant knob has
    // always been inert, and quietly changing that would alter existing sessions.
    // Voice characters that want formant-only enable pitch with amount 0.
    const bool correcting   = p.on;
    const bool engineActive = p.on || std::abs (p.transposeSemis) > 0.001f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto n = writeAbs;
        const auto wIdx = (size_t) (n & mask);

        // 1. Store input
        float mono = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            const float s = buffer.getSample (c, i);
            ring[(size_t) c][wIdx] = s;
            mono += s;
        }
        mono /= (float) numCh;

        // 2. Track pitch + voicing — ONLY when correcting. The YIN tracker is the
        //    single biggest always-on CPU cost in the whole plugin (an O(n^2) scan
        //    every analysis hop). Running it when auto-tune is OFF (the default!)
        //    wastes headroom and, on smaller DAW buffers, is enough to push the
        //    plugin into dropouts — heard as choppy/glitchy audio. Skip it when
        //    bypassed; it warms back up in a fraction of a second when re-enabled.
        if (engineActive)
            pushAnalysis (mono);
        else
            uiInHz.store (0.0f);

        if (engineActive)
        {
            // 3. Correction ratio (cents domain, glided)
            //    Humanize: quantize a centre that blends the instantaneous pitch
            //    (hard-tune, snaps the current note) with the slow pitch centre
            //    (musical, note-stable), then re-add the natural deviation around
            //    it scaled by `humanize` so vibrato/scoops survive.
            float targetCents = 0.0f;      // glided: the pull toward the note
            float exprAdjustCents = 0.0f;  // immediate: scaling of live expression
            if (correcting && voicedState && currentF0 > 0.0f)
            {
                // WHICH PITCH GETS QUANTIZED is the most consequential decision
                // in this engine, and getting it wrong is inaudible in a unit
                // test but obvious on a real vocal.
                //
                // Quantizing the INSTANTANEOUS pitch (what this did originally)
                // breaks down the moment a singer uses vibrato: as the swing
                // crosses the midpoint between two semitones the nearest allowed
                // note flips, so the engine spends half of every vibrato cycle
                // pulling UP to A and the other half pulling DOWN to G#. Averaged
                // over the cycle the correction cancels to nothing, and a note
                // whose centre is a third of a semitone flat comes out exactly as
                // flat as it went in. Measured: 35 cents in, 35 cents out.
                //
                // The fix is to quantize the NOTE CENTRE — the stable, vibrato-
                // immune estimate the musical layer already maintains — so the
                // target note is chosen once and held for the whole note, and the
                // singer's expression rides on top of a correction that no longer
                // changes its mind. See DSP/NoteIntelligence.h.
                const bool  haveCentre = ! hardTune && noteCentreHz > 0.0f;
                const float centreHz   = haveCentre ? noteCentreHz : currentF0;

                const float snapHz = quantizeTargetHz (centreHz, p);
                uiTargetHz.store (snapHz);
                if (snapHz > 0.0f)
                {
                    // How far the NOTE is from the note it should be — this is
                    // what "Correction" acts on, and what the snap dead-zone is
                    // measured against.
                    const float centreCents = 1200.0f * std::log2 (snapHz / centreHz);

                    // The singer's live deviation from their own centre: vibrato,
                    // scoops, micro-variation. Clamped because beyond roughly a
                    // semitone this is no longer expression around a note, it is
                    // a move to a different one, and the note tracker handles that.
                    const float exprCents = haveCentre
                        ? juce::jlimit (-100.0f, 100.0f,
                                        1200.0f * std::log2 (currentF0 / centreHz))
                        : 0.0f;

                    // How much of that expression survives. `humanize` biases the
                    // per-state verdict toward full preservation, so it stays a
                    // meaningful global control without being a second, competing
                    // vibrato knob.
                    const float keep = hardTune
                        ? 0.0f
                        : juce::jlimit (0.0f, 1.0f,
                                        noteExpressionKeep + humanize * (1.0f - noteExpressionKeep));

                    // SNAP THRESHOLD: leave micro-deviation completely untouched.
                    // Correcting the last few cents is what makes a voice sound
                    // synthetic even when nothing is obviously "tuned"; above the
                    // threshold the correction fades in rather than stepping, so
                    // the boundary itself is inaudible.
                    const float snapCents = juce::jlimit (0.0f, 100.0f, p.snapThreshold) * blend;
                    float gate = 1.0f;
                    if (snapCents > 0.0f)
                    {
                        const float a = std::abs (centreCents);
                        gate = a <= snapCents ? 0.0f
                             : juce::jlimit (0.0f, 1.0f, (a - snapCents) / juce::jmax (2.0f, snapCents));
                    }

                    // The centre pull is eased only where the target is genuinely
                    // uncertain (mid-slide, first frames of an attack) — never
                    // merely because the singer is being expressive.
                    const float centrePull = hardTune ? 1.0f : noteCorrectionScale;

                    //  ratio = (target/centre)^amount x (f0/centre)^(keep-1)
                    //
                    //  THE TWO HALVES TAKE DIFFERENT PATHS, and this matters.
                    //  The centre pull is glided, because a note should ARRIVE
                    //  at pitch over the retune time rather than teleport. The
                    //  expression term must NOT be: it is a direct scaling of
                    //  the singer's own live deviation, and a one-pole lag turns
                    //  scaling into interference. At the default 60 ms retune
                    //  speed the lag at vibrato rate is over 60 degrees, so
                    //  turning preservation DOWN made a 5 Hz swing come out 9%
                    //  WIDER than the untouched input instead of narrower —
                    //  measured, and audible as the warbly/seasick artifact
                    //  people associate with badly-set auto-tune.
                    targetCents = juce::jlimit (-700.0f, 700.0f,
                                     centreCents * p.amount * gate * centrePull);
                    //  Scaled by the SAME amount/gate factors as the centre pull:
                    //  "Correction 0%" has to mean the tuner is not touching the
                    //  voice at all, and a note sitting inside the snap dead-zone
                    //  is a note the engine has decided to leave alone — including
                    //  its vibrato. Without this the expression term kept
                    //  narrowing the swing with Correction all the way down.
                    exprAdjustCents = juce::jlimit (-700.0f, 700.0f,
                                     exprCents * (keep - 1.0f) * p.amount * gate);
                }
            }
            else
            {
                uiTargetHz.store (0.0f);
            }

            // The CORRECTION ratio is glided; it answers "how far to the right
            // note". Transposition is a separate, fixed interval applied on top,
            // so the two multiply: hard-tune to a scale AND drop an octave.
            // It is deliberately NOT glided — a transpose is a setting, not a
            // performance, and ramping it would smear the effect on every note.
            const float currentCents = 1200.0f * std::log2 (juce::jmax (0.02f, correctionRatio));
            const float newCents = voicedState
                                 ? glide * currentCents + (1.0f - glide) * targetCents
                                 : 0.999f * currentCents;   // ease to unity when unvoiced
            correctionRatio = std::pow (2.0f, newCents / 1200.0f);

            // The expression term updates once per analysis hop, so it is
            // de-stepped with a short (~8 ms) smoother. That is fast enough to
            // stay in phase with vibrato — the whole point of keeping it off the
            // retune glide — while removing the hop-rate staircase from the
            // grain spacing.
            exprSmoothed += (1.0f - exprSmoothCoeff) * (exprAdjustCents - exprSmoothed);
            currentRatio = correctionRatio * transposeRatio
                         * std::pow (2.0f, exprSmoothed / 1200.0f);

            // 4. Fire due grains (max grain half-span scales with the active delay)
            const int P = juce::jlimit (32, (delaySamples - activeGrainMargin) / 2,
                                        (int) std::round (smoothedPeriod));

            if (nextGrainOut < (double) n - (double) P)   // scheduler lagged
                nextGrainOut = (double) n;

            while (nextGrainOut - (double) P <= (double) n)
            {
                const double ideal = nextGrainOut - (double) delaySamples;
                double src;

                if (voicedState && gridValid)
                {
                    // Period-aligned source grid keeps waveform coherence
                    const double k = std::round ((ideal - lastSourceCentre) / (double) P);
                    src = lastSourceCentre + k * (double) P;
                    src = juce::jlimit (ideal - (double) P, ideal + (double) P, src);
                }
                else
                {
                    // Unvoiced (or first voiced grain): lock to the ideal
                    // position → transparent reconstruction, and the grid
                    // restarts phase-aligned when voicing begins.
                    src = ideal;
                    gridValid = voicedState;
                }

                // Availability: the grain reads src ± P·formantSpan which must exist
                src = juce::jmin (src, (double) n - (double) P * formantSpan);

                fireGrain (nextGrainOut, src, P, n, formantRatio);
                lastSourceCentre = src;
                // Floor lowered from 0.5 (one octave) so two-octave drops are reachable.
                nextGrainOut += (double) P / (double) juce::jmax (0.2f, currentRatio);
            }
            if (! voicedState)
                gridValid = false;

            // 5. Emit normalised OLA
            const auto rIdx = (size_t) (n & mask);
            const float norm = 1.0f / juce::jmax (winSum[rIdx], 0.35f);
            for (int c = 0; c < numCh; ++c)
            {
                buffer.setSample (c, i, olaBuf[(size_t) c][rIdx] * norm);
                olaBuf[(size_t) c][rIdx] = 0.0f;
            }
            winSum[rIdx] = 0.0f;
        }
        else
        {
            // Bypass: delayed dry, constant latency
            uiTargetHz.store (0.0f);
            for (int c = 0; c < numCh; ++c)
                buffer.setSample (c, i, ring[(size_t) c]
                                        [(size_t) ((n - delaySamples) & mask)]);

            nextGrainOut = (double) (n + 1);
            lastSourceCentre = (double) (n - delaySamples);
            currentRatio = 1.0f;
            correctionRatio = 1.0f;
            exprSmoothed = 0.0f;
            gridValid = false;

            const auto rIdx = (size_t) (n & mask);
            for (int c = 0; c < numCh; ++c) olaBuf[(size_t) c][rIdx] = 0.0f;
            winSum[rIdx] = 0.0f;
        }

        ++writeAbs;
    }
}
} // namespace vf
