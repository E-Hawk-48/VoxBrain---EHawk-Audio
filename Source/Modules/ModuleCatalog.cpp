#include "ModuleRegistry.h"
#include "BuiltinModules.h"

// ============================================================================
//  The module catalog. Implemented modules get a factory; the rest are
//  registered as searchable "roadmap" entries so Smart Search already surfaces
//  the full library and each one just needs its factory filled in over time.
// ============================================================================
namespace vf::mods
{
void registerBuiltInModules()
{
    static bool done = false;
    if (done) return;
    done = true;

    auto& r = ModuleRegistry::instance();

    // ---- IMPLEMENTED (creatable now) -------------------------------------
    r.registerType (GainModule::desc(),            [] { return std::make_unique<GainModule>(); });
    r.registerType (TrimModule::desc(),            [] { return std::make_unique<TrimModule>(); });
    r.registerType (PhaseFlipModule::desc(),       [] { return std::make_unique<PhaseFlipModule>(); });
    r.registerType (StereoWidthModule::desc(),     [] { return std::make_unique<StereoWidthModule>(); });
    r.registerType (MonoMakerModule::desc(),       [] { return std::make_unique<MonoMakerModule>(); });
    r.registerType (HighpassModule::desc(),        [] { return std::make_unique<HighpassModule>(); });
    r.registerType (LowpassModule::desc(),         [] { return std::make_unique<LowpassModule>(); });
    r.registerType (TubeSaturationModule::desc(),  [] { return std::make_unique<TubeSaturationModule>(); });

    // ---- ROADMAP (searchable now; factories added incrementally) ---------
    auto plan = [&r] (const char* id, const char* name, Category c,
                      juce::StringArray tags, const char* desc)
    {
        r.registerPlanned ({ id, name, c, std::move (tags), desc, false });
    };

    using C = Category;
    // Input
    plan ("stereo_swap", "Stereo Swap", C::Input, { "stereo", "swap", "channel" }, "Swap L/R channels.");
    plan ("preamp_emu", "Preamp Emulation", C::Input, { "preamp", "analog", "warm", "colour", "console" }, "Analog preamp colour.");
    plan ("mic_correction", "Microphone Model Correction", C::Input, { "mic", "correction", "calibration" }, "Correct a mic's response toward a target model.");
    plan ("input_meter", "Input Meter", C::Input, { "meter", "level", "analysis" }, "Input level metering.");
    // AI Analysis
    plan ("ai_vocal_analysis", "AI Vocal Analysis", C::AIAnalysis, { "ai", "analysis", "vocal" }, "Full AI read-out of the vocal.");
    plan ("ai_genre_detect", "AI Genre Detection", C::AIAnalysis, { "ai", "genre", "style" }, "Detects the likely genre.");
    plan ("ai_emotion", "AI Emotion Detection", C::AIAnalysis, { "ai", "emotion", "feel" }, "Estimates emotional tone.");
    plan ("ai_chain_builder", "AI Chain Builder", C::AIAnalysis, { "ai", "chain", "auto", "suggest" }, "Builds a full chain from analysis.");
    // EQ
    plan ("parametric_eq", "Parametric EQ", C::EQ, { "eq", "tone", "frequency", "surgical", "bell" }, "Multi-band parametric EQ.");
    plan ("dynamic_eq", "Dynamic EQ", C::EQ, { "eq", "dynamic", "resonance", "mud", "harsh", "tame" }, "Frequency-dependent dynamic EQ.");
    plan ("linear_phase_eq", "Linear Phase EQ", C::EQ, { "eq", "linear phase", "mastering", "transparent" }, "Phase-linear EQ for mastering.");
    plan ("vintage_eq", "Vintage EQ", C::EQ, { "eq", "vintage", "analog", "warm", "musical" }, "Musical vintage-voiced EQ.");
    plan ("match_eq", "Match EQ", C::EQ, { "eq", "match", "reference", "tone match" }, "Match the tone of a reference.");
    plan ("ai_smart_eq", "AI Smart EQ", C::EQ, { "eq", "ai", "auto", "smart" }, "AI-driven corrective EQ.");
    // Filter
    plan ("bandpass", "Band Pass", C::Filter, { "filter", "band pass" }, "Band-pass filter.");
    plan ("telephone", "Telephone Filter", C::Filter, { "filter", "telephone", "lofi", "band limit" }, "Band-limited 'phone' effect.");
    plan ("auto_filter", "Auto Filter", C::Filter, { "filter", "auto", "envelope", "movement" }, "Envelope-following filter.");
    // Dynamics
    plan ("compressor", "Compressor", C::Dynamics, { "compress", "dynamics", "level", "glue" }, "Feed-forward compressor.");
    plan ("multiband_comp", "Multiband Compressor", C::Dynamics, { "compress", "multiband", "dynamics" }, "Per-band compression.");
    plan ("parallel_comp", "Parallel Compressor", C::Dynamics, { "compress", "parallel", "energy", "punch" }, "New-York parallel compression.");
    plan ("optical_comp", "Optical Compressor", C::Dynamics, { "compress", "optical", "smooth", "vintage" }, "Smooth optical-style compression.");
    plan ("fet_comp", "FET Compressor", C::Dynamics, { "compress", "fet", "punch", "aggressive" }, "Fast FET-style compression.");
    plan ("limiter", "Limiter", C::Dynamics, { "limit", "ceiling", "loud", "brickwall" }, "Transparent brickwall limiter.");
    plan ("soft_clipper", "Soft Clipper", C::Dynamics, { "clip", "soft", "transient", "loud", "saturation" }, "Rounds peaks before the limiter.");
    plan ("transient_designer", "Transient Designer", C::Dynamics, { "transient", "punch", "attack", "sustain" }, "Shape attack and sustain.");
    plan ("ai_compressor", "AI Compressor", C::Dynamics, { "compress", "ai", "auto", "dynamics" }, "Self-configuring compressor.");
    // Saturation
    plan ("tape_sat", "Tape Saturation", C::Saturation, { "warm", "tape", "analog", "vintage", "saturation", "colour" }, "Smooth tape saturation.");
    plan ("console_sat", "Console Saturation", C::Saturation, { "console", "analog", "glue", "colour", "saturation" }, "Subtle console drive.");
    plan ("transformer_sat", "Transformer", C::Saturation, { "transformer", "iron", "thick", "analog", "saturation" }, "Transformer core saturation.");
    plan ("exciter", "Exciter", C::Saturation, { "exciter", "airy", "sparkle", "presence", "sheen" }, "Adds high-end harmonics.");
    plan ("bitcrusher", "Bit Crusher", C::Saturation, { "lofi", "bitcrush", "digital", "crush", "gritty" }, "Bit + sample-rate reduction.");
    plan ("vinyl_sat", "Vinyl", C::Saturation, { "vinyl", "lofi", "vintage", "warm", "crackle" }, "Vinyl character.");
    // Pitch
    plan ("autotune", "Auto Tune", C::Pitch, { "pitch", "autotune", "tune", "correction" }, "Real-time pitch correction.");
    plan ("pitch_shift", "Pitch Shift", C::Pitch, { "pitch", "shift", "octave", "transpose" }, "Shift pitch up/down.");
    plan ("formant_shift", "Formant Shift", C::Pitch, { "formant", "gender", "character", "pitch" }, "Shift vocal formants.");
    plan ("harmony", "Harmony Generator", C::Pitch, { "harmony", "pitch", "voices", "thicken" }, "Generate diatonic harmonies.");
    plan ("double_tracker", "Double Tracker", C::Pitch, { "double", "thicken", "wide", "pitch" }, "Realistic vocal doubling.");
    // Modulation
    plan ("chorus", "Chorus", C::Modulation, { "chorus", "modulation", "wide", "lush", "thicken" }, "Classic chorus.");
    plan ("stereo_chorus", "Stereo Chorus", C::Modulation, { "chorus", "stereo", "wide", "modulation" }, "Wide stereo chorus.");
    plan ("flanger", "Flanger", C::Modulation, { "flanger", "modulation", "sweep", "jet" }, "Jet-sweep flanger.");
    plan ("phaser", "Phaser", C::Modulation, { "phaser", "modulation", "sweep", "movement" }, "Classic phaser.");
    plan ("tremolo", "Tremolo", C::Modulation, { "tremolo", "modulation", "amplitude" }, "Amplitude modulation.");
    // Spatial
    plan ("reverb_hall", "Hall Reverb", C::Spatial, { "reverb", "hall", "space", "lush", "long" }, "Lush concert hall.");
    plan ("reverb_plate", "Plate Reverb", C::Spatial, { "reverb", "plate", "bright", "vocal", "space" }, "Bright plate reverb.");
    plan ("reverb_room", "Room Reverb", C::Spatial, { "reverb", "room", "tight", "space" }, "Small room ambience.");
    plan ("shimmer_reverb", "Shimmer", C::Spatial, { "reverb", "shimmer", "airy", "ethereal", "octave" }, "Octave-up shimmer reverb.");
    plan ("convolution_reverb", "Convolution Reverb", C::Spatial, { "reverb", "convolution", "ir", "realistic" }, "Impulse-response reverb.");
    // Delay
    plan ("digital_delay", "Digital Delay", C::Delay, { "delay", "echo", "clean" }, "Clean digital delay.");
    plan ("tape_delay", "Tape Delay", C::Delay, { "delay", "tape", "analog", "warm", "vintage" }, "Warm tape echo.");
    plan ("pingpong_delay", "Ping Pong Delay", C::Delay, { "delay", "ping pong", "stereo", "bounce" }, "Bouncing stereo delay.");
    plan ("bpm_delay", "BPM Sync Delay", C::Delay, { "delay", "bpm", "sync", "tempo" }, "Tempo-synced delay.");
    // Restoration
    plan ("noise_reduction", "Noise Reduction", C::Restoration, { "noise", "denoise", "clean", "restore" }, "Broadband noise reduction.");
    plan ("de_plosive", "De-Plosive", C::Restoration, { "plosive", "pop", "breath", "clean", "intelligibility" }, "Tames plosive 'p'/'b' pops.");
    plan ("de_esser", "De-Esser", C::Restoration, { "deess", "sibilance", "harsh", "ess", "clean" }, "Reduces harsh sibilance.");
    plan ("de_reverb", "De-Reverb", C::Restoration, { "reverb", "dry", "room", "restore", "clean" }, "Reduces room/reverb tails.");
    plan ("breath_control", "Breath Control", C::Restoration, { "breath", "ducking", "clean" }, "Ducks or removes breaths.");
    // Creative
    plan ("stutter", "Stutter", C::Creative, { "stutter", "glitch", "repeat", "creative" }, "Rhythmic stutter effect.");
    plan ("granular", "Granular Processor", C::Creative, { "granular", "texture", "cloud", "creative" }, "Granular texture engine.");
    plan ("reverse", "Reverse", C::Creative, { "reverse", "backwards", "creative" }, "Reverse buffers.");
    // Imaging
    plan ("ms_processor", "Mid/Side Processor", C::Imaging, { "mid side", "ms", "stereo", "image" }, "Independent mid/side processing.");
    plan ("correlation_meter", "Correlation Meter", C::Imaging, { "correlation", "phase", "meter", "mono" }, "Stereo correlation metering.");
    // Analysis
    plan ("spectrum_analyzer", "Spectrum Analyzer", C::Analysis, { "spectrum", "analysis", "frequency", "fft" }, "Real-time spectrum display.");
    plan ("lufs_meter", "LUFS Meter", C::Analysis, { "lufs", "loudness", "meter", "streaming" }, "Integrated/short-term LUFS.");
    plan ("oscilloscope", "Oscilloscope", C::Analysis, { "scope", "waveform", "analysis" }, "Waveform oscilloscope.");
    // Utility
    plan ("pan", "Pan", C::Utility, { "pan", "balance", "position", "utility" }, "Stereo pan.");
    plan ("dc_offset", "DC Offset Removal", C::Utility, { "dc", "offset", "clean", "utility" }, "Removes DC bias.");
    plan ("signal_gen", "Signal Generator", C::Utility, { "signal", "tone", "test", "utility" }, "Sine/noise generator.");
    // Master
    plan ("master_limiter", "Master Limiter", C::Master, { "master", "limit", "loud", "ceiling" }, "Transparent mastering limiter.");
    plan ("streaming_opt", "Streaming Optimizer", C::Master, { "master", "streaming", "loudness", "spotify", "youtube" }, "Targets streaming loudness.");
    plan ("reference_match", "Reference Matching", C::Master, { "master", "reference", "match", "tone" }, "Match a reference master.");
}
} // namespace vf::mods
