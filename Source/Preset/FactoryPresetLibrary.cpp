#include "FactoryPresetLibrary.h"

namespace vf
{
namespace
{
    using V = std::map<juce::String, float>;

    V merged (V base, const V& over)
    {
        for (const auto& [k, v] : over) base[k] = v;
        return base;
    }

    // Sensible baseline vocal chain shared by every factory preset.
    V defaultBase()
    {
        return {
            { "eq_on", 1 }, { "eq_hpf_freq", 80 },
            { "deess_on", 1 }, { "deess_threshold", -28 }, { "deess_freq", 6500 },
            { "comp_on", 1 }, { "comp_threshold", -18 }, { "comp_ratio", 3 },
            { "comp_attack", 8 }, { "comp_release", 120 }, { "comp_makeup", 3 }, { "comp_mix", 100 },
            { "sat_on", 1 }, { "sat_type", 1 }, { "sat_drive", 20 }, { "sat_mix", 20 },
            { "verb_on", 1 }, { "verb_type", 1 }, { "verb_size", 45 }, { "verb_mix", 12 }, { "verb_width", 100 },
            { "limit_on", 1 }, { "limit_ceiling", -1 }, { "limit_gain", 2 }
        };
    }
}

std::vector<Preset> FactoryPresetLibrary::build()
{
    std::vector<Preset> out;

    auto mk = [&out] (juce::String name, juce::String genre, juce::String style, juce::String mood,
                      int energy, juce::StringArray tags, juce::StringArray cats, V vals, float aiConf = 0.0f)
    {
        Preset p;
        p.meta.presetId       = juce::Uuid().toString();
        p.meta.name           = name;
        p.meta.creatorName    = "VoxBrain";
        p.meta.creatorId      = "voxbrain_official";
        p.meta.version        = "1.0.0";
        p.meta.minAppVersion  = "0.2.0";
        p.meta.genre          = genre;
        p.meta.vocalStyle     = style;
        p.meta.mood           = mood;
        p.meta.energy         = energy;
        p.meta.vocalType      = "Lead";
        p.meta.tags           = tags;
        p.meta.categories     = cats;
        p.meta.source         = "factory";
        p.meta.official       = true;
        p.meta.verifiedCreator = true;
        p.meta.aiConfidence   = aiConf;
        p.meta.cpuEstimate    = 5.0f + (float) vals.size() * 0.18f;
        p.meta.latencyMs      = 32.0f;
        p.meta.recQualityRec  = "16-bit/44.1 kHz or better, quiet room";
        p.meta.description    = genre + " vocal - " + mood.toLowerCase() + ", " + style.toLowerCase() + ".";
        p.values              = std::move (vals);
        p.seal();
        out.push_back (std::move (p));
    };

    // Emit 5 tasteful variations of a genre from a base value-set.
    auto family = [&] (juce::String genre, juce::String style, juce::StringArray cats,
                       juce::StringArray baseTags, V base)
    {
        mk (genre + " - Clean", genre, style, "Balanced", 5,
            baseTags, cats, base);

        mk (genre + " - Warm & Intimate", genre, style, "Warm", 4,
            baseTags, cats,
            merged (base, { { "eq_lowshelf_gain", 2.5f }, { "sat_type", 0 }, { "sat_mix", 28 },
                            { "eq_air_gain", -1 }, { "verb_size", 40 } }));

        mk (genre + " - Bright & Airy", genre, style, "Bright", 6,
            baseTags, cats,
            merged (base, { { "eq_air_gain", 3 }, { "eq_presence_gain", 2.5f }, { "verb_type", 2 },
                            { "deess_threshold", -32 } }));

        mk (genre + " - Hard & Aggressive", genre, style, "Aggressive", 8,
            baseTags, cats,
            merged (base, { { "comp_threshold", -24 }, { "comp_ratio", 5 }, { "sat_drive", 45 },
                            { "sat_mix", 40 }, { "pitch_on", 1 }, { "pitch_speed", 0 }, { "pitch_amount", 100 },
                            { "limit_gain", 4 } }));

        mk (genre + " - Wide & Lush", genre, style, "Spacious", 6,
            baseTags, cats,
            merged (base, { { "verb_mix", 20 }, { "verb_size", 62 }, { "verb_width", 100 },
                            { "delay_on", 1 }, { "delay_time_ms", 320 }, { "delay_mix", 10 } }));
    };

    // ---- Spoken / broadcast family (clarity-first) ----------------------
    V spoken = merged (defaultBase(), { { "eq_hpf_freq", 90 }, { "eq_presence_gain", 2 },
                                        { "comp_ratio", 3.5f }, { "verb_mix", 3 }, { "sat_mix", 8 },
                                        { "deess_threshold", -30 } });
    family ("Clean Vocals",     "Natural",   { "Clean", "Utility" },       { "clean", "natural", "corrective" }, spoken);
    family ("Transparent Mix",  "Neutral",   { "Clean", "Utility" },       { "transparent", "neutral" },        spoken);
    family ("Broadcast",        "Announcer", { "Broadcast", "Spoken" },    { "broadcast", "radio", "clear" },   spoken);
    family ("Podcast",          "Spoken",    { "Podcast", "Spoken" },      { "podcast", "voice", "clean" },     spoken);
    family ("Audiobook",        "Narration", { "Audiobook", "Spoken" },    { "audiobook", "narration" },        spoken);
    family ("YouTube",          "Creator",   { "YouTube", "Spoken" },      { "youtube", "creator", "clear" },   spoken);
    family ("Livestream",       "Creator",   { "Livestream", "Spoken" },   { "livestream", "stream", "mic" },   spoken);
    family ("Voice Acting",     "Character",  { "Voice Acting", "Spoken" }, { "voice acting", "character" },     spoken);

    // ---- Modern urban ---------------------------------------------------
    V pop = merged (defaultBase(), { { "eq_air_gain", 3 }, { "eq_presence_gain", 2 }, { "verb_type", 2 },
                                     { "verb_mix", 14 }, { "pitch_on", 1 }, { "pitch_speed", 30 }, { "pitch_amount", 90 } });
    family ("Pop", "Melodic", { "Pop" }, { "pop", "bright", "radio" }, pop);

    V hiphop = merged (defaultBase(), { { "eq_hpf_freq", 90 }, { "eq_presence_gain", 3 }, { "comp_threshold", -20 },
                                        { "comp_ratio", 4 }, { "sat_mix", 25 }, { "verb_type", 0 }, { "verb_mix", 7 },
                                        { "delay_on", 1 }, { "delay_time_ms", 110 }, { "delay_mix", 9 } });
    family ("Hip-Hop", "Rap", { "Hip-Hop" }, { "rap", "present", "tight" }, hiphop);

    V trap = merged (hiphop, { { "pitch_on", 1 }, { "pitch_scale", 3 }, { "pitch_speed", 15 }, { "pitch_amount", 95 },
                               { "verb_mix", 9 }, { "eq_air_gain", 2 } });
    family ("Trap", "Melodic Rap", { "Trap", "Hip-Hop" }, { "trap", "autotune", "dark" }, trap);

    V emotrap = merged (trap, { { "eq_lowshelf_gain", 1.5f }, { "verb_type", 1 }, { "verb_size", 60 }, { "verb_mix", 16 },
                                { "delay_mix", 12 }, { "sat_type", 1 } });
    family ("Emo Trap", "Melodic Rap", { "Emo Trap", "Trap" }, { "emo", "sad", "autotune", "reverb" }, emotrap);

    V melodicrap = merged (trap, { { "pitch_amount", 85 }, { "pitch_humanize", 30 }, { "verb_mix", 13 } });
    family ("Melodic Rap", "Melodic Rap", { "Melodic Rap", "Hip-Hop" }, { "melodic", "rap", "sing" }, melodicrap);

    V hyperpop = merged (pop, { { "pitch_on", 1 }, { "pitch_speed", 0 }, { "pitch_amount", 100 }, { "sat_type", 7 },
                                { "sat_drive", 40 }, { "sat_mix", 35 }, { "eq_air_gain", 4 }, { "verb_type", 5 },
                                { "verb_shimmer", 40 } });
    family ("Hyperpop", "Hard Tuned", { "Hyperpop", "Pop" }, { "hyperpop", "hardtune", "bright", "glitch" }, hyperpop);

    // ---- Soulful / warm -------------------------------------------------
    V rnb = merged (defaultBase(), { { "eq_lowshelf_gain", 2 }, { "eq_air_gain", 2 }, { "sat_type", 1 }, { "sat_mix", 22 },
                                     { "pitch_on", 1 }, { "pitch_speed", 45 }, { "pitch_amount", 70 }, { "pitch_humanize", 45 },
                                     { "verb_type", 1 }, { "verb_size", 62 }, { "verb_mix", 16 } });
    family ("R&B", "Smooth", { "R&B" }, { "rnb", "smooth", "warm", "wide" }, rnb);
    family ("Soul", "Emotive", { "Soul", "R&B" }, { "soul", "warm", "vintage" },
            merged (rnb, { { "sat_type", 0 }, { "sat_mix", 26 }, { "verb_type", 2 } }));
    family ("Gospel", "Powerful", { "Gospel" }, { "gospel", "choir", "big", "wide" },
            merged (rnb, { { "verb_size", 72 }, { "verb_mix", 22 }, { "eq_air_gain", 2.5f }, { "comp_threshold", -18 } }));

    // ---- Band / organic -------------------------------------------------
    V rock = merged (defaultBase(), { { "eq_presence_gain", 3 }, { "comp_threshold", -20 }, { "comp_ratio", 4 },
                                      { "sat_type", 2 }, { "sat_drive", 40 }, { "sat_mix", 35 }, { "verb_type", 0 },
                                      { "verb_mix", 8 } });
    family ("Rock", "Belted", { "Rock" }, { "rock", "driven", "raw" }, rock);
    family ("Alternative", "Indie", { "Alternative", "Rock" }, { "alt", "indie", "textured" },
            merged (rock, { { "sat_mix", 25 }, { "verb_type", 2 }, { "verb_mix", 12 } }));
    family ("Indie", "Intimate", { "Indie" }, { "indie", "intimate", "lofi" },
            merged (rock, { { "sat_type", 1 }, { "sat_mix", 20 }, { "eq_air_gain", 1 }, { "verb_mix", 14 } }));
    family ("Country", "Honest", { "Country" }, { "country", "natural", "warm" },
            merged (defaultBase(), { { "eq_presence_gain", 2 }, { "pitch_on", 1 }, { "pitch_speed", 80 },
                                     { "pitch_amount", 60 }, { "pitch_humanize", 60 }, { "verb_type", 2 }, { "verb_mix", 12 } }));
    family ("Acoustic", "Delicate", { "Acoustic" }, { "acoustic", "natural", "close" },
            merged (defaultBase(), { { "sat_mix", 12 }, { "verb_mix", 10 }, { "eq_air_gain", 1.5f } }));
    family ("Metal", "Screamed", { "Metal", "Rock" }, { "metal", "aggressive", "gritty" },
            merged (rock, { { "comp_threshold", -24 }, { "comp_ratio", 6 }, { "sat_drive", 55 }, { "sat_mix", 45 } }));

    // ---- Electronic / creative -----------------------------------------
    V edm = merged (pop, { { "sat_type", 6 }, { "sat_mix", 25 }, { "verb_type", 2 }, { "verb_mix", 16 },
                           { "delay_on", 1 }, { "delay_time_ms", 375 }, { "delay_mix", 12 } });
    family ("EDM", "Anthemic", { "EDM" }, { "edm", "bright", "wide", "sidechain" }, edm);
    family ("Lo-Fi", "Hazy", { "Lo-Fi" }, { "lofi", "vintage", "warm", "tape" },
            merged (defaultBase(), { { "eq_air_gain", -3 }, { "eq_hpf_freq", 60 }, { "sat_type", 7 }, { "sat_drive", 25 },
                                     { "sat_mix", 30 }, { "verb_type", 1 }, { "verb_mix", 15 }, { "verb_damp", 65 } }));
    family ("Cinematic", "Epic", { "Cinematic" }, { "cinematic", "epic", "huge", "wide" },
            merged (defaultBase(), { { "verb_type", 4 }, { "verb_size", 85 }, { "verb_mix", 26 }, { "verb_predelay", 40 },
                                     { "delay_on", 1 }, { "delay_mix", 8 }, { "eq_air_gain", 2 } }));

    return out;
}

std::vector<FactoryPresetLibrary::Collection> FactoryPresetLibrary::officialCollections()
{
    return {
        { "Modern Pop Essentials", "Radio-ready pop vocal chains.",
          { "Pop - Clean", "Pop - Bright & Airy", "Pop - Wide & Lush", "Hyperpop - Clean" } },
        { "Emo Trap Essentials", "Dark, drenched, auto-tuned.",
          { "Emo Trap - Clean", "Emo Trap - Wide & Lush", "Trap - Hard & Aggressive", "Melodic Rap - Clean" } },
        { "Hip-Hop Essentials", "Up-front, punchy rap vocals.",
          { "Hip-Hop - Clean", "Hip-Hop - Hard & Aggressive", "Trap - Clean", "Melodic Rap - Warm & Intimate" } },
        { "Alternative Rock Vocals", "Raw, driven, textured.",
          { "Rock - Clean", "Rock - Hard & Aggressive", "Alternative - Clean", "Indie - Warm & Intimate" } },
        { "Podcast Pro", "Clear, consistent spoken word.",
          { "Podcast - Clean", "Podcast - Warm & Intimate", "Broadcast - Clean", "Audiobook - Clean" } },
        { "Livestream Creator", "Set-and-forget mic polish.",
          { "Livestream - Clean", "YouTube - Clean", "YouTube - Bright & Airy" } },
        { "Radio Ready", "Loud, polished, broadcast-safe.",
          { "Pop - Clean", "Broadcast - Clean", "Pop - Hard & Aggressive" } },
        { "Vintage Analog", "Warm tape and tube colour.",
          { "Soul - Warm & Intimate", "Lo-Fi - Clean", "R&B - Warm & Intimate" } },
        { "Dream Pop", "Lush, wide, ethereal.",
          { "Pop - Wide & Lush", "Cinematic - Clean", "Hyperpop - Wide & Lush" } },
        { "Vocal FX Pack", "Creative, stylised effects.",
          { "Hyperpop - Hard & Aggressive", "Cinematic - Wide & Lush", "EDM - Wide & Lush" } }
    };
}
} // namespace vf
