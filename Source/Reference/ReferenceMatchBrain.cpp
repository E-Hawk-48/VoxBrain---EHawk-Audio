#include "ReferenceMatchBrain.h"
#include "../ParameterIDs.h"   // stable param IDs only — no juce_audio_processors/GUI dependency
#include <cmath>

namespace vf
{
using namespace vf::param;

namespace
{
    inline float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
    juce::String dbs  (float v) { return (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String hzs  (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                                      : juce::String ((int) std::round (v)) + " Hz"; }
    juce::String pctI (float v) { return juce::String ((int) std::round (v)) + "%"; }
    juce::String msI  (float v) { return juce::String ((int) std::round (v)) + " ms"; }
}

ReferenceMatchBrain::Result ReferenceMatchBrain::match (const ReferenceProfile& ref)
{
    Result r;
    if (! ref.valid)
    {
        r.summary = "The reference could not be analysed (file too short or unreadable). "
                    "Try a longer vocal section.";
        return r;
    }

    auto add = [&r] (Decision d) { r.decisions.push_back (std::move (d)); };

    // ======================================================================
    //  Pitch correction behaviour (never the notes themselves)
    // ======================================================================
    {
        Decision d; d.area = "Pitch Correction";
        const bool  tuned  = ref.pitchCorrectionStrength > 0.45f;
        const float amount = tuned ? juce::jlimit (60.0f, 100.0f, 55.0f + ref.pitchCorrectionStrength * 55.0f) : 35.0f;
        const float speed  = tuned ? juce::jlimit (8.0f, 60.0f, 60.0f - ref.pitchCorrectionStrength * 50.0f) : 90.0f;
        d.targets = { { pitchOn, 1.0f }, { pitchAmount, amount }, { pitchSpeed, speed } };
        d.rationale = tuned
            ? "The reference vocal reads as tightly tuned (median pitch deviation ~"
              + juce::String ((int) ref.pitchStabilityCents) + " cents across "
              + pctI (ref.voicedRatio * 100.0f) + " voiced frames). Firmer correction ("
              + pctI (amount) + ") with a fast " + msI (speed)
              + " retune reproduces that polished feel — applied to YOUR notes, not the reference's."
            : "The reference keeps natural pitch movement (~"
              + juce::String ((int) ref.pitchStabilityCents) + " cents). Gentle correction ("
              + pctI (amount) + ") preserves an organic delivery.";
        d.confidence = clamp01 (0.35f + 0.45f * ref.conf.pitch + 0.2f * ref.pitchCorrectionStrength);
        add (d);
    }

    // ======================================================================
    //  EQ contour / tonal balance
    // ======================================================================
    {
        Decision d; d.area = "EQ / Tonal Balance";
        const float hpf      = juce::jlimit (20.0f, 180.0f, 120.0f - ref.lowEndWeight * 90.0f);
        const float lowShelf = juce::jlimit (-4.0f, 5.0f, (ref.warmth - 0.5f) * 8.0f);
        const float mud      = juce::jlimit (-6.0f, 2.0f, (ref.warmth - 0.55f) * 10.0f);
        const float presence = juce::jlimit (-3.0f, 9.0f, (ref.presence - 0.4f) * 14.0f);
        const float air      = juce::jlimit (-3.0f, 10.0f, (ref.air - 0.4f) * 16.0f);
        d.targets = { { eqOn, 1.0f }, { eqHpfFreq, hpf }, { eqLowShelfGain, lowShelf },
                      { eqMudGain, mud }, { eqPresenceGain, presence }, { eqAirGain, air } };
        d.rationale = "Spectral balance: tilt " + dbs (ref.spectralTiltDb)
            + ", presence band at " + dbs (ref.presenceDb) + ", air at " + dbs (ref.airDb)
            + ". A comparable curve: high-pass ~" + hzs (hpf) + ", "
            + (mud < -0.2f ? "trim low-mid mud " + dbs (mud) : "low-mids left full")
            + ", presence " + dbs (presence) + " near 4.5 kHz, and an air shelf " + dbs (air) + " from ~10 kHz.";
        d.confidence = clamp01 (0.9f * ref.conf.spectral);
        add (d);
    }

    // ======================================================================
    //  Dynamic EQ — tame the specific resonances that were detected
    // ======================================================================
    if (ref.resonanceCount > 0)
    {
        Decision d; d.area = "Dynamic EQ / Resonance Control";
        // Defaults (unchanged bands sit at their thresholds so they stay idle).
        float lowF = 220.0f, midF = 1000.0f, highF = 3500.0f;
        float lowT = 0.0f, midT = 0.0f, highT = 0.0f;   // 0 dB = effectively idle
        juce::StringArray named;
        for (int i = 0; i < ref.resonanceCount; ++i)
        {
            const float f = ref.resonanceHz[(size_t) i];
            const float duck = juce::jlimit (-30.0f, -6.0f, -12.0f - ref.resonanceStrength[(size_t) i]);
            if      (f < 500.0f)  { lowF  = juce::jlimit (100.0f, 500.0f, f);  lowT  = duck; }
            else if (f < 2500.0f) { midF  = juce::jlimit (500.0f, 3000.0f, f); midT  = duck; }
            else                  { highF = juce::jlimit (2000.0f, 9000.0f, f); highT = duck; }
            named.add (hzs (f) + " (" + dbs (ref.resonanceStrength[(size_t) i]) + ")");
        }
        d.targets = { { dyneqOn, 1.0f }, { dyneqLowFreq, lowF }, { dyneqLowThresh, lowT },
                      { dyneqMidFreq, midF }, { dyneqMidThresh, midT },
                      { dyneqHighFreq, highF }, { dyneqHighThresh, highT }, { dyneqRange, 5.0f } };
        d.rationale = "Resonant peaks stand proud of the spectrum at " + named.joinIntoString (", ")
            + ". Dynamic-EQ bands parked on those frequencies duck them only when they flare, "
              "so the tone stays natural.";
        d.confidence = clamp01 (0.75f * ref.conf.spectral);
        add (d);
    }

    // ======================================================================
    //  Compression / dynamics
    // ======================================================================
    {
        Decision d; d.area = "Compression / Dynamics";
        const float grEst = juce::jlimit (1.0f, 9.0f, ref.compressionAmount * 9.0f);
        const float ratio = juce::jlimit (1.5f, 6.0f, 1.5f + ref.compressionAmount * 4.0f);
        const float thr   = juce::jlimit (-40.0f, -6.0f, ref.rmsDb - 2.0f - (1.0f - ref.compressionAmount) * 6.0f);
        const float atk   = ref.compressionAmount > 0.6f ? 3.0f : (ref.transientDensity > 4.0f ? 18.0f : 8.0f);
        const float rel   = juce::jlimit (60.0f, 260.0f, 90.0f + ref.transientDensity * 12.0f);
        d.targets = { { compOn, 1.0f }, { compThreshold, thr }, { compRatio, ratio },
                      { compAttack, atk }, { compRelease, rel }, { compMix, 100.0f } };
        d.rationale = "Crest factor " + dbs (ref.crestDb) + " and dynamic range ~"
            + juce::String (ref.dynamicRangeDb, 1) + " LU indicate roughly "
            + juce::String (grEst, 1) + " dB of gain reduction. A " + juce::String (ratio, 1)
            + ":1 ratio with a " + msI (atk) + " attack and " + msI (rel)
            + " release reproduces a similar dynamic profile.";
        d.confidence = clamp01 (0.85f * ref.conf.dynamics);
        add (d);
    }

    // ======================================================================
    //  De-essing
    // ======================================================================
    {
        Decision d; d.area = "De-Essing";
        const bool  sib = ref.sibilanceRatio > 0.35f;
        const float thr = juce::jlimit (-45.0f, -12.0f, -18.0f - ref.sibilanceRatio * 18.0f);
        const float frq = juce::jlimit (3000.0f, 12000.0f, ref.sibilanceCenterHz > 0.0f ? ref.sibilanceCenterHz : 6500.0f);
        d.targets = { { deessOn, 1.0f }, { deessThreshold, thr }, { deessFreq, frq } };
        d.rationale = sib
            ? "Sibilance energy is elevated (ratio " + juce::String (ref.sibilanceRatio, 2)
              + ", centred near " + hzs (frq) + "). A de-esser at " + hzs (frq)
              + " with threshold " + dbs (thr) + " keeps esses controlled like the reference."
            : "Sibilance is already restrained (ratio " + juce::String (ref.sibilanceRatio, 2)
              + "). A gentle de-esser at " + hzs (frq) + " is enough to match it.";
        d.confidence = clamp01 (0.85f * ref.conf.sibilance);
        add (d);
    }

    // ======================================================================
    //  Saturation / harmonic character
    // ======================================================================
    {
        Decision d; d.area = "Saturation / Harmonics";
        int model; juce::String mname;
        if      (ref.brightness > 0.62f)                      { model = 6; mname = "Exciter"; }
        else if (ref.warmth > 0.55f || ref.spectralTiltDb < -2.0f) { model = 1; mname = "Tape"; }
        else                                                  { model = 2; mname = "Console"; }
        const float drive = juce::jlimit (8.0f, 42.0f, 12.0f + ref.compressionAmount * 24.0f);
        d.targets = { { satOn, 1.0f }, { satType, (float) model }, { satDrive, drive }, { satMix, 30.0f } };
        d.rationale = "Harmonic character reads " + juce::String (ref.brightness > 0.62f ? "bright and open"
                       : (ref.warmth > 0.55f ? "warm/analog" : "neutral"))
            + " → a " + mname + "-style saturation at " + pctI (drive)
            + " drive adds comparable colour (closest VoxBrain model, not an emulation of a specific unit).";
        d.confidence = clamp01 (0.6f * ref.conf.saturation + 0.15f);
        add (d);
    }

    // ======================================================================
    //  Reverb / space
    // ======================================================================
    {
        Decision d; d.area = "Reverb / Space";
        const int   vtype = ref.reverbDecaySec > 1.2f ? 4 /*Cathedral*/
                          : ref.reverbDecaySec > 0.6f ? 1 /*Hall*/
                          : ref.reverbDecaySec > 0.3f ? 2 /*Plate*/ : 0 /*Room*/;
        const char* vname = ref.reverbDecaySec > 1.2f ? "Cathedral"
                          : ref.reverbDecaySec > 0.6f ? "Hall"
                          : ref.reverbDecaySec > 0.3f ? "Plate" : "Room";
        const float mix  = juce::jlimit (0.0f, 45.0f, ref.reverbAmount * 40.0f);
        const float size = juce::jlimit (20.0f, 90.0f, 30.0f + ref.reverbDecaySec * 40.0f);
        const float dec  = juce::jlimit (20.0f, 90.0f, 25.0f + ref.reverbDecaySec * 45.0f);
        const float pre  = juce::jlimit (0.0f, 120.0f, ref.preDelayMs > 0.0f ? ref.preDelayMs : 18.0f);
        d.targets = { { verbOn, 1.0f }, { verbType, (float) vtype }, { verbMix, mix },
                      { verbSize, size }, { verbDecay, dec }, { verbPredelay, pre } };
        d.rationale = juce::String ("Ambience estimate suggests a ") + vname + "-type space (decay ~"
            + juce::String (ref.reverbDecaySec, 2) + " s, wetness " + pctI (ref.reverbAmount * 100.0f)
            + "). Reverb " + vname + " at " + pctI (mix) + " mix approximates it. (Reverb is hard to isolate "
              "from a mix — treat as a starting point.)";
        d.confidence = clamp01 (0.45f * ref.conf.reverb + 0.2f);
        add (d);
    }

    // ======================================================================
    //  Delay (only when a rhythmic echo is actually detected)
    // ======================================================================
    if (ref.delayAmount > 0.3f)
    {
        Decision d; d.area = "Delay";
        const float t   = juce::jlimit (20.0f, 1200.0f, ref.delayTimeMs);
        const float mix = juce::jlimit (0.0f, 40.0f, ref.delayAmount * 28.0f);
        d.targets = { { delayOn, 1.0f }, { delayTime, t }, { delayMix, mix }, { delayFeedback, 20.0f } };
        d.rationale = "Envelope autocorrelation hints at a rhythmic echo near " + msI (t)
            + ". A delay at that time with " + pctI (mix) + " mix may match it. (Speculative — verify by ear.)";
        d.confidence = clamp01 (0.35f * ref.conf.delay + 0.1f);
        add (d);
    }

    // ======================================================================
    //  Loudness / limiter
    // ======================================================================
    {
        Decision d; d.area = "Loudness";
        const float gain = ref.integratedLufs > -40.0f ? juce::jlimit (0.0f, 12.0f, ref.integratedLufs + 14.0f) : 0.0f;
        d.targets = { { limitOn, 1.0f }, { limitCeiling, -1.0f }, { limitGain, gain } };
        d.rationale = "Reference integrated loudness ≈ " + juce::String (ref.integratedLufs, 1)
            + " LUFS. A -1 dB ceiling with " + dbs (gain)
            + " into the limiter moves toward that level — trim to suit your track and platform.";
        d.confidence = 0.6f;
        add (d);
    }

    // ======================================================================
    //  Complementary rack modules (characters the fixed chain lacks)
    // ======================================================================
    if (! ref.isMono && ref.stereoWidth > 0.25f)
        r.rackInserts.push_back ({ "stereo_width", "Stereo Width",
            "Reference shows notable stereo width (side energy, correlation "
            + juce::String (ref.stereoCorrelation, 2) + "). A Stereo Width module spreads YOUR vocal similarly.",
            clamp01 (ref.conf.stereo) });

    if (ref.air > 0.6f || ref.brightness > 0.66f)
        r.rackInserts.push_back ({ "exciter", "Exciter",
            "Pronounced top-end sheen (air " + pctI (ref.air * 100.0f)
            + ") — an Exciter generates high harmonics beyond what a shelf can, matching that openness.",
            0.5f });

    // ======================================================================
    //  Overall confidence, preset name, coach summary
    // ======================================================================
    {
        float sum = 0.0f; int n = 0;
        for (auto& d : r.decisions) { sum += d.confidence; ++n; }
        r.overallConfidence = n > 0 ? clamp01 (sum / (float) n) : 0.0f;
    }

    {
        const juce::String tone  = ref.brightness > 0.6f ? "Bright" : (ref.warmth > 0.55f ? "Warm" : "Balanced");
        const juce::String space = ref.reverbAmount > 0.45f ? "Spacious"
                                 : (ref.reverbAmount > 0.2f ? "Roomy" : "Dry");
        r.presetName = tone + " " + space + " Reference Match";
    }

    {
        juce::String s;
        s << "AI Reference Match — engineering analysis (your voice, performance and pitch are untouched).\n\n";
        s << "Tonal balance: tilt " << dbs (ref.spectralTiltDb) << " ("
          << (ref.brightness > 0.6f ? "bright" : ref.brightness < 0.4f ? "dark" : "even")
          << "), presence " << dbs (ref.presenceDb) << ", air " << dbs (ref.airDb) << ".\n";
        s << "Dynamics: crest " << dbs (ref.crestDb) << ", range ~" << juce::String (ref.dynamicRangeDb, 1)
          << " LU, ~" << pctI (ref.compressionAmount * 100.0f) << " levelling, "
          << juce::String (ref.integratedLufs, 1) << " LUFS.\n";
        s << "Sibilance ratio " << juce::String (ref.sibilanceRatio, 2)
          << ", stereo width " << pctI (ref.stereoWidth * 100.0f)
          << ", ambience " << pctI (ref.reverbAmount * 100.0f) << ".\n\n";
        s << "Overall match confidence: " << juce::String ((int) std::round (r.overallConfidence * 100.0f))
          << "%. Reverb, delay and pitch-behaviour estimates are the most speculative — "
             "preview each suggestion and accept only what serves your track.";
        r.summary = s;
    }

    return r;
}
} // namespace vf
