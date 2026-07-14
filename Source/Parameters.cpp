#include "Parameters.h"

namespace vf::param
{
namespace
{
    using APF   = juce::AudioParameterFloat;
    using APB   = juce::AudioParameterBool;
    using Range = juce::NormalisableRange<float>;

    Range dbRange (float lo, float hi)          { return { lo, hi, 0.1f }; }
    Range hzRange (float lo, float hi)          { return { lo, hi, 1.0f, 0.35f }; } // log-ish skew
    Range msRange (float lo, float hi)          { return { lo, hi, 0.1f, 0.4f }; }
    Range pctRange()                            { return { 0.0f, 100.0f, 0.1f }; }

    auto db  (const char* id, const char* name, float lo, float hi, float def)
        { return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, dbRange (lo, hi), def); }
    auto hz  (const char* id, const char* name, float lo, float hi, float def)
        { return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, hzRange (lo, hi), def); }
    auto ms  (const char* id, const char* name, float lo, float hi, float def)
        { return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, msRange (lo, hi), def); }
    auto pct (const char* id, const char* name, float def)
        { return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, pctRange(), def); }
    auto onOff (const char* id, const char* name, bool def)
        { return std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def); }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Global
    layout.add (db (inputGain,  "Input Gain",  -24.0f, 24.0f, 0.0f),
                db (outputGain, "Output Gain", -24.0f, 24.0f, 0.0f));

    // Pitch correction
    layout.add (onOff (pitchOn, "Pitch Correct", false),
                std::make_unique<juce::AudioParameterChoice> (
                    juce::ParameterID { pitchKey, 1 }, "Key",
                    juce::StringArray { "Auto", "C", "C#", "D", "D#", "E", "F",
                                        "F#", "G", "G#", "A", "A#", "B" }, 0),
                std::make_unique<juce::AudioParameterChoice> (
                    juce::ParameterID { pitchScale, 1 }, "Scale",
                    juce::StringArray { "Auto", "Chromatic", "Major", "Minor" }, 0),
                ms  (pitchSpeed,  "Retune Speed", 0.0f, 400.0f, 60.0f),
                pct (pitchAmount, "Correction",   100.0f));

    // Gate
    layout.add (onOff (gateOn, "Gate", true),
                db (gateThreshold, "Gate Threshold", -90.0f, -20.0f, -70.0f));

    // EQ
    layout.add (onOff (eqOn, "EQ", true),
                hz (eqHpfFreq,      "HPF Freq",      20.0f, 400.0f, 80.0f),
                db (eqLowShelfGain, "Low Shelf",     -12.0f, 12.0f, 0.0f),
                db (eqMudGain,      "Mud",           -12.0f,  6.0f, 0.0f),
                hz (eqMudFreq,      "Mud Freq",      150.0f, 800.0f, 350.0f),
                db (eqPresenceGain, "Presence",      -6.0f,  12.0f, 0.0f),
                hz (eqPresenceFreq, "Presence Freq", 1500.0f, 8000.0f, 3500.0f),
                db (eqAirGain,      "Air",           -6.0f,  12.0f, 0.0f));

    // Dynamic EQ (three downward bands; default OFF — brain enables when needed)
    layout.add (onOff (dyneqOn, "Dynamic EQ", false),
                db (dyneqLowThresh,  "DynEQ Low Thresh",  -60.0f, 0.0f, -18.0f),
                hz (dyneqLowFreq,    "DynEQ Low Freq",    100.0f, 500.0f, 220.0f),
                db (dyneqMidThresh,  "DynEQ Mid Thresh",  -60.0f, 0.0f, -18.0f),
                hz (dyneqMidFreq,    "DynEQ Mid Freq",    500.0f, 3000.0f, 1000.0f),
                db (dyneqHighThresh, "DynEQ High Thresh", -60.0f, 0.0f, -18.0f),
                hz (dyneqHighFreq,   "DynEQ High Freq",   2000.0f, 9000.0f, 3500.0f),
                db (dyneqRange,      "DynEQ Range",       0.0f, 12.0f, 5.0f));

    // De-esser
    layout.add (onOff (deessOn, "De-Esser", true),
                db (deessThreshold, "De-Ess Threshold", -60.0f, 0.0f, -30.0f),
                hz (deessFreq,      "De-Ess Freq",      3000.0f, 12000.0f, 6500.0f));

    // Compressor
    layout.add (onOff (compOn, "Compressor", true),
                db (compThreshold, "Comp Threshold", -60.0f, 0.0f, -18.0f),
                std::make_unique<APF> (juce::ParameterID { compRatio, 1 }, "Comp Ratio",
                                       Range { 1.0f, 20.0f, 0.1f, 0.5f }, 3.0f),
                ms (compAttack,  "Comp Attack",  0.1f, 100.0f, 5.0f),
                ms (compRelease, "Comp Release", 10.0f, 1000.0f, 120.0f),
                db (compMakeup,  "Comp Makeup",  0.0f, 24.0f, 0.0f),
                pct (compMix,    "Comp Mix",     100.0f));

    // Multiband compressor (default OFF — brain enables for dynamic/uneven takes)
    layout.add (onOff (mbandOn, "Multiband", false),
                db (mbandLowThresh,  "MB Low Thresh",  -60.0f, 0.0f, -20.0f),
                db (mbandMidThresh,  "MB Mid Thresh",  -60.0f, 0.0f, -20.0f),
                db (mbandHighThresh, "MB High Thresh", -60.0f, 0.0f, -20.0f),
                db (mbandLowGain,    "MB Low Gain",    -12.0f, 12.0f, 0.0f),
                db (mbandMidGain,    "MB Mid Gain",    -12.0f, 12.0f, 0.0f),
                db (mbandHighGain,   "MB High Gain",   -12.0f, 12.0f, 0.0f),
                std::make_unique<APF> (juce::ParameterID { mbandRatio, 1 }, "MB Ratio",
                                       Range { 1.0f, 10.0f, 0.1f, 0.5f }, 2.5f),
                hz (mbandLowXover,   "MB Low Xover",   80.0f, 600.0f, 250.0f),
                hz (mbandHighXover,  "MB High Xover",  1500.0f, 8000.0f, 3000.0f));

    // Saturation
    layout.add (onOff (satOn, "Saturation", true),
                std::make_unique<juce::AudioParameterChoice> (
                    juce::ParameterID { satType, 1 }, "Sat Model",
                    juce::StringArray { "Tube", "Tape", "Console", "Transformer",
                                        "Germanium", "Diode", "Exciter", "Lo-Fi" }, 0),
                pct (satDrive, "Sat Drive", 20.0f),
                pct (satTone,  "Sat Tone",  50.0f),
                pct (satBias,  "Sat Bias",  50.0f),
                pct (satMix,   "Sat Mix",   30.0f),
                onOff (satHQ,  "Sat HQ (4x)", false));

    // Delay
    layout.add (onOff (delayOn, "Delay", false),
                ms  (delayTime,     "Delay Time", 20.0f, 1200.0f, 250.0f),
                pct (delayFeedback, "Delay Feedback", 25.0f),
                pct (delayMix,      "Delay Mix", 12.0f));

    // Reverb (FDN, multi-algorithm)
    layout.add (onOff (verbOn, "Reverb", true),
                std::make_unique<juce::AudioParameterChoice> (
                    juce::ParameterID { verbType, 1 }, "Reverb Type",
                    juce::StringArray { "Room", "Hall", "Plate", "Spring",
                                        "Cathedral", "Shimmer", "Bloom" }, 1),
                pct (verbSize,      "Reverb Size",   45.0f),
                pct (verbDecay,     "Reverb Decay",  50.0f),
                ms  (verbPredelay,  "Pre-Delay",     0.0f, 200.0f, 15.0f),
                pct (verbDamp,      "Reverb Damp",   50.0f),
                pct (verbDiffusion, "Diffusion",     70.0f),
                hz  (verbLowCut,    "Reverb Low Cut",  20.0f, 1000.0f, 120.0f),
                hz  (verbHighCut,   "Reverb High Cut", 1000.0f, 20000.0f, 9000.0f),
                pct (verbModDepth,  "Modulation",    20.0f),
                pct (verbWidth,     "Reverb Width",  100.0f),
                pct (verbMix,       "Reverb Mix",    12.0f),
                pct (verbDuck,      "Duck Reverb",   0.0f),
                pct (verbShimmer,   "Shimmer",       0.0f),
                onOff (verbFreeze,  "Freeze",        false));

    // Limiter
    layout.add (onOff (limitOn, "Limiter", true),
                db (limitCeiling, "Ceiling", -6.0f, 0.0f, -1.0f),
                db (limitGain,    "Limiter Gain", 0.0f, 24.0f, 0.0f));

    // Per-module locks (default unlocked)
    layout.add (onOff (pitchLock, "Pitch Lock",      false),
                onOff (gateLock,  "Gate Lock",       false),
                onOff (eqLock,    "EQ Lock",         false),
                onOff (dyneqLock, "Dynamic EQ Lock", false),
                onOff (deessLock, "De-Esser Lock",   false),
                onOff (compLock,  "Compressor Lock", false),
                onOff (mbandLock, "Multiband Lock",  false),
                onOff (satLock,   "Saturation Lock", false),
                onOff (delayLock, "Delay Lock",      false),
                onOff (verbLock,  "Reverb Lock",     false),
                onOff (limitLock, "Limiter Lock",    false));

    return layout;
}

const char* lockParamFor (const juce::String& paramId)
{
    struct Entry { const char* prefix; const char* lock; };
    static const Entry table[] = {
        { "pitch_", pitchLock }, { "gate_",  gateLock  }, { "dyneq_", dyneqLock },
        { "eq_",    eqLock    }, { "deess_", deessLock }, { "comp_",  compLock  },
        { "mband_", mbandLock }, { "sat_",   satLock   }, { "delay_", delayLock },
        { "verb_",  verbLock  }, { "limit_", limitLock },
    };
    for (const auto& e : table)
        if (paramId.startsWith (e.prefix))
            return e.lock;
    return nullptr;   // global gains etc. are not lockable
}
} // namespace vf::param
