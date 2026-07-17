#pragma once
#include "PresetMeta.h"
#include <vector>
#include <deque>
#include <map>
#include <set>

namespace vf
{
// ============================================================================
//  PresetLibrary — fast in-memory index over every preset (factory, user, AI,
//  community). Powers search, filtering, sorting, discovery, favorites and
//  collections. Offline-first: it never needs the network. All operations run
//  on the message thread and are O(n) over an already-loaded index.
// ============================================================================
class PresetLibrary
{
public:
    enum class Source { All, Factory, User, AI, Community };
    enum class Sort   { Relevance, NameAsc, NameDesc, Newest, Oldest,
                        MostDownloaded, HighestRated, Trending };

    struct Query
    {
        juce::String text;          // fuzzy across name/creator/genre/tags/mood
        juce::String genre;         // exact (empty = any)
        juce::String mood;          // exact (empty = any)
        juce::String creatorId;     // exact (empty = any)
        Source source        = Source::All;
        float  minRating     = 0.0f;
        float  maxCpu        = 0.0f;   // 0 = no cap
        bool   aiOnly        = false;
        bool   manualOnly    = false;
        bool   verifiedOnly  = false;
        bool   officialOnly  = false;
        bool   favoritesOnly = false;
        Sort   sort          = Sort::Relevance;
        int    limit         = 0;      // 0 = unlimited
    };

    // ---- population ------------------------------------------------------
    void clear();
    void add (Preset p);
    void addAll (std::vector<Preset> ps);
    int  size() const { return (int) presets.size(); }
    const Preset* byId (const juce::String& id) const;

    // ---- query -----------------------------------------------------------
    std::vector<const Preset*> query (const Query& q) const;
    static int fuzzyScore (const Preset& p, const juce::String& lcQuery);

    // ---- favorites & collections (by presetId) ---------------------------
    void setFavorite (const juce::String& id, bool on);
    bool isFavorite (const juce::String& id) const;
    void addToCollection (const juce::String& collection, const juce::String& id);
    void removeFromCollection (const juce::String& collection, const juce::String& id);
    juce::StringArray collectionNames() const;
    std::vector<const Preset*> collection (const juce::String& name) const;

    // ---- discovery views -------------------------------------------------
    std::vector<const Preset*> trending (int n) const;
    std::vector<const Preset*> mostDownloaded (int n) const;
    std::vector<const Preset*> highestRated (int n) const;
    std::vector<const Preset*> newArrivals (int n) const;
    std::vector<const Preset*> editorsPicks (int n) const;      // featured
    /** Simple content-based recommendation: same genre, best-rated first. */
    std::vector<const Preset*> recommendedFor (const juce::String& genre, int n) const;

    static float trendingScore (const PresetMeta& m);

private:
    // deque (NOT vector): add() must never invalidate the const Preset* pointers
    // that the browser holds — a reallocation would dangle them and crash on paint.
    std::deque<Preset> presets;
    std::map<juce::String, int> idToIndex;
    std::set<juce::String> favorites;
    std::map<juce::String, std::vector<juce::String>> collections;
};
} // namespace vf
