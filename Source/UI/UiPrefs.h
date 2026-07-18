#pragma once
#include <juce_core/juce_core.h>
#include <memory>

namespace vf
{
// ============================================================================
//  UiPrefs — global, per-user interface preferences (NOT per-session/preset).
//  Follows the same "mutable inline globals + XML persistence" pattern as the
//  theme:: namespace, but kept separate because a UI density/help preference is
//  not a colour palette. Persisted to <userAppData>/VoxBrain/ui.xml so the
//  choice survives across sessions and DAWs, independent of any plugin state.
//
//  Backwards-compatible: an old install with no ui.xml (or an XML missing an
//  attribute) simply falls back to these defaults, so nothing breaks.
// ============================================================================
namespace uiprefs
{
    // Simple mode hides advanced/secondary controls to reduce clutter for
    // newcomers; Advanced mode (default off = Simple? — see below) shows all.
    // Default is Advanced (simpleMode=false) so existing users see no change.
    inline bool simpleMode = false;

    // Hover help. On by default so the built-in tooltips are discoverable; the
    // user can silence them from the header if they find them noisy.
    inline bool tooltipsOn = true;

    // Persistence -----------------------------------------------------------
    juce::File file();                                   // <userAppData>/VoxBrain/ui.xml
    std::unique_ptr<juce::XmlElement> toXml();
    void fromXml (const juce::XmlElement* e);            // null-safe, tag-checked
    void load();                                         // no-op if file absent
    void save();
}
} // namespace vf
