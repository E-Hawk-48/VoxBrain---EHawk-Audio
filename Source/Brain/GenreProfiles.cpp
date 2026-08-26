#include "GenreProfiles.h"
#include "../ParameterIDs.h"

namespace vf
{
using namespace vf::param;

namespace
{
    // Choice indices used below (see Parameters.cpp for the authoritative lists):
    //   sat_type  : 0 Tube, 1 Tape, 2 Console, 3 Transformer, 4 Germanium,
    //               5 Diode, 6 Exciter, 7 Lo-Fi
    //   verb_type : 0 Room, 1 Hall, 2 Plate, 3 Spring, 4 Cathedral,
    //               5 Shimmer, 6 Bloom
    //   pitch_scale: 0 Auto, 1 Chromatic, 2 Major, 3 Minor, …
    constexpr float SAT_TUBE = 0, SAT_TAPE = 1, SAT_CONSOLE = 2, SAT_TRANSFORMER = 3;
    constexpr float SAT_GERMANIUM = 4, SAT_DIODE = 5, SAT_LOFI = 7;
    constexpr float VERB_ROOM = 0, VERB_HALL = 1, VERB_PLATE = 2, VERB_SPRING = 3;
    constexpr float VERB_CATHEDRAL = 4, VERB_SHIMMER = 5, VERB_BLOOM = 6;
    constexpr float SCALE_MINOR = 3, SCALE_MAJOR = 2;

    std::vector<GenreProfile> build()
    {
        std::vector<GenreProfile> g;
        auto add = [&g] (const char* id, const char* name, const char* family,
                         juce::StringArray aliases, const char* sound,
                         std::vector<GenreTarget> targets)
        {
            g.push_back ({ id, name, family, std::move (aliases), sound, std::move (targets) });
        };

        // ================================================================
        //  HIP-HOP and its sub-genres
        // ================================================================
        add ("modern_rap", "Modern Rap", "Hip-Hop",
             { "rap", "hip hop", "hiphop", "hip-hop", "rap vocal" },
             "Tight, present and up-front with a short slap throw.",
             { { eqOn, 1 }, { eqHpfFreq, 95 }, { eqMudGain, -2 }, { eqPresenceGain, 3.5f }, { eqAirGain, 2 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 4 }, { compAttack, 6 }, { compRelease, 90 },
               { deessOn, 1 }, { deessFreq, 6800 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 30 }, { satMix, 25 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 20 }, { pitchAmount, 90 }, { pitchHumanize, 10 },
               { delayOn, 1 }, { delayTime, 180 }, { delayFeedback, 18 }, { delayMix, 10 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 35 }, { verbMix, 8 } });

        add ("trap", "Trap", "Hip-Hop",
             { "trap", "trap vocal" },
             "Hard-tuned and forward, with a tight room and controlled low end.",
             { { eqOn, 1 }, { eqHpfFreq, 100 }, { eqMudGain, -2.5f }, { eqPresenceGain, 4 }, { eqAirGain, 2.5f },
               { compOn, 1 }, { compThreshold, -21 }, { compRatio, 4 }, { compAttack, 5 }, { compRelease, 80 },
               { deessOn, 1 }, { deessFreq, 7000 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 35 }, { satMix, 28 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 10 }, { pitchAmount, 95 }, { pitchHardTune, 85 }, { pitchHumanize, 5 },
               { delayOn, 1 }, { delayTime, 160 }, { delayFeedback, 20 }, { delayMix, 11 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 30 }, { verbMix, 8 }, { verbDuck, 35 } });

        add ("emo_trap", "Emo Trap", "Hip-Hop",
             { "emo trap", "emo-trap", "emotrap", "sad trap", "emo rap", "sadboy", "sad boy" },
             "Washed-out and melancholy: hard tune, wide plate/hall bloom, warm tape "
             "saturation and a soft top so it feels hazy rather than crisp.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqLowShelfGain, 1.5f }, { eqMudGain, -1.5f },
               { eqPresenceGain, 1.5f }, { eqAirGain, 1 },
               { compOn, 1 }, { compThreshold, -22 }, { compRatio, 4 }, { compAttack, 8 }, { compRelease, 140 },
               { deessOn, 1 }, { deessFreq, 6500 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satDrive, 40 }, { satTone, 40 }, { satMix, 40 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 5 }, { pitchAmount, 100 }, { pitchHardTune, 90 }, { pitchHumanize, 0 },
               { delayOn, 1 }, { delayTime, 320 }, { delayFeedback, 32 }, { delayMix, 20 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 65 }, { verbDecay, 62 },
               { verbDamp, 62 }, { verbWidth, 100 }, { verbMix, 26 }, { verbDuck, 30 } });

