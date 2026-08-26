#include "TestFramework.h"
#include "../Source/Modules/ModuleRegistry.h"
#include <algorithm>

// ============================================================================
//  Rack modules — every implemented module, exercised for real.
//
//  The fixed chain is what LEARN builds and what most people touch, so it gets
//  the detailed per-control suite (Test_ChainModules). The rack is the long
//  tail: thirty modules, most of which are inserted rarely, which is exactly
//  where a module that silently outputs nothing — or NaN — can sit unnoticed.
//  This suite walks the ENTIRE registry and holds every implemented module to
//  the same four promises:
//
//      * it can be created from its registered id
//      * it survives prepare + process without producing NaN/Inf
//      * it does not explode the signal (a module that multiplies level by 40
//        is indistinguishable from a broken one in a mix)
//      * it passes audio: a module that is not an intentional gate/limiter
//        must not silence its input
//
//  VACUOUS-RESULT GUARD. ModuleRegistry::instance() does NOT self-register;
//  without an explicit registerBuiltInModules() every query silently returns
//  false and a suite like this "passes" while testing nothing. That has
//  happened in this project before, so the module count is asserted up front:
//  if registration is ever missed, THIS test fails loudly rather than the whole
//  suite quietly succeeding.
// ============================================================================
namespace
{
using namespace vftest;
constexpr double FS = 44100.0;
constexpr int    BLOCK = 256;

double rmsOf (const std::vector<float>& v, size_t from)
{
    if (from >= v.size()) return 0.0;
    double s = 0.0;
    for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (v.size() - from));
}

/** Modules whose entire job is to remove or hold back signal. They are exempt
    from the "must pass audio" check — and ONLY from that one. */
bool isAttenuator (const juce::String& id)
{
    return id.contains ("gate") || id.contains ("limit") || id.contains ("mono")
        || id.contains ("noise_reduction") || id.contains ("de_plosive")
        || id.contains ("expander");
}
} // namespace

void runRackModuleTests()
{
    Suite s ("Rack modules (every implemented module actually runs)");

    vf::mods::registerBuiltInModules();
    auto& reg = vf::mods::ModuleRegistry::instance();

    std::vector<vf::mods::Descriptor> implemented;
    for (const auto& d : reg.catalog())
        if (reg.isImplemented (d.id)) implemented.push_back (d);

    // The guard described above: a registration failure must not look like a pass.
    s.check ("the registry actually has modules registered", implemented.size() >= 20,
             juce::String ((int) implemented.size()) + " implemented modules found");
    if (implemented.size() < 20)
        return;   // everything below would be vacuous

    // A vocal-ish test signal: harmonic-rich, moderate level, plus a quiet tail
    // so envelope followers and tails are exercised rather than just steady state.
    std::vector<float> sig ((size_t) (1.2 * FS), 0.0f);
    for (size_t i = 0; i < sig.size(); ++i)
    {
        const double t = (double) i / FS;
        const double env = i < sig.size() * 3 / 4 ? 1.0 : 0.05;
        sig[i] = (float) (0.35 * env * (std::sin (2.0 * juce::MathConstants<double>::pi * 180.0 * t)
                                + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 540.0 * t)
                                + 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 2400.0 * t)));
    }

    int created = 0, finite = 0, bounded = 0, passedAudio = 0, silentUnexpected = 0;
    juce::StringArray brokenFinite, brokenBounded, brokenSilent;

    for (const auto& d : implemented)
    {
        auto m = reg.create (d.id);
        if (m == nullptr) { brokenFinite.add (d.id + " (create failed)"); continue; }
        ++created;

        juce::dsp::ProcessSpec spec { FS, (juce::uint32) BLOCK, 2 };
        m->prepare (spec);

        juce::AudioBuffer<float> buf (2, BLOCK);
        std::vector<float> out;
        out.reserve (sig.size());

        for (size_t off = 0; off + BLOCK <= sig.size(); off += BLOCK)
        {
            for (int i = 0; i < BLOCK; ++i)
            {
                buf.setSample (0, i, sig[off + (size_t) i]);
                buf.setSample (1, i, sig[off + (size_t) i]);
            }
            m->process (buf);
            for (int i = 0; i < BLOCK; ++i) out.push_back (buf.getSample (0, i));
        }

        bool allF = true, allB = true;
        for (float x : out)
        {
            if (! std::isfinite (x)) { allF = false; break; }
            if (std::abs (x) > 8.0f) allB = false;
        }
        if (allF) ++finite; else brokenFinite.add (d.id);
        if (allB) ++bounded; else brokenBounded.add (d.id);

        if (allF)
        {
            const double outRms = rmsOf (out, (size_t) (0.3 * FS));
            const double inRms  = rmsOf (sig, (size_t) (0.3 * FS));
            const bool passes = outRms > inRms * 0.02;
            if (passes) ++passedAudio;
            else if (! isAttenuator (d.id)) { ++silentUnexpected; brokenSilent.add (d.id); }
        }
    }

    s.check ("every implemented module can be instantiated", created == (int) implemented.size(),
             juce::String (created) + " of " + juce::String ((int) implemented.size()));
    s.check ("no module produces NaN or Inf", brokenFinite.isEmpty(),
             brokenFinite.isEmpty() ? juce::String (finite) + " modules clean"
                                    : brokenFinite.joinIntoString (", "));
    s.check ("no module explodes the signal level", brokenBounded.isEmpty(),
             brokenBounded.isEmpty() ? juce::String (bounded) + " modules bounded"
                                     : brokenBounded.joinIntoString (", "));
    s.check ("no module unexpectedly silences the input", silentUnexpected == 0,
             silentUnexpected == 0 ? juce::String (passedAudio) + " modules pass audio"
                                   : brokenSilent.joinIntoString (", "));

    // Every catalog entry must describe itself — the search box and the AI
    // advisor both rank on these fields, so an empty one is a module the user
    // can never find.
    juce::StringArray undocumented;
    for (const auto& d : implemented)
        if (d.name.isEmpty() || d.description.isEmpty()) undocumented.add (d.id);
    s.check ("every implemented module has a name and a description",
             undocumented.isEmpty(),
             undocumented.isEmpty() ? "all documented" : undocumented.joinIntoString (", "));
}
