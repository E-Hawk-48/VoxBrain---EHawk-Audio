#include "VoiceCharacter.h"
#include "../ParameterIDs.h"

namespace vf
{
using namespace vf::param;

namespace
{
    // Choice indices (authoritative lists live in Parameters.cpp):
    //   sat_type  : 0 Tube, 1 Tape, 2 Console, 3 Transformer, 4 Germanium,
    //               5 Diode, 6 Exciter, 7 Lo-Fi
    //   verb_type : 0 Room, 1 Hall, 2 Plate, 3 Spring, 4 Cathedral,
    //               5 Shimmer, 6 Bloom
    constexpr float SAT_TUBE = 0, SAT_TAPE = 1, SAT_CONSOLE = 2, SAT_TRANSFORMER = 3;
    constexpr float SAT_GERMANIUM = 4, SAT_DIODE = 5, SAT_LOFI = 7;
    constexpr float VERB_ROOM = 0, VERB_HALL = 1, VERB_PLATE = 2;
    constexpr float VERB_CATHEDRAL = 4, VERB_SHIMMER = 5;

    // Every character below runs the grain engine, so each one sets `pitch_on`.
    // Characters with no transposition (Ghost) still need it on to reach the
    // formant path, and set `pitch_amount` to 0 so nothing is forced to a scale.
    // Scale is left at Auto/Chromatic throughout: a voice changer must never
    // silently re-tune a performance — that is the auto-tune feature's job.

