#pragma once
#include <juce_core/juce_core.h>
#include <map>

namespace juce { class AudioProcessorValueTreeState; }   // fwd — keeps this model pure

namespace vf
{
// ============================================================================
//  PresetMeta — rich, structured metadata that turns a preset into a
//  first-class object (not just knob positions). Every field is optional and
//  round-trips through XML; unknown fields on load are ignored (forward-compat).
// ============================================================================
struct PresetMeta
{
    // ---- identity --------------------------------------------------------
    juce::String presetId;                    // stable UUID
    juce::String name          = "Untitled";
    juce::String creatorName   = "You";
    juce::String creatorId;                   // stable per-creator id
    juce::String version       = "1.0.0";
    juce::String minAppVersion = "0.2.0";     // VoxBrain compatibility floor
    juce::int64  createdUnix    = 0;
    juce::int64  updatedUnix    = 0;

    // ---- description & classification ------------------------------------
    juce::String description;
    juce::String genre;                       // "Pop", "Trap", ...
    juce::String vocalStyle;                  // "Melodic Rap", "Belted", ...
    juce::String mood;                        // "Warm", "Aggressive", ...
    int          energy        = 5;           // 1..10
    juce::String vocalType;                   // "Lead", "Backing", "Adlib"
    juce::String vocalGender;                 // optional
    juce::String vocalRange;                  // optional, e.g. "Tenor"
    juce::String keyCompat;                   // optional
    juce::String tempoCompat;                 // optional, e.g. "60-90 BPM"
    juce::String recQualityRec;               // recommended recording quality
    juce::String micRec;                      // mic recommendation

    // ---- performance estimates ------------------------------------------
    float        cpuEstimate   = 0.0f;        // %
    float        latencyMs     = 0.0f;

    // ---- discovery -------------------------------------------------------
    juce::StringArray tags;
    juce::StringArray categories;

    // ---- AI --------------------------------------------------------------
    bool         aiGenerated   = false;
    float        aiConfidence  = 0.0f;        // 0..1
    juce::String aiSummary;

    // ---- media (references; may be local paths or URLs) ------------------
    juce::String previewImage;
    juce::String demoAudio;

    // ---- provenance ------------------------------------------------------
    juce::String changelog;
    juce::String source        = "user";      // factory | user | ai | community
    bool         official      = false;
    bool         verifiedCreator = false;

    // ---- marketplace stats (populated from the marketplace) --------------
    int          downloads     = 0;
    int          likes         = 0;
    float        rating        = 0.0f;        // average 0..5
    int          ratingCount   = 0;
    bool         featured      = false;

    // ---- integrity -------------------------------------------------------
    juce::String contentHash;                 // SHA-256 of the payload
    juce::String signature;                   // digital signature (scaffold)

    void writeInto (juce::XmlElement& e) const;
    static PresetMeta readFrom (const juce::XmlElement& e);
};

// ============================================================================
//  Preset — metadata + the parameter overrides (natural units, sparse) +
//  optional rack routing. Portable, verifiable, backward-compatible.
// ============================================================================
class Preset
{
public:
    PresetMeta meta;
    std::map<juce::String, float> values;             // paramID -> NATURAL value
    std::unique_ptr<juce::XmlElement> rackXml;         // optional (cloned on copy)

    Preset() = default;
    Preset (const Preset& other);
    Preset& operator= (const Preset& other);
    Preset (Preset&&) noexcept = default;
    Preset& operator= (Preset&&) noexcept = default;

    // ---- capture / apply -------------------------------------------------
    /** Snapshot every parameter (+ rack) into a preset, filling default meta. */
    static Preset captureFrom (juce::AudioProcessorValueTreeState& apvts,
                               const juce::XmlElement* rack = nullptr);
    /** Reset params to default then apply this preset's overrides (+ rack). */
    void applyTo (juce::AudioProcessorValueTreeState& apvts,
                  juce::XmlElement** rackOut = nullptr) const;

    // ---- serialization ---------------------------------------------------
    std::unique_ptr<juce::XmlElement> toXml() const;         // <VoxBrainPreset>
    static Preset fromXml (const juce::XmlElement& e);        // (safe, tolerant)
    bool save (const juce::File& file) const;
    static bool load (const juce::File& file, Preset& out);   // false = invalid

    // ---- security --------------------------------------------------------
    /** Canonical SHA-256 over the sorted values + core identity. */
    juce::String computeContentHash() const;
    void seal();                          // sets contentHash (+ timestamps)
    bool verifyIntegrity() const;         // recomputed hash == stored hash
    /** True if this preset can run on the given VoxBrain version string. */
    bool compatibleWith (const juce::String& appVersion) const;

    /** Compare app version strings "a.b.c"; -1/0/1. */
    static int compareVersions (const juce::String& a, const juce::String& b);
};
} // namespace vf
