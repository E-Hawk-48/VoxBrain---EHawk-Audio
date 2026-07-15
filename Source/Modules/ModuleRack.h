#pragma once
#include "Module.h"
#include <memory>

namespace vf::mods
{
// ============================================================================
//  ModuleRack — an ordered, hot-swappable chain of live Module instances.
//
//  Structural edits (add/remove/duplicate/move) run on the message thread; the
//  audio thread reads the chain under a NON-BLOCKING try-lock, so it never
//  stalls waiting on an edit — at worst a single block passes through un-
//  processed during the microseconds an edit takes (inaudible; edits are
//  user-click-rate). No allocation, locking-that-blocks, or logging on the
//  audio thread. Module instances persist across reorders, so their DSP state
//  is continuous.
// ============================================================================
class ModuleRack
{
public:
    struct Node
    {
        std::shared_ptr<Module> module;
        juce::String instanceId;      // unique, stable for the life of the instance
        juce::String typeId;          // registry id (for duplicate / save)
        std::atomic<bool>  bypass { false };
        std::atomic<bool>  solo   { false };
        std::atomic<bool>  lock   { false };   // protects from AI edits (see advisor)
        std::atomic<float> cpu    { 0.0f };    // % of block time used, smoothed
    };

    struct NodeInfo   // immutable snapshot for the UI (message thread)
    {
        juce::String instanceId, typeId, name;
        Category category = Category::Utility;
        bool bypass = false, solo = false, lock = false;
        float cpu = 0.0f;
        int latency = 0;
    };

    // ---- DAW automation bridge -------------------------------------------
    //  VST3 can't add parameters when a module is inserted, so the host
    //  automates a FIXED pool of generic "slot macro" params; the processor
    //  fills this struct from them each block and the rack maps slot i's macros
    //  onto the i-th module's first parameters.
    struct Automation
    {
        static constexpr int Slots  = 8;
        static constexpr int Macros = 6;
        bool  rackOn = true;
        float macro[Slots][Macros] {};   // normalised 0..1
    };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** Audio thread. Real-time safe. If `automation` is non-null its macros are
        applied to the modules (by slot index) before processing. */
    void process (juce::AudioBuffer<float>& buffer, const Automation* automation = nullptr);

    /** Total added latency (sum of active modules) for host PDC. */
    int latencySamples() const;

    // ---- structural edits (message thread) -------------------------------
    /** Insert a new instance of `typeId` at `index` (-1 = append). Returns the
        new instanceId, or empty on failure (unknown/unimplemented type). */
    juce::String addModule (juce::StringRef typeId, int index = -1);
    bool         removeModule (juce::StringRef instanceId);
    juce::String duplicateModule (juce::StringRef instanceId);
    bool         moveModule (juce::StringRef instanceId, int newIndex);

    void setBypass (juce::StringRef instanceId, bool);
    void setSolo   (juce::StringRef instanceId, bool);
    void setLock   (juce::StringRef instanceId, bool);

    /** Access a module to read/edit its params (message thread). */
    Module* find (juce::StringRef instanceId);
    int     indexOf (juce::StringRef instanceId) const;
    int     size() const;

    std::vector<NodeInfo> snapshot() const;

    // ---- state (save routing with sessions / presets) --------------------
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement* xml);

private:
    Node* nodePtr (juce::StringRef instanceId);
    juce::String makeInstanceId (juce::StringRef typeId);

    juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };
    bool prepared = false;

    std::vector<std::unique_ptr<Node>> nodes;   // guarded by lock
    juce::SpinLock lock;                         // audio thread try-locks; edits full-lock
    int idCounter = 0;
};
} // namespace vf::mods