        add ("drill", "Drill", "Hip-Hop",
             { "drill" },
             "Dark, tight and hard-tuned with the top end pulled back.",
             { { eqOn, 1 }, { eqHpfFreq, 100 }, { eqPresenceGain, 2 }, { eqAirGain, -2 },
               { compOn, 1 }, { compThreshold, -22 }, { compRatio, 4.5f }, { compAttack, 4 }, { compRelease, 80 },
               { deessOn, 1 }, { satOn, 1 }, { satType, SAT_CONSOLE }, { satDrive, 30 }, { satMix, 25 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 0 }, { pitchAmount, 100 }, { pitchHardTune, 100 }, { pitchHumanize, 0 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 28 }, { verbMix, 6 }, { verbDamp, 70 } });

        add ("uk_drill", "UK Drill", "Hip-Hop",
             { "uk drill", "london drill", "brooklyn drill", "ny drill" },
             "Drier and darker than US drill — almost no tail, very forward diction.",
             { { eqOn, 1 }, { eqHpfFreq, 110 }, { eqMudGain, -3 }, { eqPresenceGain, 3 }, { eqAirGain, -2.5f },
               { compOn, 1 }, { compThreshold, -23 }, { compRatio, 5 }, { compAttack, 3 }, { compRelease, 70 },
               { deessOn, 1 }, { satOn, 1 }, { satType, SAT_CONSOLE }, { satDrive, 35 }, { satMix, 30 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 0 }, { pitchAmount, 100 }, { pitchHardTune, 100 },
               { delayOn, 0 }, { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 22 }, { verbMix, 4 }, { verbDamp, 75 } });

        add ("plugg", "Plugg", "Hip-Hop",
             { "plugg", "pluggnb", "plugg vocal" },
             "Soft, hazy and heavily washed — gentle tune, dreamy wide tail.",
             { { eqOn, 1 }, { eqHpfFreq, 85 }, { eqLowShelfGain, 2 }, { eqPresenceGain, 1 }, { eqAirGain, 1.5f },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 3 }, { compRelease, 160 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satDrive, 35 }, { satTone, 42 }, { satMix, 38 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 12 }, { pitchAmount, 92 }, { pitchHumanize, 8 },
               { delayOn, 1 }, { delayTime, 280 }, { delayFeedback, 30 }, { delayMix, 18 },
               { verbOn, 1 }, { verbType, VERB_BLOOM }, { verbSize, 70 }, { verbDecay, 65 }, { verbWidth, 100 }, { verbMix, 28 } });

        add ("rage", "Rage", "Hip-Hop",
             { "rage", "rage vocal", "opium" },
             "Distorted and aggressive — driven saturation, hard tune, in-your-face.",
             { { eqOn, 1 }, { eqHpfFreq, 120 }, { eqMudGain, -3 }, { eqPresenceGain, 4.5f }, { eqAirGain, 2 },
               { compOn, 1 }, { compThreshold, -24 }, { compRatio, 6 }, { compAttack, 3 }, { compRelease, 70 },
               { deessOn, 1 }, { satOn, 1 }, { satType, SAT_DIODE }, { satDrive, 65 }, { satMix, 55 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 0 }, { pitchAmount, 100 }, { pitchHardTune, 100 },
               { delayOn, 1 }, { delayTime, 200 }, { delayMix, 12 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 40 }, { verbMix, 10 } });

        add ("phonk", "Phonk", "Hip-Hop",
             { "phonk", "drift phonk", "memphis" },
             "Lo-fi, gritty and cassette-warped with a dark tail.",
             { { eqOn, 1 }, { eqHpfFreq, 120 }, { eqMudGain, -2 }, { eqPresenceGain, 2 }, { eqAirGain, -3 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 4 },
               { satOn, 1 }, { satType, SAT_LOFI }, { satDrive, 45 }, { satTone, 35 }, { satMix, 55 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 25 }, { pitchAmount, 80 },
               { delayOn, 1 }, { delayTime, 250 }, { delayFeedback, 28 }, { delayMix, 14 },
               { verbOn, 1 }, { verbType, VERB_SPRING }, { verbSize, 45 }, { verbDamp, 75 }, { verbMix, 16 } });

