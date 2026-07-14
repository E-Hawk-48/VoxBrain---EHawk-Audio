#include "ChatEngine.h"
#include "../Parameters.h"
#include <vector>

namespace vf
{
using namespace vf::param;

namespace
{
// ---------------------------------------------------------------------------
//  Move: one parameter edit. Either a relative delta (in the parameter's
//  natural units, scaled by intensity) or an absolute setting (macros).
// ---------------------------------------------------------------------------
struct Move
{
    const char* param;
    float value;
    bool  absolute;      // false → value is a delta
};

// ---------------------------------------------------------------------------
//  Rule: a recognisable production concept.
//  invertible rules flip their deltas for "less …" / "too …" / "reduce …".
// ---------------------------------------------------------------------------
struct Rule
{
    std::vector<const char*> synonyms;
    std::vector<Move> moves;             // the "more of this" direction
    bool invertible;
    const char* moreText;
    const char* lessText;                // used when inverted
};

const std::vector<Rule>& rules()
{
    static const std::vector<Rule> r = {
        { { "dark", "darker", "dull" },
          { { eqAirGain, -2.5f, false }, { eqPresenceGain, -1.5f, false }, { verbDamp, 12.0f, false } },
          true, "Darkened the top end", "Opened the top end back up" },

        { { "bright", "brighter", "crisp", "crisper", "sparkle", "shiny" },
          { { eqAirGain, 2.5f, false }, { eqPresenceGain, 1.5f, false } },
          true, "Brightened it up", "Pulled some brightness" },

        { { "air", "airy", "airier", "breathy", "open" },
          { { eqAirGain, 2.5f, false }, { verbMix, 3.0f, false } },
          true, "Added air and a touch of space", "Reduced the air" },

        { { "warm", "warmer", "warmth", "body", "fuller", "thicker", "fat", "fatter" },
          { { eqLowShelfGain, 2.0f, false }, { satMix, 8.0f, false }, { eqAirGain, -0.5f, false } },
          true, "Warmed it up with low-end body and tape colour", "Thinned it out slightly" },

        { { "mud", "muddy", "boxy", "boomy" },
          { { eqMudGain, 2.0f, false } },
          true, "Allowed more low-mids", "Cleaned up the mud" },

        { { "harsh", "harshness", "piercing", "shrill", "brittle" },
          { { eqPresenceGain, 2.0f, false } },
          true, "Pushed the presence", "Smoothed the harshness" },

        { { "sibilant", "sibilance", "essy", "de ess", "deess" },
          { { deessThreshold, 4.0f, false } },
          true, "Relaxed the de-esser", "Tightened the de-esser" },

        { { "glue", "gel", "glued", "cohesive", "cohesion", "multiband", "tighten the mix" },
          { { mbandOn, 1.0f, true }, { mbandLowThresh, -3.0f, false },
            { mbandMidThresh, -3.0f, false }, { mbandHighThresh, -3.0f, false } },
          true, "Engaged multiband glue for a cohesive, consistent tone", "Backed the multiband off" },

        { { "resonance", "resonant", "ringing", "nasal", "honky", "boxiness" },
          { { dyneqOn, 1.0f, true }, { dyneqMidThresh, -4.0f, false },
            { dyneqLowThresh, -3.0f, false }, { dyneqRange, 1.5f, false } },
          true, "Tamed the resonance dynamically", "Loosened the dynamic EQ" },

        { { "smoother", "de harsh", "deharsh", "tame highs", "silky", "smooth top" },
          { { dyneqOn, 1.0f, true }, { dyneqHighThresh, -4.0f, false },
            { dyneqRange, 1.0f, false }, { eqAirGain, -0.5f, false } },
          true, "Smoothed the top end dynamically", "Let the top breathe again" },

        { { "autotune", "auto tune", "tune", "tuned", "pitch correction", "correction" },
          { { pitchOn, 1.0f, true }, { pitchSpeed, -35.0f, false }, { pitchAmount, 10.0f, false } },
          true, "Tightened the pitch correction", "Relaxed the pitch correction" },

        { { "hard tune", "hardtune", "t pain", "tpain", "robotic", "travis" },
          { { pitchOn, 1.0f, true }, { pitchSpeed, 0.0f, true }, { pitchAmount, 100.0f, true } },
          false, "Hard-tune engaged: instant snap, full correction", "" },

        { { "natural tune", "transparent tune", "subtle tune" },
          { { pitchOn, 1.0f, true }, { pitchSpeed, 120.0f, true }, { pitchAmount, 75.0f, true } },
          false, "Set transparent, natural-sounding correction", "" },

        { { "reverb", "verb", "wet", "wetter", "space", "spacey", "ambience", "ambient" },
          { { verbOn, 1.0f, true }, { verbMix, 6.0f, false } },
          true, "Added reverb", "Dried up the reverb" },

        // Reverb ALGORITHM switches (verbType: Room 0, Hall 1, Plate 2, Spring 3,
        // Cathedral 4, Shimmer 5, Bloom 6).
        { { "room", "small room", "roomy" },
          { { verbOn, 1.0f, true }, { verbType, 0.0f, true } },
          false, "Reverb type: Room — tight and short", "" },
        { { "hall", "concert hall" },
          { { verbOn, 1.0f, true }, { verbType, 1.0f, true }, { verbMix, 4.0f, false } },
          false, "Reverb type: Hall — lush and diffuse", "" },
        { { "plate", "plate reverb", "plate verb" },
          { { verbOn, 1.0f, true }, { verbType, 2.0f, true } },
          false, "Reverb type: Plate — bright and dense", "" },
        { { "spring", "spring reverb", "surf" },
          { { verbOn, 1.0f, true }, { verbType, 3.0f, true } },
          false, "Reverb type: Spring", "" },
        { { "cathedral", "church" },
          { { verbOn, 1.0f, true }, { verbType, 4.0f, true }, { verbSize, 15.0f, false }, { verbMix, 5.0f, false } },
          false, "Reverb type: Cathedral — huge and long", "" },
        { { "shimmer", "shimmering", "ethereal", "angelic" },
          { { verbOn, 1.0f, true }, { verbType, 5.0f, true }, { verbShimmer, 45.0f, false }, { verbMix, 5.0f, false } },
          false, "Shimmer reverb — octave-up sparkle", "" },
        { { "bloom", "swell", "blooming" },
          { { verbOn, 1.0f, true }, { verbType, 6.0f, true }, { verbMix, 5.0f, false } },
          false, "Bloom reverb — a swelling tail", "" },
        { { "freeze", "frozen", "infinite reverb", "hold reverb", "freeze reverb" },
          { { verbOn, 1.0f, true }, { verbFreeze, 1.0f, true } },
          false, "Reverb frozen — infinite tail", "" },
        { { "unfreeze", "release reverb", "release the reverb" },
          { { verbFreeze, 0.0f, true } },
          false, "Reverb released", "" },

        { { "dry", "drier", "dryer" },
          { { verbMix, -6.0f, false }, { delayMix, -4.0f, false } },
          true, "Dried it up", "Added space back" },

        { { "big", "bigger", "huge", "arena", "epic", "cinematic", "stadium" },
          { { verbOn, 1.0f, true }, { verbSize, 15.0f, false }, { verbMix, 5.0f, false }, { delayMix, 3.0f, false } },
          true, "Went bigger: larger space, more tail", "Brought it back in" },

        { { "intimate", "close", "closer", "in your face", "upfront", "tight" },
          { { verbMix, -5.0f, false }, { verbSize, -12.0f, false }, { eqLowShelfGain, 1.0f, false },
            { compThreshold, -3.0f, false } },
          true, "Pulled the vocal closer to the listener", "Gave it more distance" },

        { { "delay", "echo", "echoes", "slap", "slapback", "throw" },
          { { delayOn, 1.0f, true }, { delayMix, 5.0f, false } },
          true, "Added delay", "Reduced the delay" },

        { { "compression", "compressed", "compress", "squash", "squashed", "controlled", "consistent", "level" },
          { { compThreshold, -4.0f, false }, { compRatio, 1.0f, false }, { compMakeup, 2.0f, false } },
          true, "Compressed it harder", "Eased off the compression" },

        { { "punchy", "punchier", "punch", "aggressive", "slap harder", "hit harder", "energy" },
          { { compAttack, 4.0f, false }, { satMix, 6.0f, false }, { eqPresenceGain, 1.0f, false } },
          true, "More punch: transients through, extra drive", "Softened the punch" },

        { { "loud", "louder", "volume", "hot", "hotter" },
          { { limitGain, 2.0f, false } },
          true, "Pushed it louder into the limiter", "Backed the level off" },

        { { "quiet", "quieter", "softer" },
          { { limitGain, -2.0f, false } },
          true, "Brought the level down", "Brought the level up" },

        { { "bass", "low end", "lows", "bottom", "sub" },
          { { eqLowShelfGain, 2.0f, false } },
          true, "More low end", "Less low end" },

        { { "presence", "forward", "cut through", "cuts through", "clarity", "clear", "clearer", "intelligible" },
          { { eqPresenceGain, 2.0f, false } },
          true, "More presence — it'll cut through the beat", "Pulled it back in the mix" },

        { { "saturation", "saturated", "distortion", "distorted", "drive", "grit", "gritty", "dirty", "crunch", "analog", "tape" },
          { { satOn, 1.0f, true }, { satDrive, 10.0f, false }, { satMix, 10.0f, false } },
          true, "More saturation and grit", "Cleaned up the saturation" },

        { { "clean", "cleaner", "pristine", "pure" },
          { { satDrive, -10.0f, false }, { satMix, -10.0f, false } },
          true, "Cleaned it up", "Added colour back" },

        // Saturation MODEL switches (satType indices: Tube 0, Tape 1, Console 2,
        // Transformer 3, Germanium 4, Diode 5, Exciter 6, Lo-Fi 7).
        { { "tube", "valve" },
          { { satOn, 1.0f, true }, { satType, 0.0f, true }, { satMix, 5.0f, false } },
          false, "Switched saturation to Tube — warm even harmonics", "" },
        { { "tape machine", "reel to reel", "tape saturation" },
          { { satOn, 1.0f, true }, { satType, 1.0f, true } },
          false, "Switched to Tape saturation — smooth and musical", "" },
        { { "console", "desk", "neve", "ssl", "api" },
          { { satOn, 1.0f, true }, { satType, 2.0f, true } },
          false, "Switched to Console drive — clean and subtle", "" },
        { { "transformer", "iron" },
          { { satOn, 1.0f, true }, { satType, 3.0f, true } },
          false, "Switched to Transformer — thick low-mids", "" },
        { { "germanium", "fuzz", "fuzzy" },
          { { satOn, 1.0f, true }, { satType, 4.0f, true }, { satDrive, 12.0f, false } },
          false, "Germanium fuzz engaged", "" },
        { { "exciter", "enhancer", "sheen" },
          { { satOn, 1.0f, true }, { satType, 6.0f, true }, { satMix, 8.0f, false } },
          false, "Exciter on — generating high-end sparkle", "" },
        { { "cassette", "vinyl", "bitcrush", "crushed" },
          { { satOn, 1.0f, true }, { satType, 7.0f, true }, { satDrive, 15.0f, false } },
          false, "Lo-Fi crush engaged", "" },

        { { "vintage", "retro", "old school", "lofi", "lo fi", "nostalgic" },
          { { eqAirGain, -2.0f, false }, { eqHpfFreq, 40.0f, false }, { satDrive, 12.0f, false },
            { satMix, 12.0f, false }, { verbDamp, 15.0f, false } },
          true, "Vintage character: rolled the extremes, added tape colour", "Modernised it again" },

        { { "radio", "broadcast", "commercial", "polished", "billboard" },
          { { compThreshold, -4.0f, false }, { compRatio, 1.0f, false }, { compMakeup, 2.0f, false },
            { eqPresenceGain, 1.5f, false }, { eqHpfFreq, 15.0f, false }, { deessOn, 1.0f, true } },
          true, "Radio-ready: tighter dynamics, clean and present", "Loosened the radio polish" },

        { { "telephone", "phone", "megaphone", "walkie" },
          { { eqHpfFreq, 400.0f, true }, { eqAirGain, -6.0f, true }, { eqPresenceGain, 6.0f, true },
            { satDrive, 60.0f, true }, { satMix, 60.0f, true } },
          false, "Telephone effect: band-limited and driven", "" },

        { { "expensive", "professional", "pro", "studio quality", "high end", "premium" },
          { { eqAirGain, 1.5f, false }, { satMix, 5.0f, false }, { deessOn, 1.0f, true },
            { verbDamp, 8.0f, false }, { compMakeup, 1.0f, false } },
          true, "That expensive sheen: silk air, controlled ess, glue", "Less gloss" },

        { { "wide", "wider", "stereo", "spread" },
          { { verbWidth, 15.0f, false } },
          true, "Widened the space", "Narrowed the space" },

        { { "emotion", "emotional", "feel", "feeling", "sad", "moody" },
          { { verbMix, 4.0f, false }, { verbSize, 8.0f, false }, { delayOn, 1.0f, true },
            { delayMix, 3.0f, false }, { pitchSpeed, 25.0f, false } },
          true, "More emotion: space, echo, gentler correction", "Tightened the sentiment" },
    };
    return r;
}

// ---------------------------------------------------------------------------
//  Modifier scan: looks back a few words from a match for intensity/negation
// ---------------------------------------------------------------------------
struct Modifiers { float intensity = 1.0f; bool inverted = false; };

Modifiers scanModifiers (const juce::StringArray& words, int matchWordIndex)
{
    Modifiers m;
    const int from = juce::jmax (0, matchWordIndex - 3);
    for (int i = from; i <= matchWordIndex && i < words.size(); ++i)
    {
        const auto& w = words[i];
        if (w == "less" || w == "too" || w == "reduce" || w == "remove" || w == "no"
            || w == "without" || w == "lower" || w == "cut" || w == "kill" || w == "stop")
            m.inverted = true;
        else if (w == "slightly" || w == "bit" || w == "little" || w == "touch"
                 || w == "tad" || w == "subtle" || w == "hair")
            m.intensity = 0.5f;
        else if (w == "way" || w == "much" || w == "lot" || w == "really" || w == "super"
                 || w == "very" || w == "extremely" || w == "max" || w == "heavy")
            m.intensity = 1.7f;
    }
    return m;
}

bool moduleLocked (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
{
    if (const char* lock = vf::param::lockParamFor (paramId))
        if (auto* lv = apvts.getRawParameterValue (lock))
            return lv->load() > 0.5f;
    return false;
}

void applyMove (juce::AudioProcessorValueTreeState& apvts, const Move& mv,
                float intensity, bool invert)
{
    auto* param = apvts.getParameter (mv.param);
    if (param == nullptr || moduleLocked (apvts, mv.param))
        return;                       // locked module — AI Engineer leaves it alone

    float target;
    if (mv.absolute)
    {
        if (invert) return;              // absolutes don't invert
        target = mv.value;
    }
    else
    {
        const float current = apvts.getRawParameterValue (mv.param)->load();
        const float delta = mv.value * intensity * (invert ? -1.0f : 1.0f);
        target = current + delta;
    }

    param->beginChangeGesture();
    param->setValueNotifyingHost (param->convertTo0to1 (target));  // clamps to range
    param->endChangeGesture();
}
} // namespace

// ============================================================================
juce::String ChatEngine::handleMessage (const juce::String& message,
                                        juce::AudioProcessorValueTreeState& apvts)
{
    // Normalise: lowercase, punctuation → spaces
    juce::String norm = message.toLowerCase();
    juce::String cleaned;
    for (auto c : norm)
        cleaned << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c)
                                                                  : juce::String (" "));
    cleaned = " " + cleaned.trim().replace ("  ", " ") + " ";

