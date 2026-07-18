#include "PluginEditor.h"
#include "Chat/ChatEngine.h"
#include <cmath>

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
    uiprefs::load();               // restore Simple/Advanced + hover-help choice
    lookAndFeel.refreshColours();
    setLookAndFeel (&lookAndFeel);

    learnButton.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.85f));
    learnButton.setTooltip ("Click, sing the section you want mixed, then click again — VoxBrain builds the whole chain for you.");
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);

    modulesButton.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.85f));
    modulesButton.setTooltip ("Open the modular rack — add, reorder and customise extra processing modules.");
    modulesButton.onClick = [this] { toggleRack(); };
    addAndMakeVisible (modulesButton);

    presetsButton.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.85f));
    presetsButton.setTooltip ("Browse, load, save and AI-generate presets.");
    presetsButton.onClick = [this] { togglePresets(); };
    addAndMakeVisible (presetsButton);

    addChildComponent (presetBrowser);
    presetBrowser.onClose = [this] { togglePresets(); };

    themeButton.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    themeButton.setTooltip ("Recolour VoxBrain — choose a neon scheme or pick your own colours.");
    themeButton.onClick = [this] { toggleTheme(); };
    addAndMakeVisible (themeButton);

    simpleButton.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    simpleButton.setTooltip ("Switch between Simple (essentials only) and Advanced (all controls) views.");
    simpleButton.onClick = [this] { toggleSimpleMode(); };
    addAndMakeVisible (simpleButton);

    helpButton.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    helpButton.setTooltip ("Turn hover help on or off. When on, rest the mouse over any control for a plain-language tip.");
    helpButton.onClick = [this] { toggleHelp(); };
    addAndMakeVisible (helpButton);

    referenceButton.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.65f));
    referenceButton.setTooltip ("AI Reference Match — open the analyzer, then drag an audio file in (or Browse). "
                                "The AI reverse-engineers its vocal production and suggests a chain. Your voice is never changed.");
    referenceButton.onClick = [this] { toggleReference(); };
    addAndMakeVisible (referenceButton);

    addChildComponent (referencePanel);   // overlay, hidden until REFERENCE is clicked
    referencePanel.onClose  = [this] { toggleReference(); };
    referencePanel.onBrowse = [this] { openReferenceChooser(); };
    referencePanel.onFile   = [this] (const juce::File& f) { startReference (f); };
    referencePanel.onCancel = [this] { processor.getReferenceImport().cancelCurrent(); referencePanel.showDropZone(); };
    referencePanel.onAcceptAll = [this]
    {
        if (referenceResult.ok) { processor.applyReferenceMatch (referenceResult.match); referencePanel.markAllAccepted(); }
    };
    referencePanel.onAcceptDecision = [this] (int i)
    {
        if (referenceResult.ok && i >= 0 && i < (int) referenceResult.match.decisions.size())
            processor.applyReferenceDecision (referenceResult.match.decisions[(size_t) i]);
    };
    referencePanel.onAcceptInsert = [this] (int i)
    {
        if (referenceResult.ok && i >= 0 && i < (int) referenceResult.match.rackInserts.size())
            processor.applyReferenceRackInsert (referenceResult.match.rackInserts[(size_t) i]);
    };
    referencePanel.onCompare = [this]
    {
        processor.toggleReferenceCompare();
        referencePanel.setCompareShowingOriginal (processor.referenceShowingOriginal());
    };
    referencePanel.onUndo = [this] { processor.getPresets().undo(); };
    referencePanel.onSavePreset = [this]
    {
        if (! referenceResult.ok) return;
        const auto p = processor.saveReferenceAsPreset (referenceResult);
        referencePanel.setSaveStatus (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 Saved \"")) + p.meta.name + "\" \xe2\x80\x94 see PRESETS");
    };
    referencePanel.onShare = [this]
    {
        if (! referenceResult.ok) return;
        juce::String err;
        const bool ok = processor.shareReferenceToCommunity (referenceResult, err);
        referencePanel.setSaveStatus (ok ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 Shared to Community"))
                                         : "Share failed: " + err);
    };

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

    applyUiPrefs();                // reflect the restored Simple/Advanced + help choice

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

void VoxBrainEditor::toggleSimpleMode()
{
    uiprefs::simpleMode = ! uiprefs::simpleMode;
    uiprefs::save();
    applyUiPrefs();
    resized();
}

void VoxBrainEditor::toggleHelp()
{
    uiprefs::tooltipsOn = ! uiprefs::tooltipsOn;
    uiprefs::save();
    applyUiPrefs();
}

