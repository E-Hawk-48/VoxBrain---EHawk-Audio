#include "ModuleIntelligence.h"
#include <cmath>
#include <algorithm>

namespace vf
{
namespace
{
    // ------------------------------------------------------------------
    //  Small helpers. Every dialled value is clamped to the module's real
    //  declared range (the offline test cross-checks these against the live
    //  registry, so a range change in a module can never silently drift).
    // ------------------------------------------------------------------
    void set (ModuleRecommendation& r, const char* id, float v, float lo, float hi)
    {
        r.settings.push_back ({ id, juce::jlimit (lo, hi, v) });
    }

    /** Linear ramp of `x` from a..b onto 0..1 (clamped). */
    float ramp (float x, float a, float b)
    {
        if (b <= a) return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
    }

    juce::String dbStr (float v)  { return juce::String (v, 1) + " dB"; }
    juce::String hzStr (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz"
                             : juce::String ((int) std::round (hz)) + " Hz";
    }
    juce::String pctStr (float v) { return juce::String ((int) std::round (v)) + "%"; }

    /** Signal-to-noise of the take, in dB (how far the voice sits above the floor). */
    float snrOf (const AnalysisSnapshot& s)
    {
        if (s.noiseFloorDb <= -100.0f || s.rmsDb <= -100.0f) return 60.0f;
        return s.rmsDb - s.noiseFloorDb;
    }

    /** Estimated level of the sibilant band, used to place de-ess thresholds. */
    float sibLevelDb (const AnalysisSnapshot& s) { return s.rmsDb + s.sibDb; }

    // ==================================================================
    //  Per-module expert rules.
    //  Each fills: need (0..1), rationale (with the measured evidence that
    //  justified it) and the dialled settings. Values are grounded in the
    //  analysis, never constants, so two different singers get two different
    //  chains.
    // ==================================================================

    void ruleCompressor (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        // Wants compression when the take is dynamic (high crest / wide range).
        const float byCrest = ramp (s.crestDb, 10.0f, 22.0f);
        const float byRange = ramp (s.dynamicRangeDb, 6.0f, 20.0f);
        r.need = juce::jmax (byCrest, byRange) * 0.9f;

        // Threshold a few dB under the average level so it works on the loud half.
        const float thresh = juce::jlimit (-40.0f, -4.0f, s.rmsDb - 2.0f);
        const float ratio  = 1.8f + 2.6f * juce::jmax (byCrest, byRange);   // 1.8..4.4:1
        // Fast, syllabic delivery wants a quicker grab; sustained singing a slower one.
        const float attack  = juce::jlimit (3.0f, 30.0f, 26.0f - 3.5f * s.transientDensity);
        const float release = juce::jlimit (60.0f, 320.0f, 90.0f + 12.0f * s.crestDb);
        const float makeup  = juce::jlimit (0.0f, 12.0f, (ratio - 1.0f) * 1.6f);

        set (r, "threshold", thresh,  -60.0f, 0.0f);
        set (r, "ratio",     ratio,     1.0f, 20.0f);
        set (r, "attack",    attack,    0.1f, 100.0f);
        set (r, "release",   release,  10.0f, 500.0f);
        set (r, "makeup",    makeup,    0.0f, 24.0f);
        set (r, "mix",       100.0f,    0.0f, 100.0f);

        r.rationale = "Crest is " + dbStr (s.crestDb) + " with a "
                    + dbStr (s.dynamicRangeDb) + " loud-to-quiet spread, so the level needs "
                    "evening out. Threshold sits just under the average level ("
                    + dbStr (thresh) + ") at " + juce::String (ratio, 1)
                    + ":1 — enough control to sit in a mix without squashing the performance.";
    }

    void ruleOpticalComp (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        // Smooth, invisible levelling for sustained/melodic takes.
        const float sustained = s.voicedRatio;
        r.need = ramp (s.crestDb, 13.0f, 22.0f) * juce::jlimit (0.0f, 1.0f, sustained * 1.2f) * 0.8f;

        const float thresh = juce::jlimit (-40.0f, -6.0f, s.rmsDb - 4.0f);
        const float ratio  = 2.0f + 1.4f * ramp (s.crestDb, 12.0f, 22.0f);   // 2..3.4:1

        set (r, "threshold", thresh, -50.0f, 0.0f);
        set (r, "ratio",     ratio,    1.0f, 8.0f);
        set (r, "makeup",    juce::jlimit (0.0f, 8.0f, (ratio - 1.0f) * 1.8f), 0.0f, 18.0f);
        set (r, "mix",       100.0f,   0.0f, 100.0f);

        r.rationale = "A sustained, melodic take (" + pctStr (s.voicedRatio * 100.0f)
                    + " voiced) responds beautifully to slow optical levelling — it smooths the "
                      "line without the pumping a fast compressor would add.";
    }

