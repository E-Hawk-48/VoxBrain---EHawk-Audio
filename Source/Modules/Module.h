#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <atomic>

// ============================================================================
//  VoxBrain modular DSP framework
//  ---------------------------------------------------------------------------
//  This is the foundation that turns VoxBrain from a fixed vocal chain into a
//  modular rack that can host an unbounded library of DSP modules. Every module
//  is a `Module` subclass registered with the ModuleRegistry; the ModuleRack
//  holds a dynamic, reorderable list of live instances.
//
//  Design goals it satisfies:
//   * Independently add / remove / duplicate / reorder / bypass / solo / lock
//     (handled by ModuleRack).
//   * AI parameter + module recommendations (ModuleAdvisor + per-module aiAuto).
//   * Tooltips + docs (ModuleParam.tooltip, ModuleDescriptor.description).
//   * Accurate latency reporting (Module::latencySamples, summed by the rack).
//   * CPU metering per module (ModuleRack times each process call).
//   * Beginner vs advanced controls (ModuleParam.advanced).
//   * Mono / stereo / Mid-Side (ProcessMode) where applicable.
//   * Extensible like "plug-ins inside the plugin" (register a factory).
//
//  Real-time safety: process() runs on the audio thread and must not allocate,
//  lock, or log. All structural edits happen on the message thread.
// ============================================================================
namespace vf::mods
{
    enum class Category
    {
        Input, AIAnalysis, EQ, Filter, Dynamics, Saturation, Pitch, Modulation,
        Spatial, Delay, Restoration, Creative, Imaging, Analysis, Utility, Master
    };

    juce::String categoryName (Category c);

    /** How a module folds stereo — set by the user where the module supports it. */
    enum class ProcessMode { Stereo, Mono, MidSide };

    /** One automatable control a module exposes. `value` is the live value the
        DSP reads on the audio thread (plain float: single-word, no tearing on
        the platforms we target); the rest is metadata for the UI/host. */
    struct Param
    {
        juce::String id, name, unit;
        float min = 0.0f, max = 1.0f, def = 0.0f;
        float skew = 1.0f;             // 1 = linear; <1 skews toward min (e.g. Hz)
        bool  advanced = false;        // beginner control hidden unless "advanced"
        juce::String tooltip;
        float value = 0.0f;            // current value (natural units)
    };

    /** Static, searchable metadata about a module type. */
    struct Descriptor
    {
        juce::String id;               // stable type id, e.g. "gain" (never rename)
        juce::String name;             // display name, e.g. "Gain"
        Category     category = Category::Utility;
        juce::StringArray tags;        // for Smart Search: "warm","level",…
        juce::String description;      // one-line doc / tooltip
        bool implemented = false;      // false = catalog/roadmap entry (not creatable yet)
    };

    // ------------------------------------------------------------------------
    //  Module — base class for every processing block.
    // ------------------------------------------------------------------------
    class Module
    {
    public:
        virtual ~Module() = default;

        /** Static metadata for this concrete type. */
        virtual const Descriptor& descriptor() const = 0;

        virtual void prepare (const juce::dsp::ProcessSpec& spec)
        {
            fs = spec.sampleRate;
            maxBlock = (int) spec.maximumBlockSize;
            numCh = (int) spec.numChannels;
            reset();
        }

        virtual void reset() {}

        /** In-place processing on the audio thread. Real-time safe. */
        virtual void process (juce::AudioBuffer<float>& buffer) = 0;

        /** Extra latency this module introduces (oversampling, lookahead, …). */
        virtual int latencySamples() const { return 0; }

        /** Optional per-module visual data feed (spectrum, GR, meters). Modules
            that draw override this to publish to their editor; default: none. */
        virtual bool hasVisual() const { return false; }

        // ---- parameters --------------------------------------------------
        std::vector<Param>&       params()       { return parameters; }
        const std::vector<Param>& params() const { return parameters; }

        Param* param (juce::StringRef id)
        {
            for (auto& p : parameters) if (p.id == id) return &p;
            return nullptr;
        }
        float value (juce::StringRef id) const
        {
            for (auto& p : parameters) if (p.id == id) return p.value;
            return 0.0f;
        }
        void setValue (juce::StringRef id, float v)
        {
            if (auto* p = param (id)) p->value = juce::jlimit (p->min, p->max, v);
        }

        // ---- shared per-instance state (rack-controlled, audio-thread read) --
        ProcessMode mode = ProcessMode::Stereo;
        std::atomic<bool>  aiAuto { false };   // AI-managed vs manual

    protected:
        double fs = 44100.0;
        int maxBlock = 512, numCh = 2;
        std::vector<Param> parameters;

        Param& addParam (juce::String id, juce::String name, float mn, float mx,
                         float def, juce::String unit = {}, bool advanced = false,
                         float skew = 1.0f, juce::String tooltip = {})
        {
            parameters.push_back ({ std::move (id), std::move (name), std::move (unit),
                                    mn, mx, def, skew, advanced, std::move (tooltip), def });
            return parameters.back();
        }
    };
} // namespace vf::mods
