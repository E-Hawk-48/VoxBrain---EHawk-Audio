#include "PresetMeta.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>

namespace vf
{
namespace
{
    juce::String joinArr (const juce::StringArray& a) { return a.joinIntoString (","); }
    juce::StringArray splitArr (const juce::String& s)
    {
        juce::StringArray a; a.addTokens (s, ",", ""); a.removeEmptyStrings(); a.trim(); return a;
    }
    juce::int64 nowUnix() { return juce::Time::currentTimeMillis() / 1000; }
}

// ============================================================================
//  PresetMeta serialization
// ============================================================================
void PresetMeta::writeInto (juce::XmlElement& e) const
{
    e.setAttribute ("presetId", presetId);
    e.setAttribute ("name", name);
    e.setAttribute ("creatorName", creatorName);
    e.setAttribute ("creatorId", creatorId);
    e.setAttribute ("version", version);
    e.setAttribute ("minAppVersion", minAppVersion);
    e.setAttribute ("createdUnix", juce::String (createdUnix));
    e.setAttribute ("updatedUnix", juce::String (updatedUnix));
    e.setAttribute ("description", description);
    e.setAttribute ("genre", genre);
    e.setAttribute ("vocalStyle", vocalStyle);
    e.setAttribute ("mood", mood);
    e.setAttribute ("energy", energy);
    e.setAttribute ("vocalType", vocalType);
    e.setAttribute ("vocalGender", vocalGender);
    e.setAttribute ("vocalRange", vocalRange);
    e.setAttribute ("keyCompat", keyCompat);
    e.setAttribute ("tempoCompat", tempoCompat);
    e.setAttribute ("recQualityRec", recQualityRec);
    e.setAttribute ("micRec", micRec);
    e.setAttribute ("cpuEstimate", cpuEstimate);
    e.setAttribute ("latencyMs", latencyMs);
    e.setAttribute ("tags", joinArr (tags));
    e.setAttribute ("categories", joinArr (categories));
    e.setAttribute ("aiGenerated", aiGenerated);
    e.setAttribute ("aiConfidence", aiConfidence);
    e.setAttribute ("aiSummary", aiSummary);
    e.setAttribute ("previewImage", previewImage);
    e.setAttribute ("demoAudio", demoAudio);
    e.setAttribute ("changelog", changelog);
    e.setAttribute ("source", source);
    e.setAttribute ("official", official);
    e.setAttribute ("verifiedCreator", verifiedCreator);
    e.setAttribute ("downloads", downloads);
    e.setAttribute ("likes", likes);
    e.setAttribute ("rating", rating);
    e.setAttribute ("ratingCount", ratingCount);
    e.setAttribute ("featured", featured);
    e.setAttribute ("contentHash", contentHash);
    e.setAttribute ("signature", signature);
}

PresetMeta PresetMeta::readFrom (const juce::XmlElement& e)
{
    PresetMeta m;
    m.presetId      = e.getStringAttribute ("presetId", m.presetId);
    m.name          = e.getStringAttribute ("name", m.name);
    m.creatorName   = e.getStringAttribute ("creatorName", m.creatorName);
    m.creatorId     = e.getStringAttribute ("creatorId");
    m.version       = e.getStringAttribute ("version", m.version);
    m.minAppVersion = e.getStringAttribute ("minAppVersion", m.minAppVersion);
    m.createdUnix   = e.getStringAttribute ("createdUnix").getLargeIntValue();
    m.updatedUnix   = e.getStringAttribute ("updatedUnix").getLargeIntValue();
    m.description   = e.getStringAttribute ("description");
    m.genre         = e.getStringAttribute ("genre");
    m.vocalStyle    = e.getStringAttribute ("vocalStyle");
    m.mood          = e.getStringAttribute ("mood");
    m.energy        = e.getIntAttribute ("energy", 5);
    m.vocalType     = e.getStringAttribute ("vocalType");
    m.vocalGender   = e.getStringAttribute ("vocalGender");
    m.vocalRange    = e.getStringAttribute ("vocalRange");
    m.keyCompat     = e.getStringAttribute ("keyCompat");
    m.tempoCompat   = e.getStringAttribute ("tempoCompat");
    m.recQualityRec = e.getStringAttribute ("recQualityRec");
    m.micRec        = e.getStringAttribute ("micRec");
    m.cpuEstimate   = (float) e.getDoubleAttribute ("cpuEstimate");
    m.latencyMs     = (float) e.getDoubleAttribute ("latencyMs");
    m.tags          = splitArr (e.getStringAttribute ("tags"));
    m.categories    = splitArr (e.getStringAttribute ("categories"));
    m.aiGenerated   = e.getBoolAttribute ("aiGenerated");
    m.aiConfidence  = (float) e.getDoubleAttribute ("aiConfidence");
    m.aiSummary     = e.getStringAttribute ("aiSummary");
    m.previewImage  = e.getStringAttribute ("previewImage");
    m.demoAudio     = e.getStringAttribute ("demoAudio");
    m.changelog     = e.getStringAttribute ("changelog");
    m.source        = e.getStringAttribute ("source", "user");
    m.official      = e.getBoolAttribute ("official");
    m.verifiedCreator = e.getBoolAttribute ("verifiedCreator");
    m.downloads     = e.getIntAttribute ("downloads");
    m.likes         = e.getIntAttribute ("likes");
    m.rating        = (float) e.getDoubleAttribute ("rating");
    m.ratingCount   = e.getIntAttribute ("ratingCount");
    m.featured      = e.getBoolAttribute ("featured");
    m.contentHash   = e.getStringAttribute ("contentHash");
    m.signature     = e.getStringAttribute ("signature");
    return m;
}

// ============================================================================
//  Preset — copy semantics (deep-clone rackXml)
// ============================================================================
Preset::Preset (const Preset& o) : meta (o.meta), values (o.values)
{
    if (o.rackXml != nullptr) rackXml = std::make_unique<juce::XmlElement> (*o.rackXml);
}
Preset& Preset::operator= (const Preset& o)
{
    if (this != &o)
    {
        meta = o.meta; values = o.values;
        rackXml = o.rackXml != nullptr ? std::make_unique<juce::XmlElement> (*o.rackXml) : nullptr;
    }
    return *this;
}

// ============================================================================
//  Serialization
// ============================================================================
std::unique_ptr<juce::XmlElement> Preset::toXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("VoxBrainPreset");
    root->setAttribute ("format", 2);

