#include "PluginEditor.h"
#include "Chat/ChatEngine.h"

namespace vf
{
VoxBrainEditor::VoxBrainEditor (VoxBrainProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      spectrum (p.getAnalysis()),
      dnaPanel (p.getAnalysis()),
      pitchDisplay (p.getChain().getRetune()),
      presetBar (p.getPresets()),
      moduleStrip (p.apvts),
      rackView (p.apvts, p.getRack(), [&p] { return p.suggestModules(); }),
      presetBrowser (p.getPresetLibrary(),
                     [&p] (const Preset& pr) { p.applyPreset (pr); },
                     [&p] { return p.captureCurrentAsPreset(); },
                     [&p] { return p.generateAiPreset(); })
{
    theme::load();                 // restore the user's saved colours before styling
    lookAndFeel.refreshColours();
    setLookAndFeel (&lookAndFeel);

    learnButton.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.85f));
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);

    modulesButton.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.85f));
    modulesButton.onClick = [this] { toggleRack(); };
    addAndMakeVisible (modulesButton);

    presetsButton.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.85f));
    presetsButton.onClick = [this] { togglePresets(); };
    addAndMakeVisible (presetsButton);

    addChildComponent (presetBrowser);
    presetBrowser.onClose = [this] { togglePresets(); };

    themeButton.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    themeButton.onClick = [this] { toggleTheme(); };
    addAndMakeVisible (themeButton);

    addChildComponent (themePanel);
    themePanel.onClose = [this] { toggleTheme(); };
    themePanel.onChanged = [this]
    {
        lookAndFeel.refreshColours();
        learnButton.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.85f));
        modulesButton.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.85f));
        themeButton.setColour (juce::TextButton::buttonColourId, theme::panelLight);
        theme::save();
        repaint();
    };

    addChildComponent (rackView);   // overlay, hidden until MODULES is clicked
    rackView.onClose = [this] { toggleRack(); };
    addChildComponent (updateBanner);   // shows itself only when an update is pending
    addAndMakeVisible (spectrum);
    addAndMakeVisible (dnaPanel);
    addAndMakeVisible (pitchDisplay);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (analysisPanel);
    addAndMakeVisible (moduleStrip);

    processor.getUpdater().onStateChanged = [this] (UpdateChecker::Info i)
    {
        updateBanner.setInfo (i);
        resized();
    };
    updateBanner.setInfo (processor.getUpdater().getInfo());

    processor.onAutoMixApplied = [this]
    {
        const auto& r = processor.getLastResult();
        analysisPanel.setResult (r.presetName, r.summary);
        learnButton.setButtonText ("LEARN");
        presetBar.refresh();
        rackView.refreshAdvisor();     // update module suggestions from the new analysis
    };

    // Refresh the toolbar whenever preset/undo/A-B state changes.
    processor.getPresets().onStateChanged = [this] { presetBar.refresh(); };

    analysisPanel.onChatMessage = [this] (const juce::String& msg)
    {
        return processor.applyChatMessage (msg);
    };

    setResizable (true, true);
    setResizeLimits (900, 700, 2400, 1600);
    setSize (1120, 800);
    startTimerHz (20);
}

VoxBrainEditor::~VoxBrainEditor()
{
    processor.onAutoMixApplied = nullptr;
    processor.getPresets().onStateChanged = nullptr;
    processor.getUpdater().onStateChanged = nullptr;
    setLookAndFeel (nullptr);
}

void VoxBrainEditor::learnClicked()
{
    if (processor.isLearning())
    {
        processor.setLearning (false);
        learnButton.setButtonText ("ANALYZING…");
    }
    else
    {
        processor.setLearning (true);
        learnButton.setButtonText ("LISTENING… (click to finish)");
        analysisPanel.setResult ("", "Learning… play the vocal section you want mixed, "
                                     "then click the button again.");
    }
}

void VoxBrainEditor::toggleRack()
{
    rackVisible = ! rackVisible;
    rackView.setVisible (rackVisible);
    if (rackVisible)
    {
        rackView.refreshAdvisor();
        rackView.toFront (true);
    }
    modulesButton.setButtonText (rackVisible ? "◂ MIXER" : "MODULES");
    resized();
}

void VoxBrainEditor::toggleTheme()
{
    themeVisible = ! themeVisible;
    themePanel.setVisible (themeVisible);
    if (themeVisible) themePanel.toFront (true);
    resized();
}

void VoxBrainEditor::togglePresets()
{
    browserVisible = ! browserVisible;
    presetBrowser.setVisible (browserVisible);
    if (browserVisible) { presetBrowser.refresh(); presetBrowser.toFront (true); }
    presetsButton.setButtonText (browserVisible ? "◂ MIXER" : "PRESETS");
    resized();
}

