#include "ModuleRack.h"
#include "ModuleRegistry.h"

namespace vf::mods
{
void ModuleRack::prepare (const juce::dsp::ProcessSpec& s)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    spec = s;
    prepared = true;
    for (auto& n : nodes)
        if (n->module) n->module->prepare (spec);
}

void ModuleRack::reset()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& n : nodes)
        if (n->module) n->module->reset();
}

void ModuleRack::process (juce::AudioBuffer<float>& buffer, const Automation* automation)
{
    // Non-blocking: if a structural edit holds the lock, skip this block.
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || nodes.empty())
        return;

    if (automation != nullptr && ! automation->rackOn)
        return;   // rack master-bypassed → clean passthrough

    // Map the host-automatable macro pool onto the modules (slot i → module i).
    if (automation != nullptr)
    {
        const int ns = juce::jmin ((int) nodes.size(), Automation::Slots);
        for (int i = 0; i < ns; ++i)
        {
            if (nodes[(size_t) i]->module == nullptr) continue;
            auto& ps = nodes[(size_t) i]->module->params();
            const int np = juce::jmin ((int) ps.size(), Automation::Macros);
            for (int m = 0; m < np; ++m)
                ps[(size_t) m].value = ps[(size_t) m].min
                    + (ps[(size_t) m].max - ps[(size_t) m].min) * automation->macro[i][m];
        }
    }

    bool anySolo = false;
    for (auto& n : nodes) if (n->solo.load()) { anySolo = true; break; }

    const double blockMs = (buffer.getNumSamples() / spec.sampleRate) * 1000.0;

    for (auto& n : nodes)
    {
        if (n->module == nullptr) continue;
        const bool active = anySolo ? n->solo.load() : ! n->bypass.load();
        if (! active) continue;                 // pass audio through untouched

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        n->module->process (buffer);
        const auto used = juce::Time::getMillisecondCounterHiRes() - t0;

        const float pct = blockMs > 0.0 ? (float) (used / blockMs) * 100.0f : 0.0f;
        n->cpu.store (n->cpu.load() * 0.9f + pct * 0.1f);   // smoothed
    }
}

int ModuleRack::latencySamples() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    int total = 0;
    for (auto& n : nodes)
        if (n->module && ! n->bypass.load())
            total += n->module->latencySamples();
    return total;
}

// ---------------------------------------------------------------------------
ModuleRack::Node* ModuleRack::nodePtr (juce::StringRef instanceId)
{
    for (auto& n : nodes) if (n->instanceId == instanceId) return n.get();
    return nullptr;
}

int ModuleRack::indexOf (juce::StringRef instanceId) const
{
    for (int i = 0; i < (int) nodes.size(); ++i)
        if (nodes[(size_t) i]->instanceId == instanceId) return i;
    return -1;
}

int ModuleRack::size() const { return (int) nodes.size(); }

Module* ModuleRack::find (juce::StringRef instanceId)
{
    auto* n = nodePtr (instanceId);
    return n ? n->module.get() : nullptr;
}

juce::String ModuleRack::makeInstanceId (juce::StringRef typeId)
{
    return juce::String (typeId) + "_" + juce::String (++idCounter);
}

juce::String ModuleRack::addModule (juce::StringRef typeId, int index)
{
    auto module = ModuleRegistry::instance().create (typeId);
    if (module == nullptr)
        return {};

    auto node = std::make_unique<Node>();
    node->module = std::move (module);
    node->typeId = node->module->descriptor().id;
    node->instanceId = makeInstanceId (node->typeId);

    const juce::SpinLock::ScopedLockType sl (lock);
    if (prepared) node->module->prepare (spec);   // prepare BEFORE it can be processed
    const int at = (index < 0 || index > (int) nodes.size()) ? (int) nodes.size() : index;
    const auto id = node->instanceId;
    nodes.insert (nodes.begin() + at, std::move (node));
    return id;
}

bool ModuleRack::removeModule (juce::StringRef instanceId)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    const int i = indexOf (instanceId);
    if (i < 0) return false;
    nodes.erase (nodes.begin() + i);              // module destroyed on message thread
    return true;
}

