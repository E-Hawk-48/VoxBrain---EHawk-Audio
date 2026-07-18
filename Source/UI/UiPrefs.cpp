#include "UiPrefs.h"

namespace vf::uiprefs
{
    juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("VoxBrain").getChildFile ("ui.xml");
    }

    std::unique_ptr<juce::XmlElement> toXml()
    {
        auto e = std::make_unique<juce::XmlElement> ("UiPrefs");
        e->setAttribute ("simpleMode", simpleMode ? 1 : 0);
        e->setAttribute ("tooltipsOn", tooltipsOn ? 1 : 0);
        return e;
    }

    void fromXml (const juce::XmlElement* e)
    {
        if (e == nullptr || ! e->hasTagName ("UiPrefs")) return;
        simpleMode = e->getIntAttribute ("simpleMode", simpleMode ? 1 : 0) != 0;
        tooltipsOn = e->getIntAttribute ("tooltipsOn", tooltipsOn ? 1 : 0) != 0;
    }

    void load()
    {
        if (auto xml = juce::XmlDocument::parse (file()))
            fromXml (xml.get());
    }

    void save()
    {
        auto f = file();
        f.getParentDirectory().createDirectory();
        if (auto xml = toXml())
            xml->writeTo (f, {});
    }
} // namespace vf::uiprefs
