#include "ChatEngine.h"
#include "../Parameters.h"
#include "../Brain/GenreProfiles.h"
#include "../Brain/ModuleIntelligence.h"
#include "ModuleCommands.h"
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

        // ---- Auto-tune character (new engine controls) -------------------
        { { "human", "humanize", "humanise", "natural vibrato", "preserve vibrato",
            "less robotic", "keep vibrato" },
          { { pitchOn, 1.0f, true }, { pitchHumanize, 35.0f, false } },
          true, "More human — keeping your natural vibrato and pitch movement",
          "More machine — tighter, less vibrato" },

        { { "robotic tune", "robot voice", "perfect pitch", "locked tune", "rigid tune" },
          { { pitchOn, 1.0f, true }, { pitchHumanize, 0.0f, true }, { pitchSpeed, 0.0f, true },
            { pitchAmount, 100.0f, true } },
          false, "Locked, machine-perfect tuning", "" },

        { { "deeper voice", "lower voice", "masculine", "manly", "monster voice", "demon voice" },
          { { pitchFormant, -3.0f, true } },
          false, "Lowered the formants — a deeper, bigger voice", "" },

        { { "higher voice", "chipmunk", "feminine voice", "younger voice", "helium", "lighter voice" },
          { { pitchFormant, 3.0f, true } },
          false, "Raised the formants — a brighter, lighter voice", "" },

        { { "neutral voice", "natural voice", "reset voice", "normal voice" },
          { { pitchFormant, 0.0f, true } },
          false, "Reset to neutral voice character", "" },

        { { "major key", "major scale" },
          { { pitchOn, 1.0f, true }, { pitchScale, 2.0f, true } },
          false, "Scale set to Major", "" },
        { { "minor key", "minor scale" },
          { { pitchOn, 1.0f, true }, { pitchScale, 3.0f, true } },
          false, "Scale set to Minor", "" },
        { { "harmonic minor" },
          { { pitchOn, 1.0f, true }, { pitchScale, 4.0f, true } },
          false, "Scale set to Harmonic Minor", "" },
        { { "pentatonic" },
          { { pitchOn, 1.0f, true }, { pitchScale, 9.0f, true } },
          false, "Scale set to Minor Pentatonic", "" },
        { { "blues scale", "bluesy tune" },
          { { pitchOn, 1.0f, true }, { pitchScale, 10.0f, true } },
          false, "Scale set to Blues", "" },

        // ---- more natural production phrases ------------------------------
        { { "thin", "thinner", "less body", "smaller voice" },
          { { eqLowShelfGain, -2.0f, false }, { satMix, -4.0f, false } },
          true, "Thinned the body", "Filled the body back in" },

        { { "pop out", "make it pop", "3d", "three d", "depth", "dimension", "out front" },
          { { eqPresenceGain, 1.5f, false }, { eqAirGain, 1.0f, false }, { satMix, 5.0f, false },
            { compThreshold, -2.0f, false } },
          true, "Pushed it out front with presence, air and drive", "Sat it back down" },

        { { "background", "behind", "further back", "tuck", "sit back" },
          { { verbMix, 4.0f, false }, { eqPresenceGain, -1.5f, false }, { limitGain, -1.5f, false } },
          true, "Tucked it back into the mix", "Brought it forward" },

        { { "de breath", "breaths", "less breath", "breathy noise" },
          { { gateOn, 1.0f, true }, { gateThreshold, 6.0f, false } },
          true, "Ducked the breaths with the gate", "Let the breaths through" },

        { { "double", "doubled", "thicken vocal", "stack", "wide double" },
          { { verbWidth, 12.0f, false }, { delayOn, 1.0f, true }, { delayTime, -180.0f, false },
            { delayMix, 4.0f, false } },
          true, "Faux-doubled for width and thickness", "Un-doubled" },

        // NOTE: genre one-shots used to live here. They now come from
        // Brain/GenreProfiles (data-driven, with sub-genres + aliases), matched
        // separately in handleMessage so "emo trap" beats "trap".
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
    const int from = juce::jmax (0, matchWordIndex - 4);   // look back up to 4 words
    for (int i = from; i <= matchWordIndex && i < words.size(); ++i)
    {
        const auto& w = words[i];
        if (w == "less" || w == "too" || w == "reduce" || w == "remove" || w == "no"
            || w == "without" || w == "lower" || w == "cut" || w == "kill" || w == "stop"
            || w == "drop" || w == "ease" || w == "minus" || w == "pull" || w == "dial")
            m.inverted = true;
        else if (w == "slightly" || w == "bit" || w == "little" || w == "touch"
                 || w == "tad" || w == "subtle" || w == "hair" || w == "gentle"
                 || w == "gentler" || w == "softer" || w == "lightly")
            m.intensity = 0.5f;
        else if (w == "way" || w == "much" || w == "lot" || w == "really" || w == "super"
                 || w == "very" || w == "extremely" || w == "max" || w == "heavy"
                 || w == "tons" || w == "hella" || w == "mad")
            m.intensity = 1.7f;
        else if (w == "better" || w == "more" || w == "stronger" || w == "tighter"
                 || w == "harder" || w == "cleaner" || w == "crisper" || w == "extra"
                 || w == "boost" || w == "add")
            m.intensity = juce::jmax (m.intensity, 1.35f);   // "better/more X" → push harder
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
    return handleMessage (message, apvts, Context{});
}

