#include "ModuleCommands.h"
#include "../Modules/ModuleRegistry.h"
#include <algorithm>

namespace vf
{
namespace
{
    // Extra ways producers name modules that don't match the registry name.
    struct Nickname { const char* phrase; const char* moduleId; };
    const Nickname kNicknames[] =
    {
        { "deesser",          "de_esser" },
        { "esser",            "de_esser" },
        { "sibilance",        "de_esser" },
        { "deplosive",        "de_plosive" },
        { "popfilter",        "de_plosive" },
        { "noisegate",        "gate" },
        { "expander",         "gate" },
        { "denoiser",         "noise_reduction" },
        { "noiseremoval",     "noise_reduction" },
        { "comp",             "compressor" },
        { "opto",             "optical_comp" },
        { "la2a",             "optical_comp" },
        { "parallelcomp",     "parallel_comp" },
        { "newyorkcomp",      "parallel_comp" },
        { "dynamiceq",        "dynamic_eq" },
        { "dyneq",            "dynamic_eq" },
        { "parametriceq",     "parametric_eq" },
        { "eqmodule",         "parametric_eq" },
        { "pultec",           "vintage_eq" },
        { "vintageeq",        "vintage_eq" },
        { "tapesaturation",   "tape_sat" },
        { "tape",             "tape_sat" },
        { "consolesaturation","console_sat" },
        { "console",          "console_sat" },
        { "tubesaturation",   "tube_sat" },
        { "tube",             "tube_sat" },
        { "valve",            "tube_sat" },
        { "transientdesigner","transient_designer" },
        { "transient",        "transient_designer" },
        { "softclipper",      "soft_clipper" },
        { "clipper",          "soft_clipper" },
        { "chorus",           "stereo_chorus" },
        { "widener",          "stereo_width" },
        { "stereowidth",      "stereo_width" },
        { "width",            "stereo_width" },
        { "monomaker",        "mono_maker" },
        { "highpass",         "highpass" },
        { "hpf",              "highpass" },
        { "lowcut",           "highpass" },
        { "lowpass",          "lowpass" },
        { "lpf",              "lowpass" },
        { "highcut",          "lowpass" },
        { "telephone",        "telephone" },
        { "megaphone",        "telephone" },
        { "exciter",          "exciter" },
        { "limiter",          "limiter" },
        { "phaseflip",        "phase_flip" },
        { "polarity",         "phase_flip" },
    };

    struct Candidate { juce::String needle; juce::String id; juce::String name; };

    /** Every phrase that can name a module, longest first so "tape saturation"
        wins over "tape" and "parallel compressor" over "compressor". */
    const std::vector<Candidate>& candidates()
    {
        static const std::vector<Candidate> c = []
        {
            std::vector<Candidate> out;
            mods::registerBuiltInModules();      // registry does NOT self-register

            for (const auto& d : mods::ModuleRegistry::instance().catalog())
            {
                if (! mods::ModuleRegistry::instance().isImplemented (d.id))
                    continue;                     // never offer a roadmap entry
                out.push_back ({ ModuleCommands::normalise (d.name), d.id, d.name });
                const auto fromId = ModuleCommands::normalise (d.id);
                if (fromId != ModuleCommands::normalise (d.name))
                    out.push_back ({ fromId, d.id, d.name });
            }

            for (const auto& n : kNicknames)
            {
                juce::String display = n.moduleId;
                for (const auto& d : mods::ModuleRegistry::instance().catalog())
                    if (d.id == n.moduleId) { display = d.name; break; }
                if (mods::ModuleRegistry::instance().isImplemented (n.moduleId))
                    out.push_back ({ n.phrase, n.moduleId, display });
            }

            std::stable_sort (out.begin(), out.end(),
                              [] (const Candidate& a, const Candidate& b)
                              { return a.needle.length() > b.needle.length(); });
            return out;
        }();
        return c;
    }

    bool containsWord (const juce::String& haystack, const char* word)
    {
        return haystack.contains (juce::String (" ") + word + " ");
    }
}

juce::String ModuleCommands::normalise (const juce::String& text)
{
    juce::String out;
    for (auto ch : text)
        if (juce::CharacterFunctions::isLetterOrDigit (ch))
            out << juce::String::charToString (ch).toLowerCase();
    return out;
}

ModuleCommand ModuleCommands::parse (const juce::String& lowercaseClause)
{
    ModuleCommand cmd;

    // Pad so whole-word checks are unambiguous at the ends of the clause.
    juce::String padded = " ";
    for (auto ch : lowercaseClause)
        padded << (juce::CharacterFunctions::isLetterOrDigit (ch)
                       ? juce::String::charToString (ch).toLowerCase()
                       : juce::String (" "));
    padded << " ";
    while (padded.contains ("  ")) padded = padded.replace ("  ", " ");

    // ---- verb ---------------------------------------------------------
    // "dial"/"set up"/"tune" is checked FIRST: "set up a de-esser for my voice"
    // on an existing module means re-tune it, not insert a second one.
    if (containsWord (padded, "dial") || containsWord (padded, "tune")
        || containsWord (padded, "configure") || containsWord (padded, "calibrate")
        || padded.contains (" set up ") || padded.contains (" set it up ")
        || padded.contains (" for my voice ") || padded.contains (" for my vocal "))
        cmd.verb = ModuleCommand::Verb::Dial;
    else if (containsWord (padded, "add") || containsWord (padded, "insert")
             || containsWord (padded, "put") || containsWord (padded, "give")
             || containsWord (padded, "need") || containsWord (padded, "want")
             || containsWord (padded, "throw") || containsWord (padded, "slap")
             || containsWord (padded, "use"))
        cmd.verb = ModuleCommand::Verb::Add;
    else if (containsWord (padded, "remove") || containsWord (padded, "delete")
             || containsWord (padded, "drop") || containsWord (padded, "kill")
             || containsWord (padded, "bin") || containsWord (padded, "ditch")
             || padded.contains (" take out ") || padded.contains (" get rid of ")
             || padded.contains (" take off "))
        cmd.verb = ModuleCommand::Verb::Remove;

    if (cmd.verb == ModuleCommand::Verb::None)
        return {};                               // not a module command

    // ---- which module -------------------------------------------------
    // Compare on the normalised (letters/digits only) form so "de-esser",
    // "de esser" and "deesser" all match the same entry.
    const auto flat = normalise (lowercaseClause);
    for (const auto& c : candidates())
        if (c.needle.isNotEmpty() && flat.contains (c.needle))
        {
            cmd.moduleId   = c.id;
            cmd.moduleName = c.name;
            return cmd;
        }

    return {};                                   // verb without a known module
}
} // namespace vf
