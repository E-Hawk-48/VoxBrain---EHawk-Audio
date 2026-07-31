#pragma once
#include <juce_core/juce_core.h>

namespace vf
{
// ============================================================================
//  ModuleCommands — "add a de-esser", "remove the chorus", "set up the
//  compressor for my voice".
//  ---------------------------------------------------------------------------
//  The chat assistant could always edit the FIXED chain (EQ, comp, reverb…) via
//  the APVTS, but it had no way to touch the module RACK — so the one thing the
//  AI Engineer now knows best (how to dial any module for this specific vocal,
//  see Brain/ModuleIntelligence) was unreachable by voice/typing.
//
//  This is deliberately a SEPARATE, PURE translation unit: it depends only on
//  juce_core plus the module registry, never on the APVTS/GUI stack. That keeps
//  the language parsing unit-testable on its own (the full ChatEngine can't be
//  linked in a headless test because juce_audio_processors drags in the GUI
//  module), which is where the interesting edge cases live.
// ============================================================================
struct ModuleCommand
{
    enum class Verb
    {
        None = 0,   // the clause isn't a module command
        Add,        // insert it (and dial it in for this vocal)
        Remove,     // take it out
        Dial        // re-tune an existing one to the analysed voice
    };

    Verb         verb = Verb::None;
    juce::String moduleId;      // registry type id, e.g. "de_esser"
    juce::String moduleName;    // display name, e.g. "De-Esser"

    bool isValid() const noexcept { return verb != Verb::None && moduleId.isNotEmpty(); }
};

class ModuleCommands
{
public:
    /** Interpret ONE clause. Returns an invalid command when the clause isn't
        asking for a module change (so ordinary mixing phrases like "brighter"
        fall through to the vocabulary rules untouched). */
    static ModuleCommand parse (const juce::String& lowercaseClause);

    /** Strip everything but letters and digits and lowercase the result, so
        "De-Esser", "de esser" and "deesser" all compare equal. */
    static juce::String normalise (const juce::String& text);
};
} // namespace vf
