#include "AutoMixBrain.h"
#include "../Parameters.h"
#include <cmath>

namespace vf
{
using namespace vf::param;

namespace
{
    float clampf (float v, float lo, float hi) { return juce::jlimit (lo, hi, v); }

    // ------------------------------------------------------------------
    //  Vocal-style inference — a lightweight genre classifier from the
    //  LEARN features. It doesn't need tempo: delivery (voiced ratio +
    //  pitch stability), dynamics (crest) and tone (brightness) separate
    //  the common vocal idioms well enough to bias the chain toward them.
    // ------------------------------------------------------------------
    enum class Style { Rap, Pop, RnB, Rock, Ballad, Spoken };

    Style inferStyle (const AnalysisSnapshot& s)
    {
        const float voiced = s.voicedRatio;
        const float stab   = s.pitchStabilityCents;
        const float crest  = s.crestDb;
        const float bright = s.brightness;

        // Rhythmic / talky delivery: little sustained pitch.
        if (voiced < 0.35f)
            return crest > 15.0f ? Style::Rap : Style::Spoken;

        // Melodic deliveries:
        if (crest > 18.0f && bright > 0.55f)          return Style::Rock;   // belted + bright + dynamic
        if (stab < 25.0f && crest < 14.0f && voiced > 0.6f) return Style::Ballad; // steady, controlled
        if (bright < 0.50f && voiced > 0.5f)          return Style::RnB;    // warm, sustained
        return Style::Pop;                                                   // bright, modern default
    }

