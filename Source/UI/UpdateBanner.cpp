#include "UpdateBanner.h"

namespace vf
{
UpdateBanner::UpdateBanner()
{
    text.setColour (juce::Label::textColourId, theme::bg);
    text.setJustificationType (juce::Justification::centredLeft);
    text.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (text);

    for (auto* b : { &installButton, &notesButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::bg.withAlpha (0.25f));
        b->setColour (juce::TextButton::textColourOffId, theme::bg);
        addChildComponent (*b);
    }
    installButton.onClick = [this] { doInstall(); };
    notesButton.onClick   = [this] { doNotes(); };

    setVisible (false);
}

void UpdateBanner::setInfo (const UpdateChecker::Info& newInfo)
{
    info = newInfo;
    using S = UpdateChecker::State;

    const bool haveInstaller = info.installerFile.existsAsFile();
    juce::String msg;
    bool showInstall = false, showNotes = false;

    switch (info.state)
    {
        case S::Downloading:
            msg = "Downloading VocalForge " + info.latestVersion + "…";
            active = true;
            break;

        case S::Downloaded:
            active = true;
            if (haveInstaller)
            {
                msg = launched ? "Installer launched — close your DAW to finish updating."
                               : "VocalForge " + info.latestVersion + " is ready to install.";
                showInstall = ! launched;
            }
            else
            {
                msg = "VocalForge " + info.latestVersion + " is available.";
            }
            showNotes = info.notes.isNotEmpty() || info.notesUrl.isNotEmpty();
            break;

        case S::Failed:
            // Only surface a failure if it's tied to an actual newer version.
            active = info.latestVersion.isNotEmpty()
                     && UpdateChecker::isNewer (info.latestVersion, info.currentVersion);
            msg = "Update " + info.latestVersion + " available.";
            showNotes = active && (info.notes.isNotEmpty() || info.notesUrl.isNotEmpty());
            break;

        case S::Idle:
        case S::Checking:
        case S::UpToDate:
        default:
            active = false;
            break;
    }

    text.setText (msg, juce::dontSendNotification);
    installButton.setVisible (showInstall);
    notesButton.setVisible (showNotes);
    setVisible (active);
    if (active) resized();
}

void UpdateBanner::doInstall()
{
    if (info.installerFile.existsAsFile() && info.installerFile.startAsProcess())
    {
        launched = true;
        setInfo (info);   // refresh text to the "close your DAW" message
    }
}

void UpdateBanner::doNotes()
{
    if (info.notesUrl.isNotEmpty())
    {
        juce::URL (info.notesUrl).launchInDefaultBrowser();
    }
    else if (info.notes.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon,
                                                "What's new in VocalForge " + info.latestVersion,
                                                info.notes, "OK");
    }
}

void UpdateBanner::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (theme::accentGreen);
    g.fillRoundedRectangle (b, 6.0f);
}

void UpdateBanner::resized()
{
    auto r = getLocalBounds().reduced (10, 4);
    if (notesButton.isVisible())   notesButton.setBounds   (r.removeFromRight (100).reduced (2));
    if (installButton.isVisible()) installButton.setBounds (r.removeFromRight (84).reduced (2));
    text.setBounds (r);
}
} // namespace vf
