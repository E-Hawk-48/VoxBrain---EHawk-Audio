#include "ModuleStrip.h"
#include "../Parameters.h"

namespace vf
{
namespace
{
    // Small padlock glyph drawn in the card header when a module is locked.
    void drawPadlock (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        auto box = r.withSizeKeepingCentre (r.getWidth(), r.getHeight()).reduced (1.0f);
        const float bodyH = box.getHeight() * 0.58f;
        auto body = box.removeFromBottom (bodyH);
        g.setColour (c);
        g.fillRoundedRectangle (body, 1.6f);

        const float sw = 1.4f;
        auto shackle = box.reduced (box.getWidth() * 0.18f, 0.0f);
        juce::Path p;
        p.addArc (shackle.getX(), shackle.getY(),
                  shackle.getWidth(), shackle.getHeight() * 2.0f,
                  juce::MathConstants<float>::pi * 1.0f,
                  juce::MathConstants<float>::pi * 2.0f, true);
        g.strokePath (p, juce::PathStrokeType (sw));
    }
}

// ============================================================================
//  ModuleCard
// ============================================================================
ModuleCard::ModuleCard (juce::AudioProcessorValueTreeState& apvtsRef,
                        juce::String title, juce::String bypassParamId, juce::String lockParamId,
                        std::vector<KnobSpec> knobs,
                        std::vector<ComboSpec> combos)
    : apvts (apvtsRef),
      titleText (std::move (title)),
      lockId (std::move (lockParamId))
{
    lockValue = apvts.getRawParameterValue (lockId);

    for (auto& spec : combos)
    {
        auto combo = std::make_unique<Combo>();
        combo->advanced = spec.advanced;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                               apvts.getParameter (spec.paramId)))
            combo->box.addItemList (choice->choices, 1);
        if (spec.tip.isNotEmpty()) combo->box.setTooltip (spec.tip);
        addAndMakeVisible (combo->box);

        combo->label.setText (spec.label, juce::dontSendNotification);
        combo->label.setJustificationType (juce::Justification::centred);
        combo->label.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (combo->label);

        combo->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, spec.paramId, combo->box);
        comboList.push_back (std::move (combo));
    }

    bypassButton.setButtonText ("ON");
    bypassButton.setTooltip ("Turn " + titleText + " on or off (green = active).");
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, bypassParamId, bypassButton);

    lockButton.setButtonText ("LOCK");
    lockButton.setTooltip ("Lock this module — protects it from AI Auto-Mix and the AI Engineer");
    addAndMakeVisible (lockButton);
    lockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, lockId, lockButton);

    for (auto& spec : knobs)
    {
        auto knob = std::make_unique<Knob>();
        knob->advanced = spec.advanced;
        knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 14);
        if (spec.tip.isNotEmpty()) knob->slider.setTooltip (spec.tip);
        addAndMakeVisible (knob->slider);

        knob->label.setText (spec.label, juce::dontSendNotification);
        knob->label.setJustificationType (juce::Justification::centred);
        knob->label.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (knob->label);

        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, spec.paramId, knob->slider);
        knobList.push_back (std::move (knob));
    }

    apvts.addParameterListener (lockId, this);
    applyLockState();
}

ModuleCard::~ModuleCard()
{
    apvts.removeParameterListener (lockId, this);
    cancelPendingUpdate();
}

bool ModuleCard::isLocked() const
{
    return lockValue != nullptr && lockValue->load() > 0.5f;
}

void ModuleCard::parameterChanged (const juce::String& id, float)
{
    if (id == lockId)
        triggerAsyncUpdate();     // marshal to the message thread
}

void ModuleCard::handleAsyncUpdate()
{
    applyLockState();
}

void ModuleCard::applyLockState()
{
    const bool locked = isLocked();
    for (auto& knob : knobList)   knob->slider.setEnabled (! locked);
    for (auto& combo : comboList) combo->box.setEnabled (! locked);
    // Bypass stays live even when locked.
    repaint();
}

void ModuleCard::setSimpleMode (bool simple)
{
    if (simpleMode == simple) return;
    simpleMode = simple;
    // Hide advanced controls (and their labels) in Simple mode; the card keeps
    // its essential knob(s) so it never looks empty or broken.
    for (auto& knob : knobList)
    {
        const bool vis = ! (simpleMode && knob->advanced);
        knob->slider.setVisible (vis);
        knob->label.setVisible (vis);
    }
    for (auto& combo : comboList)
    {
        const bool vis = ! (simpleMode && combo->advanced);
        combo->box.setVisible (vis);
        combo->label.setVisible (vis);
    }
    resized();
}

