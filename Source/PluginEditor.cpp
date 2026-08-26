#include "PluginEditor.h"
#include "Chat/ChatEngine.h"
#include "Support/CrashLog.h"
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
      chainView (p.apvts, p.getRack(),
                 ChainView::Hooks {
                     [&p] { return p.getChainOrder(); },
                     [&p] (std::vector<ChainItem> order, juce::StringArray rackIds)
                          { p.applyChainArrangement (std::move (order), rackIds); },
                     [&p] { p.autoBuildRack(); },
                     [&p] { return p.suggestModules(); },
                     [&p] (int idx) { p.applyVoiceCharacter (idx); } }),
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

    presetsButton.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.85f));
    presetsButton.setTooltip ("Browse, load, save and AI-generate presets.");
    presetsButton.onClick = [this] { togglePresets(); };
    addAndMakeVisible (presetsButton);

    addChildComponent (presetBrowser);
    presetBrowser.onClose = [this] { togglePresets(); };

    setupButton.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    setupButton.setTooltip ("Colours, Simple/Advanced view, and hover help.");
    setupButton.onClick = [this] { showSetupMenu(); };
    addAndMakeVisible (setupButton);

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
        presetsButton.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.85f));
        setupButton.setColour (juce::TextButton::buttonColourId, theme::panelLight);
        theme::save();
        repaint();
    };

    addChildComponent (updateBanner);   // shows itself only when an update is pending
    addAndMakeVisible (spectrum);
    addAndMakeVisible (dnaPanel);
    addAndMakeVisible (pitchDisplay);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (analysisPanel);
    addAndMakeVisible (chainView);

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
        chainView.refresh();      // new analysis → refresh chain + AI module picks
    };

    // Refresh the toolbar whenever preset/undo/A-B state changes.
    processor.getPresets().onStateChanged = [this] { presetBar.refresh(); };

    analysisPanel.onChatMessage = [this] (const juce::String& msg)
    {
        return processor.applyChatMessage (msg);
    };

    applyUiPrefs();                // reflect the restored Simple/Advanced + help choice

    setResizable (true, true);
    // The minimum height is derived from the layout minimums in resized()
    // (header + scope + pitch + report + chain + preset bar), so the window can
    // never be sized into the state where a region collapses.
    setResizeLimits (940, 740, 2600, 1800);
    setSize (baseWidth, baseHeight);
    applyUiScale();               // restore the saved interface size
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
    vf::crashlog::step ("learnClicked");
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
    chainView.setSimpleMode (uiprefs::simpleMode);

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
}

// ---------------------------------------------------------------------------
//  SETUP menu — the three former header toggles, plus the theme editor.
// ---------------------------------------------------------------------------
void VoxBrainEditor::showSetupMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&lookAndFeel);

    m.addSectionHeader ("View");
    m.addItem (1, "Simple  (essentials only)", true, uiprefs::simpleMode);
    m.addItem (2, "Advanced  (every control)", true, ! uiprefs::simpleMode);
    m.addSeparator();
    m.addItem (3, "Hover help", true, uiprefs::tooltipsOn);
    m.addSeparator();
    m.addSectionHeader ("Appearance");
    m.addItem (4, "Colours...");

    juce::PopupMenu scale;
    for (int pct : { 75, 90, 100, 115, 130, 150, 175 })
        scale.addItem (100 + pct, juce::String (pct) + "%", true,
                       uiprefs::uiScalePercent == pct);
    m.addSubMenu ("Interface size", scale);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setupButton),
                     [this] (int r)
    {
        if (r == 1 && ! uiprefs::simpleMode) toggleSimpleMode();
        else if (r == 2 && uiprefs::simpleMode) toggleSimpleMode();
        else if (r == 3) toggleHelp();
        else if (r == 4) toggleTheme();
        else if (r > 100)
        {
            uiprefs::uiScalePercent = juce::jlimit (uiprefs::kMinScale,
                                                    uiprefs::kMaxScale, r - 100);
            uiprefs::save();
            applyUiScale();
        }
    });
}

// ---------------------------------------------------------------------------
//  INTERFACE SCALE.
//  The whole editor is drawn at its natural size and then transformed, so
//  resized() never has to think about scaling — it lays out in unscaled
//  coordinates and everything downstream (knobs, fonts, the chain strip)
//  follows automatically. The window itself is resized to match so the host
//  gives us exactly the room the scaled content needs.
// ---------------------------------------------------------------------------
void VoxBrainEditor::applyUiScale()
{
    const float k = juce::jlimit (uiprefs::kMinScale, uiprefs::kMaxScale,
                                  uiprefs::uiScalePercent) * 0.01f;
    setTransform (juce::AffineTransform::scale (k));
    // setSize is in UNSCALED units; the transform does the rest. Keeping the
    // base size fixed means switching scale never reflows the layout.
    setSize (baseWidth, baseHeight);
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
    if (browserVisible) togglePresets();
    if (themeVisible)   toggleTheme();
    if (! referenceVisible) toggleReference();

    referencePanel.setAnalyzing (refFileName,
                                 juce::String (juce::CharPointer_UTF8 ("Loading\xe2\x80\xa6")), 0.0f);
    processor.getReferenceImport().analyzeFile (file, referencePanel.isolateEnabled());
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
    vf::crashlog::step ("editorTimer");
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
    if (getWidth() >= 1040)
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("AI VOCAL ENGINEER", header.translated (186, 5), juce::Justification::centredLeft);
    }
}