    std::vector<VoiceProfile> build()
    {
        std::vector<VoiceProfile> v;
        auto add = [&v] (const char* id, const char* name, const char* category,
                         juce::StringArray aliases, const char* sound,
                         const char* why, std::vector<VoiceTarget> targets)
        {
            v.push_back ({ id, name, category, std::move (aliases),
                           sound, why, std::move (targets) });
        };

        // ====================================================================
        //  OFF — index 0, and it must stay index 0 forever.
        //  No targets: selecting Off does not undo anything, it simply stops
        //  imposing a character. Restoring the previous sound is Undo's job.
        // ====================================================================
        add ("off", "Off", "None", { }, "No character — the voice as recorded.",
             "Nothing is applied.", { });

        // ====================================================================
        //  MONSTROUS — pitch and formants pulled apart to break the illusion
        //  of a human throat.
        // ====================================================================
        add ("demonic", "Demonic", "Monstrous",
             { "demonic", "demon", "devil", "evil voice", "possessed" },
             "A full octave down with a throat bigger than the note implies — "
             "menacing rather than merely deep, with a dry gravelly edge.",
             "Formants are pushed 5 semitones BELOW an already octave-down pitch, "
             "so the implied body is larger than any real person who could sing "
             "that note. Diode clipping adds the hard odd-order edge of a growl.",
             { { pitchOn, 1 }, { pitchTranspose, -12 }, { pitchFormant, -5 },
               { pitchAmount, 0 }, { pitchLatency, 2 },
               { eqOn, 1 }, { eqHpfFreq, 45 }, { eqLowShelfGain, 3 },
               { eqPresenceGain, -1.5f }, { eqAirGain, -4 },
               { satOn, 1 }, { satType, SAT_DIODE }, { satDrive, 45 },
               { satTone, 30 }, { satMix, 55 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 4 }, { compAttack, 12 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 45 },
               { verbDamp, 70 }, { verbMix, 12 } });

        add ("monster", "Monster", "Monstrous",
             { "monster", "beast", "creature", "ogre", "troll" },
             "Enormous and guttural — less pitched-down human, more something "
             "with a chest the size of a room.",
             "Formants drop 12 semitones while pitch only drops 8. Because the "
             "body is shifted FURTHER than the note, the ear hears sheer mass "
             "rather than a slowed-down recording. Germanium adds the ragged, "
             "asymmetric distortion of a strained throat.",
             { { pitchOn, 1 }, { pitchTranspose, -8 }, { pitchFormant, -12 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 40 }, { eqLowShelfGain, 4.5f },
               { eqMudGain, 1.5f }, { eqPresenceGain, -2 }, { eqAirGain, -6 },
               { satOn, 1 }, { satType, SAT_GERMANIUM }, { satDrive, 60 },
               { satBias, 62 }, { satTone, 22 }, { satMix, 65 },
               { compOn, 1 }, { compThreshold, -22 }, { compRatio, 5 }, { compAttack, 15 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 55 },
               { verbDamp, 78 }, { verbMix, 15 } });

        add ("titan", "Titan", "Monstrous",
             { "titan", "deep demon", "kaiju", "colossus", "the void" },
             "Subterranean. Nearly a semitone-and-a-half octave drop, felt more "
             "than heard, for trailer and boss-fight voices.",
             "-17 semitones is close to the engine's practical floor: below that "
             "the grain rate gets slow enough to hear as individual pulses, which "
             "is exactly the rumble this character wants. Formants trail behind at "
             "-8 so it stays intelligible instead of turning to mud.",
             { { pitchOn, 1 }, { pitchTranspose, -17 }, { pitchFormant, -8 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 30 }, { eqLowShelfGain, 5 },
               { eqPresenceGain, 1.5f }, { eqAirGain, -6 },   // -6 dB is the Air knob's floor
               { satOn, 1 }, { satType, SAT_TRANSFORMER }, { satDrive, 50 },
               { satTone, 25 }, { satMix, 60 },
               { compOn, 1 }, { compThreshold, -24 }, { compRatio, 6 }, { compAttack, 20 },
               { verbOn, 1 }, { verbType, VERB_CATHEDRAL }, { verbSize, 80 },
               { verbPredelay, 40 }, { verbDamp, 65 }, { verbMix, 22 } });

        add ("giant", "Giant", "Monstrous",
             { "giant", "big guy", "titan voice", "large", "gigantic" },
             "Huge but friendly — a big person, not a beast. Clean and warm "
             "rather than distorted.",
             "Pitch and formants move together (-7 and -9), which keeps the voice "
             "COHERENT — the listener reads it as a genuinely large human instead "
             "of an effect. The small extra formant drop is what supplies the "
             "scale. No aggressive drive, because size is not the same as menace.",
             { { pitchOn, 1 }, { pitchTranspose, -7 }, { pitchFormant, -9 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 45 }, { eqLowShelfGain, 3 },
               { eqPresenceGain, 1 }, { eqAirGain, -2 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 25 }, { satMix, 30 },
               { compOn, 1 }, { compThreshold, -19 }, { compRatio, 3 },
               { verbOn, 1 }, { verbType, VERB_HALL }, { verbSize, 65 },
               { verbPredelay, 25 }, { verbMix, 16 } });

        // ====================================================================
        //  SMALLER — the mirror image: upward shifts, with formants deciding
        //  whether the result is a real child or a cartoon.
        // ====================================================================
        add ("child", "Child", "Smaller",
             { "child", "kid", "little kid", "young voice", "childlike" },
             "A believable child rather than a sped-up adult — higher, lighter, "
             "and physically smaller.",
             "A real child's vocal tract is about 25% shorter, so the formants "
             "must rise WITH the pitch. +5 pitch against +6 formant matches the "
             "proportions of an actual young voice; pitch alone would just sound "
             "like a tape running fast.",
             { { pitchOn, 1 }, { pitchTranspose, 5 }, { pitchFormant, 6 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 140 }, { eqLowShelfGain, -2.5f },
               { eqMudGain, -2 }, { eqPresenceGain, 2 }, { eqAirGain, 2 },
               { deessOn, 1 }, { deessFreq, 8000 }, { deessThreshold, -32 },
               { compOn, 1 }, { compThreshold, -18 }, { compRatio, 3 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 30 }, { verbMix, 10 } });

        add ("chipmunk", "Chipmunk", "Smaller",
             { "chipmunk", "squeaky", "helium", "sped up", "high pitched" },
             "The classic full-octave-up novelty voice, bright and comic.",
             "A whole octave with formants following most of the way (+9). The "
             "deliberate 3-semitone gap is what keeps it recognisably a VOICE "
             "rather than a whistle — full formant tracking at +12 thins it out "
             "until the words stop landing.",
             { { pitchOn, 1 }, { pitchTranspose, 12 }, { pitchFormant, 9 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 180 }, { eqLowShelfGain, -4 },
               { eqMudGain, -3 }, { eqPresenceGain, 2.5f }, { eqAirGain, 1.5f },
               { deessOn, 1 }, { deessFreq, 9000 }, { deessThreshold, -30 },
               { compOn, 1 }, { compThreshold, -18 }, { compRatio, 3.5f },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 25 }, { verbMix, 8 } });