void ModuleCard::paint (juce::Graphics& g)
{
    const bool locked = isLocked();
    auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (locked ? theme::accent : theme::outline);
    g.drawRoundedRectangle (bounds, 10.0f, locked ? 1.6f : 1.0f);

    auto titleArea = getLocalBounds().reduced (10, 8).removeFromTop (16);
    if (locked)
    {
        drawPadlock (g, titleArea.removeFromLeft (11).toFloat(), theme::accent);
        titleArea.removeFromLeft (5);
    }
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (titleText, titleArea, juce::Justification::centredLeft);
}

void ModuleCard::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto top = area.removeFromTop (20);
    bypassButton.setBounds (top.removeFromRight (40).reduced (0, 1));
    top.removeFromRight (4);
    lockButton.setBounds (top.removeFromRight (40).reduced (0, 1));

    area.removeFromTop (2);

    // Count only the controls that are currently visible so hiding advanced
    // controls in Simple mode re-flows the remaining ones to fill the card.
    int visCombos = 0;
    for (auto& combo : comboList) if (combo->box.isVisible()) ++visCombos;
    int visKnobs = 0;
    for (auto& knob : knobList) if (knob->slider.isVisible()) ++visKnobs;

    if (visCombos > 0)
    {
        auto comboRow = area.removeFromTop (34);
        const int comboW = comboRow.getWidth() / visCombos;
        for (auto& combo : comboList)
        {
            if (! combo->box.isVisible()) continue;
            auto cell = comboRow.removeFromLeft (comboW).reduced (2, 0);
            combo->label.setBounds (cell.removeFromBottom (12));
            combo->box.setBounds (cell);
        }
    }

    if (visKnobs == 0) return;

    const int knobW = area.getWidth() / visKnobs;
    for (auto& knob : knobList)
    {
        if (! knob->slider.isVisible()) continue;
        auto cell = area.removeFromLeft (knobW);
        knob->label.setBounds (cell.removeFromBottom (14));
        knob->slider.setBounds (cell);
    }
}

