#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ============================================================================
//  VoxBrain test framework — deliberately tiny and dependency-free.
//  ---------------------------------------------------------------------------
//  WHY THIS EXISTS. For most of VoxBrain's development every DSP claim was
//  proved by an offline harness compiled in a temp directory, run once, and then
//  DISCARDED. Roughly three hundred passing assertions existed and none of them
//  could ever be re-run, so a regression introduced later would have been
//  invisible. This file turns that one-time cost into a permanent asset.
//
//  No Catch2 / GoogleTest on purpose: adding a fetched dependency to a build
//  that a non-programmer runs by double-clicking a .bat is a real risk for very
//  little gain. Everything here is a hundred lines of portable C++.
//
//  PORTABILITY RULE: this code must compile under MSVC, which is the shipping
//  compiler. No constexpr math (MSVC rejects `constexpr double x = std::log(y)`
//  — it broke a real build), no VLAs, no POSIX-only headers, no designated
//  initialisers.
// ============================================================================
namespace vftest
{
struct Result
{
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    bool ok() const { return failed == 0; }
};

/** One named group of checks. */
class Suite
{
public:
    explicit Suite (const char* suiteName) : name (suiteName)
    {
        std::printf ("\n=== %s ===\n", name);
    }

    /** Registers this suite's outcome with the Registry automatically.
        Previously each suite had to remember `Registry::instance().add(...)` as
        its last line, and a suite that forgot it still PRINTED its failures
        while contributing nothing to the totals — so the run reported "ALL
        TESTS PASSED" with visible FAIL lines above it. That is the same class
        of silently-vacuous result the module-registry test hit, so the
        bookkeeping is now impossible to forget. */
    ~Suite();

    /** Core assertion. `detail` is printed either way — a passing test that
        shows its measured value is far more useful than a bare "PASS". */
    void check (const char* what, bool condition, const juce::String& detail = {})
    {
        if (condition) ++result.passed;
        else
        {
            ++result.failed;
            result.failures.push_back (std::string (name) + " / " + what
                                       + (detail.isEmpty() ? "" : ("  [" + detail + "]").toStdString()));
        }

        std::printf ("%s - %s%s\n",
                     condition ? "PASS" : "FAIL",
                     what,
                     detail.isEmpty() ? "" : ("  [" + detail + "]").toRawUTF8());
    }

    /** Floating-point comparison with an explicit tolerance, so the tolerance is
        visible in the test rather than hidden in a helper. */
    void checkNear (const char* what, double actual, double expected, double tolerance)
    {
        const bool ok = std::abs (actual - expected) <= tolerance;
        check (what, ok, juce::String (actual, 3) + " vs " + juce::String (expected, 3)
                         + " (±" + juce::String (tolerance, 3) + ")");
    }

    const Result& results() const { return result; }

    /** Suppress auto-registration (only useful for testing the framework). */
    void detach() noexcept { registered = true; }

private:
    const char* name;
    Result result;
    bool registered = false;
};

/** Accumulates every suite's outcome and prints the final verdict. */
class Registry
{
public:
    static Registry& instance() { static Registry r; return r; }

    void add (const Result& r)
    {
        total.passed += r.passed;
        total.failed += r.failed;
        for (const auto& f : r.failures) total.failures.push_back (f);
    }

    int report() const
    {
        std::printf ("\n============================================================\n");
        std::printf ("  TOTAL: %d passed, %d failed\n", total.passed, total.failed);

        if (! total.failures.empty())
        {
            std::printf ("\n  FAILURES:\n");
            for (const auto& f : total.failures)
                std::printf ("    * %s\n", f.c_str());
        }

        std::printf ("============================================================\n");
        std::printf ("%s\n", total.ok() ? "  ALL TESTS PASSED" : "  *** TESTS FAILED ***");
        return total.ok() ? 0 : 1;
    }

private:
    Result total;
};

inline Suite::~Suite()
{
    if (! registered)
    {
        registered = true;
        Registry::instance().add (result);
    }
}

/** Every suite entry point has this shape; TestMain calls them in order. */
using SuiteFn = void (*)();

// ---- shared signal helpers (used by several suites) ---------------------
inline std::vector<float> sineWave (double hz, double seconds, float amp, double fs, int harmonics = 1)
{
    std::vector<float> v ((size_t) (seconds * fs));
    for (size_t i = 0; i < v.size(); ++i)
    {
        double s = 0.0;
        const double t = (double) i / fs;
        for (int h = 1; h <= harmonics; ++h)
            s += std::sin (2.0 * juce::MathConstants<double>::pi * hz * h * t) / h;
        v[i] = (float) (amp * s / (harmonics > 1 ? 1.6 : 1.0));
    }
    return v;
}

inline std::vector<float> sawWave (double hz, double seconds, float amp, double fs)
{
    std::vector<float> v ((size_t) (seconds * fs));
    double ph = 0.0;
    const double inc = hz / fs;
    for (size_t i = 0; i < v.size(); ++i)
    {
        v[i] = (float) (amp * (2.0 * ph - 1.0));
        ph += inc;
        if (ph >= 1.0) ph -= 1.0;
    }
    return v;
}

inline std::vector<float> whiteNoise (double seconds, float amp, double fs, unsigned seed = 1)
{
    std::vector<float> v ((size_t) (seconds * fs));
    juce::Random r ((juce::int64) seed);
    for (auto& s : v) s = (float) ((r.nextDouble() * 2.0 - 1.0) * amp);
    return v;
}

inline bool allFinite (const std::vector<float>& v, float bound)
{
    for (float s : v)
        if (! std::isfinite (s) || std::abs (s) > bound) return false;
    return true;
}

/** Largest sample-to-sample jump after a warm-up period — the standard proxy
    for clicks / discontinuities in this project. */
inline float maxJump (const std::vector<float>& v, size_t from)
{
    float m = 0.0f;
    for (size_t i = from + 1; i < v.size(); ++i)
        m = juce::jmax (m, std::abs (v[i] - v[i - 1]));
    return m;
}
} // namespace vftest
