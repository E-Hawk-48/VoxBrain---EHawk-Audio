#pragma once
#include "PresetMeta.h"
#include "PresetLibrary.h"
#include <vector>
#include <memory>

namespace vf
{
// ============================================================================
//  Community Preset Marketplace — client-side abstraction.
//
//  The plugin only ever talks to a MarketplaceClient. Today the concrete
//  implementation is LocalMarketplace (100% offline, backed by a PresetLibrary),
//  so the whole browser/discovery/ratings experience works with zero network.
//  A future networked backend implements the SAME interface and drops in with
//  no changes to the UI or the rest of the app. The premium/licensing hooks are
//  present as seams (no payment processing yet).
// ============================================================================
struct CreatorProfile
{
    juce::String creatorId, displayName, bio, website, socialUrl, avatarUrl;
    int   totalDownloads = 0, totalLikes = 0, publishedCount = 0;
    int   followers = 0, following = 0;
    float averageRating = 0.0f;
    int   level = 1;
    bool  verified = false, featured = false;
    juce::StringArray recentUploadIds, popularIds, recentlyUpdatedIds;
};

struct RatingReview
{
    juce::String userId, userName, text;
    int          stars = 0;              // 1..5
    juce::int64  timeUnix = 0;
};

struct DiscoveryQuery
{
    enum class View
    {
        TrendingToday, TrendingWeek, TrendingMonth, MostDownloaded, HighestRated,
        EditorsPicks, FeaturedCreators, NewArrivals, RecentlyUpdated,
        AiRecommended, SimilarPresets, HiddenGems
    };
    View         view  = View::TrendingWeek;
    juce::String genre;                 // filter / seed (AiRecommended, SimilarPresets)
    juce::String seedPresetId;          // for SimilarPresets
    int          limit = 24;
};

// ---------------------------------------------------------------------------
//  Abstract client. A network backend subclasses this.
// ---------------------------------------------------------------------------
class MarketplaceClient
{
public:
    virtual ~MarketplaceClient() = default;

    virtual bool isOnline() const = 0;

    // discovery / browse (metadata only — cheap, lazy-loaded)
    virtual std::vector<PresetMeta> discover (const DiscoveryQuery& q) = 0;
    virtual std::vector<PresetMeta> search (const juce::String& text, int limit) = 0;

    // download a full preset by id (async in the network impl; sync here)
    virtual bool fetchPreset (const juce::String& presetId, Preset& out) = 0;

    // creators
    virtual bool fetchCreator (const juce::String& creatorId, CreatorProfile& out) = 0;
    virtual bool followCreator (const juce::String& creatorId, bool follow) = 0;

    // social
    virtual bool ratePreset (const juce::String& presetId, int stars, const juce::String& review) = 0;
    virtual std::vector<RatingReview> fetchReviews (const juce::String& presetId) = 0;
    virtual bool reportPreset (const juce::String& presetId, const juce::String& reason) = 0;

    // publishing — MUST validate + verify integrity before accepting
    virtual bool uploadPreset (const Preset& p, juce::String& errorOut) = 0;

    // premium seams (extensible; no payment processing yet)
    virtual bool isPremium (const juce::String& /*presetId*/) { return false; }
    virtual bool hasEntitlement (const juce::String& /*presetId*/) { return true; }
};

// ---------------------------------------------------------------------------
//  LocalMarketplace — offline-first implementation over a PresetLibrary. This
//  is what ships today; it also serves as the local cache/fallback when a
//  future networked client is offline.
// ---------------------------------------------------------------------------
class LocalMarketplace : public MarketplaceClient
{
public:
    explicit LocalMarketplace (PresetLibrary& lib) : library (lib) {}

    bool isOnline() const override { return false; }

