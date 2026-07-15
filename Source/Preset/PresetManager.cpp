#include "PresetManager.h"
#include "../Parameters.h"

namespace vf
{
using namespace vf::param;

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    slotA = capture();            // A starts as the current (default) state
}

// ============================================================================
//  Snapshots
// ============================================================================
PresetManager::Snapshot PresetManager::capture() const
{
    Snapshot s;
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            s[rp->getParameterID()] = rp->getValue();     // normalised
    return s;
}

void PresetManager::restore (const Snapshot& s)
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto it = s.find (rp->getParameterID());
            if (it == s.end())
                continue;
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, it->second));
            rp->endChangeGesture();
        }
}

PresetManager::Snapshot PresetManager::defaults() const
{
    Snapshot s;
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            s[rp->getParameterID()] = rp->getDefaultValue();
    return s;
}

// ============================================================================
//  Undo / Redo
// ============================================================================
void PresetManager::pushUndo (const juce::String& label)
{
    redoStack.clear();
    undoStack.push_back ({ label, capture() });
    while (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin());
    notify();
}

bool PresetManager::undo()
{
    if (undoStack.empty())
        return false;
    redoStack.push_back ({ undoStack.back().first, capture() });
    restore (undoStack.back().second);
    undoStack.pop_back();
    notify();
    return true;
}

bool PresetManager::redo()
{
    if (redoStack.empty())
        return false;
    undoStack.push_back ({ redoStack.back().first, capture() });
    restore (redoStack.back().second);
    redoStack.pop_back();
    notify();
    return true;
}

// ============================================================================
//  A / B compare
// ============================================================================
void PresetManager::toggleAB()
{
    const int other = 1 - activeSlot;

    // Save the current edit into the active slot.
    (activeSlot == 0 ? slotA : slotB) = capture();
    slotPopulated[activeSlot] = true;

    // First time we visit the other slot, seed it with the current state so
    // B starts as a clone of A rather than jumping to defaults.
    if (! slotPopulated[other])
    {
        (other == 0 ? slotA : slotB) = capture();
        slotPopulated[other] = true;
    }

    restore (other == 0 ? slotA : slotB);
    activeSlot = other;
    notify();
}

void PresetManager::copyToOther()
{
    const int other = 1 - activeSlot;
    (other == 0 ? slotA : slotB) = capture();
    slotPopulated[other] = true;
    notify();
}

// ============================================================================
//  Presets — files
// ============================================================================
juce::File PresetManager::userPresetDir() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("VoxBrain").getChildFile ("Presets");
}

juce::String PresetManager::sanitize (const juce::String& name)
{
    juce::String out;
    for (auto c : name)
        out << (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-' || c == '_'
                    ? juce::String::charToString (c) : juce::String());
    return out.trim();
}

juce::StringArray PresetManager::userPresetNames() const
{
    juce::StringArray names;
    const auto dir = userPresetDir();
    if (dir.isDirectory())
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.vbpreset"))
            names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool PresetManager::saveUserPreset (const juce::String& name)
{
    const auto clean = sanitize (name);
    if (clean.isEmpty())
        return false;

    const auto dir = userPresetDir();
    dir.createDirectory();
    const auto file = dir.getChildFile (clean + ".vbpreset");

    juce::XmlElement xml ("VoxBrainPreset");
    xml.setAttribute ("name", clean);
    xml.setAttribute ("version", 1);
    for (const auto& [id, v] : capture())
    {
        auto* e = xml.createNewChildElement ("param");
        e->setAttribute ("id", id);
        e->setAttribute ("v", (double) v);
    }
    return xml.writeTo (file);
}

bool PresetManager::loadUserPreset (const juce::String& stem)
{
    const auto file = userPresetDir().getChildFile (stem + ".vbpreset");
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("VoxBrainPreset"))
        return false;

    pushUndo ("Load " + stem);

    Snapshot s = defaults();                 // unspecified params reset to default
    for (auto* e : xml->getChildWithTagNameIterator ("param"))
        s[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("v");

    restore (s);
    notify();
    return true;
}

// ============================================================================
//  Presets — factory (partial natural-unit overrides on top of the defaults)
// ============================================================================
const std::vector<PresetManager::Factory>& PresetManager::factories() const
{
    static const std::vector<Factory> f = {
        { "Pop Sheen",
          { { eqOn, 1 }, { eqPresenceGain, 2.5f }, { eqAirGain, 3.0f },
            { deessOn, 1 }, { deessThreshold, -32.0f },
            { compOn, 1 }, { compThreshold, -20.0f }, { compRatio, 3.0f }, { compMakeup, 3.0f },
            { satOn, 1 }, { satMix, 20.0f },
            { verbOn, 1 }, { verbSize, 40.0f }, { verbMix, 12.0f },
            { limitOn, 1 }, { limitGain, 3.0f } } },

        { "Warm Intimate",
          { { eqOn, 1 }, { eqLowShelfGain, 2.0f }, { eqMudGain, -2.0f }, { eqAirGain, 0.5f },
            { compOn, 1 }, { compThreshold, -22.0f }, { compRatio, 2.5f }, { compMix, 100.0f },
            { satOn, 1 }, { satDrive, 30.0f }, { satMix, 30.0f },
            { verbOn, 1 }, { verbSize, 30.0f }, { verbDamp, 60.0f }, { verbMix, 10.0f } } },

        { "Rap Upfront",
          { { eqOn, 1 }, { eqHpfFreq, 100.0f }, { eqPresenceGain, 2.0f },
            { compOn, 1 }, { compThreshold, -18.0f }, { compRatio, 4.0f }, { compAttack, 3.0f },
            { satOn, 1 }, { satMix, 25.0f },
            { delayOn, 1 }, { delayTime, 120.0f }, { delayMix, 8.0f },
            { verbOn, 1 }, { verbSize, 30.0f }, { verbMix, 8.0f },
            { limitOn, 1 }, { limitGain, 4.0f } } },

        { "Vintage Tape",
          { { eqOn, 1 }, { eqHpfFreq, 60.0f }, { eqAirGain, -2.0f },
            { satOn, 1 }, { satDrive, 45.0f }, { satMix, 40.0f },
            { verbOn, 1 }, { verbDamp, 65.0f }, { verbMix, 12.0f },
            { limitOn, 1 }, { limitGain, 2.0f } } },

        { "Podcast Clean",
          { { pitchOn, 0 },
            { gateOn, 1 }, { gateThreshold, -55.0f },
            { eqOn, 1 }, { eqHpfFreq, 90.0f },
            { deessOn, 1 }, { deessThreshold, -30.0f },
            { compOn, 1 }, { compThreshold, -20.0f }, { compRatio, 3.0f }, { compMakeup, 3.0f },
            { satOn, 0 }, { delayOn, 0 }, { verbOn, 0 },
            { limitOn, 1 }, { limitGain, 3.0f } } },
    };
    return f;
}

juce::StringArray PresetManager::factoryNames() const
{
    juce::StringArray names;
    for (const auto& fp : factories())
        names.add (fp.name);
    return names;
}

void PresetManager::loadFactory (int index)
{
    const auto& list = factories();
    if (index < 0 || index >= (int) list.size())
        return;

    pushUndo ("Preset: " + list[(size_t) index].name);

    Snapshot s = defaults();
    for (const auto& [id, natural] : list[(size_t) index].overrides)
        if (auto* p = apvts.getParameter (id))
            s[id] = p->convertTo0to1 (natural);

    restore (s);
    notify();
}
} // namespace vf
