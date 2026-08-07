#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Analysis/AnalysisEngine.h"
#include "DSP/VocalChain.h"
#include "DSP/ChainOrder.h"
#include "Brain/AutoMixBrain.h"
#include "Brain/ModuleIntelligence.h"
#include "AI/CrepeAnalyzer.h"
#include "Preset/PresetManager.h"
#include "Preset/PresetLibrary.h"
#include "Preset/AiPresetGenerator.h"
#include "Preset/Marketplace.h"
#include "Update/UpdateChecker.h"
#include "Modules/ModuleRack.h"
#include "Modules/ModuleAdvisor.h"
#include "Reference/ReferenceImport.h"
#include "Reference/ReferenceApply.h"

namespace vf
{
// ============================================================================
//  VoxBrainProcessor — plugin entry point.
//  LEARN workflow:
//    1. GUI calls setLearning(true); analysis accumulates while audio plays.
//    2. GUI calls setLearning(false); snapshot is finalised on the audio
//       thread and picked up here; AutoMixBrain runs on the message thread
//       and writes every decision into the APVTS (visible to host automation).
// ============================================================================
class VoxBrainProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    VoxBrainProcessor();
    ~VoxBrainProcessor() override;

    // ---- AudioProcessor ----------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "VoxBrain"; }
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

    // ---- VoxBrain API (GUI) ----------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    AnalysisEngine& getAnalysis() noexcept                { return analysis; }
    VocalChain&     getChain()    noexcept                { return chain; }

    void setLearning (bool shouldLearn);
    bool isLearning() const noexcept                      { return analysis.isLearning(); }

    /** Chat entry point: snapshots for undo, then applies the requested moves. */
    juce::String applyChatMessage (const juce::String& message);

    PresetManager& getPresets() noexcept                  { return presets; }
    UpdateChecker& getUpdater() noexcept                  { return updater; }
    mods::ModuleRack& getRack() noexcept                  { return rack; }

    /** AI Reference Mix Analyzer — drag-drop file import + background analysis.
        Owned here so an analysis survives the editor closing/reopening. */
    ReferenceImport& getReferenceImport() noexcept        { return referenceImport; }

    // ---- Unified reorderable chain (message thread) ------------------------
    /** The current chain order (fixed stages + rack tokens, in playing order). */
    std::vector<ChainItem> getChainOrder() const;
    /** Replace the order; it is repaired against the rack before taking effect. */
    void setChainOrder (std::vector<ChainItem> order);
    /** Drag-reorder helper: move the item at `from` so it sits at `to`. */
    void moveChainItem (int from, int to);
    /** Apply a full VISUAL arrangement of the chain in one atomic edit.
        `order` is the stage/rack-token sequence (rack tokens are anonymous);
        `rackIdsInOrder` lists the rack module instanceIds in that same visual
        order, so the rack NODE list is reordered to match (the Nth rack token in
        the chain runs the Nth rack node). This is what makes reordering rack
        modules among themselves — and inserting one at a chosen slot — actually
        change the sound instead of silently mapping to the wrong node. Macros
        follow their modules; one version bump. */
    void applyChainArrangement (std::vector<ChainItem> order,
                                const juce::StringArray& rackIdsInOrder);
    /** Re-sync the order with the rack after modules are added/removed. */
    void syncChainOrderWithRack();
    /** User-triggered AI rack build: the proven default front-end (when the rack
        is empty) plus any advisor picks from the last LEARN that aren't present. */
    void autoBuildRack();
    /** "Set this module up for my voice" — re-dial ANY rack module (including one
        the user inserted by hand) from the last LEARN analysis, using the AI
        Engineer's knowledge base. One undo step. No-op without a valid analysis. */
    void aiDialRackModule (const juce::String& instanceId);

    // ---- Reference apply / compare (phase 4) -------------------------------
    /** Apply the whole suggested chain (params + rack inserts). One undo. */
    void applyReferenceMatch (const ReferenceMatchBrain::Result& m);
    /** Apply a single suggested decision. One undo. */
    void applyReferenceDecision (const ReferenceMatchBrain::Decision& d);
    /** Insert a single suggested complementary rack module. One undo. */
    void applyReferenceRackInsert (const ReferenceMatchBrain::RackInsertion& ins);
    /** Non-destructive A/B: flip between the pre-apply original and the applied
        settings (full state incl. rack). Does nothing until something is applied. */
    void toggleReferenceCompare();
    void resetReferenceCompare() noexcept;          // call when a new reference lands
    bool hasReferenceCompare() const noexcept        { return haveRefOriginal && refApplied.valid; }
    bool referenceShowingOriginal() const noexcept   { return refShowingOriginal; }

    // ---- Reference → preset / community share (phase 5) --------------------
    /** Build a preset from the reference's suggested chain (params + rack +
        analysis metadata), add it to the library and write a .vbpreset. */
    Preset saveReferenceAsPreset (const ReferenceResult& r);
    /** Build + upload the match to the (offline) community marketplace. */
    bool   shareReferenceToCommunity (const ReferenceResult& r, juce::String& errorOut);

