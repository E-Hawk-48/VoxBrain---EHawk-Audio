#include "PresetLibrary.h"
#include <algorithm>
#include <functional>

namespace vf
{
namespace
{
    PresetLibrary::Source sourceOf (const juce::String& s)
    {
        if (s == "factory")   return PresetLibrary::Source::Factory;
        if (s == "ai")        return PresetLibrary::Source::AI;
        if (s == "community") return PresetLibrary::Source::Community;
        return PresetLibrary::Source::User;
    }
}

void PresetLibrary::clear() { presets.clear(); idToIndex.clear(); }

void PresetLibrary::add (Preset p)
{
    if (p.meta.presetId.isEmpty())
        p.meta.presetId = juce::Uuid().toString();
    idToIndex[p.meta.presetId] = (int) presets.size();
    presets.push_back (std::move (p));
}

void PresetLibrary::addAll (std::vector<Preset> ps)
{
    for (auto& p : ps) add (std::move (p));
}

const Preset* PresetLibrary::byId (const juce::String& id) const
{
    auto it = idToIndex.find (id);
    return it != idToIndex.end() ? &presets[(size_t) it->second] : nullptr;
}

// ============================================================================
//  Fuzzy scoring — name/creator/genre/mood/tags/categories, token-aware.
// ============================================================================
int PresetLibrary::fuzzyScore (const Preset& p, const juce::String& q)
{
    if (q.isEmpty()) return 1;   // empty query matches everything (score 1)

    const auto& m = p.meta;
    const auto name = m.name.toLowerCase();
    int score = 0;

    if (name == q)                score += 120;
    else if (name.startsWith (q)) score += 70;
    else if (name.contains (q))   score += 45;

    if (m.genre.toLowerCase().contains (q))      score += 30;
    if (m.mood.toLowerCase().contains (q))       score += 18;
    if (m.vocalStyle.toLowerCase().contains (q)) score += 18;
    if (m.creatorName.toLowerCase().contains (q))score += 25;

    for (const auto& t : m.tags)
        if (t.toLowerCase() == q)            score += 35;
        else if (t.toLowerCase().contains (q)) score += 16;

    for (const auto& c : m.categories)
        if (c.toLowerCase().contains (q))    score += 20;

    if (m.description.toLowerCase().contains (q)) score += 6;

    // token match: every query word appearing somewhere gives partial credit
    juce::StringArray words; words.addTokens (q, " ", "");
    const auto hay = (name + " " + m.genre + " " + m.mood + " " + m.tags.joinIntoString (" ")).toLowerCase();
    for (const auto& w : words)
        if (w.isNotEmpty() && hay.contains (w)) score += 8;

    if (score > 0 && m.official) score += 4;   // tiebreak ONLY when already relevant
    return score;
}

float PresetLibrary::trendingScore (const PresetMeta& m)
{
    // Recency-weighted popularity. Newer + downloaded + liked + well-rated ranks up.
    const auto nowU = juce::Time::currentTimeMillis() / 1000;
    const double ageDays = juce::jmax (0.0, (double) (nowU - m.updatedUnix) / 86400.0);
    const double recency = 1.0 / (1.0 + ageDays / 14.0);         // ~2-week half-life
    const double pop = m.downloads * 0.5 + m.likes * 1.0 + m.rating * m.ratingCount * 0.6;
    return (float) (pop * (0.5 + 0.5 * recency) + (m.featured ? 50.0 : 0.0));
}

// ============================================================================
//  Query
// ============================================================================
std::vector<const Preset*> PresetLibrary::query (const Query& q) const
{
    const auto lc = q.text.toLowerCase().trim();

    struct Hit { const Preset* p; int score; };
    std::vector<Hit> hits;
    hits.reserve (presets.size());

    for (const auto& p : presets)
    {
        const auto& m = p.meta;
        if (q.source != Source::All && sourceOf (m.source) != q.source) continue;
        if (q.genre.isNotEmpty()     && ! m.genre.equalsIgnoreCase (q.genre)) continue;
        if (q.mood.isNotEmpty()      && ! m.mood.equalsIgnoreCase (q.mood)) continue;
        if (q.creatorId.isNotEmpty() && m.creatorId != q.creatorId) continue;
        if (q.minRating > 0.0f       && m.rating < q.minRating) continue;
        if (q.maxCpu > 0.0f          && m.cpuEstimate > q.maxCpu) continue;
        if (q.aiOnly       && ! m.aiGenerated) continue;
        if (q.manualOnly   && m.aiGenerated) continue;
        if (q.verifiedOnly && ! m.verifiedCreator) continue;
        if (q.officialOnly && ! m.official) continue;
        if (q.favoritesOnly && ! isFavorite (m.presetId)) continue;

        const int s = fuzzyScore (p, lc);
        if (lc.isNotEmpty() && s <= 0) continue;   // text given but no match
        hits.push_back ({ &p, s });
    }

    auto byName = [] (const Hit& a, const Hit& b) { return a.p->meta.name.compareIgnoreCase (b.p->meta.name) < 0; };
    switch (q.sort)
    {
        case Sort::Relevance:      std::stable_sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return a.score > b.score; }); break;
        case Sort::NameAsc:        std::stable_sort (hits.begin(), hits.end(), byName); break;
        case Sort::NameDesc:       std::stable_sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return a.p->meta.name.compareIgnoreCase (b.p->meta.name) > 0; }); break;
        case Sort::Newest:         std::stable_sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return a.p->meta.createdUnix > b.p->meta.createdUnix; }); break;
        case Sort::Oldest:         std::stable_sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return a.p->meta.createdUnix < b.p->meta.createdUnix; }); break;
        case Sort::MostDownloaded: std::stable_sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return a.p->meta.downloads > b.p->meta.downloads; }); break;
        case Sort::HighestRated:   std::stable_sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return a.p->meta.rating > b.p->meta.rating; }); break;
        case Sort::Trending:       std::stable_sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return trendingScore (a.p->meta) > trendingScore (b.p->meta); }); break;
    }

    std::vector<const Preset*> out;
    for (const auto& h : hits)
    {
        out.push_back (h.p);
        if (q.limit > 0 && (int) out.size() >= q.limit) break;
    }
    return out;
}

