#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>

namespace vf
{
// ============================================================================
//  PresetManager — session workflow layer on top of the APVTS.
//
//  * Snapshots        : full parameter state captured as normalised (0..1)
//                       values, so every parameter type round-trips exactly.
//  * Undo / Redo      : stacks of snapshots. The processor pushes one BEFORE
//                       each auto-mix / chat batch / preset load, so a single
//                       Undo cleanly reverts the last "big" change.
//  * A / B compare    : two slots. Toggling saves the current edit into the
//                       active slot and recalls the other (the inactive slot is
//                       auto-cloned on first use so B starts as a copy of A).
//  * Presets          : built-in factory presets (partial overrides on top of
//                       the defaults) plus user .vbpreset files under
//                       <userAppData>/VoxBrain/Presets.
//
//  All operations run on the message thread (parameter writes via
//  setValueNotifyingHost keep host automation + UI attachments in sync).
// ============================================================================
class PresetManager
{
public:
    using Snapshot = std::map<juce::String, float>;   // paramID -> normalised 0..1

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    // ---- snapshots ---------------------------------------------------------
    Snapshot capture() const;
    void     restore (const Snapshot& s);

    // ---- undo / redo -------------------------------------------------------
    void pushUndo (const juce::String& label);   // snapshot current state first
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    juce::String undoLabel() const { return undoStack.empty() ? juce::String() : undoStack.back().first; }
    juce::String redoLabel() const { return redoStack.empty() ? juce::String() : redoStack.back().first; }

    // ---- A / B compare -----------------------------------------------------
    void toggleAB();       // store current into active slot, recall the other
    void copyToOther();    // duplicate current into the inactive slot
    bool isSlotB() const { return activeSlot == 1; }

    // ---- presets -----------------------------------------------------------
    juce::StringArray factoryNames() const;
    juce::StringArray userPresetNames() const;        // file stems, sorted
    void loadFactory (int index);                     // pushes undo
    bool loadUserPreset (const juce::String& stem);   // pushes undo
    bool saveUserPreset (const juce::String& name);
    juce::File userPresetDir() const;

    std::function<void()> onStateChanged;             // UI refresh hook

private:
    struct Factory { juce::String name; std::vector<std::pair<juce::String, float>> overrides; };
    const std::vector<Factory>& factories() const;

    Snapshot defaults() const;
    void notify() { if (onStateChanged) onStateChanged(); }
    static juce::String sanitize (const juce::String& name);

    juce::AudioProcessorValueTreeState& apvts;

    std::vector<std::pair<juce::String, Snapshot>> undoStack, redoStack;
    static constexpr size_t maxUndo = 32;

    Snapshot slotA, slotB;
    bool slotPopulated[2] { true, false };
    int  activeSlot = 0;                               // 0 = A, 1 = B
};
} // namespace vf