    void ruleParallelComp (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        // Already-dense takes gain energy from parallel rather than more downward comp.
        r.need = (s.crestDb > 0.0f ? (1.0f - ramp (s.crestDb, 8.0f, 16.0f)) : 0.0f) * 0.6f;

        const float thresh = juce::jlimit (-45.0f, -10.0f, s.rmsDb - 8.0f);
        set (r, "threshold", thresh, -60.0f, 0.0f);
        set (r, "ratio",     8.0f,     1.0f, 20.0f);
        set (r, "attack",    15.0f,    0.1f, 100.0f);
        set (r, "release",   180.0f,  10.0f, 500.0f);
        set (r, "blend",     juce::jlimit (20.0f, 45.0f, 45.0f - 1.5f * s.crestDb), 0.0f, 100.0f);

        r.rationale = "The performance is already fairly even (crest " + dbStr (s.crestDb)
                    + "), so density is better added underneath in parallel than by "
                      "compressing harder on top.";
    }

    void ruleGate (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        const float snr = snrOf (s);
        r.need = (1.0f - ramp (snr, 30.0f, 50.0f)) * 0.85f;

        // Sit the gate safely above the measured floor but well under the voice.
        const float thresh = juce::jlimit (-70.0f, -20.0f, s.noiseFloorDb + 6.0f);
        set (r, "threshold", thresh, -80.0f, 0.0f);
        set (r, "ratio",     3.0f,     1.0f, 20.0f);
        set (r, "attack",    1.5f,     0.1f, 50.0f);
        set (r, "release",   180.0f,  10.0f, 800.0f);
        // Gentle range: fully closing a vocal gate sounds unnatural between words.
        set (r, "range",     juce::jlimit (12.0f, 30.0f, 30.0f - snr * 0.3f), 0.0f, 80.0f);

        r.rationale = "The voice sits " + dbStr (snr) + " above the noise floor ("
                    + dbStr (s.noiseFloorDb) + "), so the gaps between phrases are audible. "
                      "Threshold is placed 6 dB above the measured floor with a partial range "
                      "so breaths stay natural instead of chopping.";
    }

    void ruleNoiseReduction (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        const float snr = snrOf (s);
        r.need = (1.0f - ramp (snr, 26.0f, 44.0f)) * 0.8f;

        set (r, "threshold", juce::jlimit (-78.0f, -24.0f, s.noiseFloorDb + 4.0f), -80.0f, -20.0f);
        set (r, "reduction", juce::jlimit (6.0f, 20.0f, 24.0f - snr * 0.35f),        0.0f, 40.0f);
        set (r, "attack",    5.0f,   0.5f, 50.0f);
        set (r, "release",   250.0f, 20.0f, 800.0f);

        r.rationale = "Broadband noise is measurable (floor " + dbStr (s.noiseFloorDb)
                    + ", SNR " + dbStr (snr) + "). A moderate reduction cleans the track; going "
                      "deeper than this starts to sound watery on vocals.";
    }

    void ruleDeEsser (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        const float byRatio = ramp (s.sibilanceRatio, 0.10f, 0.40f);
        const float byDna   = s.dna.valid ? s.dna.sibilance : 0.0f;
        r.need = juce::jmax (byRatio, byDna) * 0.95f;

        // Brighter voices sibilate higher up; place the band where the energy is.
        const float freq   = juce::jlimit (5000.0f, 9000.0f, 5800.0f + 2600.0f * s.brightness);
        const float thresh = juce::jlimit (-40.0f, -12.0f, sibLevelDb (s) + 4.0f);
        const float amount = juce::jlimit (3.0f, 12.0f, 3.0f + 16.0f * juce::jmax (byRatio, byDna));

        set (r, "freq",      freq,   2000.0f, 12000.0f);
        set (r, "threshold", thresh,  -60.0f, 0.0f);
        set (r, "range",     amount,    0.0f, 24.0f);

        r.rationale = "Sibilance measures " + juce::String (s.sibilanceRatio, 2)
                    + " against the voice. The band is centred at " + hzStr (freq)
                    + " (where this voice's esses actually sit) and ducks up to "
                    + dbStr (amount) + " — only on the esses, so the tone stays open.";
    }

