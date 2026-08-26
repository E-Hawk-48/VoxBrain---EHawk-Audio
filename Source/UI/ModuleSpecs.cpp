#include "ModuleSpecs.h"
#include "../ParameterIDs.h"

namespace vf
{
using S = VocalChain::Stage;

const StageSpec& specForStage (S s)
{
    using namespace vf::param;
    // Local alias — the Voice Changer selector lives with the pitch controls.
    constexpr auto pitchVoice = vf::param::voiceCharacter;
    using K = ModuleCard::KnobSpec;
    using C = ModuleCard::ComboSpec;

    // Built once, on first use. KnobSpec/ComboSpec = { paramId, label, hover-help,
    // advanced? } — `advanced` controls hide in Simple mode.
    static const StageSpec specs[VocalChain::kStageCount] =
    {
        // Retune -----------------------------------------------------------
        //  THE PITCH CARD used to put twelve knobs and five menus on one face,
        //  and six of those knobs were different answers to the same question:
        //  "how much tuning?". Amount, Hard Tune, Flex, Snap, Vibrato and
        //  Humanize all pull on correction depth, they interact, and no
        //  ordering of them is obvious — so the card read as noise even to
        //  someone who knows what auto-tune does.
        //
        //  Three controls answer that question in a way a producer can hold in
        //  their head, and they are the only ones on the face now:
        //     AMOUNT    how much of the error gets fixed   (0 = off, 100 = in tune)
        //     SPEED     how fast it gets there             (fast = the robotic sound)
        //     HARD TUNE the one-knob modern sound          (overrides the rest)
        //  ...plus the musical context (Key / Scale) and the Voice menu.
        //
        //  Nothing was removed: every other control is still here, still
        //  automatable, still saved with sessions and presets — just behind
        //  Advanced, where a control you reach for once a month belongs. The
        //  parameter IDs are untouched, so existing sessions and presets load
        //  exactly as before.
        { "Pitch", pitchOn, pitchLock,
          { { pitchAmount, "Amount", "How strongly the vocal is pulled onto the correct pitch. 0 = off, 100% = fully in tune. This is the main tuning control." },
            { pitchSpeed, "Speed", "How fast the pitch arrives at the note. Fast (near 0) gives the hard, robotic sound; slow lets the singer's phrasing through." },
            { pitchHardTune, "Hard Tune", "One knob for the modern auto-tune sound: turn it up and tuning becomes instant and locked, overriding the expression controls in Advanced." },
            // --- expression controls (Advanced) ---
            { pitchFlex, "Flex", "How much natural pitch variation to leave alone. Higher = only obvious wrong notes get fixed, so the performance stays human.", true },
            { pitchVibrato, "Vibrato", "Protects your vibrato. High leaves the wobble untouched; low tightens it. Your note still gets tuned either way — this only decides how much of the wobble survives.", true },
            { pitchTransition, "Glide", "Lets slides and bends between notes breathe instead of snapping. Turn down for stepped, robotic transitions.", true },
            { pitchDrift, "Drift", "Pulls slow drifting off-pitch back to the note, without touching fast expression.", true },
            { pitchSnap, "Snap", "Leaves pitch differences smaller than this (in cents) completely alone. A little keeps the voice natural; 0 corrects absolutely everything.", true },
            { pitchSensitivity, "Sens", "How eagerly the tuner decides something is a sung note. Lower it for breathy or noisy takes, raise it for quiet singing.", true },
            { pitchHumanize, "Human", "Biases all the expression controls above toward 'leave it alone'. A quick way to soften tuning without setting each one.", true },
            // --- Voice Changer (Advanced: the Voice menu drives these) ---
            { pitchTranspose, "Transpose", "Shifts the whole voice up or down in semitones (12 = a full octave). Separate from tuning: you can be perfectly in tune AND an octave lower.", true },
            { pitchFormant, "Formant", "Changes the apparent SIZE of the singer's head and throat without changing the note. Down = bigger/deeper body, up = smaller/younger. Move it with Transpose for a believable bigger or smaller person, against it for something inhuman.", true } },
          { { pitchKey, "Key", "The musical key the tuner snaps to — set this to your song's key." },
            { pitchScale, "Scale", "The scale used for tuning (Major, Minor, Chromatic…). Match your song." },
            { pitchVoice, "Voice", "Voice Changer: pick a character (Demonic, Child, Robot, Radio…) and the whole chain is set up for it in one move. Undo restores your previous sound." },
            { pitchLatency, "Latency", "Live = lowest delay, best for tracking/performing (won't tune very low notes); Studio = most accurate on low notes; Balanced is in between.", true },
            { pitchHQ, "HQ Render", "Highest-quality tracking for bouncing/exporting. Costs a little extra delay, so leave it off while recording.", true } },
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
