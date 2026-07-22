#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

// ============================================================================
//  CrashLog — lightweight field-crash diagnostics.
//  ---------------------------------------------------------------------------
//  A non-reproducible crash in a tester's host is impossible to fix from a raw
//  address alone. This installs a process crash handler that, on a fault, writes
//  the current call stack + the last "breadcrumb" step to
//     <userAppData>/VoxBrain/crash.log
//  so the tester can just send us that file. `step()` is a single relaxed atomic
//  store — safe to call from any thread, effectively free — so we can drop
//  breadcrumbs along the LEARN / auto-mix / timer paths to see which operation
//  was in flight when it died. Zero behaviour change otherwise.
// ============================================================================
namespace vf::crashlog
{
    inline std::atomic<const char*> g_step { "startup" };

    inline void step (const char* s) noexcept { g_step.store (s, std::memory_order_relaxed); }

    inline juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("VoxBrain").getChildFile ("crash.log");
    }

    inline void handler (void*)
    {
        auto f = file();
        f.getParentDirectory().createDirectory();
        juce::String s;
        s << "==== VoxBrain crash " << juce::Time::getCurrentTime().toISO8601 (true) << " ====\n"
          << "version:   " << juce::String (VOXBRAIN_VERSION) << "\n"
          << "last step: " << juce::String (g_step.load (std::memory_order_relaxed)) << "\n"
          << "backtrace:\n" << juce::SystemStats::getStackBacktrace() << "\n\n";
        f.appendText (s);
    }

    inline void install()
    {
        static bool done = false;
        if (done) return;
        done = true;
        juce::SystemStats::setApplicationCrashHandler (handler);
    }
} // namespace vf::crashlog