    void ruleDePlosive (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        const float bySub = ramp (s.subDb, -30.0f, -16.0f);
        const float byDna = s.dna.valid ? s.dna.plosives : 0.0f;
        r.need = juce::jmax (bySub, byDna) * 0.75f;

        set (r, "freq",      juce::jlimit (70.0f, 120.0f, 110.0f - 40.0f * bySub), 40.0f, 200.0f);
        set (r, "threshold", juce::jlimit (-40.0f, -14.0f, s.rmsDb - 6.0f),        -50.0f, 0.0f);
        set (r, "range",     juce::jlimit (6.0f, 16.0f, 6.0f + 12.0f * bySub),       0.0f, 24.0f);

        r.rationale = "Sub-bass energy is high (" + dbStr (s.subDb)
                    + " relative), which on a vocal means plosive pops rather than musical "
                      "content. This ducks the low end only when a pop hits, so body is kept.";
    }

    void ruleDynamicEq (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        // Target whichever resonance problem is worst: mud, boxiness, nasal or harsh.
        struct Cand { float amt; float freq; float q; const char* what; };
        const auto& d = s.dna;
        std::vector<Cand> c;
        if (d.valid)
        {
            c.push_back ({ d.muddiness, 260.0f, 1.8f, "low-mid mud" });
            c.push_back ({ d.boxiness,  450.0f, 2.2f, "boxiness"    });
            c.push_back ({ d.nasal,    1050.0f, 3.0f, "nasal honk"  });
            c.push_back ({ d.harshness, 3200.0f, 2.6f, "harshness"  });
        }
        else
        {
            c.push_back ({ ramp (s.mudDb, -14.0f, -6.0f), 300.0f, 2.0f, "low-mid buildup" });
            c.push_back ({ ramp (s.harshnessDb, 2.0f, 10.0f), 3200.0f, 2.6f, "harshness" });
        }
        auto best = *std::max_element (c.begin(), c.end(),
                                       [] (const Cand& a, const Cand& b) { return a.amt < b.amt; });

        r.need = juce::jlimit (0.0f, 1.0f, best.amt) * 0.9f;

        set (r, "freq",      best.freq,   80.0f, 12000.0f);
        set (r, "q",         best.q,       0.3f, 8.0f);
        set (r, "threshold", juce::jlimit (-40.0f, -10.0f, s.rmsDb - 4.0f), -60.0f, 0.0f);
        set (r, "range",     juce::jlimit (3.0f, 10.0f, 2.0f + 10.0f * best.amt), 0.0f, 18.0f);

        r.rationale = juce::String ("The strongest tonal problem here is ") + best.what
                    + ", so a dynamic band at " + hzStr (best.freq)
                    + " only pulls it down when it actually spikes — the tone stays full the "
                      "rest of the time, which a static cut can't do.";
    }

    void ruleParametricEq (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        // Broad tonal shaping — nearly always some use, stronger when unbalanced.
        const auto& d = s.dna;
        const float mudAmt  = d.valid ? d.muddiness : ramp (s.mudDb, -14.0f, -6.0f);
        const float darkAmt = 1.0f - s.brightness;
        r.need = juce::jlimit (0.0f, 1.0f, 0.35f + 0.5f * juce::jmax (mudAmt, std::abs (darkAmt - 0.5f) * 2.0f)) * 0.7f;

        const float low  = juce::jlimit (-4.0f, 3.0f, (d.valid ? (0.5f - d.warmth) * -6.0f : 0.0f));
        const float mud  = juce::jlimit (-6.0f, 0.0f, -6.0f * mudAmt);
        const float pres = juce::jlimit (-3.0f, 4.0f, (0.5f - (d.valid ? d.presence : 0.5f)) * 6.0f);
        const float air  = juce::jlimit (-2.0f, 4.0f, (0.55f - s.brightness) * 7.0f);

        set (r, "low",  low,  -12.0f, 12.0f);
        set (r, "mud",  mud,  -12.0f, 12.0f);
        set (r, "pres", pres, -12.0f, 12.0f);
        set (r, "air",  air,  -12.0f, 12.0f);

        r.rationale = "Static tone shaping from the measured balance: "
                    + juce::String (mud < -0.5f ? "cutting the mud region, " : "")
                    + juce::String (pres >  0.5f ? "lifting presence for intelligibility, " : "")
                    + juce::String (air  >  0.5f ? "adding air on a dark-tilted tone, " : "")
                    + "with everything else left flat so the voice keeps its character.";
    }