juce::String ChatEngine::handleMessage (const juce::String& message,
                                        juce::AudioProcessorValueTreeState& apvts,
                                        Context ctx)
{
    const juce::String lower = message.toLowerCase();

    // "reset" shortcut
    if ((" " + lower + " ").contains (" reset ") || lower.contains ("start over"))
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

    // ---- Full-sentence parsing -------------------------------------------
    //  Split the request into independent clauses so each modifier scopes only
    //  to its own target. e.g. "make it more emo-trap, less reverb, better
    //  autotune and brighter" → 4 clauses, each interpreted on its own.
    juce::String segmented = lower;
    for (auto* c : { ",", ";", ".", " and ", " then ", " but ", " plus ", " also ", " with ", " while " })
        segmented = segmented.replace (c, " | ");
    juce::StringArray clauses;
    clauses.addTokens (segmented, "|", "");

    // (rule, synonym) pairs, longest-synonym-first ("hard tune" beats "tune").
    std::vector<std::pair<const Rule*, const char*>> pairs;
    for (const auto& rule : rules())
        for (const auto* syn : rule.synonyms)
            pairs.push_back ({ &rule, syn });
    std::sort (pairs.begin(), pairs.end(),
               [] (const auto& a, const auto& b)
               { return juce::String (a.second).length() > juce::String (b.second).length(); });

    struct Match { const Rule* rule; Modifiers mods; };
    std::vector<Match> matches;

    // ---- Module rack commands --------------------------------------------
    //  "add a de-esser", "remove the chorus", "set up the compressor for my
    //  voice". Insert/dial use the SAME knowledge base LEARN uses, so a module
    //  added by request arrives already tuned to the analysed vocal rather than
    //  at factory defaults. Parsing lives in ModuleCommands (pure + tested).
    juce::StringArray moduleReplies;
    bool rackChanged = false;
    if (ctx.rack != nullptr)
    {
        for (const auto& clauseRaw : clauses)
        {
            const auto cmd = ModuleCommands::parse (clauseRaw.toLowerCase());
            if (! cmd.isValid()) continue;

            const bool haveAnalysis = ctx.snapshot != nullptr && ctx.snapshot->valid;

            // Is it already in the rack?
            juce::String existingId;
            for (const auto& n : ctx.rack->snapshot())
                if (n.typeId == cmd.moduleId) { existingId = n.instanceId; break; }

            auto dialInto = [&] (const juce::String& instanceId) -> juce::String
            {
                if (! haveAnalysis)
                    return " (run LEARN and I can tune it to your voice)";
                const auto rec = ModuleIntelligence::dialFor (cmd.moduleId, *ctx.snapshot);
                if (rec.settings.empty()) return {};
                if (auto* m = ctx.rack->find (instanceId))
                    for (const auto& st : rec.settings)
                        m->setValue (st.paramId, st.value);
                return rec.settingsSummary.isNotEmpty() ? " — set to " + rec.settingsSummary
                                                        : juce::String();
            };

            switch (cmd.verb)
            {
                case ModuleCommand::Verb::Add:
                {
                    if (existingId.isNotEmpty())
                    {
                        moduleReplies.add (cmd.moduleName + " is already in the chain"
                                           + dialInto (existingId));
                        rackChanged = true;
                        break;
                    }
                    const auto iid = ctx.rack->addModule (cmd.moduleId);
                    if (iid.isEmpty()) { moduleReplies.add ("I couldn't add " + cmd.moduleName); break; }
                    moduleReplies.add ("Added " + cmd.moduleName + dialInto (iid));
                    rackChanged = true;
                    break;
                }
                case ModuleCommand::Verb::Remove:
                {
                    if (existingId.isEmpty())
                        moduleReplies.add (cmd.moduleName + " isn't in the chain");
                    else
                    {
                        // Respect the lock the same way every other AI move does.
                        bool locked = false;
                        for (const auto& n : ctx.rack->snapshot())
                            if (n.instanceId == existingId) locked = n.lock;
                        if (locked)
                            moduleReplies.add (cmd.moduleName + " is locked — unlock it first");
                        else
                        {
                            ctx.rack->removeModule (existingId);
                            moduleReplies.add ("Removed " + cmd.moduleName);
                            rackChanged = true;
                        }
                    }
                    break;
                }
                case ModuleCommand::Verb::Dial:
                {
                    if (existingId.isEmpty())
                    {
                        const auto iid = ctx.rack->addModule (cmd.moduleId);
                        if (iid.isEmpty()) { moduleReplies.add ("I couldn't add " + cmd.moduleName); break; }
                        moduleReplies.add ("Added " + cmd.moduleName + dialInto (iid));
                    }
                    else
                    {
                        moduleReplies.add ("Dialled " + cmd.moduleName + " in for your voice"
                                           + dialInto (existingId));
                    }
                    rackChanged = true;
                    break;
                }
                default: break;
            }
        }

        if (rackChanged && ctx.onRackChanged)
            ctx.onRackChanged();
    }

    // ---- Genre / sub-genre one-shots -------------------------------------
    //  Checked FIRST and per clause, using longest-alias matching, so "emo trap"
    //  resolves to Emo Trap rather than Trap, and a compound request like
    //  "make it emo trap but less reverb" applies the genre and THEN lets the
    //  vocabulary rules adjust it. Genre targets are absolute, so applying them
    //  before the adjustments is what makes that ordering work.
    juce::StringArray genreReplies;
    const GenreProfile* appliedGenre = nullptr;
    for (const auto& clauseRaw : clauses)
    {
        const auto* g = GenreProfiles::match (clauseRaw.toLowerCase());
        if (g == nullptr || g == appliedGenre) continue;
        appliedGenre = g;

        int applied = 0, lockedHits = 0;
        for (const auto& t : g->targets)
        {
            if (moduleLocked (apvts, t.paramId)) { ++lockedHits; continue; }
            applyMove (apvts, { t.paramId, t.value, true }, 1.0f, false);
            ++applied;
        }

        if (applied > 0)
        {
            juce::String line = g->name + " (" + g->family + "): " + g->sound;
            if (lockedHits > 0)
                line += " (left your locked modules alone)";
            genreReplies.add (line);
        }
        else if (lockedHits > 0)
        {
            genreReplies.add ("Everything " + g->name + " would touch is locked — unlock a module first");
        }
    }

    for (const auto& clauseRaw : clauses)
    {
        juce::String cl;                          // normalise this clause
        for (auto ch : clauseRaw)
            cl << (juce::CharacterFunctions::isLetterOrDigit (ch) ? juce::String::charToString (ch)
                                                                  : juce::String (" "));
        cl = " " + cl.trim().replace ("  ", " ") + " ";
        if (cl.trim().isEmpty()) continue;

        juce::StringArray words; words.addTokens (cl.trim(), " ", "");
        juce::String consumed = cl;

        for (const auto& [rule, syn] : pairs)
        {
            const juce::String needle = " " + juce::String (syn) + " ";
            const int pos = consumed.indexOf (needle);
            if (pos < 0) continue;

            bool dup = false;                     // never apply the same rule twice
            for (const auto& mm : matches) dup = dup || mm.rule == rule;
            if (dup) continue;

            const int wordIdx = juce::StringArray::fromTokens (
                                    consumed.substring (0, pos + 1).trim(), " ", "").size();
            matches.push_back ({ rule, scanModifiers (words, wordIdx) });   // clause-scoped
            consumed = consumed.substring (0, pos) + " " + consumed.substring (pos + needle.length());
        }
    }

    if (matches.empty() && genreReplies.isEmpty() && moduleReplies.isEmpty())
        return "I didn't catch a mixing move in that. Try things like: \"darker\", "
               "\"more air\", \"punchier\", \"warmer\", \"make it pop\", \"tuck it back\", "
               "\"more human\", \"deeper voice\", \"minor key\", \"hard tune\", "
               "\"more reverb\", \"glue\", \"tame the resonance\", \"double it\", "
               "\"vintage\", \"radio ready\" — or name a genre or sub-genre such as "
             + GenreProfiles::examplesForHelp (8)
             + " (I know " + juce::String ((int) GenreProfiles::all().size())
             + " across " + juce::String (GenreProfiles::families().size())
             + " families) — or say \"reset\".";

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

    // Genre first (it sets the foundation), then module changes, then the
    // parameter adjustments made on top.
    juce::StringArray all;
    all.addArray (genreReplies);
    all.addArray (moduleReplies);
    all.addArray (replies);

    juce::String reply = all.joinIntoString (". ");
    if (reply.isEmpty())
        reply = "Nothing to change there.";
    return reply + ".";
}
} // namespace vf
