#include "PresetMeta.h"
#include <juce_audio_processors/juce_audio_processors.h>

// ============================================================================
//  The only part of the preset engine that touches the APVTS. Kept separate so
//  the preset MODEL (PresetMeta.cpp) stays pure (juce_core + cryptography) and
//  is trivially testable without the audio-processor graph.
// ============================================================================
namespace vf
{
Preset Preset::captureFrom (juce::AudioProcessorValueTreeState& apvts, const juce::XmlElement* rack)
{
    Preset p;
    p.meta.presetId    = juce::Uuid().toString();
    p.meta.source      = "user";
    p.meta.createdUnix = p.meta.updatedUnix = juce::Time::currentTimeMillis() / 1000;

    for (auto* param : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            p.values[rp->getParameterID()] = rp->convertFrom0to1 (rp->getValue());

    if (rack != nullptr)
        p.rackXml = std::make_unique<juce::XmlElement> (*rack);

    p.seal();
    return p;
}

void Preset::applyTo (juce::AudioProcessorValueTreeState& apvts, juce::XmlElement** rackOut) const
{
    // Reset everything to default, then apply the (possibly partial) overrides.
    for (auto* param : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }

    for (const auto& [id, natural] : values)
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (natural));   // clamps to range
            p->endChangeGesture();
        }

    if (rackOut != nullptr)
        *rackOut = rackXml != nullptr ? new juce::XmlElement (*rackXml) : nullptr;
}
} // namespace vf
