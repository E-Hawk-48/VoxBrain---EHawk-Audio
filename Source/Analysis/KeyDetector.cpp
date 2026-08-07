#include "KeyDetector.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace vf
{
namespace
{
    static const char* kNoteNames[12] =
        { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    // ------------------------------------------------------------------
    //  Mode definitions.
    //  Rather than hand-typing six 12-element magic vectors, each mode is
    //  described by what it IS musically — its scale degrees, its third, and
    //  the ONE degree that distinguishes it from its parent scale — and the
    //  profile is built from a tonal hierarchy. That keeps the table readable
    //  and makes the reasoning auditable.
    // ------------------------------------------------------------------
    struct ModeDef
    {
        KeyMode     mode;
        const char* name;
        int         degrees[7];     // semitones from the root
        int         third;          // 3 = minor third, 4 = major third
        int         characteristic; // the degree that separates it from its parent
        KeyMode     parent;         // what it collapses to if `characteristic` is unsung
    };

    // The `characteristic` degree matters as much as the third:
    //   Dorian vs Natural Minor  → natural 6th (9)
    //   Mixolydian vs Major      → flat 7th   (10)
    //   Phrygian vs Natural Minor→ flat 2nd   (1)
    //   Harmonic vs Natural Minor→ natural 7th(11)
    // If a melody never sings that degree, the two modes are genuinely
    // indistinguishable and the detector says so instead of guessing.
    const ModeDef kModes[] =
    {
        { KeyMode::Major,         "major",          { 0, 2, 4, 5, 7, 9, 11 },  4, 4,  KeyMode::Major },
        { KeyMode::NaturalMinor,  "minor",          { 0, 2, 3, 5, 7, 8, 10 },  3, 3,  KeyMode::NaturalMinor },
        { KeyMode::HarmonicMinor, "harmonic minor", { 0, 2, 3, 5, 7, 8, 11 },  3, 11, KeyMode::NaturalMinor },
        { KeyMode::Dorian,        "Dorian",         { 0, 2, 3, 5, 7, 9, 10 },  3, 9,  KeyMode::NaturalMinor },
        { KeyMode::Mixolydian,    "Mixolydian",     { 0, 2, 4, 5, 7, 9, 10 },  4, 10, KeyMode::Major },
        { KeyMode::Phrygian,      "Phrygian",       { 0, 1, 3, 5, 7, 8, 10 },  3, 1,  KeyMode::NaturalMinor },
    };
    constexpr int kNumModes = (int) (sizeof (kModes) / sizeof (kModes[0]));

    // Tonal hierarchy weights. Tonic and fifth anchor the key; the third sets
    // the colour; the characteristic degree is boosted so a mode can actually
    // be told apart from its parent when it IS sung.
    constexpr double kwTonic          = 6.6;
    constexpr double kwFifth          = 4.9;
    constexpr double kwThird          = 4.3;
    constexpr double kwCharacteristic = 3.9;
    constexpr double kwOtherScale     = 3.0;
    constexpr double kwNonScale       = 1.5;

    /** Build the 12-element weight profile for a mode, root = C. */
    std::array<double, 12> buildProfile (const ModeDef& m)
    {
        std::array<double, 12> p {};
        for (auto& v : p) v = kwNonScale;

        for (int d : m.degrees)
            p[(size_t) d] = kwOtherScale;

        p[(size_t) m.characteristic] = std::max (p[(size_t) m.characteristic], kwCharacteristic);
        p[(size_t) m.third]          = std::max (p[(size_t) m.third],          kwThird);
        p[7]                         = std::max (p[7],                         kwFifth);
        p[0]                         = kwTonic;
        return p;
    }

    const std::array<std::array<double, 12>, kNumModes>& profiles()
    {
        static const auto p = []
        {
            std::array<std::array<double, 12>, kNumModes> a {};
            for (int i = 0; i < kNumModes; ++i) a[(size_t) i] = buildProfile (kModes[i]);
            return a;
        }();
        return p;
    }

    /** Pearson correlation of a histogram against a profile rotated to `root`. */
    double correlate (const std::array<double, 12>& hist,
                      const std::array<double, 12>& prof, int root)
    {
        double hm = 0.0, pm = 0.0;
        for (int i = 0; i < 12; ++i) { hm += hist[(size_t) i]; pm += prof[(size_t) i]; }
        hm /= 12.0; pm /= 12.0;

        double num = 0.0, dh = 0.0, dp = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = hist[(size_t) i] - hm;
            const double b = prof[(size_t) (((i - root) % 12 + 12) % 12)] - pm;
            num += a * b; dh += a * a; dp += b * b;
        }
        return (dh > 1.0e-12 && dp > 1.0e-12) ? num / std::sqrt (dh * dp) : 0.0;
    }

    struct Scored { int root; int modeIdx; double score; };

    /** Score all 12 roots × 6 modes against one histogram, best first. */
    std::vector<Scored> scoreAll (const std::array<double, 12>& hist)
    {
        std::vector<Scored> out;
        out.reserve (12 * kNumModes);
        for (int root = 0; root < 12; ++root)
            for (int m = 0; m < kNumModes; ++m)
                out.push_back ({ root, m, correlate (hist, profiles()[(size_t) m], root) });

        std::sort (out.begin(), out.end(),
                   [] (const Scored& a, const Scored& b) { return a.score > b.score; });
        return out;
    }

    /** True when two candidates are the relative major/minor of one another —
        the classic melodic ambiguity, since they share all seven pitch classes. */
    bool areRelative (int rootA, KeyMode modeA, int rootB, KeyMode modeB)
    {
        auto isMajorish = [] (KeyMode m) { return m == KeyMode::Major || m == KeyMode::Mixolydian; };
        auto isMinorish = [] (KeyMode m) { return m == KeyMode::NaturalMinor || m == KeyMode::Dorian
                                               || m == KeyMode::Phrygian || m == KeyMode::HarmonicMinor; };
        if (isMajorish (modeA) && isMinorish (modeB))
            return ((rootA + 9) % 12) == rootB;      // relative minor is a M6 up
        if (isMinorish (modeA) && isMajorish (modeB))
            return ((rootB + 9) % 12) == rootA;
        return false;
    }

    KeyEstimate makeEstimate (const Scored& s, float confidence)
    {
        KeyEstimate e;
        e.root       = s.root;
        e.mode       = kModes[(size_t) s.modeIdx].mode;
        e.score      = (float) s.score;
        e.confidence = confidence;
        return e;
    }
}

// ============================================================================
juce::String KeyDetector::modeName (KeyMode m)
{
    for (const auto& d : kModes)
        if (d.mode == m) return d.name;
    return "unknown";
}

juce::String KeyEstimate::name() const
{
    if (! isValid()) return "undetermined";
    return juce::String (kNoteNames[juce::jlimit (0, 11, root)])
         + " " + KeyDetector::modeName (mode);
}

// ============================================================================
KeyAnalysis KeyDetector::analyseFrames (const std::vector<float>& hz, double frameRateHz)
{
    // NOTE ON TIME: LEARN keeps only VOICED frames, so wall-clock timestamps are
    // not recoverable. Using the voiced-frame index as the time axis compresses
    // silence out — which is not a defect here: the ORDER of sung material is
    // preserved, and that is what modulation detection actually needs. Times
    // reported are therefore "sung seconds", not wall-clock seconds.
    std::vector<Frame> f;
    f.reserve (hz.size());
    const double dt = frameRateHz > 0.0 ? 1.0 / frameRateHz : 0.01;
    for (size_t i = 0; i < hz.size(); ++i)
        if (hz[i] > 0.0f)
            f.push_back ({ hz[i], 1.0f, (double) i * dt });
    return analyse (f);
}

// ============================================================================
KeyAnalysis KeyDetector::analyse (const std::vector<Frame>& frames)
{
    KeyAnalysis out;

    // ---- gather usable frames ----------------------------------------
    std::vector<Frame> f;
    f.reserve (frames.size());
    for (const auto& fr : frames)
        if (fr.hz > 0.0f && fr.confidence > 0.0f)
            f.push_back (fr);

    if (f.size() < 32)
    {
        out.summary = "Not enough pitched material to determine a key.";
        return out;
    }

    const double firstT = f.front().timeSec;
    const double lastT  = f.back().timeSec;

    // ---- CONFIDENCE-WEIGHTED pitch-class histogram --------------------
    // Weighting by tracker confidence is what stops a doubtful frame on a
    // consonant from pushing as hard as a cleanly-tracked held note.
    auto histogramFor = [&f] (double t0, double t1)
    {
        std::array<double, 12> h {};
        for (const auto& fr : f)
        {
            if (fr.timeSec < t0 || fr.timeSec >= t1) continue;
            const double midi = 69.0 + 12.0 * std::log2 ((double) fr.hz / 440.0);
            const int pc = (int) (((long long) std::llround (midi)) % 12 + 12) % 12;
            h[(size_t) pc] += (double) fr.confidence;
        }
        return h;
    };

    const auto globalHist = histogramFor (firstT, lastT + 1.0);

    // ---- harmonic-evidence gate --------------------------------------
    // Frame COUNT is not evidence; pitch-class DIVERSITY is. A long held C and
    // E give hundreds of frames but cannot separate C major, A minor, C
    // Mixolydian or E Phrygian — they all contain both notes.
    {
        double total = 0.0;
        for (double v : globalHist) total += v;
        int classes = 0;
        if (total > 0.0)
            for (double v : globalHist)
                if (v / total >= 0.01) ++classes;

        if (classes < kMinPitchClasses)
        {
            out.summary = "Not enough of the scale was sung to identify a key ("
                        + juce::String (classes) + " distinct notes).";
            return out;
        }
    }

    const auto globalRank = scoreAll (globalHist);

    // ---- chromatic / atonal check ------------------------------------
    if (globalRank.empty() || globalRank[0].score < (double) kChromaticCeiling)
    {
        out.chromatic = true;
        out.summary   = "No clear key — the melody is chromatic or too sparse to fit a scale.";
        return out;
    }

    // ================================================================
    //  SEGMENTED ANALYSIS + VITERBI  → modulation detection
    // ================================================================
    struct Window { double t0, t1; std::vector<Scored> rank; };
    std::vector<Window> windows;

    for (double t = firstT; t < lastT; t += kHopSec)
    {
        const double t1 = t + kWindowSec;
        auto h = histogramFor (t, t1);
        double total = 0.0;
        for (double v : h) total += v;
        if (total < 8.0) continue;                 // too little material in this window
        windows.push_back ({ t, std::min (t1, lastT), scoreAll (h) });
    }

    // With too few windows to reason about change, report the global answer.
    if (windows.size() < 2)
    {
        const auto& best = globalRank[0];

        // Confidence from the margin over the best DIFFERENT key.
        double rival = -2.0;
        for (const auto& c : globalRank)
            if (c.root != best.root || c.modeIdx != best.modeIdx) { rival = c.score; break; }
        float conf = (float) juce::jlimit (0.0, 1.0, (best.score - rival) * 6.0 + 0.25);

        out.global = makeEstimate (best, conf);
        out.timeline.push_back ({ firstT, lastT, out.global });
    }
    else
    {
        // ---- Viterbi over the key sequence ---------------------------
        // States are the top candidates of each window. A key change must beat
        // a strong self-transition, so a single ambiguous phrase cannot flip the
        // reading — but sustained evidence of a new key still gets through.
        constexpr int kKeep = 8;                       // candidates per window
        // NOTE: these are `const`, not `constexpr`. `std::log` is only constexpr
        // as a GCC/Clang extension — MSVC rejects it (error C2131), and MSVC is
        // the shipping compiler. Runtime initialisation here costs nothing.
        const double kStay   = std::log (0.97);          // stay in the same key
        const double kSwitch = std::log (0.03 / 71.0);   // change key (rare)

        const int W = (int) windows.size();
        std::vector<std::array<double, kKeep>> score ((size_t) W);
        std::vector<std::array<int, kKeep>>    back  ((size_t) W);

        auto obs = [&] (int w, int i)
        {
            const auto& r = windows[(size_t) w].rank;
            if (i >= (int) r.size()) return -1.0e9;
            // Map correlation (−1..1) to a log-probability-like quantity.
            return (double) r[(size_t) i].score * 6.0;
        };
        auto same = [&] (int wa, int ia, int wb, int ib)
        {
            const auto& A = windows[(size_t) wa].rank;
            const auto& B = windows[(size_t) wb].rank;
            if (ia >= (int) A.size() || ib >= (int) B.size()) return false;
            return A[(size_t) ia].root == B[(size_t) ib].root
                && A[(size_t) ia].modeIdx == B[(size_t) ib].modeIdx;
        };

        for (int i = 0; i < kKeep; ++i) { score[0][(size_t) i] = obs (0, i); back[0][(size_t) i] = 0; }

        for (int w = 1; w < W; ++w)
            for (int i = 0; i < kKeep; ++i)
            {
                double best = -1.0e18; int bp = 0;
                for (int j = 0; j < kKeep; ++j)
                {
                    const double tr = same (w, i, w - 1, j) ? kStay : kSwitch;
                    const double v  = score[(size_t) (w - 1)][(size_t) j] + tr;
                    if (v > best) { best = v; bp = j; }
                }
                score[(size_t) w][(size_t) i] = best + obs (w, i);
                back[(size_t) w][(size_t) i]  = bp;
            }

        int endBest = 0;
        for (int i = 1; i < kKeep; ++i)
            if (score[(size_t) (W - 1)][(size_t) i] > score[(size_t) (W - 1)][(size_t) endBest])
                endBest = i;

        std::vector<int> path ((size_t) W);
        path[(size_t) (W - 1)] = endBest;
        for (int w = W - 1; w > 0; --w)
            path[(size_t) (w - 1)] = back[(size_t) w][(size_t) path[(size_t) w]];

        // ---- merge consecutive windows that share a key into segments ----
        for (int w = 0; w < W; ++w)
        {
            const auto& r = windows[(size_t) w].rank;
            const int idx = path[(size_t) w];
            if (idx >= (int) r.size()) continue;
            const auto& c = r[(size_t) idx];

            if (! out.timeline.empty()
                && out.timeline.back().key.root == c.root
                && out.timeline.back().key.mode == kModes[(size_t) c.modeIdx].mode)
            {
                out.timeline.back().endSec = windows[(size_t) w].t1;   // extend
            }
            else
            {
                KeySegment seg;
                seg.startSec = windows[(size_t) w].t0;
                seg.endSec   = windows[(size_t) w].t1;
                seg.key      = makeEstimate (c, (float) juce::jlimit (0.0, 1.0, c.score * 1.4));
                out.timeline.push_back (seg);
            }
        }

        // ---- absorb short-lived segments ------------------------------
        // Viterbi already resists flapping, but a phrase that genuinely leans
        // toward a neighbouring key for a couple of windows would still emit a
        // segment. Musically that is not a modulation, so anything shorter than
        // kMinSegmentSec is folded into whichever neighbour is longer.
        bool merged = true;
        while (merged && out.timeline.size() > 1)
        {
            merged = false;
            for (size_t i = 0; i < out.timeline.size(); ++i)
            {
                const double dur = out.timeline[i].endSec - out.timeline[i].startSec;
                if (dur >= kMinSegmentSec) continue;

                const bool hasPrev = i > 0;
                const bool hasNext = i + 1 < out.timeline.size();
                const double prevDur = hasPrev ? out.timeline[i - 1].endSec - out.timeline[i - 1].startSec : -1.0;
                const double nextDur = hasNext ? out.timeline[i + 1].endSec - out.timeline[i + 1].startSec : -1.0;

                if (hasPrev && (! hasNext || prevDur >= nextDur))
                    out.timeline[i - 1].endSec = out.timeline[i].endSec;
                else if (hasNext)
                    out.timeline[i + 1].startSec = out.timeline[i].startSec;

                out.timeline.erase (out.timeline.begin() + (long) i);
                merged = true;
                break;
            }
        }

        // Coalesce neighbours that now share a key.
        for (size_t i = 1; i < out.timeline.size();)
        {
            if (out.timeline[i].key.root == out.timeline[i - 1].key.root
                && out.timeline[i].key.mode == out.timeline[i - 1].key.mode)
            {
                out.timeline[i - 1].endSec = out.timeline[i].endSec;
                out.timeline.erase (out.timeline.begin() + (long) i);
            }
            else ++i;
        }

        out.modulationDetected = out.timeline.size() > 1;

        // ---- global key = the region that holds the most sung time ------
        if (! out.timeline.empty())
        {
            const KeySegment* longest = &out.timeline[0];
            for (const auto& s : out.timeline)
                if ((s.endSec - s.startSec) > (longest->endSec - longest->startSec))
                    longest = &s;

            // Score the dominant key against the GLOBAL histogram so its
            // confidence reflects the whole take, not just its own window.
            double rival = -2.0, mine = -2.0;
            for (const auto& c : globalRank)
            {
                const bool isMine = c.root == longest->key.root
                                 && kModes[(size_t) c.modeIdx].mode == longest->key.mode;
                if (isMine && mine < -1.0) mine = c.score;
                else if (! isMine && rival < -1.0) rival = c.score;
                if (mine > -1.0 && rival > -1.0) break;
            }
            out.global = longest->key;
            out.global.score = (float) mine;
            out.global.confidence = (float) juce::jlimit (0.0, 1.0, (mine - rival) * 6.0 + 0.25);
        }
    }

    if (! out.global.isValid())
    {
        out.summary = "Key could not be determined confidently.";
        return out;
    }

    // ---- alternates (runners-up, distinct keys) -----------------------
    for (const auto& c : globalRank)
    {
        if (out.alternates.size() >= 3) break;
        const bool isGlobal = c.root == out.global.root
                           && kModes[(size_t) c.modeIdx].mode == out.global.mode;
        if (isGlobal) continue;
        bool dup = false;
        for (const auto& a : out.alternates)
            if (a.root == c.root && a.mode == kModes[(size_t) c.modeIdx].mode) dup = true;
        if (! dup)
            out.alternates.push_back (makeEstimate (c, (float) juce::jlimit (0.0, 1.0, c.score)));
    }

    // ================================================================
    //  HONEST AMBIGUITY REPORTING
    // ================================================================
    juce::String caveat;

    // (a) Relative major/minor — they share all seven pitch classes, so a melody
    //     that never settles on a tonic genuinely cannot distinguish them.
    if (! out.alternates.empty())
    {
        const auto& alt = out.alternates[0];
        const double margin = out.global.score - alt.score;
        if (areRelative (out.global.root, out.global.mode, alt.root, alt.mode) && margin < 0.12)
        {
            out.ambiguous = true;
            caveat = " It could equally be " + alt.name()
                   + " — a melody alone can't always separate relative keys.";
        }
        else if (margin < 0.05)
        {
            out.ambiguous = true;
            caveat = " " + alt.name() + " fits almost as well.";
        }
    }

    // (b) Mode vs parent — if the CHARACTERISTIC degree was never sung, the mode
    //     is a guess. Say so rather than quietly committing.
    {
        const ModeDef* def = nullptr;
        for (const auto& d : kModes) if (d.mode == out.global.mode) def = &d;
        if (def != nullptr && def->parent != def->mode)
        {
            double total = 0.0;
            for (double v : globalHist) total += v;
            const int pc = (out.global.root + def->characteristic) % 12;
            const double share = total > 0.0 ? globalHist[(size_t) pc] / total : 0.0;
            if (share < 0.02)
            {
                out.ambiguous = true;
                caveat += " The note that defines " + juce::String (def->name)
                        + " is barely sung, so it may simply be "
                        + juce::String (KeyDetector::modeName (def->parent)) + ".";
                out.global.confidence *= 0.6f;
            }
        }
    }

    // ---- plain-language summary --------------------------------------
    out.summary = "Detected " + out.global.name()
                + " (confidence " + juce::String ((int) std::round (out.global.confidence * 100.0f)) + "%).";

    if (out.modulationDetected)
    {
        out.summary += " Key change detected: ";
        juce::StringArray parts;
        for (const auto& s : out.timeline)
            parts.add (s.key.name());
        out.summary += parts.joinIntoString (" → ") + ".";
    }
    out.summary += caveat;

    return out;
}
} // namespace vf