        add ("boom_bap", "Boom Bap", "Hip-Hop",
             { "boom bap", "boombap", "90s hip hop", "old school rap", "golden era" },
             "Dry, punchy and mid-forward like a 90s SP-1200 record — barely any tail.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqMudGain, -1 }, { eqPresenceGain, 3 }, { eqAirGain, -1 },
               { compOn, 1 }, { compThreshold, -18 }, { compRatio, 4 }, { compAttack, 8 }, { compRelease, 110 },
               { satOn, 1 }, { satType, SAT_TRANSFORMER }, { satDrive, 35 }, { satMix, 35 },
               { pitchOn, 0 },
               { delayOn, 0 }, { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 20 }, { verbMix, 4 } });

        add ("cloud_rap", "Cloud Rap", "Hip-Hop",
             { "cloud rap", "cloudrap", "ethereal rap" },
             "Floating and reverb-drenched with a soft, distant character.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqPresenceGain, 1 }, { eqAirGain, 2.5f },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 3 }, { compRelease, 180 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 30 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 15 }, { pitchAmount, 90 },
               { delayOn, 1 }, { delayTime, 400 }, { delayFeedback, 38 }, { delayMix, 24 },
               { verbOn, 1 }, { verbType, VERB_CATHEDRAL }, { verbSize, 85 }, { verbDecay, 75 },
               { verbWidth, 100 }, { verbMix, 34 }, { verbShimmer, 20 } });

        add ("melodic_rap", "Melodic Rap", "Hip-Hop",
             { "melodic rap", "sung rap", "singing rap", "melodic trap" },
             "Sung-rap hybrid: tuned but still human, with a musical plate.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqPresenceGain, 2.5f }, { eqAirGain, 2 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 3.5f }, { compRelease, 120 },
               { deessOn, 1 }, { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 28 },
               { pitchOn, 1 }, { pitchScale, SCALE_MINOR }, { pitchSpeed, 25 }, { pitchAmount, 88 }, { pitchHumanize, 25 },
               { delayOn, 1 }, { delayTime, 260 }, { delayMix, 14 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 50 }, { verbWidth, 100 }, { verbMix, 16 } });

        // ================================================================
        //  POP
        // ================================================================
        add ("pop", "Pop", "Pop",
             { "pop vocal", "pop music", "radio pop", "mainstream pop", "pop" },
             "Polished and radio-ready: bright, airy, wide and tightly controlled.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqMudGain, -2 }, { eqPresenceGain, 2.5f }, { eqAirGain, 3.5f },
               { deessOn, 1 }, { deessFreq, 7000 },
               { compOn, 1 }, { compThreshold, -18 }, { compRatio, 3.5f }, { compAttack, 6 }, { compRelease, 100 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 22 },
               { pitchOn, 1 }, { pitchSpeed, 30 }, { pitchAmount, 90 }, { pitchHumanize, 20 },
               { delayOn, 1 }, { delayTime, 250 }, { delayMix, 10 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 45 }, { verbWidth, 100 }, { verbMix, 14 } });

        add ("hyperpop", "Hyperpop", "Pop",
             { "hyperpop", "hyper pop", "glitch pop", "pc music" },
             "Extreme and synthetic: hard tune, huge air, bright shimmer and heavy effects.",
             { { eqOn, 1 }, { eqHpfFreq, 120 }, { eqMudGain, -3 }, { eqPresenceGain, 5 }, { eqAirGain, 6 },
               { deessOn, 1 }, { deessThreshold, -34 },
               { compOn, 1 }, { compThreshold, -24 }, { compRatio, 6 }, { compAttack, 2 }, { compRelease, 70 },
               { satOn, 1 }, { satType, SAT_DIODE }, { satDrive, 50 }, { satMix, 45 },
               { pitchOn, 1 }, { pitchSpeed, 0 }, { pitchAmount, 100 }, { pitchHardTune, 100 }, { pitchHumanize, 0 }, { pitchFormant, 1 },
               { delayOn, 1 }, { delayTime, 140 }, { delayFeedback, 30 }, { delayMix, 16 },
               { verbOn, 1 }, { verbType, VERB_SHIMMER }, { verbSize, 55 }, { verbWidth, 100 },
               { verbMix, 20 }, { verbShimmer, 45 } });

        add ("bedroom_pop", "Bedroom Pop", "Pop",
             { "bedroom pop", "lo-fi pop", "lofi pop", "dream pop" },
             "Intimate and slightly lo-fi — soft top, tape warmth, close and hazy.",
             { { eqOn, 1 }, { eqHpfFreq, 80 }, { eqLowShelfGain, 1.5f }, { eqPresenceGain, 1 }, { eqAirGain, 0.5f },
               { compOn, 1 }, { compThreshold, -18 }, { compRatio, 3 }, { compRelease, 150 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satDrive, 40 }, { satTone, 42 }, { satMix, 42 },
               { pitchOn, 1 }, { pitchSpeed, 60 }, { pitchAmount, 65 }, { pitchHumanize, 50 },
               { delayOn, 1 }, { delayTime, 300 }, { delayMix, 14 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 45 }, { verbDamp, 62 }, { verbMix, 18 } });

        add ("indie_pop", "Indie Pop", "Pop",
             { "indie pop", "indie vocal", "indie" },
             "Natural and characterful — light touch, roomy, not over-polished.",
             { { eqOn, 1 }, { eqHpfFreq, 85 }, { eqPresenceGain, 2 }, { eqAirGain, 2 },
               { compOn, 1 }, { compThreshold, -17 }, { compRatio, 3 }, { compRelease, 140 },
               { satOn, 1 }, { satType, SAT_CONSOLE }, { satMix, 25 },
               { pitchOn, 1 }, { pitchSpeed, 90 }, { pitchAmount, 55 }, { pitchHumanize, 65 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 50 }, { verbMix, 15 } });

        add ("kpop", "K-Pop", "Pop",
             { "kpop", "k-pop", "k pop" },
             "Ultra-polished and crisp with a big wide plate and very clean top.",
             { { eqOn, 1 }, { eqHpfFreq, 100 }, { eqMudGain, -2.5f }, { eqPresenceGain, 3.5f }, { eqAirGain, 4.5f },
               { deessOn, 1 }, { deessThreshold, -34 }, { deessFreq, 7200 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 4 }, { compAttack, 4 }, { compRelease, 90 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 20 },
               { pitchOn, 1 }, { pitchSpeed, 18 }, { pitchAmount, 95 }, { pitchHumanize, 12 },
               { delayOn, 1 }, { delayTime, 220 }, { delayMix, 10 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 50 }, { verbWidth, 100 }, { verbMix, 15 } });

        add ("synthwave", "Synthwave / 80s", "Pop",
             { "synthwave", "80s", "eighties", "retrowave", "new wave" },
             "Big 80s production: lush wide hall, gated-style ambience and bright sheen.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqPresenceGain, 3 }, { eqAirGain, 3 },
               { compOn, 1 }, { compThreshold, -19 }, { compRatio, 4 },
               { satOn, 1 }, { satType, SAT_TRANSFORMER }, { satMix, 30 },
               { pitchOn, 1 }, { pitchSpeed, 45 }, { pitchAmount, 75 }, { pitchHumanize, 35 },
               { delayOn, 1 }, { delayTime, 375 }, { delayFeedback, 30 }, { delayMix, 18 },
               { verbOn, 1 }, { verbType, VERB_HALL }, { verbSize, 72 }, { verbDecay, 62 },
               { verbWidth, 100 }, { verbMix, 26 } });

        add ("dance_pop", "Dance Pop", "Pop",
             { "dance pop", "club pop", "pop dance" },
             "Loud, tight and energetic — heavy control so it cuts over a busy track.",
             { { eqOn, 1 }, { eqHpfFreq, 110 }, { eqMudGain, -3 }, { eqPresenceGain, 3.5f }, { eqAirGain, 4 },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -22 }, { compRatio, 5 }, { compAttack, 3 }, { compRelease, 80 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satMix, 28 },
               { pitchOn, 1 }, { pitchSpeed, 20 }, { pitchAmount, 92 },
               { delayOn, 1 }, { delayTime, 200 }, { delayMix, 12 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 40 }, { verbMix, 12 } });

        // ================================================================
        //  R&B / SOUL
        // ================================================================
        add ("rnb", "R&B", "R&B / Soul",
             { "rnb", "r and b", "r n b", "r&b", "rhythm and blues" },
             "Smooth, warm and lush with natural vibrato preserved.",
             { { eqOn, 1 }, { eqHpfFreq, 80 }, { eqLowShelfGain, 2 }, { eqPresenceGain, 1.5f }, { eqAirGain, 2.5f },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -18 }, { compRatio, 3 }, { compRelease, 150 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 25 },
               { pitchOn, 1 }, { pitchSpeed, 45 }, { pitchAmount, 70 }, { pitchHumanize, 45 },
               { delayOn, 1 }, { delayTime, 300 }, { delayMix, 12 },
               { verbOn, 1 }, { verbType, VERB_HALL }, { verbSize, 55 }, { verbWidth, 100 }, { verbMix, 18 } });

        add ("neo_soul", "Neo Soul", "R&B / Soul",
             { "neo soul", "neosoul", "neo-soul" },
             "Organic and warm with vintage colour and plenty of expression left in.",
             { { eqOn, 1 }, { eqHpfFreq, 75 }, { eqLowShelfGain, 2.5f }, { eqPresenceGain, 1 }, { eqAirGain, 1.5f },
               { compOn, 1 }, { compThreshold, -16 }, { compRatio, 2.5f }, { compRelease, 180 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 35 }, { satMix, 32 },
               { pitchOn, 1 }, { pitchSpeed, 120 }, { pitchAmount, 45 }, { pitchHumanize, 75 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 50 }, { verbMix, 16 } });

        add ("alt_rnb", "Alt R&B", "R&B / Soul",
             { "alt rnb", "alternative rnb", "alt r&b", "dark rnb" },
             "Moody and atmospheric — darker top, deep wide space, hushed delivery.",
             { { eqOn, 1 }, { eqHpfFreq, 85 }, { eqLowShelfGain, 1.5f }, { eqPresenceGain, 0.5f }, { eqAirGain, 1 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 3.5f }, { compRelease, 160 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satTone, 42 }, { satMix, 35 },
               { pitchOn, 1 }, { pitchSpeed, 35 }, { pitchAmount, 80 }, { pitchHumanize, 30 },
               { delayOn, 1 }, { delayTime, 350 }, { delayFeedback, 32 }, { delayMix, 18 },
               { verbOn, 1 }, { verbType, VERB_BLOOM }, { verbSize, 70 }, { verbDamp, 65 },
               { verbWidth, 100 }, { verbMix, 24 }, { verbDuck, 30 } });

        add ("soul", "Soul / Motown", "R&B / Soul",
             { "soul", "motown", "classic soul" },
             "Vintage and mid-forward with valve warmth and a live plate.",
             { { eqOn, 1 }, { eqHpfFreq, 85 }, { eqLowShelfGain, 1.5f }, { eqPresenceGain, 3 }, { eqAirGain, 0.5f },
               { compOn, 1 }, { compThreshold, -17 }, { compRatio, 3 }, { compRelease, 130 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 40 }, { satMix, 38 },
               { pitchOn, 0 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 48 }, { verbMix, 16 } });

        // ================================================================
        //  ROCK / METAL
        // ================================================================
        add ("rock", "Rock", "Rock / Metal",
             { "rock vocal", "rock", "grunge" },
             "Driven, aggressive and raw with a tight room.",
             { { eqOn, 1 }, { eqHpfFreq, 100 }, { eqMudGain, -2 }, { eqPresenceGain, 3 }, { eqAirGain, 1 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 4 }, { compAttack, 8 }, { compRelease, 110 },
               { satOn, 1 }, { satType, SAT_CONSOLE }, { satDrive, 40 }, { satMix, 35 },
               { pitchOn, 0 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 40 }, { verbMix, 8 } });

        add ("punk", "Punk", "Rock / Metal",
             { "punk vocal", "punk", "hardcore punk" },
             "Loud, flat-out and slightly distorted — no polish, all attitude.",
             { { eqOn, 1 }, { eqHpfFreq, 120 }, { eqMudGain, -3 }, { eqPresenceGain, 4 },
               { compOn, 1 }, { compThreshold, -24 }, { compRatio, 6 }, { compAttack, 4 }, { compRelease, 90 },
               { satOn, 1 }, { satType, SAT_GERMANIUM }, { satDrive, 55 }, { satMix, 45 },
               { pitchOn, 0 }, { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 30 }, { verbMix, 6 } });

        add ("indie_rock", "Indie Rock", "Rock / Metal",
             { "indie rock", "alt rock", "alternative rock" },
             "Roomy and honest with light console colour.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqPresenceGain, 2.5f }, { eqAirGain, 1.5f },
               { compOn, 1 }, { compThreshold, -18 }, { compRatio, 3 }, { compRelease, 130 },
               { satOn, 1 }, { satType, SAT_CONSOLE }, { satMix, 28 },
               { pitchOn, 0 }, { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 52 }, { verbMix, 14 } });

        add ("metal", "Metal", "Rock / Metal",
             { "metal vocal", "metal", "metalcore", "screamo", "death metal" },
             "Extremely controlled and forward so screams stay intelligible over guitars.",
             { { eqOn, 1 }, { eqHpfFreq, 130 }, { eqMudGain, -4 }, { eqPresenceGain, 4.5f }, { eqAirGain, 1.5f },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -26 }, { compRatio, 6 }, { compAttack, 3 }, { compRelease, 80 },
               { satOn, 1 }, { satType, SAT_DIODE }, { satDrive, 45 }, { satMix, 35 },
               { pitchOn, 0 }, { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 32 }, { verbMix, 6 } });

        // ================================================================
        //  ELECTRONIC
        // ================================================================
        add ("edm", "EDM", "Electronic",
             { "edm", "big room", "festival" },
             "Huge and polished — very controlled, bright, wide, sits on top of the drop.",
             { { eqOn, 1 }, { eqHpfFreq, 120 }, { eqMudGain, -3 }, { eqPresenceGain, 4 }, { eqAirGain, 4.5f },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -24 }, { compRatio, 6 }, { compAttack, 3 }, { compRelease, 80 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 35 }, { satMix, 30 },
               { pitchOn, 1 }, { pitchSpeed, 15 }, { pitchAmount, 95 },
               { delayOn, 1 }, { delayTime, 250 }, { delayFeedback, 30 }, { delayMix, 16 },
               { verbOn, 1 }, { verbType, VERB_HALL }, { verbSize, 65 }, { verbWidth, 100 }, { verbMix, 20 }, { verbDuck, 40 } });

        add ("house", "House", "Electronic",
             { "house", "deep house", "tech house", "garage" },
             "Warm, filtered and groovy with a musical plate.",
             { { eqOn, 1 }, { eqHpfFreq, 110 }, { eqLowShelfGain, 1 }, { eqPresenceGain, 2 }, { eqAirGain, 2.5f },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 4 }, { compRelease, 100 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 28 },
               { pitchOn, 1 }, { pitchSpeed, 40 }, { pitchAmount, 75 },
               { delayOn, 1 }, { delayTime, 300 }, { delayFeedback, 32 }, { delayMix, 18 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 55 }, { verbMix, 18 } });

        add ("dubstep", "Dubstep / Bass", "Electronic",
             { "dubstep", "bass music", "riddim", "future bass" },
             "Processed and effect-heavy, cutting through a dense low end.",
             { { eqOn, 1 }, { eqHpfFreq, 140 }, { eqMudGain, -4 }, { eqPresenceGain, 4 }, { eqAirGain, 4 },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -24 }, { compRatio, 6 }, { compAttack, 2 },
               { satOn, 1 }, { satType, SAT_DIODE }, { satDrive, 45 }, { satMix, 40 },
               { pitchOn, 1 }, { pitchSpeed, 10 }, { pitchAmount, 95 },
               { delayOn, 1 }, { delayTime, 190 }, { delayFeedback, 34 }, { delayMix, 16 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 50 }, { verbMix, 16 }, { verbDuck, 45 } });

        add ("dnb", "Drum & Bass", "Electronic",
             { "drum and bass", "dnb", "d&b", "jungle", "liquid dnb" },
             "Bright and tight with a fast, short space so it keeps up with the tempo.",
             { { eqOn, 1 }, { eqHpfFreq, 130 }, { eqMudGain, -3 }, { eqPresenceGain, 3.5f }, { eqAirGain, 3.5f },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -22 }, { compRatio, 5 }, { compAttack, 3 }, { compRelease, 60 },
               { satOn, 1 }, { satType, SAT_CONSOLE }, { satMix, 25 },
               { pitchOn, 1 }, { pitchSpeed, 25 }, { pitchAmount, 85 },
               { delayOn, 1 }, { delayTime, 170 }, { delayFeedback, 26 }, { delayMix, 13 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 38 }, { verbMix, 12 } });

        add ("lofi", "Lo-Fi", "Electronic",
             { "lofi", "lo-fi", "lo fi", "chillhop", "study beats" },
             "Deliberately degraded: band-limited, cassette-warped and soft.",
             { { eqOn, 1 }, { eqHpfFreq, 140 }, { eqMudGain, -1 }, { eqPresenceGain, 0 }, { eqAirGain, -4 },
               { compOn, 1 }, { compThreshold, -18 }, { compRatio, 3 }, { compRelease, 150 },
               { satOn, 1 }, { satType, SAT_LOFI }, { satDrive, 40 }, { satTone, 30 }, { satMix, 55 },
               { pitchOn, 0 },
               { verbOn, 1 }, { verbType, VERB_SPRING }, { verbSize, 42 }, { verbDamp, 78 }, { verbMix, 16 } });

        // ================================================================
        //  ACOUSTIC & TRADITIONAL
        // ================================================================
        add ("country", "Country", "Acoustic & Traditional",
             { "country vocal", "country", "americana", "nashville" },
             "Natural, warm and honest — clear diction, gentle correction.",
             { { eqOn, 1 }, { eqHpfFreq, 85 }, { eqLowShelfGain, 1 }, { eqPresenceGain, 2.5f }, { eqAirGain, 2 },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -17 }, { compRatio, 3 }, { compRelease, 130 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 20 },
               { pitchOn, 1 }, { pitchSpeed, 80 }, { pitchAmount, 60 }, { pitchHumanize, 60 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 45 }, { verbMix, 12 } });

        add ("folk", "Folk", "Acoustic & Traditional",
             { "folk", "singer songwriter", "acoustic vocal", "bluegrass" },
             "Close, intimate and barely processed — the performance carries it.",
             { { eqOn, 1 }, { eqHpfFreq, 80 }, { eqPresenceGain, 1.5f }, { eqAirGain, 1.5f },
               { compOn, 1 }, { compThreshold, -15 }, { compRatio, 2.5f }, { compRelease, 160 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satMix, 15 },
               { pitchOn, 1 }, { pitchSpeed, 150 }, { pitchAmount, 35 }, { pitchHumanize, 80 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 42 }, { verbMix, 12 } });

        add ("jazz", "Jazz", "Acoustic & Traditional",
             { "jazz", "jazz vocal", "crooner", "swing" },
             "Rich, dynamic and vintage — light touch so phrasing breathes.",
             { { eqOn, 1 }, { eqHpfFreq, 75 }, { eqLowShelfGain, 2 }, { eqPresenceGain, 1.5f }, { eqAirGain, 1 },
               { compOn, 1 }, { compThreshold, -15 }, { compRatio, 2.5f }, { compAttack, 15 }, { compRelease, 200 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 30 }, { satMix, 25 },
               { pitchOn, 0 },
               { verbOn, 1 }, { verbType, VERB_HALL }, { verbSize, 55 }, { verbMix, 16 } });

        add ("blues", "Blues", "Acoustic & Traditional",
             { "blues", "blues vocal", "soul blues" },
             "Gritty and expressive with valve drive and a live room.",
             { { eqOn, 1 }, { eqHpfFreq, 85 }, { eqLowShelfGain, 1.5f }, { eqPresenceGain, 3 },
               { compOn, 1 }, { compThreshold, -17 }, { compRatio, 3 }, { compRelease, 140 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 45 }, { satMix, 40 },
               { pitchOn, 0 }, { verbOn, 1 }, { verbType, VERB_SPRING }, { verbSize, 45 }, { verbMix, 14 } });

        add ("gospel", "Gospel", "Acoustic & Traditional",
             { "gospel", "choir vocal", "worship vocal", "worship", "praise" },
             "Big, lush, wide and uplifting with a long musical tail.",
             { { eqOn, 1 }, { eqHpfFreq, 80 }, { eqPresenceGain, 2 }, { eqAirGain, 3 },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -18 }, { compRatio, 3.5f }, { compRelease, 140 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 20 },
               { pitchOn, 1 }, { pitchSpeed, 90 }, { pitchAmount, 55 }, { pitchHumanize, 65 },
               { verbOn, 1 }, { verbType, VERB_HALL }, { verbSize, 75 }, { verbDecay, 65 },
               { verbWidth, 100 }, { verbMix, 24 } });

        add ("ballad", "Ballad", "Acoustic & Traditional",
             { "ballad", "power ballad", "slow song", "emotional ballad" },
             "Intimate verses that open into a wide, cinematic chorus.",
             { { eqOn, 1 }, { eqHpfFreq, 80 }, { eqLowShelfGain, 1.5f }, { eqPresenceGain, 2 }, { eqAirGain, 3 },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -18 }, { compRatio, 3 }, { compRelease, 160 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 20 },
               { pitchOn, 1 }, { pitchSpeed, 110 }, { pitchAmount, 60 }, { pitchHumanize, 70 },
               { delayOn, 1 }, { delayTime, 400 }, { delayMix, 12 },
               { verbOn, 1 }, { verbType, VERB_HALL }, { verbSize, 70 }, { verbWidth, 100 }, { verbMix, 22 } });

        add ("opera", "Opera / Classical", "Acoustic & Traditional",
             { "opera", "classical", "operatic", "choral" },
             "Untouched dynamics in a large natural hall — minimal processing by design.",
             { { eqOn, 1 }, { eqHpfFreq, 70 }, { eqPresenceGain, 0.5f }, { eqAirGain, 1 },
               { compOn, 1 }, { compThreshold, -12 }, { compRatio, 2 }, { compAttack, 25 }, { compRelease, 250 },
               { satOn, 0 }, { pitchOn, 0 },
               { verbOn, 1 }, { verbType, VERB_CATHEDRAL }, { verbSize, 85 }, { verbDecay, 72 },
               { verbWidth, 100 }, { verbMix, 26 } });

        // ================================================================
        //  GLOBAL
        // ================================================================
        add ("afrobeats", "Afrobeats", "Global",
             { "afrobeats", "afrobeat", "afro pop", "afropop" },
             "Warm, rhythmic and present with a light, bouncy space.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqLowShelfGain, 1.5f }, { eqPresenceGain, 3 }, { eqAirGain, 3 },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -19 }, { compRatio, 3.5f }, { compRelease, 110 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 25 },
               { pitchOn, 1 }, { pitchSpeed, 35 }, { pitchAmount, 80 }, { pitchHumanize, 30 },
               { delayOn, 1 }, { delayTime, 240 }, { delayMix, 14 },
               { verbOn, 1 }, { verbType, VERB_PLATE }, { verbSize, 45 }, { verbMix, 14 } });

        add ("reggaeton", "Reggaeton", "Global",
             { "reggaeton", "latin trap", "perreo" },
             "Tight, loud and forward with a short slap so it rides the dembow.",
             { { eqOn, 1 }, { eqHpfFreq, 110 }, { eqMudGain, -2.5f }, { eqPresenceGain, 4 }, { eqAirGain, 3 },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -22 }, { compRatio, 5 }, { compAttack, 4 }, { compRelease, 85 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satDrive, 35 }, { satMix, 30 },
               { pitchOn, 1 }, { pitchSpeed, 15 }, { pitchAmount, 92 },
               { delayOn, 1 }, { delayTime, 160 }, { delayFeedback, 22 }, { delayMix, 12 },
               { verbOn, 1 }, { verbType, VERB_ROOM }, { verbSize, 35 }, { verbMix, 9 } });

        add ("dancehall", "Dancehall", "Global",
             { "dancehall", "reggae", "dub vocal", "dub" },
             "Mid-forward with dub-style echo throws and a spring character.",
             { { eqOn, 1 }, { eqHpfFreq, 100 }, { eqPresenceGain, 3.5f }, { eqAirGain, 1.5f },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 4 }, { compRelease, 100 },
               { satOn, 1 }, { satType, SAT_TRANSFORMER }, { satMix, 30 },
               { pitchOn, 0 },
               { delayOn, 1 }, { delayTime, 375 }, { delayFeedback, 45 }, { delayMix, 22 },
               { verbOn, 1 }, { verbType, VERB_SPRING }, { verbSize, 50 }, { verbMix, 16 } });

        add ("amapiano", "Amapiano", "Global",
             { "amapiano", "piano vocal", "yanos" },
             "Smooth, spacious and airy, floating over the log-drum groove.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqLowShelfGain, 1 }, { eqPresenceGain, 2 }, { eqAirGain, 3.5f },
               { deessOn, 1 }, { compOn, 1 }, { compThreshold, -19 }, { compRatio, 3.5f }, { compRelease, 140 },
               { satOn, 1 }, { satType, SAT_TAPE }, { satMix, 22 },
               { pitchOn, 1 }, { pitchSpeed, 40 }, { pitchAmount, 78 }, { pitchHumanize, 30 },
               { delayOn, 1 }, { delayTime, 320 }, { delayFeedback, 30 }, { delayMix, 18 },
               { verbOn, 1 }, { verbType, VERB_HALL }, { verbSize, 60 }, { verbWidth, 100 }, { verbMix, 20 } });

        // ================================================================
        //  SPOKEN WORD
        // ================================================================
        add ("podcast", "Podcast", "Spoken",
             { "podcast", "podcast vocal", "interview" },
             "Clear, even and easy to listen to for an hour — no effects.",
             { { eqOn, 1 }, { eqHpfFreq, 100 }, { eqMudGain, -2.5f }, { eqPresenceGain, 3 }, { eqAirGain, 1.5f },
               { gateOn, 1 }, { deessOn, 1 },
               { compOn, 1 }, { compThreshold, -22 }, { compRatio, 4 }, { compAttack, 8 }, { compRelease, 120 },
               { satOn, 0 }, { pitchOn, 0 }, { delayOn, 0 }, { verbOn, 0 } });

        add ("broadcast", "Broadcast", "Spoken",
             { "broadcast", "radio voice", "announcer", "news" },
             "Big, authoritative and heavily levelled — classic radio chest tone.",
             { { eqOn, 1 }, { eqHpfFreq, 90 }, { eqLowShelfGain, 2.5f }, { eqMudGain, -2 },
               { eqPresenceGain, 3.5f }, { eqAirGain, 2 },
               { gateOn, 1 }, { deessOn, 1 },
               { compOn, 1 }, { compThreshold, -26 }, { compRatio, 6 }, { compAttack, 5 }, { compRelease, 100 },
               { satOn, 1 }, { satType, SAT_TUBE }, { satMix, 18 },
               { pitchOn, 0 }, { delayOn, 0 }, { verbOn, 0 } });

        add ("audiobook", "Audiobook", "Spoken",
             { "audiobook", "narration", "voice over", "voiceover" },
             "Neutral and consistent with a quiet floor — nothing to distract.",
             { { eqOn, 1 }, { eqHpfFreq, 95 }, { eqMudGain, -2 }, { eqPresenceGain, 2.5f }, { eqAirGain, 1 },
               { gateOn, 1 }, { deessOn, 1 },
               { compOn, 1 }, { compThreshold, -20 }, { compRatio, 3.5f }, { compRelease, 140 },
               { satOn, 0 }, { pitchOn, 0 }, { delayOn, 0 }, { verbOn, 0 } });

        add ("asmr", "ASMR", "Spoken",
             { "asmr", "whisper", "whispered" },
             "Very close and detailed — quiet parts lifted, sibilance kept soft.",
             { { eqOn, 1 }, { eqHpfFreq, 80 }, { eqLowShelfGain, 1 }, { eqPresenceGain, 1.5f }, { eqAirGain, 3 },
               { deessOn, 1 }, { deessThreshold, -38 },
               { compOn, 1 }, { compThreshold, -30 }, { compRatio, 4 }, { compRelease, 200 },
               { satOn, 0 }, { pitchOn, 0 }, { delayOn, 0 }, { verbOn, 0 } });

        return g;
    }
} // namespace

