#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace vf
{
// ============================================================================
//  GenreProfiles — the AI Engineer's genre & SUB-GENRE knowledge.
//  ---------------------------------------------------------------------------
//  "Make it emo trap" is a complete mixing instruction to a human engineer: it
//  implies a tuning style, a reverb size, a saturation colour, how bright the
//  top is, how wide it sits. This table encodes that knowledge so the chat
//  assistant (and, later, the auto-mix brain) can act on it.
//
//  Genre rules used to live as a handful of hard-coded entries inside the chat
//  rule list, which meant sub-genres were impossible: "emo trap" just matched
//  "trap". Profiles now live here as DATA with:
//    * `aliases`  — every phrase a producer might actually type. Matching is
//                   LONGEST-ALIAS-FIRST, so "emo trap" resolves to Emo Trap and
//                   "uk drill" to UK Drill rather than their parent genres.
//    * `family`   — the parent style, so the UI can group them and the reply can
//                   say what it did ("Emo Trap — a Hip-Hop sub-genre").
//    * `targets`  — absolute {paramId, value} settings in NATURAL units.
//    * `sound`    — what the result should feel like, in plain language.
//
//  Nothing here depends on the APVTS or DSP; the offline test cross-checks every
//  target against the real parameter layout (id exists + value inside range), so
//  a future range change can't silently break a genre.
// ============================================================================
struct GenreTarget
{
    const char* paramId;
    float       value;      // natural units (dB, Hz, ms, %, choice index)
};

struct GenreProfile
{
    juce::String id;              // stable key, e.g. "emo_trap"
    juce::String name;            // display name, e.g. "Emo Trap"
    juce::String family;          // parent style, e.g. "Hip-Hop"
    juce::StringArray aliases;    // chat trigger phrases (lowercase)
    juce::String sound;           // what it should sound like
    std::vector<GenreTarget> targets;
};

class GenreProfiles
{
public:
    /** Every profile the engine knows (stable order, grouped by family). */
    static const std::vector<GenreProfile>& all();

    /** Find the genre a message asks for. Scans the text for the LONGEST
        matching alias, so sub-genres beat their parents. Returns nullptr when
        the text names no genre. */
    static const GenreProfile* match (const juce::String& lowercaseText);

    /** Exact lookup by stable id. */
    static const GenreProfile* byId (const juce::String& id);

    /** All distinct family names, in table order. */
    static juce::StringArray families();

    /** Genres belonging to one family. */
    static std::vector<const GenreProfile*> inFamily (const juce::String& family);

    /** Comma-separated example names, for help text. */
    static juce::String examplesForHelp (int maxItems = 10);
};
} // namespace vf
