#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <memory>

namespace vf
{
// ============================================================================
//  UpdateChecker — cross-platform in-plugin auto-update client (Win + macOS).
//
//  Runs entirely OFF the audio thread on its own background thread. It fetches
//  an appcast.json from VOCALFORGE_UPDATE_URL over HTTPS, compares the
//  advertised version to this build (VOCALFORGE_VERSION), and — using the
//  "silent download" model — quietly downloads the platform installer to a
//  local Updates folder, verifying its SHA-256 if the manifest provides one.
//
//  A loaded VST3 can't replace its own binary, so the checker never tries to.
//  When the installer is downloaded it reports Downloaded; the UI then offers
//  to launch it so the user can apply the update after closing their DAW.
//
//  All results are delivered on the message thread via onStateChanged.
//
//  appcast.json shape:
//  {
//    "version": "1.2.0",
//    "notes":   "• line one\n• line two",
//    "notesUrl":"https://yoursite/changelog",
//    "windows": { "url": "https://…/VocalForge-Setup-1.2.0.exe", "sha256": "…" },
//    "macos":   { "url": "https://…/VocalForge-1.2.0.pkg",       "sha256": "…" }
//  }
// ============================================================================
class UpdateChecker : private juce::Thread
{
public:
    enum class State { Idle, Checking, UpToDate, Downloading, Downloaded, Failed };

    struct Info
    {
        State        state = State::Idle;
        juce::String currentVersion;
        juce::String latestVersion;
        juce::String notes;
        juce::String notesUrl;
        juce::File   installerFile;      // valid when state == Downloaded
        juce::String error;
    };

    UpdateChecker();
    ~UpdateChecker() override;

    /** Start a check on the background thread. Respects the once-per-few-hours
        throttle unless force == true (e.g. a manual "Check for updates"). */
    void startCheck (bool force = false);

    Info getInfo() const;                          // thread-safe snapshot
    std::function<void (Info)> onStateChanged;     // fired on the message thread

    /** Semantic-version compare ("1.2.0" style; tolerant of a leading v). */
    static bool isNewer (const juce::String& candidate, const juce::String& current);

    static juce::String currentVersion();

private:
    void run() override;
    void publish (Info info);

    static juce::String updateUrl();
    static juce::File   updatesDir();
    juce::File settingsFile() const;
    bool throttled() const;
    void stampChecked() const;

    juce::CriticalSection lock;
    Info info;                                     // guarded by lock
    std::atomic<bool> forceCheck { false };
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateChecker)
};
} // namespace vf
