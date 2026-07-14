#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Update/UpdateChecker.h"
#include "LookAndFeel.h"

namespace vf
{
// ============================================================================
//  UpdateBanner — slim strip shown only when an update is downloading/ready.
//  "Install" launches the downloaded installer (the user then closes the DAW
//  to finish). "Release notes" opens the changelog or shows the notes text.
// ============================================================================
class UpdateBanner : public juce::Component
{
public:
    UpdateBanner();

    void setInfo (const UpdateChecker::Info& info);
    bool isActive() const noexcept { return active; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void doInstall();
    void doNotes();

    UpdateChecker::Info info;
    bool active = false;
    bool launched = false;

    juce::Label      text;
    juce::TextButton installButton { "Install" };
    juce::TextButton notesButton   { "Release notes" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateBanner)
};
} // namespace vf