        add ("cartoon", "Cartoon", "Smaller",
             { "cartoon", "toon", "silly voice", "goofy", "mouse" },
             "Squeaky and animated — the voice of something small and cheerful.",
             "Formants are pushed HIGHER than the pitch (+12 vs +7), the inverse "
             "of the monster trick. An impossibly tiny head on a moderate pitch "
             "reads as drawn rather than recorded, which is exactly the point.",
             { { pitchOn, 1 }, { pitchTranspose, 7 }, { pitchFormant, 12 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 200 }, { eqLowShelfGain, -5 },
               { eqMudGain, -3.5f }, { eqPresenceGain, 3 }, { eqAirGain, 2 },
               { deessOn, 1 }, { deessFreq, 9500 }, { deessThreshold, -28 },
               { compOn, 1 }, { compThreshold, -17 }, { compRatio, 4 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 20 }, { satMix, 20 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 22 }, { verbMix, 8 } });

        // ====================================================================
        //  SYNTHETIC — where the character comes from the TUNING and the
        //  processing rather than from a size change.
        // ====================================================================
        add ("robot", "Robot", "Synthetic",
             { "robot", "robotic", "android", "cyborg", "machine voice", "vocoder" },
             "Rigidly pitched, band-limited and lifeless — a machine reading the "
             "line rather than a person singing it.",
             "This one uses NO transposition at all. What makes a voice sound "
             "robotic is the removal of micro-variation, so Hard Tune goes to "
             "100% with every expressive allowance at zero: no vibrato, no "
             "slides, no drift. Lo-Fi quantisation supplies the digital grain.",
             { { pitchOn, 1 }, { pitchTranspose, 0 }, { pitchFormant, 0 },
               { pitchAmount, 100 }, { pitchHardTune, 100 }, { pitchSpeed, 0 },
               { pitchHumanize, 0 }, { pitchFlex, 0 }, { pitchVibrato, 0 },
               { pitchTransition, 0 }, { pitchSnap, 0 },
               { eqOn, 1 }, { eqHpfFreq, 160 }, { eqLowShelfGain, -3 },
               { eqPresenceGain, 4 }, { eqAirGain, -3 },
               { satOn, 1 }, { satType, SAT_LOFI }, { satDrive, 40 },
               { satTone, 55 }, { satMix, 45 },
               { compOn, 1 }, { compThreshold, -24 }, { compRatio, 8 }, { compAttack, 1 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 20 }, { verbMix, 6 } });

        add ("alien", "Alien", "Synthetic",
             { "alien", "extraterrestrial", "ufo", "otherworldly", "xeno" },
             "Unsettling and not-quite-right — higher than a person but with a "
             "cavity that could not belong to one.",
             "The most extreme pitch/formant CONTRADICTION in the table: pitch "
             "up 4, formants down 8. No living thing has a small skull and a low "
             "resonance, so the brain refuses to place it. Shimmer reverb adds "
             "octave-up regeneration that has no acoustic equivalent.",
             { { pitchOn, 1 }, { pitchTranspose, 4 }, { pitchFormant, -8 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 90 }, { eqMudGain, -2 },
               { eqPresenceGain, 3 }, { eqAirGain, 3 },
               { satOn, 1 }, { satType, SAT_DIODE }, { satDrive, 30 },
               { satTone, 65 }, { satMix, 35 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 4 },
               { verbOn, 1 }, { verbType, VERB_SHIMMER }, { verbSize, 60 },
               { verbShimmer, 45 }, { verbModDepth, 55 }, { verbMix, 24 } });

