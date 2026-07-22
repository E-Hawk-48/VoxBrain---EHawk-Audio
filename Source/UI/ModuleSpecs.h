#pragma once
#include "ModuleStrip.h"          // ModuleCard::KnobSpec / ComboSpec
#include "../DSP/VocalChain.h"    // VocalChain::Stage
#include <vector>

namespace vf
{
// ============================================================================
//  StageSpec — everything the UI needs to build the controls for ONE fixed
//  VocalChain stage. Single source of truth, shared by the unified ChainView's
//  focused panel and the legacy ModuleStrip, so a knob/tooltip is defined once.
// ============================================================================
struct StageSpec
{
    juce::String title;                              // "EQ", "Compressor", …
    const char*  bypassId = nullptr;                 // vf::param on/off id
    const char*  lockId   = nullptr;                 // vf::param lock id
    std::vector<ModuleCard::KnobSpec>  knobs;
    std::vector<ModuleCard::ComboSpec> combos;
    bool advanced = false;                           // hidden in Simple mode
};

/** Controls for a fixed chain stage. */
const StageSpec& specForStage (VocalChain::Stage s);
} // namespace vf