    void ruleVintageEq (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        // Broad, musical colour — favoured when the voice is dull rather than problematic.
        const float dark = juce::jlimit (0.0f, 1.0f, (0.55f - s.brightness) * 2.5f);
        r.need = dark * 0.55f;

        set (r, "low",  juce::jlimit (-2.0f, 3.0f, (s.dna.valid ? (0.5f - s.dna.warmth) * 5.0f : 1.0f)), -10.0f, 10.0f);
        set (r, "pres", juce::jlimit (0.0f, 4.0f, 4.0f * dark), -10.0f, 10.0f);
        set (r, "high", juce::jlimit (0.0f, 4.0f, 3.5f * dark), -10.0f, 10.0f);

        r.rationale = "Broad, gentle analogue-style curves flatter a darker tone (brightness "
                    + juce::String (s.brightness, 2) + ") without the surgical feel of a "
                      "parametric — good for character rather than problem-solving.";
    }

    void ruleExciter (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        const float dark = juce::jlimit (0.0f, 1.0f, (0.50f - s.brightness) * 3.0f);
        const float airLack = s.dna.valid ? (1.0f - s.dna.air) : dark;
        r.need = juce::jmax (dark, airLack * 0.8f) * 0.75f;

        // Excite ABOVE the sibilant region when the singer is already essy, so it
        // adds sheen instead of amplifying the esses.
        const float essy = juce::jlimit (0.0f, 1.0f, s.sibilanceRatio * 3.0f);
        const float freq = juce::jlimit (3000.0f, 8000.0f, 3600.0f + 3600.0f * essy);

        set (r, "freq",   freq,  1500.0f, 9000.0f);
        set (r, "amount", juce::jlimit (15.0f, 55.0f, 20.0f + 40.0f * dark), 0.0f, 100.0f);
        set (r, "mix",    juce::jlimit (12.0f, 40.0f, 15.0f + 30.0f * dark * (1.0f - essy)), 0.0f, 100.0f);

        r.rationale = "The tone reads dark (brightness " + juce::String (s.brightness, 2)
                    + "), so generated harmonics add sheen that an EQ boost alone would make "
                      "harsh. Placed at " + hzStr (freq)
                    + juce::String (essy > 0.4f ? " — above the sibilant band, because this voice is already essy."
                                                : " for openness.");
    }

    void ruleTapeSat (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        const float wantsWarmth = juce::jlimit (0.0f, 1.0f, (0.58f - s.brightness) * 2.2f);
        r.need = wantsWarmth * 0.65f;

        set (r, "drive", juce::jlimit (12.0f, 40.0f, 14.0f + 30.0f * wantsWarmth), 0.0f, 100.0f);
        set (r, "tone",  juce::jlimit (45.0f, 70.0f, 70.0f - 25.0f * wantsWarmth), 0.0f, 100.0f);
        set (r, "mix",   juce::jlimit (20.0f, 45.0f, 22.0f + 25.0f * wantsWarmth), 0.0f, 100.0f);

        r.rationale = "Tape-style saturation adds body and gentle glue. Kept as a partial blend "
                      "so it thickens the voice without dulling articulation.";
    }

    void ruleConsoleSat (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        r.need = juce::jlimit (0.0f, 1.0f, 0.35f + 0.3f * ramp (s.crestDb, 10.0f, 20.0f)) * 0.5f;
        set (r, "drive", juce::jlimit (12.0f, 35.0f, 18.0f + 12.0f * ramp (s.crestDb, 10.0f, 20.0f)), 0.0f, 100.0f);
        set (r, "mix",   100.0f, 0.0f, 100.0f);
        r.rationale = "A light console-desk colour glues the take together and softens peak "
                      "edges — subtle by design.";
    }

