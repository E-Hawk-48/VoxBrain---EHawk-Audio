#include "UpdateChecker.h"
#include <juce_events/juce_events.h>          // MessageManager::callAsync
#include <juce_cryptography/juce_cryptography.h>
#include <array>

namespace vf
{
namespace
{
    constexpr int kMinCheckIntervalHours = 4;

    juce::File vocalForgeAppDataDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("VocalForge");
    }
}

UpdateChecker::UpdateChecker() : juce::Thread ("VocalForge Update") {}

UpdateChecker::~UpdateChecker()
{
    *alive = false;
    stopThread (5000);
}

// ---------------------------------------------------------------------------
juce::String UpdateChecker::currentVersion()
{
   #ifdef VOCALFORGE_VERSION
    return juce::String (VOCALFORGE_VERSION);
   #else
    return "0.0.0";
   #endif
}

juce::String UpdateChecker::updateUrl()
{
   #ifdef VOCALFORGE_UPDATE_URL
    return juce::String (VOCALFORGE_UPDATE_URL);
   #else
    return {};
   #endif
}

juce::File UpdateChecker::updatesDir()
{
    auto d = vocalForgeAppDataDir().getChildFile ("Updates");
    d.createDirectory();
    return d;
}

juce::File UpdateChecker::settingsFile() const
{
    return vocalForgeAppDataDir().getChildFile ("update.check");
}

bool UpdateChecker::throttled() const
{
    const auto f = settingsFile();
    if (! f.existsAsFile())
        return false;
    const auto last = f.loadFileAsString().trim().getLargeIntValue();
    const auto ageMs = juce::Time::getCurrentTime().toMilliseconds() - last;
    return ageMs >= 0 && ageMs < (juce::int64) kMinCheckIntervalHours * 3600 * 1000;
}

void UpdateChecker::stampChecked() const
{
    vocalForgeAppDataDir().createDirectory();
    settingsFile().replaceWithText (juce::String (juce::Time::getCurrentTime().toMilliseconds()));
}

// ---------------------------------------------------------------------------
void UpdateChecker::startCheck (bool force)
{
    const auto url = updateUrl();
    if (url.isEmpty() || url.contains ("YOUR-DOMAIN"))
        return;                        // not configured for release yet — no-op
    if (isThreadRunning())
        return;
    forceCheck = force;
    startThread (juce::Thread::Priority::background);
}

bool UpdateChecker::isNewer (const juce::String& candidate, const juce::String& current)
{
    auto parse = [] (juce::String v) -> std::array<int, 3>
    {
        v = v.trim().trimCharactersAtStart ("vV");
        juce::StringArray parts;
        parts.addTokens (v, ".", "");
        std::array<int, 3> out { 0, 0, 0 };
        for (int i = 0; i < 3 && i < parts.size(); ++i)
            out[(size_t) i] = parts[i].getIntValue();
        return out;
    };
    const auto a = parse (candidate);
    const auto b = parse (current);
    for (int i = 0; i < 3; ++i)
    {
        if (a[(size_t) i] > b[(size_t) i]) return true;
        if (a[(size_t) i] < b[(size_t) i]) return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
UpdateChecker::Info UpdateChecker::getInfo() const
{
    const juce::ScopedLock sl (lock);
    return info;
}

void UpdateChecker::publish (Info in)
{
    {
        const juce::ScopedLock sl (lock);
        info = in;
    }
    juce::MessageManager::callAsync ([a = alive, this, in]
    {
        if (*a && onStateChanged)
            onStateChanged (in);
    });
}

// ---------------------------------------------------------------------------
void UpdateChecker::run()
{
    Info r;
    r.currentVersion = currentVersion();

    if (! forceCheck.load() && throttled())
        return;                        // keep the last state; don't hammer the server

    { Info i = r; i.state = State::Checking; publish (i); }

    // ---- fetch the manifest ----
    juce::String text;
    if (auto stream = juce::URL (updateUrl()).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (8000)))
        text = stream->readEntireStreamAsString();

    if (threadShouldExit()) return;

    if (text.isEmpty())
    {
        r.state = State::Failed;
        r.error = "Could not reach the update server.";
        publish (r);
        return;
    }

    const auto json = juce::JSON::parse (text);
    const juce::String latest = json.getProperty ("version", "").toString();
    if (latest.isEmpty())
    {
        r.state = State::Failed;
        r.error = "Update manifest was malformed.";
        publish (r);
        return;
    }

    r.latestVersion = latest;
    r.notes    = json.getProperty ("notes", "").toString();
    r.notesUrl = json.getProperty ("notesUrl", "").toString();
    stampChecked();

    if (! isNewer (latest, r.currentVersion))
    {
        r.state = State::UpToDate;
        publish (r);
        return;
    }

    // ---- resolve the installer for this platform ----
   #if JUCE_WINDOWS
    const auto platform = json.getProperty ("windows", juce::var());
   #elif JUCE_MAC
    const auto platform = json.getProperty ("macos", juce::var());
   #else
    const juce::var platform;
   #endif
    const juce::String durl = platform.getProperty ("url", "").toString();
    const juce::String sha  = platform.getProperty ("sha256", "").toString();

    if (durl.isEmpty())
    {
        // A new version exists but there's no installer for this OS. Still
        // surface it (notes-only) so the user knows to grab it manually.
        r.state = State::Downloaded;
        r.error = "No installer for this platform in the manifest.";
        publish (r);
        return;
    }

    // ---- download (silent) ----
    const juce::URL du (durl);
    const auto name = du.getFileName().isNotEmpty() ? du.getFileName() : ("VocalForge-" + latest);
    const juce::File dest = updatesDir().getChildFile (name);

    const auto matchesSha = [&] (const juce::File& f)
    {
        return sha.isEmpty() || juce::SHA256 (f).toHexString().equalsIgnoreCase (sha);
    };

    if (dest.existsAsFile() && dest.getSize() > 0 && matchesSha (dest))
    {
        r.state = State::Downloaded;      // already downloaded in a prior session
        r.installerFile = dest;
        publish (r);
        return;
    }

    { Info i = r; i.state = State::Downloading; publish (i); }

    dest.deleteFile();
    bool ok = false;
    if (auto in = du.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (15000)))
    {
        juce::FileOutputStream out (dest);
        if (! out.failedToOpen())
        {
            out.writeFromInputStream (*in, -1);
            out.flush();
            ok = dest.getSize() > 0;
        }
    }

    if (threadShouldExit()) return;

    if (! ok)
    {
        dest.deleteFile();
        r.state = State::Failed;
        r.error = "Update download failed.";
        publish (r);
        return;
    }

    if (! matchesSha (dest))
    {
        dest.deleteFile();
        r.state = State::Failed;
        r.error = "Downloaded update failed its integrity check.";
        publish (r);
        return;
    }

    r.state = State::Downloaded;
    r.installerFile = dest;
    publish (r);
}
} // namespace vf
