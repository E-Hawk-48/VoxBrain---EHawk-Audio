#include "ModuleSpecs.h"
#include "../ParameterIDs.h"

namespace vf
{
using S = VocalChain::Stage;

const StageSpec& specForStage (S s)
{
    using namespace vf::param;
    using K = ModuleCard::KnobSpec;
    using C = ModuleCard::ComboSpec;

    // Built once, on first use. KnobSpec/ComboSpec = { paramId, label, hover-help,
    // advanced? } — `advanced` controls hide in Simple mode.
    static const StageSpec specs[VocalChain::kStageCount] =
    {
        // Retune -----------------------------------------------------------
        { "Pitch", pitchOn, pitchLock,
          { { pitchSpeed, "Speed", "How fast the pitch snaps to the note. Fast = robotic/hard-tune, slow = natural.", true },
            { pitchAmount, "Amount", "How strongly the vocal is pulled onto the correct pitch. 0 = off, 100% = fully tuned." } },
          { { pitchKey, "Key", "The musical key the tuner snaps to — set this to your song's key." },
            { pitchScale, "Scale", "The scale used for tuning (Major, Minor, Chromatic…). Match your song." },
            { pitchLatency, "Latency", "Live = lowest delay, best for tracking/performing (won't tune very low notes); Studio = most accurate on low notes; Balanced is in between." } },
          false },

        // Gate -------------------------------------------------------------
        { "Gate", gateOn, gateLock,
          { { gateThreshold, "Thresh", "Silences the mic below this level to remove hiss and room noise between phrases." } },
          {}, false },

        // EQ ---------------------------------------------------------------
        { "EQ", eqOn, eqLock,
          { { eqHpfFreq, "HPF", "High-pass filter: removes low rumble and mic pops below this frequency." },
            { eqMudGain, "Mud", "Cuts boxy low-mid 'mud' for a clearer, less muffled vocal.", true },
            { eqPresenceGain, "Pres", "Presence: lifts the upper mids for clarity and intelligibility." },
            { eqAirGain, "Air", "Adds sparkle and openness at the very top." } },
          {}, false },

        // Dynamic EQ -------------------------------------------------------
        { "Dynamic EQ", dyneqOn, dyneqLock,
          { { dyneqLowThresh, "Low", "Tames boomy low-mids only when they get too loud." },
            { dyneqMidThresh, "Mid", "Reduces nasal/honky mids only when they spike." },
            { dyneqHighThresh, "High", "Softens harsh presence only when it gets too strong." },
            { dyneqRange, "Range", "The most each band is allowed to duck." } },
          {}, true },

        // De-Esser ---------------------------------------------------------
        { "De-Esser", deessOn, deessLock,
          { { deessThreshold, "Thresh", "How aggressively harsh 'S' and 'T' sounds are tamed." },
            { deessFreq, "Freq", "Which frequency the de-esser targets. Raise it for brighter voices.", true } },
          {}, false },

        // Compressor -------------------------------------------------------
        { "Compressor", compOn, compLock,
          { { compThreshold, "Thresh", "Level where compression begins. Lower = a more even, controlled vocal." },
            { compRatio, "Ratio", "How hard the compressor squeezes once it engages.", true },
            { compMix, "Mix", "Blends compressed with dry (parallel) for punch without squashing.", true } },
          {}, false },

        // Multiband --------------------------------------------------------
        { "Multiband", mbandOn, mbandLock,
          { { mbandLowThresh, "Low", "Controls the low band's dynamics independently." },
            { mbandMidThresh, "Mid", "Controls the mid band's dynamics independently." },
            { mbandHighThresh, "High", "Controls the high band's dynamics independently." },
            { mbandRatio, "Ratio", "How hard all three bands compress." } },
          {}, true },

        // Saturation -------------------------------------------------------
        { "Saturation", satOn, satLock,
          { { satDrive, "Drive", "Adds harmonic warmth and thickness. More drive = more colour." },
            { satTone, "Tone", "Tilts the saturated tone darker or brighter.", true },
            { satMix, "Mix", "Blend of the saturated and clean signal.", true } },
          { { satType, "Model", "Saturation flavour: Tube, Tape, Console, Fuzz, Exciter, Lo-Fi…" } },
          false },

        // Delay ------------------------------------------------------------
        { "Delay", delayOn, delayLock,
          { { delayTime, "Time", "The echo (delay) time in milliseconds." },
            { delayMix, "Mix", "How loud the echoes are in the mix." } },
          {}, false },

        // Reverb -----------------------------------------------------------
        { "Reverb", verbOn, verbLock,
          { { verbSize, "Size", "Size of the simulated space. Bigger = longer, roomier tail." },
            { verbDecay, "Decay", "How long the reverb tail rings out.", true },
            { verbMix, "Mix", "How much reverb is blended in." } },
          { { verbType, "Type", "Reverb character: Room, Hall, Plate, Spring, Shimmer…" } },
          false },

        // Limiter ----------------------------------------------------------
        { "Limiter", limitOn, limitLock,
          { { limitGain, "Gain", "Drives the vocal louder into the limiter." },
            { limitCeiling, "Ceil", "Maximum output level — the vocal never goes past this.", true } },
          {}, false },
    };

    const int i = juce::jlimit (0, VocalChain::kStageCount - 1, (int) s);
    return specs[i];
}
} // namespace vf