    void ruleTubeSat (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        const float wantsWarmth = juce::jlimit (0.0f, 1.0f, (0.55f - s.brightness) * 2.0f);
        r.need = wantsWarmth * 0.55f;
        set (r, "drive", juce::jlimit (12.0f, 40.0f, 15.0f + 28.0f * wantsWarmth), 0.0f, 100.0f);
        set (r, "mix",   juce::jlimit (25.0f, 60.0f, 30.0f + 30.0f * wantsWarmth), 0.0f, 100.0f);
        r.rationale = "Valve-style even harmonics add warmth and perceived loudness on a "
                      "darker / thinner tone, blended in parallel to protect clarity.";
    }

    void ruleTransient (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        // Punch helps mid-crest takes; very dynamic ones are already peaky.
        const float mid = 1.0f - std::abs (ramp (s.crestDb, 6.0f, 20.0f) - 0.5f) * 2.0f;
        r.need = juce::jlimit (0.0f, 1.0f, mid) * 0.6f;

        set (r, "attack",  juce::jlimit (10.0f, 45.0f, 12.0f + 4.0f * s.transientDensity), -100.0f, 100.0f);
        // Tighten the tail on roomy/reverberant takes, leave it alone otherwise.
        set (r, "sustain", juce::jlimit (-30.0f, 5.0f, s.dna.valid ? -30.0f * s.dna.breathiness : 0.0f), -100.0f, 100.0f);

        r.rationale = "Sharpening the attack adds consonant snap and articulation without "
                      "raising the overall level — it makes the vocal read clearly in a busy mix.";
    }

    void ruleSoftClipper (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        r.need = ramp (s.crestDb, 16.0f, 24.0f) * 0.6f;
        set (r, "drive",   juce::jlimit (1.0f, 6.0f, ramp (s.crestDb, 16.0f, 24.0f) * 6.0f), 0.0f, 24.0f);
        set (r, "ceiling", -0.8f, -12.0f, 0.0f);
        set (r, "mix",     100.0f, 0.0f, 100.0f);
        r.rationale = "Occasional sharp peaks (crest " + dbStr (s.crestDb)
                    + ") are rounded off before the limiter, so the limiter isn't forced to "
                      "clamp hard and dull the whole line.";
    }

    void ruleLimiter (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        r.need = 0.35f + 0.25f * ramp (s.peakDb, -6.0f, 0.0f);
        set (r, "gain",    0.0f,   0.0f, 24.0f);
        set (r, "ceiling", -0.5f, -12.0f, 0.0f);
        set (r, "release", juce::jlimit (60.0f, 160.0f, 80.0f + 4.0f * s.crestDb), 10.0f, 500.0f);
        r.rationale = "A safety ceiling so nothing clips downstream; release is matched to the "
                      "take's dynamics so it stays inaudible.";
    }

    void ruleStereoChorus (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        // Width flatters sustained melodic material; it smears fast rap/spoken delivery.
        const float melodic = juce::jlimit (0.0f, 1.0f, s.voicedRatio * 1.3f)
                            * (1.0f - ramp (s.transientDensity, 3.0f, 6.0f))
                            * (1.0f - ramp (s.pitchStabilityCents, 40.0f, 90.0f));
        r.need = melodic * 0.5f;

        set (r, "rate",  0.5f,   0.05f, 8.0f);
        set (r, "depth", juce::jlimit (25.0f, 50.0f, 30.0f + 20.0f * melodic), 0.0f, 100.0f);
        set (r, "mix",   juce::jlimit (15.0f, 32.0f, 18.0f + 14.0f * melodic), 0.0f, 100.0f);
        set (r, "width", 80.0f, 0.0f, 100.0f);

        r.rationale = "A sustained, stable melodic take widens nicely. Kept to a low blend so "
                      "the centre image stays solid and mono-compatible.";
    }

    void ruleHighpass (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        const float rumble = ramp (s.subDb, -34.0f, -18.0f);
        r.need = rumble * 0.7f;
        // Follow the singer's pitch: never cut into the fundamental.
        const float byPitch = s.pitchMedianHz > 0.0f ? s.pitchMedianHz * 0.62f : 85.0f;
        set (r, "freq", juce::jlimit (60.0f, 140.0f, byPitch), 20.0f, 1000.0f);
        r.rationale = "Rolls off rumble below the voice. The corner is set from the measured "
                      "median pitch (" + hzStr (juce::jmax (1.0f, s.pitchMedianHz))
                    + ") so it never eats the fundamental.";
    }

