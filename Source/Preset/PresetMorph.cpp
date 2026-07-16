#include "PresetMorph.h"
#include <set>

namespace vf
{
Preset PresetMorph::blend (const Preset& a, const Preset& b, float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    Preset out;
    out.meta.presetId    = juce::Uuid().toString();
    out.meta.name        = a.meta.name + " ⇄ " + b.meta.name;
    out.meta.source      = "user";
    out.meta.genre       = t < 0.5f ? a.meta.genre : b.meta.genre;
    out.meta.mood        = t < 0.5f ? a.meta.mood  : b.meta.mood;
    out.meta.vocalStyle  = t < 0.5f ? a.meta.vocalStyle : b.meta.vocalStyle;
    out.meta.energy      = (int) std::round (a.meta.energy * (1.0f - t) + b.meta.energy * t);
    out.meta.description = "Morph: " + juce::String ((int) ((1.0f - t) * 100.0f)) + "% "
                         + a.meta.name + " / " + juce::String ((int) (t * 100.0f)) + "% " + b.meta.name;
    out.meta.aiGenerated = a.meta.aiGenerated || b.meta.aiGenerated;

    std::set<juce::String> keys;
    for (auto& kv : a.values) keys.insert (kv.first);
    for (auto& kv : b.values) keys.insert (kv.first);

    for (const auto& k : keys)
    {
        const bool ha = a.values.count (k) > 0;
        const bool hb = b.values.count (k) > 0;
        if (ha && hb) out.values[k] = a.values.at (k) * (1.0f - t) + b.values.at (k) * t;
        else if (ha)  out.values[k] = a.values.at (k);   // only A has it → carry through
        else          out.values[k] = b.values.at (k);
    }

    // Prefer A's rack below the midpoint, else B's.
    const Preset& rackSrc = t < 0.5f ? a : b;
    if (rackSrc.rackXml != nullptr)
        out.rackXml = std::make_unique<juce::XmlElement> (*rackSrc.rackXml);

    out.seal();
    return out;
}

Preset PresetMorph::blendMany (const std::vector<std::pair<const Preset*, float>>& weighted)
{
    Preset out;
    out.meta.presetId = juce::Uuid().toString();
    out.meta.name     = "Multi-Morph";
    out.meta.source   = "user";

    float total = 0.0f;
    for (const auto& [p, w] : weighted) if (p != nullptr) total += juce::jmax (0.0f, w);
    if (total <= 0.0f) return out;

    std::map<juce::String, float> acc, wsum;
    float energyAcc = 0.0f;
    const Preset* dominant = nullptr; float dw = -1.0f;

    for (const auto& [p, w] : weighted)
    {
        if (p == nullptr || w <= 0.0f) continue;
        const float nw = w / total;
        energyAcc += p->meta.energy * nw;
        if (nw > dw) { dw = nw; dominant = p; }
        for (const auto& [k, v] : p->values) { acc[k] += v * nw; wsum[k] += nw; }
    }

    for (const auto& [k, v] : acc)
        out.values[k] = wsum[k] > 0.0f ? v / wsum[k] : v;   // normalise by present weight

    out.meta.energy = (int) std::round (energyAcc);
    if (dominant != nullptr)
    {
        out.meta.genre = dominant->meta.genre;
        out.meta.mood  = dominant->meta.mood;
        if (dominant->rackXml != nullptr)
            out.rackXml = std::make_unique<juce::XmlElement> (*dominant->rackXml);
    }
    out.seal();
    return out;
}
} // namespace vf
