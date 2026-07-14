#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Analysis/AnalysisEngine.h"
#include "DSP/VocalChain.h"
#include "Brain/AutoMixBrain.h"
#include "AI/CrepeAnalyzer.h"
#include "Preset/PresetManager.h"
#include "Update/UpdateChecker.h"

namespace vf
{
// ============================================================================
//  VocalForgeProcessor — plugin entry point.
//  LEARN workflow:
//    1. GUI calls setLearning(true); analysis accumulates while audio plays.
//    2. GUI calls setLearning(false); snapshot is finalised on the audio
//       thread and picked up here; AutoMixBrain runs on the message thread
//       and writes every decision into the APVTS (visible to host automation).
// ============================================================================
class VocalForgeProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    VocalForgeProcessor();
    ~VocalForgeProcessor() override;

    // ---- AudioProcessor ----------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "VocalForge"; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 3.0; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- VocalForge API (GUI) ----------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    AnalysisEngine& getAnalysis() noexcept                { return analysis; }
    VocalChain&     getChain()    noexcept                { return chain; }

    void setLearning (bool shouldLearn);
    bool isLearning() const noexcept                      { return analysis.isLearning(); }

    /** Chat entry point: snapshots for undo, then applies the requested moves. */
    juce::String applyChatMessage (const juce::String& message);

    PresetManager& getPresets() noexcept                  { return presets; }
    UpdateChecker& getUpdater() noexcept                  { return updater; }

    /** Last auto-mix result (message thread only). */
    const AutoMixBrain::Result& getLastResult() const noexcept { return lastResult; }
    std::function<void()> onAutoMixApplied;   // GUI callback

private:
    void timerCallback() override;
    ChainParams readChainParams() const;
    void applyBrainResult (const AutoMixBrain::Result& r);
    bool isModuleLocked (const juce::String& paramId) const;

    AnalysisEngine analysis;
    VocalChain     chain;
    PresetManager  presets { apvts };   // declared after apvts → constructed after it
    UpdateChecker  updater;

    // Learn handshake: audio thread finalises snapshot → message thread applies
    std::atomic<bool> learnStopRequested { false };
    std::atomic<bool> snapshotFresh { false };
    AnalysisSnapshot pendingSnapshot;          // written on audio thread while
                                               // snapshotFresh is false, read after true
    AutoMixBrain::Result lastResult;

    // Detected key from the last LEARN pass (for the "Auto" key/scale modes)
    std::atomic<int>  autoKeyRoot  { -1 };
    std::atomic<bool> autoKeyMajor { true };

    // ---- Phase 3: neural analysis of the LEARN recording ---------------------
    void finishAutoMix (const AnalysisSnapshot& snapshot, const juce::String& engineNote);

    std::unique_ptr<CrepeAnalyzer> crepe;      // lazy-created on message thread
    std::vector<float> learn16k;               // 16 kHz mono LEARN recording
    std::atomic<int>   learn16kLen { 0 };
    juce::LagrangeInterpolator learnResampler;
    double resampleRatio = 2.75625;            // fs / 16000
    static constexpr int learn16kCapacity = 16000 * 120;   // 2 minutes

    // Guards async message-thread callbacks against use-after-free
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);

    juce::ThreadPool aiPool { 1 };             // declared last → destroyed first

    // Cached raw-value pointers (lock-free reads on audio thread)
    std::unordered_map<juce::String, std::atomic<float>*> raw;
    std::atomic<float>* rawParam (const char* id) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalForgeProcessor)
};
} // namespace vf
