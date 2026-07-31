#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace vf
{
// ============================================================================
//  KeyDetector — musical key, MODE and MODULATION analysis for a vocal take.
//  ---------------------------------------------------------------------------
//  WHAT WAS WRONG BEFORE. Key detection was a single Krumhansl-Kessler
//  correlation over ONE pitch-class histogram covering the whole LEARN pass,
//  testing only major and minor. Three consequences:
//    * MODULATION WAS INVISIBLE. A song that changes key produced one blended
//      histogram and therefore one wrong answer for the whole take.
//    * MODES WERE INVISIBLE. A Dorian vocal reads as its relative major, and a
//      Phrygian vocal as its relative minor — which matters enormously here,
//      because Drill and Trap lean Phrygian/harmonic-minor and Neo Soul leans
//      Dorian. Tuning a Phrygian melody to "natural minor" flattens the one
//      interval that defines the style.
//    * EVERY FRAME COUNTED EQUALLY. A low-confidence frame on a consonant
//      pushed the histogram as hard as a confidently-tracked held note.
//
//  WHAT THIS DOES INSTEAD:
//    1. CONFIDENCE-WEIGHTED histogram — each frame contributes in proportion to
//       how sure the tracker was, so guesses no longer outvote clear notes.
//    2. SIX MODAL PROFILES (Major, Natural Minor, Harmonic Minor, Dorian,
//       Mixolydian, Phrygian) which map 1:1 onto the engine's existing scale
//       types, so a detection can be applied directly.
//    3. SEGMENTED ANALYSIS + VITERBI smoothing over the key sequence. Windows
//       are scored independently, then decoded with a strong self-transition
//       bias: a key change must be supported by sustained evidence, not one
//       ambiguous phrase. This is the same tool the pitch tracker uses, for the
//       same reason — it turns per-window guesses into a stable reading while
//       still allowing a genuine modulation through.
//    4. HONEST AMBIGUITY. A monophonic vocal is genuinely weaker evidence than
//       full harmony: C major and A minor share all seven pitch classes, and a
//       melody that never lands firmly on a tonic cannot distinguish them. The
//       detector reports that ambiguity instead of inventing confidence, and
//       flags chromatic/atonal passages rather than forcing a key onto them.
//
//  Pure and headless (juce_core only) so it is unit-testable offline.
// ============================================================================

/** Modes the detector can identify. The values map DIRECTLY onto
    `RetuneParams::scaleType` so a detection can drive the corrector. */
enum class KeyMode
{
    Unknown       = -1,
    Major         = 1,   // scaleType 1
    NaturalMinor  = 2,   // scaleType 2
    HarmonicMinor = 3,   // scaleType 3
    Dorian        = 4,   // scaleType 4
    Mixolydian    = 5,   // scaleType 5
    Phrygian      = 6    // scaleType 6
};

struct KeyEstimate
{
    int     root       = -1;      // 0 = C … 11 = B, -1 = undetermined
    KeyMode mode       = KeyMode::Unknown;
    float   confidence = 0.0f;    // 0..1
    float   score      = 0.0f;    // raw correlation (diagnostic)

    bool isValid() const noexcept { return root >= 0 && mode != KeyMode::Unknown; }

    /** "C major", "F# Dorian", … */
    juce::String name() const;
    /** The `scaleType` the retune engine should use (0 = chromatic fallback). */
    int scaleType() const noexcept { return mode == KeyMode::Unknown ? 0 : (int) mode; }
};

/** One stretch of the take that holds a single key. */
struct KeySegment
{
    double      startSec = 0.0;
    double      endSec   = 0.0;
    KeyEstimate key;
};

struct KeyAnalysis
{
    KeyEstimate global;                  // best single answer for the whole take
    std::vector<KeySegment> timeline;    // one entry per detected key region
    std::vector<KeyEstimate> alternates; // runners-up for the global key (≤3)

    bool modulationDetected = false;     // more than one key region
    bool ambiguous          = false;     // near-tie or relative major/minor clash
    bool chromatic          = false;     // no profile fits — atonal/chromatic
    juce::String summary;                // plain-language explanation

    bool isValid() const noexcept { return global.isValid(); }
};

class KeyDetector
{
public:
    /** One analysis frame. `confidence` 0..1 from the tracker; unvoiced frames
        should simply be omitted (or passed with hz <= 0). */
    struct Frame
    {
        float  hz         = 0.0f;
        float  confidence = 1.0f;
        double timeSec    = 0.0;
    };

    /** Full analysis with timeline + modulation detection. */
    static KeyAnalysis analyse (const std::vector<Frame>& frames);

    /** Convenience for callers that only have a flat list of voiced pitches
        (the shape LEARN already accumulates). `frameRateHz` converts index to
        time; confidence is assumed uniform. */
    static KeyAnalysis analyseFrames (const std::vector<float>& hz, double frameRateHz);

    /** Name of a mode, e.g. "Dorian". */
    static juce::String modeName (KeyMode m);

    /** Window length used for segmentation, in seconds. Long enough to contain
        a musical phrase, short enough to localise a modulation. */
    static constexpr double kWindowSec = 4.0;
    static constexpr double kHopSec    = 2.0;   // 50% overlap

    /** Below this correlation nothing fits well enough to call a key. */
    static constexpr float kChromaticCeiling = 0.32f;

    /** A key change must be SUSTAINED to count. A two-second wobble where one
        window happens to fit a neighbouring key is an analysis artifact, not a
        modulation; segments shorter than this are absorbed into their neighbour. */
    static constexpr double kMinSegmentSec = 6.0;

    /** A key needs harmonic evidence, not just notes. Two pitch classes cannot
        distinguish C major from A minor from E Phrygian — so below this many
        distinct classes the detector reports "undetermined" rather than guessing. */
    static constexpr int kMinPitchClasses = 4;
};
} // namespace vf