    void ruleLowpass (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        // Rarely wanted on a vocal — only for very bright/hissy sources.
        r.need = ramp (s.brightness, 0.80f, 0.95f) * 0.3f;
        set (r, "freq", juce::jlimit (12000.0f, 18000.0f, 18000.0f - 6000.0f * ramp (s.brightness, 0.8f, 1.0f)),
             1000.0f, 20000.0f);
        r.rationale = "Only for very bright or hissy sources — tames the extreme top without "
                      "dulling presence.";
    }

    void ruleStereoWidth (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        r.need = 0.2f;   // a creative choice, not a fix
        set (r, "width", 115.0f, 0.0f, 200.0f);
        juce::ignoreUnused (s);
        r.rationale = "A modest widening for backing/double-tracked parts; lead vocals usually "
                      "stay centred, so this is left gentle.";
    }

    void ruleMonoMaker (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        r.need = 0.25f;
        set (r, "freq", juce::jlimit (90.0f, 160.0f, s.pitchMedianHz > 0.0f ? s.pitchMedianHz : 120.0f),
             20.0f, 500.0f);
        r.rationale = "Keeps the low end mono so the vocal's body stays solid and translates on "
                      "club/phone systems.";
    }

    void ruleTelephone (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        r.need = 0.05f;    // pure effect — never auto-inserted
        juce::ignoreUnused (s);
        set (r, "low",   400.0f,  100.0f, 900.0f);
        set (r, "high", 3400.0f, 1800.0f, 6000.0f);
        set (r, "drive",  35.0f,    0.0f, 100.0f);
        set (r, "mix",   100.0f,    0.0f, 100.0f);
        r.rationale = "A creative band-limited effect (verse/intro treatment) — not a corrective "
                      "move, so it is never added automatically.";
    }

    void ruleGain (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        r.need = 0.15f;
        // Gain-stage toward a -18 dBFS RMS working level.
        set (r, "gain", juce::jlimit (-12.0f, 12.0f, -18.0f - s.rmsDb), -24.0f, 24.0f);
        r.rationale = "Levels the take toward a -18 dBFS working level so every module after it "
                      "operates in its sweet spot.";
    }

    void ruleTrim (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        r.need = 0.15f;
        set (r, "trim", juce::jlimit (-10.0f, 10.0f, -18.0f - s.rmsDb), -12.0f, 12.0f);
        r.rationale = "Input trim toward a -18 dBFS working level.";
    }

    void rulePhaseFlip (const AnalysisSnapshot& s, ModuleRecommendation& r)
    {
        juce::ignoreUnused (s);
        r.need = 0.05f;
        set (r, "flip", 0.0f, 0.0f, 1.0f);
        r.rationale = "Only useful against a second mic or a doubled take — left off for a "
                      "single source.";
    }

    // ------------------------------------------------------------------
    struct Entry
    {
        const char* id;
        const char* name;
        const char* role;
        void (*rule) (const AnalysisSnapshot&, ModuleRecommendation&);
    };

    const Entry& table (int i);
    int tableSize();