void VoxBrainEditor::applyUiPrefs()
{
    // Simple/Advanced view
    moduleStrip.setSimpleMode (uiprefs::simpleMode);
    simpleButton.setButtonText (uiprefs::simpleMode ? "SIMPLE" : "ADVANCED");

    // Hover help: creating the TooltipWindow activates every setTooltip in the
    // plugin; destroying it disables them. Owned by the editor so it lives and
    // dies with the window.
    if (uiprefs::tooltipsOn)
    {
        if (tooltipWindow == nullptr)
            tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 550);
    }
    else
    {
        tooltipWindow.reset();
    }
    helpButton.setButtonText (uiprefs::tooltipsOn ? "HELP ON" : "HELP OFF");
    helpButton.setColour (juce::TextButton::buttonColourId,
                          uiprefs::tooltipsOn ? theme::accent.withAlpha (0.55f) : theme::panelLight);
}

// ============================================================================
//  AI Reference Mix Analyzer — drag-drop + browse + result polling
// ============================================================================
namespace
{
    bool hasAudioExtension (const juce::String& path)
    {
        const auto ext = path.fromLastOccurrenceOf (".", false, true).toLowerCase();
        return ext == "wav" || ext == "aif" || ext == "aiff" || ext == "flac"
            || ext == "mp3" || ext == "ogg" || ext == "m4a" || ext == "aac" || ext == "wma";
    }
}

bool VoxBrainEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (hasAudioExtension (f)) return true;
    return false;
}

void VoxBrainEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (hasAudioExtension (f)) { startReference (juce::File (f)); break; }
}

void VoxBrainEditor::openReferenceChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a reference vocal or song",
        juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.aac;*.wma");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) startReference (f);
        });
}

void VoxBrainEditor::toggleReference()
{
    referenceVisible = ! referenceVisible;
    referencePanel.setVisible (referenceVisible);
    if (referenceVisible)
    {
        if (! processor.getReferenceImport().isBusy() && ! referencePanel.hasResult())
            referencePanel.showDropZone();
        referencePanel.toFront (true);
    }
    referenceButton.setButtonText (referenceVisible ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x82 MIXER"))
                                                    : juce::String ("REFERENCE"));
    resized();
}

void VoxBrainEditor::startReference (const juce::File& file)
{
    refFileName = file.getFileName();

    // Open the analyzer (closing any other full-window overlay) so the result shows.
    if (rackVisible)    toggleRack();
    if (browserVisible) togglePresets();
    if (themeVisible)   toggleTheme();
    if (! referenceVisible) toggleReference();

    referencePanel.setAnalyzing (refFileName,
                                 juce::String (juce::CharPointer_UTF8 ("Loading\xe2\x80\xa6")), 0.0f);
    processor.getReferenceImport().analyzeFile (file);
}

void VoxBrainEditor::pollReference()
{
    auto& imp = processor.getReferenceImport();

    ReferenceResult res;
    if (imp.takeResult (res))
    {
        referenceResult = res;                       // remember for the apply workflow
        processor.resetReferenceCompare();           // fresh reference → fresh A/B baseline
        referencePanel.setCompareShowingOriginal (false);
        if (! referenceVisible) toggleReference();
        referencePanel.showResult (res);
        referenceButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x82 MIXER")));
        return;
    }

    if (imp.isBusy())
    {
        referencePanel.setAnalyzing (refFileName, imp.statusText(), imp.getProgress());
        referenceButton.setButtonText (juce::String ((int) std::round (imp.getProgress() * 100.0f)) + "%");
    }
}

void VoxBrainEditor::timerCallback()
{
    pollReference();

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
    // The header now holds seven buttons; only show the decorative subtitle when
    // the window is wide enough that it can't collide with the leftmost button.
    if (getWidth() >= 1200)
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("AI VOCAL ENGINEER", header.translated (186, 5), juce::Justification::centredLeft);
    }
}

void VoxBrainEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (52).reduced (16, 8);
    learnButton.setBounds (header.removeFromRight (200));
    header.removeFromRight (8);
    modulesButton.setBounds (header.removeFromRight (106));
    header.removeFromRight (8);
    presetsButton.setBounds (header.removeFromRight (96));
    header.removeFromRight (8);
    referenceButton.setBounds (header.removeFromRight (104));
    header.removeFromRight (8);
    themeButton.setBounds (header.removeFromRight (74));
    header.removeFromRight (8);
    simpleButton.setBounds (header.removeFromRight (96));
    header.removeFromRight (8);
    helpButton.setBounds (header.removeFromRight (74));

    // Theme panel: a floating card, centred over the content.
    themePanel.setBounds (getLocalBounds().withSizeKeepingCentre (420, 620));
    // Preset browser: full-window overlay below the header.
    presetBrowser.setBounds (area);
    if (browserVisible) return;   // browser covers the mixer

    // The rack is a full-window overlay below the header.
    rackView.setBounds (area);
    if (rackVisible)
        return;   // rack covers the mixer; skip laying the mixer out underneath

    // The AI Reference Analyzer is a full-window overlay below the header.
    referencePanel.setBounds (area);
    if (referenceVisible)
        return;

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