    const char* styleName (Style st)
    {
        switch (st)
        {
            case Style::Rap:    return "Rap";
            case Style::Pop:    return "Pop";
            case Style::RnB:    return "R&B";
            case Style::Rock:   return "Rock";
            case Style::Ballad: return "Ballad";
            case Style::Spoken: return "Spoken";
        }
        return "Vocal";
    }
}

// ============================================================================
AutoMixBrain::Result AutoMixBrain::computeChain (const AnalysisSnapshot& s)
{
    Result r;
    if (! s.valid)
    {
        r.summary = "Not enough audio analysed. Play at least a few seconds of vocal while LEARN is active.";
        return r;
    }
    auto add = [&r] (const char* id, float v, juce::String why)
    {
        r.decisions.push_back ({ id, v, std::move (why) });
    };

    // Genre-aware: infer the vocal style once and bias the chain toward it.
    const Style style = inferStyle (s);

    // ------------------------------------------------------------------
    // 1. GAIN STAGING — bring the vocal to a healthy internal level.
    //    Target ~-18 dB RMS into the chain (analog-style headroom).
    // ------------------------------------------------------------------
    const float gainTrim = clampf (-18.0f - s.rmsDb, -24.0f, 24.0f);
    add (inputGain, gainTrim,
         juce::String ("Recorded RMS is ") + juce::String (s.rmsDb, 1)
         + " dB; trimming " + juce::String (gainTrim, 1) + " dB to hit -18 dB internal level.");

    // Everything downstream assumes this trim, so compute the "staged" levels.
    const float stagedRms   = s.rmsDb + gainTrim;             // ≈ -18
    const float stagedPeak  = s.peakDb + gainTrim;
    const float stagedNoise = s.noiseFloorDb + gainTrim;

    // ------------------------------------------------------------------
    // 1b. PITCH CORRECTION — only for pitched material; tightness scales
    //     with how much the performance actually drifts.
    // ------------------------------------------------------------------
    static const char* keyNames[] = { "C", "C#", "D", "D#", "E", "F",
                                      "F#", "G", "G#", "A", "A#", "B" };
    if (s.voicedRatio > 0.25f && s.pitchMedianHz > 0.0f)
    {
        add (pitchOn, 1.0f, "");

        if (s.keyRoot >= 0 && s.keyConfidence > 0.3f)
        {
            add (pitchKey,   (float) (s.keyRoot + 1),
                 juce::String ("Detected key: ") + keyNames[s.keyRoot]
                 + (s.keyIsMajor ? " major" : " minor")
                 + " (confidence " + juce::String (s.keyConfidence, 2) + ").");
            add (pitchScale, s.keyIsMajor ? 2.0f : 3.0f, "");
        }
        else
        {
            add (pitchKey, 0.0f, "Key unclear — correcting chromatically (nearest semitone).");
            add (pitchScale, 1.0f, "");
        }

        // Drifty singing gets a tighter retune; stable singing stays transparent.
        float speed, amount;
        if (s.pitchStabilityCents > 45.0f)      { speed = 25.0f;  amount = 100.0f; }
        else if (s.pitchStabilityCents > 25.0f) { speed = 60.0f;  amount = 90.0f; }
        else                                    { speed = 110.0f; amount = 75.0f; }

        // Genre bias: modern Pop/Rap tune tight; R&B/Ballad/Rock keep it human.
        float humanize = 25.0f;
        switch (style)
        {
            case Style::Rap:    speed = juce::jmin (speed, 20.0f); amount = 95.0f; humanize = 10.0f; break;
            case Style::Pop:    speed = juce::jmin (speed, 45.0f); amount = 90.0f; humanize = 20.0f; break;
            case Style::RnB:    speed = juce::jmax (speed, 60.0f); amount = 80.0f; humanize = 45.0f; break;
            case Style::Ballad: speed = juce::jmax (speed, 90.0f); amount = 70.0f; humanize = 60.0f; break;
            case Style::Rock:   speed = juce::jmax (speed, 120.0f); amount = 55.0f; humanize = 70.0f; break;
            case Style::Spoken: break;
        }
        add (pitchSpeed, speed,
             juce::String (styleName (style)) + " delivery, pitch drift "
             + juce::String (s.pitchStabilityCents, 0) + " cents — retune speed "
             + juce::String (speed, 0) + " ms.");
        add (pitchAmount, amount, "");
        add (pitchHumanize, humanize,
             humanize >= 40.0f ? "Preserving natural vibrato for an organic, human feel."
                               : "Tight correction with a touch of humanization.");
    }
    else
    {
        add (pitchOn, 0.0f, "Mostly unpitched delivery — pitch correction bypassed.");
    }

    // ------------------------------------------------------------------
    // 2. GATE — sit just above the noise floor, never near the voice.
    // ------------------------------------------------------------------
    const float snr = stagedRms - stagedNoise;
    if (snr < 40.0f && stagedNoise > -85.0f)
    {
        const float gateThresh = clampf (stagedNoise + 6.0f, -90.0f, stagedRms - 15.0f);
        add (gateOn, 1.0f, "Noise floor is high relative to the vocal — enabling gate.");
        add (gateThreshold, gateThresh,
             juce::String ("Noise floor ") + juce::String (stagedNoise, 1)
             + " dB; gating 6 dB above it while staying 15 dB clear of the voice.");
    }
    else
    {
        add (gateOn, 0.0f, "Recording is clean (SNR > 40 dB) — no gate needed.");
    }

    // ------------------------------------------------------------------
    // 3. EQ — surgical cleanup + tonal shaping from spectral balance.
    // ------------------------------------------------------------------
    add (eqOn, 1.0f, "");

    // HPF: more rumble → higher cutoff. Male median pitch → keep body.
    float hpfHz = 80.0f;
    if (s.subDb > -25.0f)      hpfHz = 120.0f;   // serious rumble/plosives
    else if (s.subDb > -35.0f) hpfHz = 100.0f;
    if (s.pitchMedianHz > 0.0f && s.pitchMedianHz < 130.0f)
        hpfHz = std::min (hpfHz, 75.0f);          // low voice: protect fundamentals
    add (eqHpfFreq, hpfHz,
         juce::String ("Sub band at ") + juce::String (s.subDb, 1)
         + " dB rel.; high-passing at " + juce::String ((int) hpfHz) + " Hz.");

    // Mud: 250–600 Hz buildup is the #1 home-recording issue (proximity effect).
    float mudCut = 0.0f;
    if (s.mudDb > -8.0f)       mudCut = -4.5f;
    else if (s.mudDb > -12.0f) mudCut = -2.5f;
    add (eqMudGain, mudCut,
         mudCut < 0.0f ? juce::String ("Mud band is elevated (") + juce::String (s.mudDb, 1)
                         + " dB rel.) — cutting " + juce::String (-mudCut, 1) + " dB around 350 Hz."
                       : juce::String ("Low-mids are balanced — no mud cut."));
    add (eqMudFreq, 350.0f, "");

    // Presence & air from brightness: dark vocal → lift, bright/harsh → restrain.
    float presGain = 0.0f, airGain = 0.0f;
    if (s.brightness < 0.35f)      { presGain = 3.0f; airGain = 3.5f; }
    else if (s.brightness < 0.5f)  { presGain = 2.0f; airGain = 2.0f; }
    else if (s.brightness < 0.65f) { presGain = 1.0f; airGain = 1.0f; }
    else                           { presGain = 0.0f; airGain = 0.5f; }
    // Harshness guard: strong presence band already? Don't add more.
    if (s.presenceDb > -10.0f) presGain = std::min (presGain, 0.0f);
    add (eqPresenceGain, presGain,
         juce::String ("Brightness index ") + juce::String (s.brightness, 2)
         + " — presence " + (presGain > 0 ? "+" : "") + juce::String (presGain, 1) + " dB.");
    add (eqPresenceFreq, 3800.0f, "");
    add (eqAirGain, airGain,
         airGain > 0.0f ? "Adding air shelf for sheen." : "Top end already present.");

    // Low shelf: thin vocal (low band weak) gets warmth.
    float lowShelf = 0.0f;
    if (s.lowDb < -20.0f && hpfHz <= 90.0f) lowShelf = 2.0f;
    add (eqLowShelfGain, lowShelf,
         lowShelf > 0.0f ? "Vocal is thin — adding 2 dB warmth at 180 Hz." : "");

    // ------------------------------------------------------------------
    // 3b. DYNAMIC EQ — tame resonances/harshness that only spike on peaks.
    //     Sits after the static EQ; each band is a downward (compressive) dip
    //     that stays out of the way until its frequency gets loud.
    // ------------------------------------------------------------------
    const bool boomy    = s.mudDb > -11.0f || s.lowDb > -9.0f;
    const bool nasal    = s.midDb > -7.0f;
    const bool harshDyn = s.presenceDb > -11.0f || s.brightness > 0.62f;
    if (boomy || nasal || harshDyn)
    {
        add (dyneqOn, 1.0f, "");

        const float lowFreq = s.mudDb > s.lowDb ? 320.0f : 190.0f;
        const float lowAbs  = stagedRms + juce::jmax (s.lowDb, s.mudDb);
        add (dyneqLowFreq, lowFreq, "");
        add (dyneqLowThresh, clampf (lowAbs + 6.0f, -50.0f, -6.0f),
             boomy ? juce::String ("Low-mids build up (")
                     + juce::String (juce::jmax (s.lowDb, s.mudDb), 1)
                     + " dB rel.) — dynamic band at " + juce::String ((int) lowFreq)
                     + " Hz ducks the boom on loud notes only."
                   : juce::String());

        add (dyneqMidFreq, 1000.0f, "");
        add (dyneqMidThresh, clampf (stagedRms + s.midDb + 7.0f, -50.0f, -6.0f),
             nasal ? juce::String ("Core mids are forward — a gentle dynamic dip at 1 kHz "
                                   "keeps it from honking.")
                   : juce::String());

        const float highFreq = s.brightness > 0.55f ? 3800.0f : 3200.0f;
        add (dyneqHighFreq, highFreq, "");
        add (dyneqHighThresh, clampf (stagedRms + s.presenceDb + 6.0f, -50.0f, -6.0f),
             harshDyn ? juce::String ("Presence can turn harsh — dynamic band at ")
                        + juce::String ((int) highFreq)
                        + " Hz smooths peaks without dulling the tone."
                      : juce::String());

        add (dyneqRange, (harshDyn || boomy) ? 6.0f : 3.5f,
             "Dynamic cut capped gently so the EQ stays transparent between hits.");
    }
    else
    {
        add (dyneqOn, 0.0f, "Tone is steady across the spectrum — dynamic EQ not needed.");
    }

    // ------------------------------------------------------------------
    // 4. DE-ESSER — driven by measured sibilance ratio.
    // ------------------------------------------------------------------
    if (s.sibilanceRatio > 0.15f)
    {
        add (deessOn, 1.0f, "");
        // Higher ratio → deeper threshold below the staged peak.
        const float depth = clampf (12.0f + 40.0f * s.sibilanceRatio, 12.0f, 30.0f);
        add (deessThreshold, clampf (stagedPeak - depth, -60.0f, 0.0f),
             juce::String ("Sibilance ratio ") + juce::String (s.sibilanceRatio, 2)
             + " — de-essing " + juce::String (depth, 0) + " dB below peaks.");
        // Bright voices sibilate higher.
        add (deessFreq, s.brightness > 0.55f ? 7500.0f : 6000.0f, "");
    }
    else
    {
        add (deessOn, 0.0f, "Sibilance is under control — de-esser bypassed.");
    }

    // ------------------------------------------------------------------
    // 5. COMPRESSOR — settings scale with measured dynamics (crest factor).
    //    Engineers compress dynamic performances harder, transparent ones less.
    // ------------------------------------------------------------------
    add (compOn, 1.0f, "");
    float ratio, thresholdOffset, attack, release;
    if (s.crestDb > 20.0f)        { ratio = 4.0f; thresholdOffset = 10.0f; attack = 3.0f;  release = 80.0f; }
    else if (s.crestDb > 14.0f)   { ratio = 3.0f; thresholdOffset = 8.0f;  attack = 5.0f;  release = 120.0f; }
    else                          { ratio = 2.0f; thresholdOffset = 6.0f;  attack = 8.0f;  release = 150.0f; }

    const float compThresh = clampf (stagedRms - thresholdOffset + 6.0f, -60.0f, 0.0f);
    add (compThreshold, compThresh,
         juce::String ("Crest factor ") + juce::String (s.crestDb, 1)
         + " dB — " + juce::String (ratio, 1) + ":1 catching ~6-10 dB on peaks.");
    add (compRatio, ratio, "");
    add (compAttack, attack, "Attack lets consonants through for intelligibility.");
    add (compRelease, release, "Release timed to breathe with a vocal phrase.");

    // Makeup ≈ half the expected reduction; parallel blend for dense sources.
    const float expectedGr = (s.crestDb > 14.0f ? 6.0f : 4.0f) * (1.0f - 1.0f / ratio);
    add (compMakeup, clampf (expectedGr, 0.0f, 12.0f), "");
    add (compMix, s.crestDb > 20.0f ? 85.0f : 100.0f,
         s.crestDb > 20.0f ? "Parallel blend keeps life in a very dynamic take." : "");

    // ------------------------------------------------------------------
    // 5b. MULTIBAND COMPRESSOR — glue + spectral consistency for very dynamic
    //     or spectrally uneven takes. Bypassed when the single band is enough.
    // ------------------------------------------------------------------
    const float bandHi = juce::jmax (s.lowDb, juce::jmax (s.midDb, s.presenceDb));
    const float bandLo = juce::jmin (s.lowDb, juce::jmin (s.midDb, s.presenceDb));
    const float spread = bandHi - bandLo;
    const bool veryDynamic = s.crestDb > 18.0f;
    const bool uneven      = spread > 12.0f;
    if (veryDynamic || uneven)
    {
        add (mbandOn, 1.0f, "");
        add (mbandLowXover, 250.0f, "");
        add (mbandHighXover, 3000.0f, "");
        add (mbandLowThresh,  clampf (stagedRms + s.lowDb      + 4.0f, -48.0f, -6.0f), "");
        add (mbandMidThresh,  clampf (stagedRms + s.midDb      + 4.0f, -48.0f, -6.0f), "");
        add (mbandHighThresh, clampf (stagedRms + s.presenceDb + 4.0f, -48.0f, -6.0f), "");
        add (mbandRatio, 2.0f,
             veryDynamic ? juce::String ("Crest ") + juce::String (s.crestDb, 1)
                           + " dB — 2:1 per band glues the take without squashing it."
                         : juce::String ("Spectral balance is uneven (") + juce::String (spread, 0)
                           + " dB across bands) — multiband evens it out.");
        add (mbandLowGain,  s.lowDb < -16.0f ? 1.5f : 0.0f,
             s.lowDb < -16.0f ? "Low band is light — +1.5 dB body after glue." : juce::String());
        add (mbandMidGain,  0.0f, "");
        add (mbandHighGain, s.presenceDb > -8.0f ? -1.5f : 0.0f,
             s.presenceDb > -8.0f ? "Presence band runs hot — trimming 1.5 dB post-comp." : juce::String());
    }
    else
    {
        add (mbandOn, 0.0f, "Single-band compression handles the dynamics — multiband bypassed.");
    }

    // ------------------------------------------------------------------
    // 6. SATURATION — pick an analog model from tone/dynamics, then dose it.
    //    Model indices: 0 Tube, 1 Tape, 2 Console, 3 Transformer.
    // ------------------------------------------------------------------
    add (satOn, 1.0f, "");
    int satModel; juce::String satWhy;
    if (s.brightness < 0.40f)      { satModel = 0; satWhy = "Tube — rich even-harmonic warmth for a dark/thin vocal"; }
    else if (s.brightness > 0.62f) { satModel = 2; satWhy = "Console — clean drive that won't add harshness to a bright vocal"; }
    else if (s.crestDb > 16.0f)    { satModel = 3; satWhy = "Transformer — thick, glued low-mids for a dynamic delivery"; }
    else                           { satModel = 1; satWhy = "Tape — smooth, musical saturation for a balanced tone"; }
    add (satType, (float) satModel, "Saturation model: " + satWhy + ".");

    const float satAmt = s.brightness < 0.45f ? 35.0f : 22.0f;
    add (satDrive, satAmt, "Gentle harmonic density for perceived loudness and warmth.");
    add (satTone, s.brightness > 0.60f ? 42.0f : 55.0f,
         "Tone tilt matched to the vocal so the drive doesn't tip into harshness.");
    add (satBias, 50.0f, "");   // neutral asymmetry
    add (satMix, s.brightness < 0.45f ? 35.0f : 25.0f, "");

    // ------------------------------------------------------------------
    // 7. SPACE — delay & reverb sized to delivery.
    //    Sustained/melodic (high voiced ratio, stable pitch) → lusher space.
    //    Rhythmic/spoken (low voiced ratio) → tight slap, drier verb.
    // ------------------------------------------------------------------
    const bool melodic = s.voicedRatio > 0.45f && s.pitchStabilityCents < 60.0f;
    if (melodic)
    {
        add (delayOn, 1.0f, "");
        add (delayTime, 380.0f, "Melodic delivery — 1/4-note-ish ambient delay.");
        add (delayFeedback, 30.0f, "");
        add (delayMix, 10.0f, "");
        add (verbOn, 1.0f, "");
        add (verbSize, 55.0f, "Sustained vocal supports a medium plate.");
        add (verbMix, 14.0f, "");
    }
    else
    {
        add (delayOn, 1.0f, "");
        add (delayTime, 120.0f, "Rhythmic delivery — short slap keeps energy tight.");
        add (delayFeedback, 15.0f, "");
        add (delayMix, 8.0f, "");
        add (verbOn, 1.0f, "");
        add (verbSize, 35.0f, "Small room glue; big verbs smear rhythmic vocals.");
        add (verbMix, 8.0f, "");
    }
    add (verbDamp, s.brightness > 0.55f ? 60.0f : 45.0f,
         "Damping matched to vocal brightness so the tail never hisses.");
    add (verbWidth, 100.0f, "");

    // Reverb ALGORITHM + send tone (FDN engine).
    //   Type indices: 0 Room, 1 Hall, 2 Plate, 3 Spring, 4 Cathedral, 5 Shimmer, 6 Bloom.
    int verbModel; juce::String vWhy;
    if (! melodic)                 { verbModel = 0; vWhy = "Room — tight, short ambience that won't smear a rhythmic delivery"; }
    else if (s.brightness > 0.60f) { verbModel = 2; vWhy = "Plate — bright, dense tail that flatters an airy vocal"; }
    else                           { verbModel = 1; vWhy = "Hall — lush, diffuse space for a sustained vocal"; }
    add (verbType, (float) verbModel, "Reverb type: " + vWhy + ".");
    add (verbDecay, melodic ? 55.0f : 32.0f, "Tail length matched to the delivery.");
    add (verbPredelay, melodic ? 25.0f : 10.0f, "Pre-delay keeps the vocal in front of its own tail.");
    add (verbDiffusion, 72.0f, "");
    add (verbLowCut, s.mudDb > -11.0f ? 200.0f : 140.0f,
         "High-passing the reverb send so the tail never adds mud.");
    add (verbHighCut, s.brightness > 0.55f ? 8000.0f : 10500.0f, "");
    add (verbModDepth, melodic ? 25.0f : 12.0f, "");

    // Genre space bias — a later decision on the same param overrides the
    // generic choice above, tailoring the space to the idiom.
    switch (style)
    {
        case Style::Rap:
            add (verbType, 0.0f, "Rap: a tight, dry room keeps the vocal right up front.");
            add (verbMix, 7.0f, "");  add (verbSize, 30.0f, "");
            add (delayTime, 110.0f, "Rap: short slap for rhythmic energy.");
            add (delayMix, 9.0f, "");
            break;
        case Style::Pop:
            add (verbType, 2.0f, "Pop: a bright plate for that polished, radio sheen.");
            add (verbMix, 15.0f, "");
            break;
        case Style::RnB:
            add (verbType, 1.0f, "R&B: a lush hall for a warm, spacious vocal.");
            add (verbMix, 18.0f, "");  add (verbSize, 62.0f, "");  add (verbDecay, 60.0f, "");
            break;
        case Style::Ballad:
            add (verbType, 1.0f, "Ballad: a big, long hall for emotional space.");
            add (verbMix, 22.0f, "");  add (verbSize, 72.0f, "");
            add (verbDecay, 68.0f, ""); add (verbPredelay, 30.0f, "");
            break;
        case Style::Rock:
            add (verbType, 0.0f, "Rock: a short room so the vocal stays raw and driven.");
            add (verbMix, 9.0f, "");  add (verbSize, 38.0f, "");
            break;
        case Style::Spoken:
            add (verbMix, 5.0f, "Spoken word: minimal space for maximum clarity.");
            break;
    }

    // Genre saturation bias.
    if (style == Style::Rock)        { add (satDrive, 45.0f, "Rock: heavier drive for grit and attitude."); add (satMix, 40.0f, ""); }
    else if (style == Style::Rap)    { add (satMix, 30.0f, "Rap: extra saturation for a thick, present tone."); }
    else if (style == Style::Ballad) { add (satMix, 16.0f, "Ballad: light colour to stay clean and intimate."); }

    // ------------------------------------------------------------------
    // 8. LIMITER — final safety + loudness to roughly streaming-ready level.
    // ------------------------------------------------------------------
    add (limitOn, 1.0f, "");
    add (limitCeiling, -1.0f, "-1 dBTP ceiling for codec safety (streaming standard).");
    // After staging + makeup we sit around -14..-12; push a touch of gain.
    add (limitGain, 3.0f, "Light limiting for consistent presence in the mix.");
    add (outputGain, 0.0f, "");

    r.presetName = generatePresetName (s);
    r.summary    = buildSummary (s, r.decisions);
    return r;
}

// ============================================================================
juce::String AutoMixBrain::detectedStyle (const AnalysisSnapshot& s)
{
    return styleName (inferStyle (s));
}

// ============================================================================
juce::String AutoMixBrain::generatePresetName (const AnalysisSnapshot& s)
{
    juce::String tone = s.brightness < 0.35f ? "Dark"
                      : s.brightness < 0.50f ? "Warm"
                      : s.brightness < 0.65f ? "Smooth" : "Bright";

    juce::String character = s.crestDb > 20.0f ? "Dynamic"
                           : s.crestDb > 14.0f ? "Expressive" : "Dense";

    return tone + " " + character + " " + styleName (inferStyle (s)) + " Vocal";
}

// ============================================================================
juce::String AutoMixBrain::buildSummary (const AnalysisSnapshot& s,
                                         const std::vector<Decision>& d)
{
    juce::String out;
    out << "== VoxBrain Analysis ==\n";
    out << "Detected style: " << styleName (inferStyle (s)) << " — chain tuned to match.\n";
    out << "Peak " << juce::String (s.peakDb, 1) << " dB | RMS " << juce::String (s.rmsDb, 1)
        << " dB | Crest " << juce::String (s.crestDb, 1) << " dB\n";
    out << "Integrated " << juce::String (s.integratedLufs, 1) << " LUFS | Noise floor "
        << juce::String (s.noiseFloorDb, 1) << " dB\n";
    if (s.pitchMedianHz > 0.0f)
        out << "Median pitch " << juce::String (s.pitchMedianHz, 1) << " Hz | Stability "
            << juce::String (s.pitchStabilityCents, 0) << " cents | Voiced "
            << juce::String (s.voicedRatio * 100.0f, 0) << "%\n";
    if (s.keyRoot >= 0)
    {
        static const char* kn[] = { "C", "C#", "D", "D#", "E", "F",
                                    "F#", "G", "G#", "A", "A#", "B" };
        out << "Detected key: " << kn[s.keyRoot] << (s.keyIsMajor ? " major" : " minor")
            << " (confidence " << juce::String (s.keyConfidence, 2) << ")\n";
    }
    out << "Brightness " << juce::String (s.brightness, 2) << " | Sibilance "
        << juce::String (s.sibilanceRatio, 2) << "\n\n";
    out << "== Decisions ==\n";
    for (const auto& dec : d)
        if (dec.rationale.isNotEmpty())
            out << "- " << dec.rationale << "\n";
    return out;
}
} // namespace vf