// ============================================================================
//  ModuleStrip
// ============================================================================
ModuleStrip::ModuleStrip (juce::AudioProcessorValueTreeState& apvts)
{
    using namespace vf::param;
    using K = ModuleCard::KnobSpec;
    using C = ModuleCard::ComboSpec;

    auto make = [&] (int row, float weight, juce::String title,
                     const char* bypassId, const char* lockIdStr,
                     std::vector<K> knobs, std::vector<C> combos = {},
                     bool advancedCard = false)
    {
        auto card = std::make_unique<ModuleCard> (apvts, std::move (title), bypassId, lockIdStr,
                                                  std::move (knobs), std::move (combos));
        addAndMakeVisible (*card);
        cards.push_back ({ std::move (card), row, weight, advancedCard });
    };

    // KnobSpec / ComboSpec = { paramId, label, hover-help, advanced? }.
    // `advanced` controls hide in Simple mode; whole "advanced" cards hide too.

    // Row 0 — input & tone shaping
    make (0, 2.0f, "PITCH",  pitchOn, pitchLock,
          { { pitchSpeed, "Speed", "How fast the pitch snaps to the note. Fast = robotic/hard-tune, slow = natural.", true },
            { pitchAmount, "Amount", "How strongly the vocal is pulled onto the correct pitch. 0 = off, 100% = fully tuned." } },
          { { pitchKey, "Key", "The musical key the tuner snaps to — set this to your song's key." },
            { pitchScale, "Scale", "The scale used for tuning (Major, Minor, Chromatic…). Match your song." },
            { pitchLatency, "Latency", "Live = lowest delay, best for tracking/performing (won't tune very low notes); Studio = most accurate on low notes; Balanced is in between." } });
    make (0, 1.0f, "GATE",   gateOn,  gateLock,
          { { gateThreshold, "Thresh", "Silences the mic below this level to remove hiss and room noise between phrases." } });
    make (0, 2.0f, "EQ",     eqOn,    eqLock,
          { { eqHpfFreq, "HPF", "High-pass filter: removes low rumble and mic pops below this frequency." },
            { eqMudGain, "Mud", "Cuts boxy low-mid 'mud' for a clearer, less muffled vocal.", true },
            { eqPresenceGain, "Pres", "Presence: lifts the upper mids for clarity and intelligibility." },
            { eqAirGain, "Air", "Adds sparkle and openness at the very top." } });
    make (0, 2.0f, "DYN EQ", dyneqOn, dyneqLock,
          { { dyneqLowThresh, "Low", "Tames boomy low-mids only when they get too loud." },
            { dyneqMidThresh, "Mid", "Reduces nasal/honky mids only when they spike." },
            { dyneqHighThresh, "High", "Softens harsh presence only when it gets too strong." },
            { dyneqRange, "Range", "The most each band is allowed to duck." } },
          {}, /*advancedCard*/ true);
    make (0, 1.4f, "DE-ESS", deessOn, deessLock,
          { { deessThreshold, "Thresh", "How aggressively harsh 'S' and 'T' sounds are tamed." },
            { deessFreq, "Freq", "Which frequency the de-esser targets. Raise it for brighter voices.", true } });

    // Row 1 — dynamics, colour & space
    make (1, 1.6f, "COMP",   compOn,  compLock,
          { { compThreshold, "Thresh", "Level where compression begins. Lower = a more even, controlled vocal." },
            { compRatio, "Ratio", "How hard the compressor squeezes once it engages.", true },
            { compMix, "Mix", "Blends compressed with dry (parallel) for punch without squashing.", true } });
    make (1, 2.2f, "MULTIBAND", mbandOn, mbandLock,
          { { mbandLowThresh, "Low", "Controls the low band's dynamics independently." },
            { mbandMidThresh, "Mid", "Controls the mid band's dynamics independently." },
            { mbandHighThresh, "High", "Controls the high band's dynamics independently." },
            { mbandRatio, "Ratio", "How hard all three bands compress." } },
          {}, /*advancedCard*/ true);
    make (1, 1.9f, "SAT",    satOn,   satLock,
          { { satDrive, "Drive", "Adds harmonic warmth and thickness. More drive = more colour." },
            { satTone, "Tone", "Tilts the saturated tone darker or brighter.", true },
            { satMix, "Mix", "Blend of the saturated and clean signal.", true } },
          { { satType, "Model", "Saturation flavour: Tube, Tape, Console, Fuzz, Exciter, Lo-Fi…" } });
    make (1, 1.2f, "DELAY",  delayOn, delayLock,
          { { delayTime, "Time", "The echo (delay) time in milliseconds." },
            { delayMix, "Mix", "How loud the echoes are in the mix." } });
    make (1, 2.1f, "REVERB", verbOn,  verbLock,
          { { verbSize, "Size", "Size of the simulated space. Bigger = longer, roomier tail." },
            { verbDecay, "Decay", "How long the reverb tail rings out.", true },
            { verbMix, "Mix", "How much reverb is blended in." } },
          { { verbType, "Type", "Reverb character: Room, Hall, Plate, Spring, Shimmer…" } });
    make (1, 1.2f, "LIMIT",  limitOn, limitLock,
          { { limitGain, "Gain", "Drives the vocal louder into the limiter." },
            { limitCeiling, "Ceil", "Maximum output level — the vocal never goes past this.", true } });
}

void ModuleStrip::setSimpleMode (bool simple)
{
    simpleMode = simple;
    for (auto& e : cards)
    {
        e.card->setVisible (! (simpleMode && e.advancedCard));
        e.card->setSimpleMode (simpleMode);
    }
    resized();
}

void ModuleStrip::resized()
{
    if (cards.empty()) return;
    auto area = getLocalBounds();

    int numRows = 1;
    for (auto& e : cards) numRows = juce::jmax (numRows, e.row + 1);
    const int rowH = area.getHeight() / numRows;

    for (int r = 0; r < numRows; ++r)
    {
        juce::FlexBox fb;
        fb.flexDirection = juce::FlexBox::Direction::row;
        for (auto& e : cards)
            if (e.row == r && e.card->isVisible())   // hidden advanced cards take no space
                fb.items.add (juce::FlexItem (*e.card).withFlex (e.weight).withMargin (2.0f));
        fb.performLayout (area.removeFromTop (rowH));
    }
}
} // namespace vf