// ============================================================================
//  Favorites & collections
// ============================================================================
void PresetLibrary::setFavorite (const juce::String& id, bool on)
{
    if (on) favorites.insert (id); else favorites.erase (id);
}
bool PresetLibrary::isFavorite (const juce::String& id) const { return favorites.count (id) > 0; }

void PresetLibrary::addToCollection (const juce::String& c, const juce::String& id)
{
    auto& v = collections[c];
    if (std::find (v.begin(), v.end(), id) == v.end()) v.push_back (id);
}
void PresetLibrary::removeFromCollection (const juce::String& c, const juce::String& id)
{
    auto it = collections.find (c);
    if (it == collections.end()) return;
    auto& v = it->second;
    v.erase (std::remove (v.begin(), v.end(), id), v.end());
}
juce::StringArray PresetLibrary::collectionNames() const
{
    juce::StringArray n; for (const auto& kv : collections) n.add (kv.first); return n;
}
std::vector<const Preset*> PresetLibrary::collection (const juce::String& name) const
{
    std::vector<const Preset*> out;
    auto it = collections.find (name);
    if (it == collections.end()) return out;
    for (const auto& id : it->second) if (auto* p = byId (id)) out.push_back (p);
    return out;
}

// ============================================================================
//  Discovery views
// ============================================================================
static std::vector<const Preset*> topBy (const std::vector<Preset>& ps, int n,
                                         std::function<bool (const Preset&, const Preset&)> less)
{
    std::vector<const Preset*> out;
    for (const auto& p : ps) out.push_back (&p);
    std::stable_sort (out.begin(), out.end(), [&] (const Preset* a, const Preset* b) { return less (*a, *b); });
    if (n > 0 && (int) out.size() > n) out.resize ((size_t) n);
    return out;
}

std::vector<const Preset*> PresetLibrary::trending (int n) const
{
    return topBy (presets, n, [] (const Preset& a, const Preset& b) { return trendingScore (a.meta) > trendingScore (b.meta); });
}
std::vector<const Preset*> PresetLibrary::mostDownloaded (int n) const
{
    return topBy (presets, n, [] (const Preset& a, const Preset& b) { return a.meta.downloads > b.meta.downloads; });
}
std::vector<const Preset*> PresetLibrary::highestRated (int n) const
{
    return topBy (presets, n, [] (const Preset& a, const Preset& b) { return a.meta.rating > b.meta.rating; });
}
std::vector<const Preset*> PresetLibrary::newArrivals (int n) const
{
    return topBy (presets, n, [] (const Preset& a, const Preset& b) { return a.meta.createdUnix > b.meta.createdUnix; });
}
std::vector<const Preset*> PresetLibrary::editorsPicks (int n) const
{
    std::vector<const Preset*> out;
    for (const auto& p : presets) if (p.meta.featured) out.push_back (&p);
    if (n > 0 && (int) out.size() > n) out.resize ((size_t) n);
    return out;
}
std::vector<const Preset*> PresetLibrary::recommendedFor (const juce::String& genre, int n) const
{
    std::vector<const Preset*> out;
    for (const auto& p : presets) if (p.meta.genre.equalsIgnoreCase (genre)) out.push_back (&p);
    std::stable_sort (out.begin(), out.end(), [] (const Preset* a, const Preset* b) { return a->meta.rating > b->meta.rating; });
    if (n > 0 && (int) out.size() > n) out.resize ((size_t) n);
    return out;
}
} // namespace vf