void VoxBrainEditor::timerCallback()
{
    // Pulse the learn button while listening
    if (processor.isLearning())
    {
        pulsePhase += 0.15f;
        const float glow = 0.65f + 0.35f * std::sin (pulsePhase);
        learnButton.setColour (juce::TextButton::buttonColourId,
                               theme::accentWarm.withAlpha (glow));
    }
}

void VoxBrainEditor::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    const float glow = juce::jlimit (0.2f, 1.0f, theme::glow);

    // Base vertical gradient backdrop for depth.
    g.setGradientFill (juce::ColourGradient (theme::bg.brighter (0.04f), 0.0f, 0.0f,
                                             theme::bg.darker (0.30f), 0.0f, h, false));
    g.fillAll();

    // Holographic glow: two large radial pools of the two accent colours in
    // opposite corners. Low alpha so it reads as ambient light through glass.
    auto radial = [&g] (juce::Colour c, float cxp, float cyp, float radius, float w_, float h_)
    {
        juce::ColourGradient gr (c, cxp, cyp, juce::Colours::transparentBlack,
                                 cxp, cyp + radius, true);
        g.setGradientFill (gr);
        g.fillRect (0.0f, 0.0f, w_, h_);
    };
    radial (theme::accent.withAlpha    (0.16f * glow), w * 0.82f, h * 0.06f, h * 0.62f, w, h);
    radial (theme::accentWarm.withAlpha (0.13f * glow), w * 0.10f, h * 0.94f, h * 0.60f, w, h);

    // Vignette for a glassy, focused centre.
    juce::ColourGradient vig (juce::Colours::transparentBlack, w * 0.5f, h * 0.5f,
                              juce::Colours::black.withAlpha (0.35f), w * 0.5f, h, true);
    g.setGradientFill (vig);
    g.fillRect (getLocalBounds());

    // ---- frosted header bar ----
    auto bar = getLocalBounds().removeFromTop (52);
    g.setGradientFill (juce::ColourGradient (theme::panelLight.withAlpha (0.55f), 0.0f, 0.0f,
                                             theme::panel.withAlpha (0.75f), 0.0f, 52.0f, false));
    g.fillRect (bar);
    // top highlight line (glass edge) + accent underline with soft glow
    g.setColour (theme::text.withAlpha (0.06f));
    g.fillRect (bar.getX(), bar.getY(), bar.getWidth(), 1);
    g.setColour (theme::accent.withAlpha (0.25f * glow));
    g.fillRect (0, 51, getWidth(), 4);
    g.setColour (theme::accent);
    g.fillRect (0, 51, getWidth(), 2);

    auto header = getLocalBounds().removeFromTop (52).reduced (16, 0);
    // glowing accent dot
    const float dotY = header.getCentreY() - 4.0f;
    g.setColour (theme::accent.withAlpha (0.35f * glow));
    g.fillEllipse ((float) header.getX() - 3.0f, dotY - 3.0f, 14.0f, 14.0f);
    g.setColour (theme::accent);
    g.fillEllipse ((float) header.getX(), dotY, 8.0f, 8.0f);
    header.removeFromLeft (16);

    // soft glow behind the wordmark
    g.setColour (theme::accent.withAlpha (0.18f * glow));
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("VOXBRAIN", header.translated (0, 0), juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.drawText ("VOXBRAIN", header, juce::Justification::centredLeft);
    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("AI VOCAL ENGINEER", header.translated (186, 5), juce::Justification::centredLeft);
}

void VoxBrainEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (52).reduced (16, 8);
    learnButton.setBounds (header.removeFromRight (240));
    header.removeFromRight (8);
    modulesButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (8);
    themeButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (8);
    presetsButton.setBounds (header.removeFromRight (110));

    // Theme panel: a floating card, centred over the content.
    themePanel.setBounds (getLocalBounds().withSizeKeepingCentre (420, 620));
    // Preset browser: full-window overlay below the header.
    presetBrowser.setBounds (area);
    if (browserVisible) return;   // browser covers the mixer

    // The rack is a full-window overlay below the header.
    rackView.setBounds (area);
    if (rackVisible)
        return;   // rack covers the mixer; skip laying the mixer out underneath

    if (updateBanner.isActive())
        updateBanner.setBounds (area.removeFromTop (34).reduced (10, 2));

    area.reduce (10, 0);
    moduleStrip.setBounds (area.removeFromBottom (270));
    analysisPanel.setBounds (area.removeFromBottom (170).reduced (0, 4));
    presetBar.setBounds (area.removeFromBottom (36));
    pitchDisplay.setBounds (area.removeFromBottom (120).reduced (0, 4));

    // Top visual row: spectrum on the left, live Vocal DNA radar on the right.
    auto top = area.reduced (3, 4);
    const int radarW = juce::jlimit (200, 320, top.getWidth() * 38 / 100);
    dnaPanel.setBounds (top.removeFromRight (radarW).reduced (6, 0));
    spectrum.setBounds (top);
}
} // namespace vf