    std::vector<PresetMeta> discover (const DiscoveryQuery& q) override
    {
        std::vector<const Preset*> hits;
        switch (q.view)
        {
            case DiscoveryQuery::View::MostDownloaded: hits = library.mostDownloaded (q.limit); break;
            case DiscoveryQuery::View::HighestRated:   hits = library.highestRated (q.limit); break;
            case DiscoveryQuery::View::NewArrivals:
            case DiscoveryQuery::View::RecentlyUpdated: hits = library.newArrivals (q.limit); break;
            case DiscoveryQuery::View::EditorsPicks:   hits = library.editorsPicks (q.limit); break;
            case DiscoveryQuery::View::AiRecommended:
            case DiscoveryQuery::View::SimilarPresets: hits = library.recommendedFor (q.genre, q.limit); break;
            default:                                    hits = library.trending (q.limit); break;
        }
        return metasOf (hits);
    }

    std::vector<PresetMeta> search (const juce::String& text, int limit) override
    {
        PresetLibrary::Query q; q.text = text; q.limit = limit; q.sort = PresetLibrary::Sort::Relevance;
        return metasOf (library.query (q));
    }

    bool fetchPreset (const juce::String& presetId, Preset& out) override
    {
        if (auto* p = library.byId (presetId)) { out = *p; return out.verifyIntegrity(); }
        return false;
    }

    bool fetchCreator (const juce::String& creatorId, CreatorProfile& out) override
    {
        out = CreatorProfile();
        out.creatorId = creatorId;
        PresetLibrary::Query q; q.creatorId = creatorId;
        const auto mine = library.query (q);
        out.publishedCount = (int) mine.size();
        for (auto* p : mine)
        {
            out.displayName = p->meta.creatorName;
            out.verified   |= p->meta.verifiedCreator;
            out.totalDownloads += p->meta.downloads;
            out.totalLikes     += p->meta.likes;
            out.averageRating  += p->meta.rating;
            out.recentUploadIds.add (p->meta.presetId);
        }
        if (out.publishedCount > 0) out.averageRating /= (float) out.publishedCount;
        out.level = juce::jlimit (1, 10, 1 + out.totalDownloads / 500 + out.publishedCount / 5);
        return out.publishedCount > 0;
    }

    bool followCreator (const juce::String&, bool) override { return true; }   // local: no-op success

    bool ratePreset (const juce::String& presetId, int stars, const juce::String& review) override
    {
        if (stars < 1 || stars > 5) return false;
        localReviews[presetId].push_back ({ "local", "You", review, stars,
                                            juce::Time::currentTimeMillis() / 1000 });
        return true;
    }
    std::vector<RatingReview> fetchReviews (const juce::String& presetId) override
    {
        auto it = localReviews.find (presetId);
        return it != localReviews.end() ? it->second : std::vector<RatingReview>{};
    }
    bool reportPreset (const juce::String&, const juce::String&) override { return true; }

    bool uploadPreset (const Preset& p, juce::String& errorOut) override
    {
        // Safe parsing + validation + integrity + duplicate detection before accept.
        if (p.values.empty())          { errorOut = "Preset has no parameters.";     return false; }
        if (! p.verifyIntegrity())     { errorOut = "Integrity check failed.";        return false; }
        if (p.meta.name.trim().isEmpty()) { errorOut = "Preset needs a name.";        return false; }
        const auto hash = p.computeContentHash();
        if (seenHashes.count (hash))   { errorOut = "Duplicate of an existing preset."; return false; }
        seenHashes.insert (hash);
        Preset staged = p;
        staged.meta.source = "community";
        library.add (std::move (staged));
        errorOut.clear();
        return true;
    }

private:
    static std::vector<PresetMeta> metasOf (const std::vector<const Preset*>& ps)
    {
        std::vector<PresetMeta> out; out.reserve (ps.size());
        for (auto* p : ps) out.push_back (p->meta);
        return out;
    }

    PresetLibrary& library;
    std::map<juce::String, std::vector<RatingReview>> localReviews;
    std::set<juce::String> seenHashes;
};
} // namespace vf
