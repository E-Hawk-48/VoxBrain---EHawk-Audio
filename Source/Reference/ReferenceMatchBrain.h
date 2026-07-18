#pragma once
#include <juce_core/juce_core.h>
#include "ReferenceProfile.h"
#include <vector>

namespace vf
{
// ============================================================================
//  ReferenceMatchBrain
//  ---------------------------------------------------------------------------
//  The "reverse-engineer": turns a measured ReferenceProfile into an EDITABLE
//  VoxBrain chain plan that approximates the reference's ENGINEERING choices
//  (EQ contour, dynamics, de-essing, saturation, space, width, loudness…) while
//  never touching the user's pitch, timbre or performance.
//
//  Like AutoMixBrain it is a pure function that emits {parameterID → value}
//  targets in NATURAL units, each carrying an engineer's rationale — but it adds
//  a per-decision CONFIDENCE so the UI can flag speculative estimates, and a
//  list of complementary rack-module insertions (width / air) for characters the
//  fixed chain can't cover. Nothing is applied here; the caller (apply workflow)
//  decides what to accept.
//
//  Future-proofing: add a new Decision to cover a new module without touching
//  existing ones; a genre-aware or relative-to-user variant can wrap `match`.
// ============================================================================
class ReferenceMatchBrain
{
public:
    struct ParamTarget
    {
        juce::String parameterId;   // vf::param id
        float        value;         // natural units (matches the APVTS range)
    };

    struct Decision
    {
        juce::String             area;        // "EQ", "Compression", …
        std::vector<ParamTarget> targets;     // applied only if the user accepts
        juce::String             rationale;   // engineer's explanation (shown in UI)
        float                    confidence = 0.0f;   // 0..1
    };

    struct RackInsertion
    {
        juce::String moduleTypeId;  // registry id, e.g. "stereo_width"
        juce::String name;          // display name
        juce::String rationale;
        float        confidence = 0.0f;
    };

    struct Result
    {
        std::vector<Decision>      decisions;      // fixed-chain moves
        std::vector<RackInsertion> rackInserts;    // complementary rack modules
        juce::String               presetName;     // generated name for a preset
        juce::String               summary;        // multi-line coach report
        float                      overallConfidence = 0.0f;
    };

    /** Pure function: measured reference profile → editable chain plan. */
    static Result match (const ReferenceProfile& ref);
};
} // namespace vf
