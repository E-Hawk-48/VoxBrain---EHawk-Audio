#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "ReferenceProfile.h"
#include "ReferenceMatchBrain.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace vf
{
// ============================================================================
//  ReferenceResult — everything a finished reference analysis produced.
// ============================================================================
struct ReferenceResult
{
    juce::String            fileName;
    juce::String            filePath;
    ReferenceProfile        profile;
    ReferenceMatchBrain::Result match;
    bool                    ok = false;
    juce::String            error;
};

// ============================================================================
//  ReferenceImport
//  ---------------------------------------------------------------------------
//  The drag-drop / file entry point for the AI Reference Mix Analyzer. Decodes
//  any common audio file (WAV/AIFF/FLAC/MP3/OGG/AAC via AudioFormatManager) into
//  a float buffer, then runs the offline ReferenceAnalyzer + ReferenceMatchBrain
//  on a BACKGROUND thread — never the audio thread, and never blocking the UI.
//
//  The owner (the processor) keeps one instance alive, so an analysis started in
//  the editor survives the editor closing/reopening. The UI observes progress by
//  polling getState()/getProgress() on its existing timer and collects the
//  finished result with takeResult() — no cross-thread callbacks to marshal.
//
//  Results are cached by file path + modification time + size, so re-dropping
//  the same file returns instantly.
// ============================================================================
class ReferenceImport : private juce::Thread
{
public:
    enum class State { Idle, Loading, Analyzing, Done, Error, Cancelled };

    ReferenceImport();
    ~ReferenceImport() override;

    /** Start analysing `file` on the background thread (message thread call).
        @param isolateVocal  centre-extract the vocal first (for full-mix songs). */
    void analyzeFile (const juce::File& file, bool isolateVocal = false);
    /** Ask the running job to stop as soon as it can. */
    void cancelCurrent();

    State  getState()    const noexcept { return state.load(); }
    float  getProgress() const noexcept { return progress.load(); }
    bool   isBusy()      const noexcept { const auto s = state.load(); return s == State::Loading || s == State::Analyzing; }
    juce::String statusText() const;

    /** Poll: returns true once when a fresh finished result is available and
        copies it out. After returning true it won't return true again until the
        next job completes. */
    bool takeResult (ReferenceResult& out);

    // ---- reusable / testable static helpers --------------------------------
    /** Synchronous decode + analyze + match. Also the unit-test entry point. */
    static ReferenceResult analyzeFileSync (const juce::File& file,
                                            std::function<void (float)> progress = {},
                                            const std::atomic<bool>* cancel = nullptr,
                                            bool isolateVocal = false);
    /** Decode any supported audio file → float buffer (≤2 ch) + sample rate. */
    static bool decode (const juce::File& file, juce::AudioBuffer<float>& out,
                        double& sampleRate, juce::String& error);
    /** Human-readable multi-line report of a match result, for the report panel. */
    static juce::String buildReport (const ReferenceResult& r);

    /** Longest reference we decode (bounds memory/time); the rest is ignored. */
    static constexpr double kMaxSeconds = 180.0;

private:
    void run() override;
    static juce::String signatureOf (const juce::File& file);

    std::atomic<State> state    { State::Idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool>  cancelFlag { false };

    std::mutex   jobMutex;
    juce::File   pendingFile;
    bool         pendingIsolate = false;

    std::mutex      resultMutex;
    ReferenceResult finishedResult;
    bool            resultReady = false;

    // cache
    juce::String    cacheKey;
    ReferenceResult cacheResult;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceImport)
};
} // namespace vf
