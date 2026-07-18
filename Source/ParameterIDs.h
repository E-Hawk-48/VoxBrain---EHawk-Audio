#pragma once
// ============================================================================
//  ParameterIDs.h
//  ---------------------------------------------------------------------------
//  The stable string IDs of every automatable VoxBrain parameter, with NO JUCE
//  dependency (plain const char*). Split out of Parameters.h so lightweight,
//  headless consumers — the AutoMix/Reference brains, tests, tooling — can name
//  parameters without pulling in the whole juce_audio_processors/GUI stack.
//  Parameters.h includes this and adds the APVTS layout builder on top.
//
//  IDs are a stable API — never reorder or rename after release.
// ============================================================================
namespace vf::param
{
    // ---- Global -----------------------------------------------------------
    inline constexpr auto inputGain   = "global_input_gain";
    inline constexpr auto outputGain  = "global_output_gain";

    // ---- Pitch correction ---------------------------------------------------
    inline constexpr auto pitchOn     = "pitch_on";
    inline constexpr auto pitchKey    = "pitch_key";     // choice: Auto, C…B
    inline constexpr auto pitchScale  = "pitch_scale";   // choice: Auto, Chromatic, Major, Minor, …
    inline constexpr auto pitchSpeed  = "pitch_speed";   // retune ms (0 = hard)
    inline constexpr auto pitchAmount = "pitch_amount";  // %
    inline constexpr auto pitchHumanize = "pitch_humanize"; // % preserve vibrato
    inline constexpr auto pitchFormant  = "pitch_formant";  // semitones formant shift
    inline constexpr auto pitchLatency  = "pitch_latency";  // choice: Live/Balanced/Studio

    // ---- Gate / Expander ---------------------------------------------------
    inline constexpr auto gateOn        = "gate_on";
    inline constexpr auto gateThreshold = "gate_threshold";

    // ---- EQ ----------------------------------------------------------------
    inline constexpr auto eqOn           = "eq_on";
    inline constexpr auto eqHpfFreq      = "eq_hpf_freq";
    inline constexpr auto eqLowShelfGain = "eq_lowshelf_gain";   // 180 Hz shelf
    inline constexpr auto eqMudGain      = "eq_mud_gain";        // bell
    inline constexpr auto eqMudFreq      = "eq_mud_freq";
    inline constexpr auto eqPresenceGain = "eq_presence_gain";   // bell
    inline constexpr auto eqPresenceFreq = "eq_presence_freq";
    inline constexpr auto eqAirGain      = "eq_air_gain";        // 12 kHz shelf

    // ---- Dynamic EQ (3 downward bands: low-mid boom, mid honk, presence harsh)
    inline constexpr auto dyneqOn         = "dyneq_on";
    inline constexpr auto dyneqLowThresh  = "dyneq_low_thresh";
    inline constexpr auto dyneqLowFreq    = "dyneq_low_freq";
    inline constexpr auto dyneqMidThresh  = "dyneq_mid_thresh";
    inline constexpr auto dyneqMidFreq    = "dyneq_mid_freq";
    inline constexpr auto dyneqHighThresh = "dyneq_high_thresh";
    inline constexpr auto dyneqHighFreq   = "dyneq_high_freq";
    inline constexpr auto dyneqRange      = "dyneq_range";        // shared max cut (dB)

    // ---- De-esser ----------------------------------------------------------
    inline constexpr auto deessOn        = "deess_on";
    inline constexpr auto deessThreshold = "deess_threshold";
    inline constexpr auto deessFreq      = "deess_freq";

    // ---- Compressor ---------------------------------------------------------
    inline constexpr auto compOn        = "comp_on";
    inline constexpr auto compThreshold = "comp_threshold";
    inline constexpr auto compRatio     = "comp_ratio";
    inline constexpr auto compAttack    = "comp_attack";
    inline constexpr auto compRelease   = "comp_release";
    inline constexpr auto compMakeup    = "comp_makeup";
    inline constexpr auto compMix       = "comp_mix";            // parallel blend