    juce::StringArray words;
    words.addTokens (cleaned.trim(), " ", "");

    // "reset" macro
    if (cleaned.contains (" reset ") || cleaned.contains (" start over "))
    {
        for (auto* p : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                if (moduleLocked (apvts, rp->getParameterID()))
                    continue;   // keep locked modules (and their lock) intact
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (rp->getDefaultValue());
                rp->endChangeGesture();
            }
        return "Reset the chain to defaults (locked modules kept). Clean slate.";
    }

    // Match rules (longest synonyms first so "hard tune" beats "tune")
    struct Match { const Rule* rule; Modifiers mods; };
    std::vector<Match> matches;
    juce::String consumed = cleaned;

    // Collect all (rule, synonym) pairs and sort by synonym length descending
    std::vector<std::pair<const Rule*, const char*>> pairs;
    for (const auto& rule : rules())
        for (const auto* syn : rule.synonyms)
            pairs.push_back ({ &rule, syn });
    std::sort (pairs.begin(), pairs.end(),
               [] (const auto& a, const auto& b)
               { return juce::String (a.second).length() > juce::String (b.second).length(); });

    for (const auto& [rule, syn] : pairs)
    {
        const juce::String needle = " " + juce::String (syn) + " ";
        const int pos = consumed.indexOf (needle);
        if (pos < 0)
            continue;

        // Already matched this rule via a longer synonym?
        bool dup = false;
        for (const auto& m : matches) dup = dup || m.rule == rule;
        if (dup) continue;

        // Word index of the match (for modifier lookback)
        const int wordIdx = juce::StringArray::fromTokens (
                                consumed.substring (0, pos + 1).trim(), " ", "").size();
        matches.push_back ({ rule, scanModifiers (words, wordIdx) });

        // Blank out the matched phrase so shorter synonyms can't re-match it
        consumed = consumed.substring (0, pos) + " "
                 + consumed.substring (pos + needle.length());
    }

    if (matches.empty())
        return "I didn't catch a mixing move in that. Try things like: \"darker\", "
               "\"more air\", \"less autotune\", \"hard tune\", \"warmer\", \"punchier\", "
               "\"more reverb\", \"glue\", \"tame the resonance\", \"smoother\", "
               "\"vintage\", \"radio ready\", \"telephone\", or \"reset\".";

    // Apply and build the reply
    juce::StringArray replies;
    for (const auto& m : matches)
    {
        if (! m.rule->invertible && m.mods.inverted)
            continue;

        int applied = 0, lockedHits = 0;
        for (const auto& mv : m.rule->moves)
        {
            if (moduleLocked (apvts, mv.param)) { ++lockedHits; continue; }
            applyMove (apvts, mv, m.mods.intensity, m.mods.inverted);
            ++applied;
        }

        if (applied > 0)
            replies.add (m.mods.inverted ? m.rule->lessText : m.rule->moreText);
        else if (lockedHits > 0)
            replies.add ("that module's locked — unlock it first");
    }

    juce::String reply = replies.joinIntoString (". ");
    if (reply.isEmpty())
        reply = "Nothing to change there.";
    return reply + ".";
}
} // namespace vf