juce::String ModuleRack::duplicateModule (juce::StringRef instanceId)
{
    // Read the source (type + param values) then create a fresh instance.
    juce::String typeId;
    std::vector<Param> srcParams;
    int at = -1;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto* src = nodePtr (instanceId);
        if (src == nullptr || src->module == nullptr) return {};
        typeId = src->typeId;
        srcParams = src->module->params();
        at = indexOf (instanceId) + 1;
    }

    const auto newId = addModule (typeId, at);
    if (newId.isEmpty()) return {};

    const juce::SpinLock::ScopedLockType sl (lock);
    if (auto* dst = nodePtr (newId))
        for (const auto& p : srcParams)
            dst->module->setValue (p.id, p.value);
    return newId;
}

bool ModuleRack::moveModule (juce::StringRef instanceId, int newIndex)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    const int from = indexOf (instanceId);
    if (from < 0) return false;
    const int to = juce::jlimit (0, (int) nodes.size() - 1, newIndex);
    if (from == to) return true;

    auto moved = std::move (nodes[(size_t) from]);
    nodes.erase (nodes.begin() + from);
    nodes.insert (nodes.begin() + to, std::move (moved));
    return true;
}

void ModuleRack::setBypass (juce::StringRef id, bool b) { if (auto* n = nodePtr (id)) n->bypass.store (b); }
void ModuleRack::setSolo   (juce::StringRef id, bool b) { if (auto* n = nodePtr (id)) n->solo.store (b); }
void ModuleRack::setLock   (juce::StringRef id, bool b) { if (auto* n = nodePtr (id)) n->lock.store (b); }

std::unique_ptr<juce::XmlElement> ModuleRack::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Rack");
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& n : nodes)
    {
        if (n->module == nullptr) continue;
        auto* e = xml->createNewChildElement ("Module");
        e->setAttribute ("type", n->typeId);
        e->setAttribute ("bypass", n->bypass.load());
        e->setAttribute ("solo",   n->solo.load());
        e->setAttribute ("lock",   n->lock.load());
        for (const auto& p : n->module->params())
        {
            auto* pe = e->createNewChildElement ("Param");
            pe->setAttribute ("id", p.id);
            pe->setAttribute ("v", (double) p.value);
        }
    }
    return xml;
}

void ModuleRack::fromXml (const juce::XmlElement* xml)
{
    if (xml == nullptr || ! xml->hasTagName ("Rack"))
        return;

    const juce::SpinLock::ScopedLockType sl (lock);
    nodes.clear();
    for (auto* e : xml->getChildWithTagNameIterator ("Module"))
    {
        auto module = ModuleRegistry::instance().create (e->getStringAttribute ("type"));
        if (module == nullptr) continue;              // unknown/unimplemented — skip

        auto node = std::make_unique<Node>();
        node->module = std::move (module);
        node->typeId = node->module->descriptor().id;
        node->instanceId = makeInstanceId (node->typeId);
        node->bypass.store (e->getBoolAttribute ("bypass"));
        node->solo.store   (e->getBoolAttribute ("solo"));
        node->lock.store   (e->getBoolAttribute ("lock"));
        if (prepared) node->module->prepare (spec);
        for (auto* pe : e->getChildWithTagNameIterator ("Param"))
            node->module->setValue (pe->getStringAttribute ("id"), (float) pe->getDoubleAttribute ("v"));
        nodes.push_back (std::move (node));
    }
}

std::vector<ModuleRack::NodeInfo> ModuleRack::snapshot() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    std::vector<NodeInfo> out;
    out.reserve (nodes.size());
    for (auto& n : nodes)
    {
        NodeInfo ni;
        ni.instanceId = n->instanceId;
        ni.typeId = n->typeId;
        if (n->module)
        {
            ni.name = n->module->descriptor().name;
            ni.category = n->module->descriptor().category;
            ni.latency = n->module->latencySamples();
        }
        ni.bypass = n->bypass.load();
        ni.solo = n->solo.load();
        ni.lock = n->lock.load();
        ni.cpu = n->cpu.load();
        out.push_back (std::move (ni));
    }
    return out;
}
} // namespace vf::mods
