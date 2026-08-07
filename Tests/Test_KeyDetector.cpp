#include "TestFramework.h"
#include "../Source/Analysis/KeyDetector.h"

// ============================================================================
//  KeyDetector — key, MODE and MODULATION.
//
//  The modal cases matter commercially, not just academically: Drill and Trap
//  lean Phrygian/harmonic-minor and Neo Soul leans Dorian, so a detector that
//  collapses those to plain minor actively fights the genre system. The
//  "steady key is not split" and "sparse input refuses to guess" cases are
//  regression guards for two real bugs found during development.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FPS = 100.0;

float midiToHz (int m) { return 440.0f * std::pow (2.0f, (float) (m - 69) / 12.0f); }

struct Note { int degree; float beats; };

std::vector<vf::KeyDetector::Frame> melody (int rootMidi, const std::vector<Note>& notes,
                                            double startSec = 0.0, float conf = 1.0f, int repeats = 1)
{
    std::vector<vf::KeyDetector::Frame> f;
    double t = startSec;
    for (int r = 0; r < repeats; ++r)
        for (const auto& n : notes)
        {
            const int frames = (int) std::floor (n.beats * 0.5 * FPS + 0.5);
            const float hz = midiToHz (rootMidi + n.degree);
            for (int i = 0; i < frames; ++i)
            {
                vf::KeyDetector::Frame fr;
                fr.hz = hz; fr.confidence = conf; fr.timeSec = t;
                f.push_back (fr);
                t += 1.0 / FPS;
            }
        }
    return f;
}

void append (std::vector<vf::KeyDetector::Frame>& a, const std::vector<vf::KeyDetector::Frame>& b)
{
    a.insert (a.end(), b.begin(), b.end());
}
} // namespace

void runKeyDetectorTests()
{
    using namespace vftest;
    Suite s ("KeyDetector");

    const int C4 = 60, A3 = 57, D4 = 62, E4 = 64, Eb4 = 63, G4 = 67;

    // ---- major / minor baseline ---------------------------------------
    {
        const auto f = melody (C4, { {0,2},{4,1},{7,2},{4,1},{5,1},{2,1},{0,2},{11,1},{9,1},{7,2},{0,2} }, 0.0, 1.0f, 4);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("C major melody detected", k.global.root == 0 && k.global.mode == vf::KeyMode::Major,
                 k.global.name());
        s.check ("confidence is meaningful", k.global.confidence > 0.3f,
                 juce::String ((int) (k.global.confidence * 100.0f)) + "%");
    }
    {
        const auto f = melody (A3, { {0,2},{3,1},{7,2},{3,1},{5,1},{2,1},{0,2},{10,1},{8,1},{7,2},{0,2} }, 0.0, 1.0f, 4);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("A minor root detected", k.global.root == 9, k.global.name());
    }

    // ---- modes: the cases the old detector could not express ----------
    {
        // D Dorian — the natural 6th (B) is what separates it from D minor.
        const auto f = melody (D4, { {0,2},{3,1},{7,2},{9,2},{5,1},{3,1},{0,2},{9,1},{7,1},{10,1},{0,2} }, 0.0, 1.0f, 4);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("D Dorian is not read as plain minor/major",
                 k.global.mode == vf::KeyMode::Dorian && k.global.root == 2, k.global.name());
    }
    {
        // E Phrygian — the ♭2 (F) is the signature, and defines Drill/Trap.
        const auto f = melody (E4, { {0,2},{1,2},{3,1},{7,2},{1,1},{0,2},{8,1},{7,1},{3,1},{1,1},{0,2} }, 0.0, 1.0f, 4);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("E Phrygian detected",
                 k.global.mode == vf::KeyMode::Phrygian && k.global.root == 4, k.global.name());
    }
    {
        const auto f = melody (A3, { {0,2},{3,1},{7,2},{11,2},{0,2},{8,1},{7,1},{11,2},{0,2},{3,1},{11,1} }, 0.0, 1.0f, 4);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("A harmonic minor detected", k.global.mode == vf::KeyMode::HarmonicMinor, k.global.name());
    }
    {
        const auto f = melody (G4, { {0,2},{4,2},{7,2},{10,2},{0,2},{4,1},{10,2},{7,1},{0,2},{5,1},{2,1} }, 0.0, 1.0f, 4);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("G Mixolydian detected", k.global.mode == vf::KeyMode::Mixolydian, k.global.name());
    }

    // ---- every mode maps onto the engine's scaleType -------------------
    {
        const auto f = melody (D4, { {0,2},{3,1},{7,2},{9,2},{5,1},{3,1},{0,2},{9,1},{10,1} }, 0.0, 1.0f, 4);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("Dorian maps to engine scaleType 4", k.global.scaleType() == 4,
                 "scaleType = " + juce::String (k.global.scaleType()));
    }

    // ---- modulation ----------------------------------------------------
    {
        std::vector<vf::KeyDetector::Frame> f;
        append (f, melody (C4, { {0,2},{4,1},{7,2},{4,1},{0,2},{11,1},{7,2} }, 0.0, 1.0f, 4));
        append (f, melody (Eb4, { {0,2},{4,1},{7,2},{4,1},{0,2},{11,1},{7,2} }, f.back().timeSec + 0.01, 1.0f, 4));
        const auto k = vf::KeyDetector::analyse (f);

        bool sawC = false, sawEb = false;
        for (const auto& seg : k.timeline)
        {
            if (seg.key.root == 0) sawC = true;
            if (seg.key.root == 3) sawEb = true;
        }
        s.check ("modulation detected with both keys present",
                 k.modulationDetected && sawC && sawEb,
                 juce::String ((int) k.timeline.size()) + " segments");
    }
    {
        // REGRESSION GUARD: a steady take was once falsely split because a single
        // disagreeing window created a segment.
        const auto f = melody (C4, { {0,2},{4,1},{7,2},{4,1},{5,1},{2,1},{0,2},{11,1},{9,1},{7,2} }, 0.0, 1.0f, 10);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("a steady key is not falsely split", ! k.modulationDetected,
                 juce::String ((int) k.timeline.size()) + " segment(s)");
    }

    // ---- honesty --------------------------------------------------------
    {
        std::vector<Note> chromatic;
        for (int i = 0; i < 12; ++i) chromatic.push_back ({ i, 1 });
        const auto f = melody (C4, chromatic, 0.0, 1.0f, 8);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("chromatic passage is flagged, not forced into a key",
                 k.chromatic || k.global.confidence < 0.35f,
                 k.chromatic ? "chromatic" : juce::String ((int) (k.global.confidence * 100.0f)) + "%");
    }
    {
        // REGRESSION GUARD: two notes once produced a confident key. C and E
        // cannot separate C major / A minor / C Mixolydian / E Phrygian.
        const auto f = melody (C4, { {0,1},{4,1} }, 0.0, 1.0f, 1);
        const auto k = vf::KeyDetector::analyse (f);
        s.check ("sparse input refuses to guess", ! k.isValid());
    }
    {
        // Low-confidence frames must not outvote clearly-tracked ones.
        auto good = melody (C4, { {0,2},{4,2},{7,2},{0,2} }, 0.0, 1.0f, 6);
        const auto junk = melody (C4, { {1,2},{6,2},{10,2},{8,2} }, good.back().timeSec + 0.01, 0.05f, 6);
        append (good, junk);
        const auto k = vf::KeyDetector::analyse (good);
        s.check ("low-confidence frames cannot hijack the key", k.global.root == 0, k.global.name());
    }

}
