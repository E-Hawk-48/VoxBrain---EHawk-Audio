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
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                               apvts.getParameter (spec.paramId)))
            combo->box.addItemList (choice->choices, 1);
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
        knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 14);
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

    if (! comboList.empty())
    {
        auto comboRow = area.removeFromTop (34);
        const int comboW = comboRow.getWidth() / (int) comboList.size();
        for (auto& combo : comboList)
        {
            auto cell = comboRow.removeFromLeft (comboW).reduced (2, 0);
            combo->label.setBounds (cell.removeFromBottom (12));
            combo->box.setBounds (cell);
        }
    }

    if (knobList.empty()) return;

    const int knobW = area.getWidth() / (int) knobList.size();
    for (auto& knob : knobList)
    {
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
                     std::vector<K> knobs, std::vector<C> combos = {})
    {
        auto card = std::make_unique<ModuleCard> (apvts, std::move (title), bypassId, lockIdStr,
                                                  std::move (knobs), std::move (combos));
        addAndMakeVisible (*card);
        cards.push_back ({ std::move (card), row, weight });
    };

    // Row 0 — input & tone shaping
    make (0, 2.0f, "PITCH",  pitchOn, pitchLock, { { pitchSpeed, "Speed" }, { pitchAmount, "Amount" } },
                            { { pitchKey, "Key" }, { pitchScale, "Scale" } });
    make (0, 1.0f, "GATE",   gateOn,  gateLock,  { { gateThreshold, "Thresh" } });
    make (0, 2.0f, "EQ",     eqOn,    eqLock,    { { eqHpfFreq, "HPF" }, { eqMudGain, "Mud" },
                                        { eqPresenceGain, "Pres" }, { eqAirGain, "Air" } });
    make (0, 2.0f, "DYN EQ", dyneqOn, dyneqLock, { { dyneqLowThresh, "Low" }, { dyneqMidThresh, "Mid" },
                                        { dyneqHighThresh, "High" }, { dyneqRange, "Range" } });
    make (0, 1.4f, "DE-ESS", deessOn, deessLock, { { deessThreshold, "Thresh" }, { deessFreq, "Freq" } });

    // Row 1 — dynamics, colour & space
    make (1, 1.6f, "COMP",   compOn,  compLock,  { { compThreshold, "Thresh" }, { compRatio, "Ratio" },
                                        { compMix, "Mix" } });
    make (1, 2.2f, "MULTIBAND", mbandOn, mbandLock, { { mbandLowThresh, "Low" }, { mbandMidThresh, "Mid" },
                                           { mbandHighThresh, "High" }, { mbandRatio, "Ratio" } });
    make (1, 1.9f, "SAT",    satOn,   satLock,   { { satDrive, "Drive" }, { satTone, "Tone" },
                                        { satMix, "Mix" } }, { { satType, "Model" } });
    make (1, 1.2f, "DELAY",  delayOn, delayLock, { { delayTime, "Time" }, { delayMix, "Mix" } });
    make (1, 2.1f, "REVERB", verbOn,  verbLock,  { { verbSize, "Size" }, { verbDecay, "Decay" },
                                        { verbMix, "Mix" } }, { { verbType, "Type" } });
    make (1, 1.2f, "LIMIT",  limitOn, limitLock, { { limitGain, "Gain" }, { limitCeiling, "Ceil" } });
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
            if (e.row == r)
                fb.items.add (juce::FlexItem (*e.card).withFlex (e.weight).withMargin (2.0f));
        fb.performLayout (area.removeFromTop (rowH));
    }
}
} // namespace vf