        add ("ghost", "Ghost", "Synthetic",
             { "ghost", "ghostly", "spirit", "haunted", "ethereal", "spectral" },
             "Thin, distant and disembodied — present in the room but never "
             "quite in front of you.",
             "The only character with NO pitch change: the pitch stays honest and "
             "the formants alone lift 4 semitones, which hollows the voice without "
             "making it sound sped up. The body is then removed with a high pass "
             "and replaced by a huge, pre-delayed cathedral.",
             { { pitchOn, 1 }, { pitchTranspose, 0 }, { pitchFormant, 4 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 240 }, { eqLowShelfGain, -6 },
               { eqMudGain, -4 }, { eqPresenceGain, 1 }, { eqAirGain, 5 },
               { deessOn, 1 }, { deessFreq, 8500 }, { deessThreshold, -34 },
               { compOn, 1 }, { compThreshold, -22 }, { compRatio, 3 }, { compMix, 70 },
               { delayOn, 1 }, { delayTime, 320 }, { delayFeedback, 35 }, { delayMix, 22 },
               { verbOn, 1 }, { verbType, VERB_CATHEDRAL }, { verbSize, 88 },
               { verbPredelay, 60 }, { verbDecay, 80 }, { verbWidth, 90 },
               { verbDamp, 35 }, { verbMix, 42 } });

        // ====================================================================
        //  HUMAN — small, plausible shifts. These are the ones you can use on a
        //  real record without anyone noticing an effect was applied.
        // ====================================================================
        add ("old_man", "Old Man", "Human",
             { "old man", "old", "elderly", "grandpa", "aged voice", "old woman" },
             "Weathered and slightly frail — a little lower, a little rougher, "
             "and noticeably short of top end.",
             "Ageing does not simply lower a voice; it costs it high frequency and "
             "adds irregularity. So the shift is deliberately SMALL (-3 pitch, -2 "
             "formant) and the character comes from rolled-off air, gentle "
             "germanium grit for a rasp, and vibrato left fully intact — an "
             "unsteady voice is the strongest age cue there is.",
             { { pitchOn, 1 }, { pitchTranspose, -3 }, { pitchFormant, -2 },
               { pitchAmount, 0 }, { pitchVibrato, 100 }, { pitchHumanize, 90 },
               { eqOn, 1 }, { eqHpfFreq, 90 }, { eqLowShelfGain, 1 },
               { eqMudGain, 1 }, { eqPresenceGain, -1 }, { eqAirGain, -6 },
               { satOn, 1 }, { satType, SAT_GERMANIUM }, { satDrive, 32 },
               { satBias, 58 }, { satTone, 32 }, { satMix, 35 },
               { compOn, 1 }, { compThreshold, -19 }, { compRatio, 2.5f },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 35 }, { verbMix, 10 } });

        add ("radio", "Radio", "Human",
             { "radio", "radio voice", "announcer", "broadcast", "narrator",
               "movie trailer", "dj voice" },
             "That authoritative late-night broadcast sound — big, close and "
             "impossibly even.",
             "Barely any shift (-2 and -3): the weight comes from the FORMANTS "
             "dropping slightly further than the pitch, which is the same trick "
             "a presenter uses by lowering their larynx. Everything else is "
             "classic broadcast chain — heavy fast compression, console warmth, "
             "and a low shelf for proximity.",
             { { pitchOn, 1 }, { pitchTranspose, -2 }, { pitchFormant, -3 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 70 }, { eqLowShelfGain, 3.5f },
               { eqMudGain, -2 }, { eqPresenceGain, 3.5f }, { eqAirGain, 2 },
               { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -34 },
               { compOn, 1 }, { compThreshold, -26 }, { compRatio, 6 },
               { compAttack, 3 }, { compRelease, 90 }, { compMakeup, 6 },
               { satOn, 1 }, { satType, SAT_CONSOLE }, { satDrive, 28 }, { satMix, 35 },
               { mbandOn, 1 }, { mbandRatio, 3 },
               { limitOn, 1 }, { limitCeiling, -1 } });