void VoxBrainEditor::resized()
{
    // Remember the unscaled size the user has dragged to, so a later interface-
    // size change scales THAT rather than snapping back to the shipped default.
    baseWidth  = juce::jmax (940, getWidth());
    baseHeight = juce::jmax (740, getHeight());

    auto area = getLocalBounds();

    auto header = area.removeFromTop (52).reduced (16, 8);
    learnButton.setBounds (header.removeFromRight (210));
    header.removeFromRight (8);
    presetsButton.setBounds (header.removeFromRight (104));
    header.removeFromRight (8);
    referenceButton.setBounds (header.removeFromRight (104));
    header.removeFromRight (8);
    setupButton.setBounds (header.removeFromRight (86));

    // Theme panel: a floating card, centred over the content.
    themePanel.setBounds (getLocalBounds().withSizeKeepingCentre (420, 620));
    // Preset browser: full-window overlay below the header.
    presetBrowser.setBounds (area);
    if (browserVisible) return;   // browser covers the mixer

    // The AI Reference Analyzer is a full-window overlay below the header.
    referencePanel.setBounds (area);
    if (referenceVisible)
        return;

    if (updateBanner.isActive())
        updateBanner.setBounds (area.removeFromTop (34).reduced (10, 2));

    area.reduce (10, 0);

    // ------------------------------------------------------------------
    //  VERTICAL LAYOUT — minimums first, then share out what is left.
    //
    //  This used to carve fixed heights off the BOTTOM in sequence and give
    //  the scope whatever survived. At the default window size the arithmetic
    //  worked out to 54 pixels for the spectrum and the Vocal DNA radar
    //  combined, and at the minimum window height the scope got 5 — the two
    //  headline visualisations in the plugin, squeezed to nothing, on every
    //  machine. Nothing about that is visible when you write it; it only shows
    //  up when you add up the constants.
    //
    //  Allocating each region its minimum first and distributing the leftover
    //  by weight makes the failure impossible: every region is always at least
    //  usable, and extra height goes where it helps most.
    // ------------------------------------------------------------------
    struct Region { int minH; int weight; int h; };
    Region scope   { 130, 3, 0 };   // spectrum + Vocal DNA radar
    Region pitch   {  92, 1, 0 };   // live tuning display
    Region report  { 112, 1, 0 };   // AI report + chat
    Region chain   { 300, 4, 0 };   // the module chain (main working area)
    const int presetBarH = 36;

    Region* regions[] = { &scope, &pitch, &report, &chain };

    int totalMin = presetBarH, totalWeight = 0;
    for (auto* r : regions) { r->h = r->minH; totalMin += r->minH; totalWeight += r->weight; }

    int spare = area.getHeight() - totalMin;
    if (spare > 0 && totalWeight > 0)
    {
        int given = 0;
        for (auto* r : regions)
        {
            const int add = spare * r->weight / totalWeight;
            r->h += add;
            given += add;
        }
        chain.h += spare - given;                       // rounding remainder
    }
    else if (spare < 0)
    {
        // Window shorter than the sum of the minimums (a host can force this).
        // Shrink proportionally rather than letting the last region go negative.
        const double k = (double) juce::jmax (1, area.getHeight() - presetBarH)
                       / (double) juce::jmax (1, totalMin - presetBarH);
        for (auto* r : regions) r->h = juce::jmax (24, (int) (r->h * k));
    }

    chain.h = juce::jmin (chain.h, 560);                // stop it eating a tall window
    chainView.setBounds (area.removeFromBottom (chain.h));
    analysisPanel.setBounds (area.removeFromBottom (report.h).reduced (0, 4));
    presetBar.setBounds (area.removeFromBottom (presetBarH));
    pitchDisplay.setBounds (area.removeFromBottom (pitch.h).reduced (0, 4));

    // Top visual row: spectrum on the left, live Vocal DNA radar on the right.
    // The radar is dropped entirely on narrow windows rather than being crushed
    // into an unreadable sliver.
    auto top = area.reduced (3, 4);
    const int radarW = juce::jlimit (200, 320, top.getWidth() * 38 / 100);
    const bool showRadar = top.getWidth() - radarW >= 320;
    dnaPanel.setVisible (showRadar);
    if (showRadar)
        dnaPanel.setBounds (top.removeFromRight (radarW).reduced (6, 0));
    spectrum.setBounds (top);
}
} // namespace vf
