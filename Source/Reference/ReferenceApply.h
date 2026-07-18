#pragma once
#include "ReferenceMatchBrain.h"
#include <functional>
#include <vector>

namespace vf::refapply
{
// ============================================================================
//  ReferenceApply — pure planning for the "accept" workflow.
//  ---------------------------------------------------------------------------
//  Flattens a reference match (or a single decision) into an ordered list of
//  {parameterId, natural value} writes, SKIPPING any parameter whose module is
//  locked (the caller supplies the lock predicate — same policy the AutoMix
//  brain and chat engine respect). Keeping this pure (juce_core only, no APVTS)
//  means the accept logic is unit-testable; the processor just executes the
//  plan via setValueNotifyingHost.
// ============================================================================
struct Write
{
    juce::String parameterId;
    float        value;        // natural units (matches the APVTS range)
};

using LockPredicate = std::function<bool (const juce::String&)>;

inline void appendDecision (std::vector<Write>& out,
                            const ReferenceMatchBrain::Decision& d,
                            const LockPredicate& isLocked)
{
    for (const auto& t : d.targets)
        if (! (isLocked && isLocked (t.parameterId)))
            out.push_back ({ t.parameterId, t.value });
}

/** Writes for one accepted decision (locked params removed). */
inline std::vector<Write> planDecision (const ReferenceMatchBrain::Decision& d,
                                        const LockPredicate& isLocked)
{
    std::vector<Write> w;
    appendDecision (w, d, isLocked);
    return w;
}

/** Writes for accepting the whole match (locked params removed). Rack inserts
    are handled separately by the caller (they add modules, not params). */
inline std::vector<Write> planAll (const ReferenceMatchBrain::Result& r,
                                   const LockPredicate& isLocked)
{
    std::vector<Write> w;
    for (const auto& d : r.decisions)
        appendDecision (w, d, isLocked);
    return w;
}
} // namespace vf::refapply
