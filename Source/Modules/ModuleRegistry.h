#pragma once
#include "Module.h"
#include <functional>
#include <map>
#include <memory>

namespace vf::mods
{
// ============================================================================
//  ModuleRegistry — the "plug-ins inside the plugin" catalog.
//
//  Concrete modules register a Descriptor + a factory. The registry powers:
//    * instantiation by stable id (ModuleRack::addModule)
//    * the full browsable catalog (100+ entries; implemented + roadmap)
//    * Smart Search: type "warm" / "reverb" / "pitch" and get ranked matches
//      across name, tags, category, and description.
//
//  New modules (built-in, premium, or community/marketplace) just call
//  registerType — nothing else in the codebase needs to change.
// ============================================================================
class ModuleRegistry
{
public:
    using Factory = std::function<std::unique_ptr<Module>()>;

    static ModuleRegistry& instance();

    /** Register a creatable module type (implemented=true is set automatically). */
    void registerType (Descriptor desc, Factory factory);

    /** Register a catalog/roadmap entry with no factory yet (searchable only). */
    void registerPlanned (Descriptor desc);

    /** Create a live instance, or nullptr if the id is unknown / not implemented. */
    std::unique_ptr<Module> create (juce::StringRef id) const;

    bool isImplemented (juce::StringRef id) const;

    const std::vector<Descriptor>& catalog() const { return descriptors; }
    std::vector<Descriptor> inCategory (Category c) const;

    /** Smart Search — returns matching descriptors ranked best-first. Empty
        query returns the whole catalog (implemented first). */
    std::vector<Descriptor> search (const juce::String& query) const;

    /** Score a single descriptor against a lowercased query (0 = no match). */
    static int scoreMatch (const Descriptor& d, const juce::String& lcQuery);

private:
    ModuleRegistry() = default;
    std::vector<Descriptor> descriptors;
    std::map<juce::String, Factory> factories;
};

/** Registers every built-in module + the full catalog. Called once at startup
    (idempotent). Defined in ModuleCatalog.cpp. */
void registerBuiltInModules();

} // namespace vf::mods