    const Entry kTable[] =
    {
        { "compressor", "Compressor",
          "Evens out level so every word sits in the mix.", ruleCompressor },
        { "optical_comp", "Optical Compressor",
          "Slow, invisible levelling with vintage character.", ruleOpticalComp },
        { "parallel_comp", "Parallel Compressor",
          "Adds density underneath without squashing the top.", ruleParallelComp },
        { "gate", "Gate / Expander",
          "Quiets the gaps between phrases.", ruleGate },
        { "noise_reduction", "Noise Reduction",
          "Pushes down constant background hiss/hum.", ruleNoiseReduction },
        { "de_esser", "De-Esser",
          "Tames harsh 's' and 't' sounds only when they occur.", ruleDeEsser },
        { "de_plosive", "De-Plosive",
          "Removes 'p'/'b' pops without thinning the voice.", ruleDePlosive },
        { "dynamic_eq", "Dynamic EQ",
          "Ducks a problem frequency only when it spikes.", ruleDynamicEq },
        { "parametric_eq", "Parametric EQ",
          "Static tone shaping: weight, mud, presence, air.", ruleParametricEq },
        { "vintage_eq", "Vintage EQ",
          "Broad, musical analogue-style tone colour.", ruleVintageEq },
        { "exciter", "Exciter",
          "Generates high harmonics for air and sheen.", ruleExciter },
        { "tape_sat", "Tape Saturation",
          "Analogue warmth, body and gentle glue.", ruleTapeSat },
        { "console_sat", "Console Saturation",
          "Subtle desk colour that binds the take together.", ruleConsoleSat },
        { "tube_sat", "Tube Saturation",
          "Valve-style even harmonics for warmth.", ruleTubeSat },
        { "transient_designer", "Transient Designer",
          "Shapes attack and tail for punch or tightness.", ruleTransient },
        { "soft_clipper", "Soft Clipper",
          "Rounds off sharp peaks before the limiter.", ruleSoftClipper },
        { "limiter", "Limiter",
          "Final safety ceiling against clipping.", ruleLimiter },
        { "stereo_chorus", "Stereo Chorus",
          "Widens and thickens sustained melodic parts.", ruleStereoChorus },
        { "highpass", "High Pass",
          "Removes rumble below the voice.", ruleHighpass },
        { "lowpass", "Low Pass",
          "Tames extreme top end on bright sources.", ruleLowpass },
        { "stereo_width", "Stereo Width",
          "Controls how wide the vocal image sits.", ruleStereoWidth },
        { "mono_maker", "Mono Maker",
          "Keeps the low end centred and solid.", ruleMonoMaker },
        { "telephone", "Telephone",
          "Band-limited 'phone/radio' creative effect.", ruleTelephone },
        { "gain", "Gain",
          "Sets working level for the modules after it.", ruleGain },
        { "trim", "Trim",
          "Input level trim.", ruleTrim },
        { "phase_flip", "Phase Flip",
          "Inverts polarity (for multi-mic sources).", rulePhaseFlip },
    };

    int tableSize() { return (int) (sizeof (kTable) / sizeof (kTable[0])); }
    const Entry& table (int i) { return kTable[i]; }

    const Entry* findEntry (const juce::String& id)
    {
        for (int i = 0; i < tableSize(); ++i)
            if (id == table (i).id) return &table (i);
        return nullptr;
    }

    juce::String summarise (const ModuleRecommendation& r)
    {
        juce::StringArray parts;
        for (const auto& st : r.settings)
        {
            // Compact, human-readable: ids are short and self-describing.
            juce::String v;
            if (std::abs (st.value) >= 1000.0f) v = juce::String (st.value / 1000.0f, 1) + "k";
            else if (std::abs (st.value) >= 10.0f) v = juce::String ((int) std::round (st.value));
            else v = juce::String (st.value, 1);
            parts.add (st.paramId + " " + v);
        }
        return parts.joinIntoString ("  ·  ");
    }
} // namespace

// ============================================================================
ModuleRecommendation ModuleIntelligence::dialFor (const juce::String& moduleId,
                                                  const AnalysisSnapshot& s)
{
    ModuleRecommendation r;
    const auto* e = findEntry (moduleId);
    if (e == nullptr)
        return r;                       // unknown module → empty moduleId

    r.moduleId   = e->id;
    r.moduleName = e->name;
    r.role       = e->role;

    if (! s.valid)
    {
        r.rationale = "No analysis yet — run LEARN and I'll dial this in for your voice.";
        return r;
    }

    e->rule (s, r);
    r.need = juce::jlimit (0.0f, 1.0f, r.need);
    r.settingsSummary = summarise (r);
    return r;
}

std::vector<ModuleRecommendation> ModuleIntelligence::recommend (const AnalysisSnapshot& s)
{
    std::vector<ModuleRecommendation> out;
    if (! s.valid)
        return out;

    for (int i = 0; i < tableSize(); ++i)
    {
        auto r = dialFor (table (i).id, s);
        if (r.moduleId.isNotEmpty() && r.need > 0.0f)
            out.push_back (std::move (r));
    }

    std::stable_sort (out.begin(), out.end(),
                      [] (const ModuleRecommendation& a, const ModuleRecommendation& b)
                      { return a.need > b.need; });
    return out;
}

bool ModuleIntelligence::knows (const juce::String& moduleId)
{
    return findEntry (moduleId) != nullptr;
}

juce::String ModuleIntelligence::roleOf (const juce::String& moduleId)
{
    const auto* e = findEntry (moduleId);
    return e != nullptr ? juce::String (e->role) : juce::String();
}

juce::StringArray ModuleIntelligence::knownModules()
{
    juce::StringArray ids;
    for (int i = 0; i < tableSize(); ++i)
        ids.add (table (i).id);
    return ids;
}
} // namespace vf
