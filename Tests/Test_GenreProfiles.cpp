#include "TestFramework.h"
#include "../Source/Brain/GenreProfiles.h"
#include "../Source/ParameterIDs.h"

// ============================================================================
//  Genre profiles — does each preset deliver the sound its own words promise?
//
//  The 46 genre profiles are DATA: a description a producer reads, and a set of
//  parameter targets that are supposed to realise it. Nothing keeps those two
//  halves honest, and they had already drifted. Six profiles — Drill, UK Drill,
//  Rage, Hyperpop, Emo Trap and Trap — describe themselves as "hard-tuned" and
//  set Retune Speed to 0, but never touched Hard Tune. Speed alone does not
//  produce that sound: the expressive allowances (Flex 35 / Vibrato 75 /
//  Transition 60) stay at their defaults and keep handing the performance back
//  to the singer, so asking for Drill got you a soft, natural tuning.
//
//  This suite ties the description to the settings, so a profile cannot promise
//  something its targets do not do.
// ============================================================================
namespace
{
using namespace vftest;

bool describesHardTune (const juce::String& text)
{
    const auto t = text.toLowerCase();
    return t.contains ("hard tune") || t.contains ("hard-tune")
        || t.contains ("hard tuned") || t.contains ("hard-tuned")
        || t.contains ("robotic");
}

/** Value a profile sets for a parameter, or a sentinel when it does not. */
double targetFor (const vf::GenreProfile& g, const char* id, double absent = -1.0e9)
{
    for (const auto& t : g.targets)
        if (juce::String (t.paramId) == id) return t.value;
    return absent;
}
} // namespace

void runGenreProfileTests()
{
    Suite s ("Genre profiles (do the settings match the description?)");

    const auto& all = vf::GenreProfiles::all();
    s.check ("the genre table is populated", all.size() >= 40,
             juce::String ((int) all.size()) + " profiles");
    if (all.size() < 40) return;

    // ---- the regression this suite was written for ---------------------
    juce::StringArray promisedButSoft;
    int hardTuneGenres = 0;
    for (const auto& g : all)
    {
        if (! describesHardTune (g.sound)) continue;
        ++hardTuneGenres;
        if (targetFor (g, vf::param::pitchHardTune) < 50.0)
            promisedButSoft.add (g.name);
    }
    s.check ("some profiles do describe a hard-tuned sound", hardTuneGenres >= 4,
             juce::String (hardTuneGenres) + " profiles say hard tune / robotic");
    s.check ("every profile that promises hard tuning actually sets Hard Tune",
             promisedButSoft.isEmpty(),
             promisedButSoft.isEmpty() ? "all consistent"
                                       : "still soft: " + promisedButSoft.joinIntoString (", "));

    // ---- and the converse: a profile that calls itself gentle must not be
    //      secretly hard-tuned, or the description is just as wrong.
    juce::StringArray gentleButHard;
    for (const auto& g : all)
    {
        const auto t = g.sound.toLowerCase();
        const bool gentle = t.contains ("gentle") || t.contains ("natural")
                         || t.contains ("soft, ") || t.contains ("floating");
        if (gentle && targetFor (g, vf::param::pitchHardTune) >= 50.0)
            gentleButHard.add (g.name);
    }
    s.check ("no profile described as gentle is secretly hard-tuned",
             gentleButHard.isEmpty(),
             gentleButHard.isEmpty() ? "all consistent" : gentleButHard.joinIntoString (", "));

    // ---- general integrity ---------------------------------------------
    juce::StringArray noPitch, noDescription;
    for (const auto& g : all)
    {
        if (g.sound.isEmpty() || g.name.isEmpty()) noDescription.add (g.id);
        if (targetFor (g, vf::param::pitchOn) > 0.5
            && targetFor (g, vf::param::pitchAmount, 0.0) <= 0.0)
            noPitch.add (g.name);
    }
    s.check ("every profile describes itself", noDescription.isEmpty(),
             noDescription.isEmpty() ? "all documented" : noDescription.joinIntoString (", "));
    s.check ("no profile enables tuning without setting a correction amount",
             noPitch.isEmpty(),
             noPitch.isEmpty() ? "all consistent" : noPitch.joinIntoString (", "));
}
