#include "ChainOrder.h"
#include <algorithm>

namespace vf
{
std::vector<ChainItem> ChainOrder::defaultOrder (int numRackModules)
{
    std::vector<ChainItem> order;
    order.reserve ((size_t) (VocalChain::kStageCount + juce::jmax (0, numRackModules)));
    for (int i = 0; i < VocalChain::kStageCount; ++i)
        order.push_back (ChainItem::makeStage ((VocalChain::Stage) i));
    for (int i = 0; i < juce::jmax (0, numRackModules); ++i)
        order.push_back (ChainItem::makeRack());
    return order;
}

int ChainOrder::rackTokenCount (const std::vector<ChainItem>& order)
{
    int n = 0;
    for (const auto& it : order) if (it.isRack()) ++n;
    return n;
}

void ChainOrder::repair (std::vector<ChainItem>& order, int numRackModules)
{
    numRackModules = juce::jmax (0, numRackModules);

    std::vector<ChainItem> out;
    out.reserve (order.size() + (size_t) VocalChain::kStageCount);

    bool seen[VocalChain::kStageCount] = {};
    int  racks = 0;

    for (const auto& it : order)
    {
        if (it.isStage())
        {
            const int idx = (int) it.stage;
            if (idx < 0 || idx >= VocalChain::kStageCount) continue;   // unknown → drop
            if (seen[idx]) continue;                                    // duplicate → drop
            seen[idx] = true;
            out.push_back (it);
        }
        else
        {
            if (racks >= numRackModules) continue;                      // extra token → drop
            ++racks;
            out.push_back (it);
        }
    }

    // Any stage the saved order didn't mention gets appended in canonical order,
    // so a partial/corrupt list still plays every processor.
    for (int i = 0; i < VocalChain::kStageCount; ++i)
        if (! seen[i])
            out.push_back (ChainItem::makeStage ((VocalChain::Stage) i));

    // Newly added rack modules append at the end (where the rack used to run).
    for (; racks < numRackModules; ++racks)
        out.push_back (ChainItem::makeRack());

    order.swap (out);
}

std::unique_ptr<juce::XmlElement> ChainOrder::toXml (const std::vector<ChainItem>& order)
{
    auto xml = std::make_unique<juce::XmlElement> ("ChainOrder");
    for (const auto& it : order)
    {
        auto* e = xml->createNewChildElement ("Item");
        if (it.isStage())
        {
            e->setAttribute ("kind", "stage");
            e->setAttribute ("id", VocalChain::stageId (it.stage));
        }
        else
        {
            e->setAttribute ("kind", "rack");
        }
    }
    return xml;
}

std::vector<ChainItem> ChainOrder::fromXml (const juce::XmlElement* xml, int numRackModules)
{
    if (xml == nullptr || ! xml->hasTagName ("ChainOrder"))
        return defaultOrder (numRackModules);

    std::vector<ChainItem> order;
    for (auto* e : xml->getChildWithTagNameIterator ("Item"))
    {
        const auto kind = e->getStringAttribute ("kind");
        if (kind == "rack")
        {
            order.push_back (ChainItem::makeRack());
        }
        else
        {
            VocalChain::Stage s;
            if (VocalChain::stageFromId (e->getStringAttribute ("id"), s))
                order.push_back (ChainItem::makeStage (s));
        }
    }

    repair (order, numRackModules);   // always hand back something playable
    return order;
}
} // namespace vf
