#include "ModuleRegistry.h"

namespace vf::mods
{
juce::String categoryName (Category c)
{
    switch (c)
    {
        case Category::Input:       return "Input";
        case Category::AIAnalysis:  return "AI Analysis";
        case Category::EQ:          return "EQ";
        case Category::Filter:      return "Filter";
        case Category::Dynamics:    return "Dynamics";
        case Category::Saturation:  return "Saturation";
        case Category::Pitch:       return "Pitch";
        case Category::Modulation:  return "Modulation";
        case Category::Spatial:     return "Spatial";
        case Category::Delay:       return "Delay";
        case Category::Restoration: return "Restoration";
        case Category::Creative:    return "Creative";
        case Category::Imaging:     return "Imaging";
        case Category::Analysis:    return "Analysis";
        case Category::Utility:     return "Utility";
        case Category::Master:      return "Master";
    }
    return "Other";
}

ModuleRegistry& ModuleRegistry::instance()
{
    static ModuleRegistry reg;
    return reg;
}

void ModuleRegistry::registerType (Descriptor desc, Factory factory)
{
    desc.implemented = true;
    // Replace any existing (planned) entry with the same id.
    for (auto& d : descriptors)
        if (d.id == desc.id) { d = desc; factories[desc.id] = std::move (factory); return; }
    descriptors.push_back (std::move (desc));
    factories[descriptors.back().id] = std::move (factory);
}

void ModuleRegistry::registerPlanned (Descriptor desc)
{
    for (const auto& d : descriptors) if (d.id == desc.id) return;   // don't clobber
    desc.implemented = false;
    descriptors.push_back (std::move (desc));
}

std::unique_ptr<Module> ModuleRegistry::create (juce::StringRef id) const
{
    const auto it = factories.find (id);
    return it != factories.end() ? it->second() : nullptr;
}

bool ModuleRegistry::isImplemented (juce::StringRef id) const
{
    return factories.find (id) != factories.end();
}

std::vector<Descriptor> ModuleRegistry::inCategory (Category c) const
{
    std::vector<Descriptor> out;
    for (const auto& d : descriptors) if (d.category == c) out.push_back (d);
    return out;
}

int ModuleRegistry::scoreMatch (const Descriptor& d, const juce::String& q)
{
    if (q.isEmpty()) return d.implemented ? 1 : 0;   // "browse all" ordering

    int score = 0;
    const auto name = d.name.toLowerCase();
    const auto cat  = categoryName (d.category).toLowerCase();

    if (name == q)                 score += 100;      // exact name
    else if (name.startsWith (q))  score += 60;
    else if (name.contains (q))    score += 40;

    if (cat.contains (q))          score += 20;       // category word

    for (const auto& tag : d.tags)                    // tags
        if (tag.toLowerCase() == q)         score += 35;
        else if (tag.toLowerCase().contains (q)) score += 18;

    if (d.description.toLowerCase().contains (q)) score += 8;   // description

    if (score > 0 && d.implemented) score += 5;       // prefer creatable modules
    return score;
}

std::vector<Descriptor> ModuleRegistry::search (const juce::String& query) const
{
    const auto q = query.trim().toLowerCase();

    std::vector<std::pair<int, const Descriptor*>> scored;
    for (const auto& d : descriptors)
    {
        const int s = scoreMatch (d, q);
        if (q.isEmpty() || s > 0)
            scored.push_back ({ s, &d });
    }

    std::stable_sort (scored.begin(), scored.end(),
                      [] (const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<Descriptor> out;
    out.reserve (scored.size());
    for (const auto& [s, d] : scored) { juce::ignoreUnused (s); out.push_back (*d); }
    return out;
}
} // namespace vf::mods
