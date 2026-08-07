#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace vf
{
// ============================================================================
//  VoiceCharacter — the Voice Changer's knowledge of what makes a voice sound
//  like someone (or something) else.
//  ---------------------------------------------------------------------------
//  THE ONE IDEA THAT MAKES THIS WORK, and the reason cheap voice changers sound
//  cheap: a voice has TWO independent sizes.
//
//    * PITCH (f0)      — how fast the vocal folds vibrate. Transposing this
//                        alone is what a tape speed-up does NOT do.
//    * FORMANTS        — the resonances of the throat and mouth, i.e. the
//                        physical SIZE of the head. This is what actually tells
//                        a listener "child" or "giant".
//
//  VoxBrain's TD-PSOLA engine can move them separately (`pitch_transpose` and
//  `pitch_formant`), and the whole expressive range of this feature comes from
//  how they are combined:
//
//    SAME DIRECTION, SIMILAR AMOUNT  -> a believable person of a different size.
//        A real child is higher-pitched AND has a smaller head, so +5 pitch with
//        +6 formant reads as a genuine child rather than a sped-up adult.
//        (Pitch alone = chipmunk cartoon. That is a valid effect, not realism.)
//
//    OPPOSITE DIRECTIONS             -> uncanny, inhuman, "wrong".
//        Nothing alive has a small skull and a low voice, so +4 pitch with -8
//        formant reads as alien. This mismatch is the single most useful trick
//        for monsters, and it is why "Demonic" is not merely "pitched down".
//
//    FORMANTS FURTHER THAN PITCH     -> exaggerated size, comic or monstrous.
//        -8 pitch with -12 formant is a creature far bigger than the note it is
//        singing, which the ear hears as sheer mass.
//
//  Everything else in a profile (saturation model, EQ tilt, reverb) is there to
//  sell the illusion the shift already created — grit for a monster's throat,
//  rolled-off air for an old voice, a cathedral for a ghost.
//
//  Structure deliberately mirrors Brain/GenreProfiles: profiles are DATA
//  ({paramId, natural value} targets), not code, so they are inspectable,
//  range-checkable by an offline test, and applied through the same APVTS path
//  as everything else (so they are automatable, undoable, and preset-savable).
// ============================================================================
struct VoiceTarget
{
    const char* paramId;
    float       value;      // natural units (semitones, dB, Hz, ms, %, choice)
};

struct VoiceProfile
{
    juce::String id;              // stable key, e.g. "demonic"
    juce::String name;            // display name, e.g. "Demonic"
    juce::String category;        // grouping, e.g. "Monstrous"
    juce::StringArray aliases;    // chat trigger phrases (lowercase)
    juce::String sound;           // what it should sound like, plain language
    juce::String why;             // the engineering reason it works
    std::vector<VoiceTarget> targets;
};

class VoiceCharacters
{
public:
    /** Every character, in menu order. Index 0 is always "off". */
    static const std::vector<VoiceProfile>& all();

    /** Display names in menu order — feeds the `voice_character` choice list.
        The parameter's choice indices are a stable API, so NEVER reorder this;
        append new characters at the end. */
    static juce::StringArray menuNames();

    /** Character at a choice index, or nullptr if out of range. */
    static const VoiceProfile* byIndex (int index);

    /** Exact lookup by stable id. */
    static const VoiceProfile* byId (const juce::String& id);

    /** Find the character a chat message asks for, matching the LONGEST alias so
        "deep demon" beats "demon". Returns nullptr when none is named. */
    static const VoiceProfile* match (const juce::String& lowercaseText);

    /** Menu index of an id, or 0 (Off) if unknown. */
    static int indexOf (const juce::String& id);

    /** Distinct category names, in table order. */
    static juce::StringArray categories();
};
} // namespace vf