const std::vector<GenreProfile>& GenreProfiles::all()
{
    static const std::vector<GenreProfile> g = build();
    return g;
}

const GenreProfile* GenreProfiles::match (const juce::String& lowercaseText)
{
    // LONGEST alias wins, so "emo trap" beats "trap" and "uk drill" beats "drill".
    const GenreProfile* best = nullptr;
    int bestLen = 0;
    for (const auto& p : all())
        for (const auto& a : p.aliases)
            if (a.length() > bestLen && lowercaseText.contains (a))
            {
                best = &p;
                bestLen = a.length();
            }
    return best;
}

const GenreProfile* GenreProfiles::byId (const juce::String& id)
{
    for (const auto& p : all())
        if (p.id == id) return &p;
    return nullptr;
}

juce::StringArray GenreProfiles::families()
{
    juce::StringArray f;
    for (const auto& p : all())
        f.addIfNotAlreadyThere (p.family);
    return f;
}

std::vector<const GenreProfile*> GenreProfiles::inFamily (const juce::String& family)
{
    std::vector<const GenreProfile*> out;
    for (const auto& p : all())
        if (p.family == family) out.push_back (&p);
    return out;
}

juce::String GenreProfiles::examplesForHelp (int maxItems)
{
    juce::StringArray names;
    for (const auto& p : all())
    {
        if (names.size() >= maxItems) break;
        names.add ("\"" + p.name.toLowerCase() + "\"");
    }
    return names.joinIntoString (", ");
}
} // namespace vf
