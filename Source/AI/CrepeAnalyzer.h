#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include "../Analysis/AnalysisEngine.h"

namespace vf
{
// ============================================================================
//  CrepeAnalyzer — neural pitch analysis via ONNX Runtime (CREPE model).
//
//  Used OFFLINE on the recorded LEARN audio (16 kHz mono): far more robust
//  than YIN on breathy/noisy/quiet vocals, which sharpens key detection and
//  drift measurement. The real-time retune path stays DSP (latency).
//
//  Model file: crepe-tiny.onnx, searched in
//    1. <plugin binary dir>/VocalForgeModels/
//    2. <user app data>/VocalForge/Models/
//  If absent or ONNX Runtime fails to load, isAvailable() returns false and
//  the caller falls back to the DSP analysis — the plugin never breaks.
// ============================================================================
struct PitchFrame
{
    float timeSec    = 0.0f;
    float f0Hz       = 0.0f;   // 0 = unvoiced
    float confidence = 0.0f;   // 0..1
};

class CrepeAnalyzer
{
public:
    CrepeAnalyzer();             // loads the model if it can be found
    ~CrepeAnalyzer();

    bool isAvailable() const noexcept;
    juce::String getStatus() const noexcept { return status; }

    /** Analyze mono 16 kHz audio. Frames every hopSamples (160 = 10 ms). */
    std::vector<PitchFrame> analyze (const float* audio16k, int numSamples,
                                     int hopSamples = 160);

    static juce::File findModelFile();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::String status;
};

/** Overwrites the snapshot's pitch/key statistics using neural pitch frames. */
void refineSnapshotWithPitchFrames (AnalysisSnapshot& snapshot,
                                    const std::vector<PitchFrame>& frames);
} // namespace vf