    auto* m = root->createNewChildElement ("Meta");
    meta.writeInto (*m);

    auto* vals = root->createNewChildElement ("Values");
    for (const auto& [id, v] : values)
    {
        auto* pe = vals->createNewChildElement ("P");
        pe->setAttribute ("id", id);
        pe->setAttribute ("v", (double) v);
    }

    if (rackXml != nullptr)
        root->addChildElement (new juce::XmlElement (*rackXml));

    return root;
}

Preset Preset::fromXml (const juce::XmlElement& e)
{
    Preset p;
    if (! e.hasTagName ("VoxBrainPreset"))
        return p;   // caller checks verifyIntegrity()/values.empty() for validity

    if (auto* m = e.getChildByName ("Meta"))
        p.meta = PresetMeta::readFrom (*m);

    if (auto* vals = e.getChildByName ("Values"))
        for (auto* pe : vals->getChildWithTagNameIterator ("P"))
        {
            const auto id = pe->getStringAttribute ("id");
            if (id.isNotEmpty())
                p.values[id] = (float) pe->getDoubleAttribute ("v");
        }

    if (auto* rk = e.getChildByName ("Rack"))
        p.rackXml = std::make_unique<juce::XmlElement> (*rk);

    return p;
}

bool Preset::save (const juce::File& file) const
{
    if (auto xml = toXml())
        return xml->writeTo (file, {});
    return false;
}

bool Preset::load (const juce::File& file, Preset& out)
{
    if (! file.existsAsFile()) return false;
    auto xml = juce::XmlDocument::parse (file);           // safe/tolerant parse
    if (xml == nullptr || ! xml->hasTagName ("VoxBrainPreset"))
        return false;                                     // not a v2 preset
    out = Preset::fromXml (*xml);
    if (out.values.empty()) return false;                 // corrupt / empty
    return out.verifyIntegrity();                         // integrity gate
}

// ============================================================================
//  Security / integrity
// ============================================================================
juce::String Preset::computeContentHash() const
{
    // Canonical form: sorted "id=value;" pairs + core identity. Deterministic.
    juce::StringArray keys;
    for (const auto& kv : values) keys.add (kv.first);
    keys.sort (true);

    juce::String canon;
    for (const auto& k : keys)
        canon << k << "=" << juce::String (values.at (k), 6) << ";";
    canon << "|" << meta.name << "|" << meta.creatorId << "|" << meta.version;
    if (rackXml != nullptr) canon << "|" << rackXml->toString (juce::XmlElement::TextFormat().singleLine());

    const auto utf8 = canon.toRawUTF8();
    juce::SHA256 hash (utf8, (size_t) canon.getNumBytesAsUTF8());
    return hash.toHexString();
}

void Preset::seal()
{
    if (meta.createdUnix == 0) meta.createdUnix = nowUnix();
    meta.updatedUnix = nowUnix();
    meta.contentHash = computeContentHash();
}

bool Preset::verifyIntegrity() const
{
    if (meta.contentHash.isEmpty()) return true;          // unsealed/legacy — allow
    return meta.contentHash == computeContentHash();
}

bool Preset::compatibleWith (const juce::String& appVersion) const
{
    return compareVersions (appVersion, meta.minAppVersion) >= 0;
}

int Preset::compareVersions (const juce::String& a, const juce::String& b)
{
    juce::StringArray pa, pb;
    pa.addTokens (a, ".", ""); pb.addTokens (b, ".", "");
    const int n = juce::jmax (pa.size(), pb.size());
    for (int i = 0; i < n; ++i)
    {
        const int va = i < pa.size() ? pa[i].getIntValue() : 0;
        const int vb = i < pb.size() ? pb[i].getIntValue() : 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}
} // namespace vf
