#pragma once
#include "VocalChain.h"
#include <vector>

namespace vf
{
// ============================================================================
//  ChainOrder — the UNIFIED, reorderable signal chain.
//  ---------------------------------------------------------------------------
//  VoxBrain used to have two separate worlds: a hard-wired VocalChain (fixed
//  order, compiled in) and a dynamic ModuleRack that always ran after it. This
//  model merges them into ONE ordered list the user can rearrange:
//
//     [ Stage(Gate) ][ RackModule ][ Stage(Eq) ][ Stage(Comp) ] …
//
//  A `RackModule` item is a TOKEN, not a name: the Nth rack token runs the Nth
//  module in the rack's own node list. So reordering rack modules relative to
//  each other = reordering the rack; moving a rack module between fixed stages
//  = moving its token in this list. That keeps the audio-thread representation
//  tiny and POD (no strings), which is what makes it real-time safe.
//
//  The DEFAULT order is the historical chain (all 11 stages in enum order, then
//  the rack), so existing sessions/presets sound identical until something is
//  dragged. `repair()` guarantees the list is always playable: every stage
//  exactly once, and exactly one token per rack module.
// ============================================================================
struct ChainItem
{
    enum class Kind : int { Stage = 0, RackModule = 1 };

    Kind              kind  = Kind::Stage;
    VocalChain::Stage stage = VocalChain::Stage::Retune;   // only when kind == Stage

    static ChainItem makeStage (VocalChain::Stage s) { return { Kind::Stage, s }; }
    static ChainItem makeRack()                      { return { Kind::RackModule, VocalChain::Stage::Retune }; }

    bool isStage() const noexcept { return kind == Kind::Stage; }
    bool isRack()  const noexcept { return kind == Kind::RackModule; }
};

class ChainOrder
{
public:
    /** Today's shipping order: the 11 stages in enum order, then the rack. */
    static std::vector<ChainItem> defaultOrder (int numRackModules);

    /** Force the list into a playable state, preserving the user's arrangement
        wherever possible: drops duplicate/unknown stages, appends any missing
        stage, and makes the rack-token count match `numRackModules`. */
    static void repair (std::vector<ChainItem>& order, int numRackModules);

    /** Number of RackModule tokens in the list. */
    static int rackTokenCount (const std::vector<ChainItem>& order);

    // ---- state (saved with the session + presets) -------------------------
    static std::unique_ptr<juce::XmlElement> toXml (const std::vector<ChainItem>& order);
    static std::vector<ChainItem> fromXml (const juce::XmlElement* xml, int numRackModules);
};
} // namespace vf