    /** Module-level AI recommendations from the last LEARN pass (message thread). */
    std::vector<mods::ModuleSuggestion> suggestModules() const;

    // ---- Preset ecosystem (factory + user + AI + community) --------------
    PresetLibrary& getPresetLibrary() noexcept        { return presetLibrary; }
    /** Build an AI preset from the last LEARN analysis (does not apply it). */
    Preset generateAiPreset() const                   { return AiPresetGenerator::generate (lastSnapshot); }
    /** Snapshot the current chain + rack into a shareable preset. */
    Preset captureCurrentAsPreset();
    /** Load a full preset (params + rack). Pushes undo so it's revertible. */
    void applyPreset (const Preset& p);


    // ---- Voice Changer ------------------------------------------------------
    //  Applies a whole character (see DSP/VoiceCharacter.h) as one undoable
    //  batch, honouring module locks exactly like AutoMix and the chat engine.
    //  Selecting "Off" (index 0) writes nothing — it stops imposing a character
    //  rather than trying to guess what the voice sounded like before.
    //  Returns the plain-language description, or an empty string for Off.
    juce::String applyVoiceCharacter (int menuIndex);
    juce::String applyVoiceCharacterById (const juce::String& id);
    /** The character currently selected in the menu (index, 0 = Off). */
    int  currentVoiceCharacter() const;

    /** Last auto-mix result (message thread only). */
    const AutoMixBrain::Result& getLastResult() const noexcept { return lastResult; }
    std::function<void()> onAutoMixApplied;   // GUI callback

private:
    void timerCallback() override;
    ChainParams readChainParams() const;
    void applyBrainResult (const AutoMixBrain::Result& r);
    bool isModuleLocked (const juce::String& paramId) const;


    // LEARN → auto-populate the rack with complementary polish (additive to the
    // fixed chain; only when the rack is empty so it never clobbers a user rack).
    void autoBuildRackFromAnalysis();
    void syncRackMacros();   // push each rack module's values into its slot macros

    // Reference apply/compare internals ------------------------------------
    struct RefState { PresetManager::Snapshot params; juce::String rackXml; bool valid = false; };
    RefState captureRefState() const;
    void     restoreRefState (const RefState& s);
    void     captureRefOriginalIfNeeded();
    void     writeReferencePlan (const std::vector<refapply::Write>& plan);
    void     addRackInsertInternal (const ReferenceMatchBrain::RackInsertion& ins);
    bool     rackHasType (const juce::String& typeId) const;
    RefState refOriginal, refApplied;
    bool     haveRefOriginal = false;
    bool     refShowingOriginal = false;
    Preset   buildReferencePreset (const ReferenceResult& r) const;

    AnalysisEngine analysis;
    VocalChain     chain;
    int            preparedBlockSize = 512;   // max block our internal buffers are sized for
    PresetManager  presets { apvts };   // declared after apvts → constructed after it
    PresetLibrary  presetLibrary;       // factory + user + AI + community index
    LocalMarketplace marketplace { presetLibrary };   // offline community share
    UpdateChecker  updater;

    // ---- Unified chain order --------------------------------------------
    //  Master copy lives on the message thread under `chainOrderLock`. The audio
    //  thread keeps its OWN preallocated cache and refreshes it only when the
    //  version changes AND the try-lock succeeds — so a reorder never blocks or
    //  drops a block (worst case the old order plays for one more buffer).
    static constexpr int kMaxChainItems = 64;
    std::vector<ChainItem> chainOrderMaster;
    juce::SpinLock         chainOrderLock;
    std::atomic<uint32_t>  chainOrderVersion { 1 };
    ChainItem              chainOrderCache[kMaxChainItems];
    int                    chainOrderCacheCount = 0;
    uint32_t               chainOrderCacheVersion = 0;
    void refreshChainOrderCache();   // audio thread

    // AI Reference Mix Analyzer (background decode + analysis; message/bg thread).
    ReferenceImport referenceImport;

    // Modular rack (runs after the fixed chain; empty by default = passthrough).
    mods::ModuleRack rack;
    std::atomic<float>* rackOnPtr = nullptr;
    std::atomic<float>* rackMacroPtr[mods::ModuleRack::Automation::Slots]
                                    [mods::ModuleRack::Automation::Macros] { {} };
    mods::ModuleRack::Automation readRackAutomation() const;

    // Learn handshake: audio thread finalises snapshot → message thread applies
    std::atomic<bool> learnStopRequested { false };
    // Audio thread sets this after stopping accumulation; the message-thread
    // timer then builds the snapshot (heavy: sorting + allocation).
    std::atomic<bool> learnFinalizePending { false };
    std::atomic<bool> snapshotFresh { false };
    AnalysisSnapshot pendingSnapshot;          // written on audio thread while
                                               // snapshotFresh is false, read after true
    AutoMixBrain::Result lastResult;
    AnalysisSnapshot     lastSnapshot;   // last LEARN snapshot (for module advisor)

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxBrainProcessor)
};
} // namespace vf