    // ---- Multiband compressor (low / mid / high, LR crossovers) ------------
    inline constexpr auto mbandOn        = "mband_on";
    inline constexpr auto mbandLowThresh = "mband_low_thresh";
    inline constexpr auto mbandMidThresh = "mband_mid_thresh";
    inline constexpr auto mbandHighThresh = "mband_high_thresh";
    inline constexpr auto mbandLowGain   = "mband_low_gain";     // makeup / tonal balance
    inline constexpr auto mbandMidGain   = "mband_mid_gain";
    inline constexpr auto mbandHighGain  = "mband_high_gain";
    inline constexpr auto mbandRatio     = "mband_ratio";        // shared
    inline constexpr auto mbandLowXover  = "mband_low_xover";    // low|mid split Hz
    inline constexpr auto mbandHighXover = "mband_high_xover";   // mid|high split Hz

    // ---- Saturation ---------------------------------------------------------
    inline constexpr auto satOn    = "sat_on";
    inline constexpr auto satType  = "sat_type";     // choice: 8 models
    inline constexpr auto satDrive = "sat_drive";
    inline constexpr auto satTone  = "sat_tone";     // tilt (0 dark .. 100 bright)
    inline constexpr auto satBias  = "sat_bias";     // asymmetry (50 = neutral)
    inline constexpr auto satMix   = "sat_mix";
    inline constexpr auto satHQ    = "sat_hq";       // 2x (off) / 4x (on) oversampling

    // ---- Delay --------------------------------------------------------------
    inline constexpr auto delayOn       = "delay_on";
    inline constexpr auto delayTime     = "delay_time_ms";
    inline constexpr auto delayFeedback = "delay_feedback";
    inline constexpr auto delayMix      = "delay_mix";

    // ---- Reverb (FDN, multi-algorithm) -------------------------------------
    inline constexpr auto verbOn        = "verb_on";
    inline constexpr auto verbType      = "verb_type";       // choice: 7 algorithms
    inline constexpr auto verbSize      = "verb_size";
    inline constexpr auto verbDecay     = "verb_decay";      // tail length
    inline constexpr auto verbPredelay  = "verb_predelay";   // ms
    inline constexpr auto verbDamp      = "verb_damp";
    inline constexpr auto verbDiffusion = "verb_diffusion";
    inline constexpr auto verbLowCut    = "verb_lowcut";     // Hz (HPF on the send)
    inline constexpr auto verbHighCut   = "verb_highcut";    // Hz (LPF on the send)
    inline constexpr auto verbModDepth  = "verb_mod";        // tail modulation
    inline constexpr auto verbWidth     = "verb_width";
    inline constexpr auto verbMix       = "verb_mix";
    inline constexpr auto verbDuck      = "verb_duck";       // duck under the dry
    inline constexpr auto verbShimmer   = "verb_shimmer";    // octave-up regen
    inline constexpr auto verbFreeze    = "verb_freeze";     // infinite sustain

    // ---- Limiter ------------------------------------------------------------
    inline constexpr auto limitOn      = "limit_on";
    inline constexpr auto limitCeiling = "limit_ceiling";
    inline constexpr auto limitGain    = "limit_gain";

    // ---- Per-module locks --------------------------------------------------
    inline constexpr auto pitchLock = "pitch_lock";
    inline constexpr auto gateLock  = "gate_lock";
    inline constexpr auto eqLock    = "eq_lock";
    inline constexpr auto dyneqLock = "dyneq_lock";
    inline constexpr auto deessLock = "deess_lock";
    inline constexpr auto compLock  = "comp_lock";
    inline constexpr auto mbandLock = "mband_lock";
    inline constexpr auto satLock   = "sat_lock";
    inline constexpr auto delayLock = "delay_lock";
    inline constexpr auto verbLock  = "verb_lock";
    inline constexpr auto limitLock = "limit_lock";
} // namespace vf::param