        add ("telephone", "Telephone", "Human",
             { "telephone", "phone", "phone call", "walkie talkie", "intercom",
               "megaphone", "lo fi voice" },
             "Squeezed down a phone line: no lows, no air, and clipped hard.",
             "A pure band-limit character — the size of the voice is untouched, "
             "which is what keeps it believable as the SAME person on a bad "
             "connection. The 300 Hz to ~3 kHz window plus diode clipping is the "
             "actual bandwidth of a landline.",
             { { pitchOn, 1 }, { pitchTranspose, 0 }, { pitchFormant, 1 },
               { pitchAmount, 0 },
               { eqOn, 1 }, { eqHpfFreq, 320 }, { eqLowShelfGain, -12 },
               { eqMudGain, 2 }, { eqPresenceGain, 6 }, { eqAirGain, -6 },
               { satOn, 1 }, { satType, SAT_DIODE }, { satDrive, 55 },
               { satTone, 60 }, { satMix, 70 },
               { compOn, 1 }, { compThreshold, -26 }, { compRatio, 8 }, { compAttack, 1 },
               { verbOn, 0 }, { delayOn, 0 } });

        add ("smoky", "Smoky", "Human",
             { "smoky", "smokey", "sultry", "husky", "raspy", "whiskey", "sexy voice" },
             "Lower, closer and rougher — late-night and lived-in without "
             "sounding processed.",
             "Only two semitones down, with formants matched, so it stays fully "
             "natural; the entire character is tape saturation plus a proximity "
             "shelf. This is the one to reach for on a real vocal that needs more "
             "authority rather than a costume.",
             { { pitchOn, 1 }, { pitchTranspose, -2 }, { pitchFormant, -2 },
               { pitchAmount, 0 }, { pitchVibrato, 90 }, { pitchHumanize, 80 },
               { eqOn, 1 }, { eqHpfFreq, 65 }, { eqLowShelfGain, 2.5f },
               { eqMudGain, -1 }, { eqPresenceGain, 1.5f }, { eqAirGain, 1 },
               { deessOn, 1 }, { deessFreq, 6200 }, { deessThreshold, -32 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satDrive, 38 },
               { satTone, 40 }, { satMix, 45 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 3.5f },
               { compAttack, 8 }, { compRelease, 140 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 40 },
               { verbDamp, 55 }, { verbMix, 14 } });

        return v;
    }

    juce::String normalise (const juce::String& s)
    {
        juce::String out;
        for (auto c : s)
            if (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ')
                out += juce::String::charToString (c).toLowerCase();
        return out;
    }
} // namespace

const std::vector<VoiceProfile>& VoiceCharacters::all()
{
    static const std::vector<VoiceProfile> table = build();
    return table;
}

juce::StringArray VoiceCharacters::menuNames()
{
    juce::StringArray names;
    for (const auto& p : all())
        names.add (p.name);
    return names;
}

const VoiceProfile* VoiceCharacters::byIndex (int index)
{
    const auto& t = all();
    if (index < 0 || index >= (int) t.size())
        return nullptr;
    return &t[(size_t) index];
}

const VoiceProfile* VoiceCharacters::byId (const juce::String& id)
{
    for (const auto& p : all())
        if (p.id == id)
            return &p;
    return nullptr;
}

int VoiceCharacters::indexOf (const juce::String& id)
{
    const auto& t = all();
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i].id == id)
            return (int) i;
    return 0;
}

const VoiceProfile* VoiceCharacters::match (const juce::String& lowercaseText)
{
    // LONGEST alias wins, exactly as in GenreProfiles: "deep demon" must beat
    // "demon", and "old woman" must not be swallowed by a shorter phrase.
    const auto hay = normalise (lowercaseText);
    const VoiceProfile* best = nullptr;
    int bestLen = 0;

    for (const auto& p : all())
    {
        if (p.id == "off")
            continue;

        for (const auto& a : p.aliases)
        {
            const auto needle = normalise (a);
            if (needle.isEmpty() || needle.length() <= bestLen)
                continue;
            if (hay.contains (needle))
            {
                best = &p;
                bestLen = needle.length();
            }
        }
    }
    return best;
}

juce::StringArray VoiceCharacters::categories()
{
    juce::StringArray out;
    for (const auto& p : all())
        if (p.category != "None")
            out.addIfNotAlreadyThere (p.category);
    return out;
}
} // namespace vf
