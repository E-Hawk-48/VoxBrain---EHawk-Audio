#include "PluginProcessor.h"
#include "Modules/ModuleRegistry.h"
#include "Preset/FactoryPresetLibrary.h"
#include "Reference/ReferencePreset.h"
#include "PluginEditor.h"
#include "Chat/ChatEngine.h"
#include <cmath>    // std::isfinite for the output safety net

namespace vf
{
using namespace vf::param;

VoxBrainProcessor::VoxBrainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", vf::param::createParameterLayout())
{
    // Cache raw parameter pointers for lock-free audio-thread access
    for (const char* id : { inputGain, outputGain,
                            pitchOn, pitchKey, pitchScale, pitchSpeed, pitchAmount,
                            pitchHumanize, pitchFormant, pitchLatency,
                            gateOn, gateThreshold,
                            eqOn, eqHpfFreq, eqLowShelfGain, eqMudGain, eqMudFreq,
                            eqPresenceGain, eqPresenceFreq, eqAirGain,
                            dyneqOn, dyneqLowThresh, dyneqLowFreq, dyneqMidThresh,
                            dyneqMidFreq, dyneqHighThresh, dyneqHighFreq, dyneqRange,
                            deessOn, deessThreshold, deessFreq,
                            compOn, compThreshold, compRatio, compAttack,
                            compRelease, compMakeup, compMix,
                            mbandOn, mbandLowThresh, mbandMidThresh, mbandHighThresh,
                            mbandLowGain, mbandMidGain, mbandHighGain, mbandRatio,
                            mbandLowXover, mbandHighXover,
                            satOn, satType, satDrive, satTone, satBias, satMix, satHQ,
                            delayOn, delayTime, delayFeedback, delayMix,
                            verbOn, verbType, verbSize, verbDecay, verbPredelay, verbDamp,
                            verbDiffusion, verbLowCut, verbHighCut, verbModDepth, verbWidth,
                            verbMix, verbDuck, verbShimmer, verbFreeze,
                            limitOn, limitCeiling, limitGain })
    {
        raw[juce::String (id)] = apvts.getRawParameterValue (id);
    }

    // Modular rack: register the module library + cache its automation pool.
    mods::registerBuiltInModules();
    rackOnPtr = apvts.getRawParameterValue ("rack_on");
    for (int s = 0; s < mods::ModuleRack::Automation::Slots; ++s)
        for (int m = 0; m < mods::ModuleRack::Automation::Macros; ++m)
            rackMacroPtr[s][m] = apvts.getRawParameterValue (
                "rack_s" + juce::String (s) + "_m" + juce::String (m));

    // Load the built-in factory preset library into the ecosystem index.
    presetLibrary.addAll (FactoryPresetLibrary::build());

    learn16k.resize ((size_t) learn16kCapacity, 0.0f);   // preallocated: no RT alloc
    startTimerHz (10);   // message-thread poll for finished learn passes

    updater.startCheck();   // background, throttled; no-op until the update URL is set
}

mods::ModuleRack::Automation VoxBrainProcessor::readRackAutomation() const
{
    mods::ModuleRack::Automation a;
    a.rackOn = rackOnPtr == nullptr || rackOnPtr->load() > 0.5f;
    for (int s = 0; s < mods::ModuleRack::Automation::Slots; ++s)
        for (int m = 0; m < mods::ModuleRack::Automation::Macros; ++m)
            if (rackMacroPtr[s][m] != nullptr)
                a.macro[s][m] = rackMacroPtr[s][m]->load();
    return a;
}

VoxBrainProcessor::~VoxBrainProcessor()
{
    *aliveFlag = false;               // invalidate pending async callbacks
    aiPool.removeAllJobs (true, 4000);
}

std::atomic<float>* VoxBrainProcessor::rawParam (const char* id) const
{
    auto it = raw.find (juce::String (id));
    jassert (it != raw.end());
    return it->second;
}

bool VoxBrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void VoxBrainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) std::max (1, getTotalNumOutputChannels()) };
    preparedBlockSize = std::max (1, samplesPerBlock);
    chain.prepare (spec);
    rack.prepare (spec);
    analysis.prepare (sampleRate, (int) spec.numChannels);
    // Apply the saved retune latency mode up front so the reported latency is
    // correct from the first block (avoids a host re-sync right after load).
    if (auto* lm = apvts.getRawParameterValue (pitchLatency))
        chain.getRetune().setLatencyMode ((int) lm->load());
    setLatencySamples (chain.getLatencySamples() + rack.latencySamples());
    resampleRatio = sampleRate / 16000.0;
    learnResampler.reset();
}

ChainParams VoxBrainProcessor::readChainParams() const
{
    auto v = [this] (const char* id) { return rawParam (id)->load (std::memory_order_relaxed); };
    auto b = [&v]   (const char* id) { return v (id) > 0.5f; };

    ChainParams p;
    p.inputGainDb  = v (inputGain);
    p.outputGainDb = v (outputGain);

    // Pitch: resolve "Auto" key/scale from the last LEARN pass
    {
        RetuneParams rp;
        rp.on       = b (pitchOn);
        rp.speedMs  = v (pitchSpeed);
        rp.amount   = v (pitchAmount) * 0.01f;

        const int keyChoice   = (int) v (pitchKey);     // 0=Auto, 1=C … 12=B
        const int scaleChoice = (int) v (pitchScale);   // 0=Auto,1=Chrom,2=Maj,3=Min,…10=Blues

        const int  root    = keyChoice == 0 ? autoKeyRoot.load() : keyChoice - 1;
        const bool major   = (scaleChoice == 2) ? true
                           : (scaleChoice == 3) ? false
                           : autoKeyMajor.load();
        const bool haveKey = root >= 0;

        // Map the Scale menu onto the engine's scaleType (0=Chromatic … 9=Blues).
        int scaleType;
        if      (scaleChoice == 0) scaleType = ! haveKey ? 0 : (major ? 1 : 2); // Auto
        else if (scaleChoice == 1) scaleType = 0;                              // Chromatic
        else                       scaleType = scaleChoice - 1;               // Major…Blues

        rp.scaleType  = scaleType;
        rp.chromatic  = scaleType == 0;
        rp.keyRoot    = haveKey ? root : -1;
        rp.majorScale = major;
        rp.humanize   = v (pitchHumanize) * 0.01f;
        rp.formant    = v (pitchFormant);
        rp.latencyMode = (int) v (pitchLatency);   // 0=Live,1=Balanced,2=Studio
        p.retune = rp;
    }

    p.gate  = { b (gateOn), v (gateThreshold) };
    p.eq    = { b (eqOn), v (eqHpfFreq), v (eqLowShelfGain), v (eqMudGain), v (eqMudFreq),
                v (eqPresenceGain), v (eqPresenceFreq), v (eqAirGain) };
    p.dyneq = { b (dyneqOn), v (dyneqLowThresh), v (dyneqLowFreq), v (dyneqMidThresh),
                v (dyneqMidFreq), v (dyneqHighThresh), v (dyneqHighFreq), v (dyneqRange) };
    p.deess = { b (deessOn), v (deessThreshold), v (deessFreq) };
    p.comp  = { b (compOn), v (compThreshold), v (compRatio), v (compAttack),
                v (compRelease), v (compMakeup), v (compMix) * 0.01f };
    p.mband = { b (mbandOn), v (mbandLowThresh), v (mbandMidThresh), v (mbandHighThresh),
                v (mbandLowGain), v (mbandMidGain), v (mbandHighGain), v (mbandRatio),
                v (mbandLowXover), v (mbandHighXover) };
    p.sat   = { b (satOn), (int) v (satType), v (satDrive) * 0.01f, v (satTone) * 0.01f,
                v (satBias) * 0.01f, v (satMix) * 0.01f, b (satHQ) };
    p.delay = { b (delayOn), v (delayTime), v (delayFeedback) * 0.01f, v (delayMix) * 0.01f };
    p.verb  = { b (verbOn), (int) v (verbType), v (verbSize) * 0.01f, v (verbDecay) * 0.01f,
                v (verbPredelay), v (verbDamp) * 0.01f, v (verbDiffusion) * 0.01f,
                v (verbLowCut), v (verbHighCut), v (verbModDepth) * 0.01f, v (verbWidth) * 0.01f,
                v (verbMix) * 0.01f, v (verbDuck) * 0.01f, v (verbShimmer) * 0.01f, b (verbFreeze) };
    p.limit = { b (limitOn), v (limitCeiling), v (limitGain) };
    return p;
}

void VoxBrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    // Analyse the raw input (pre-chain) — this is what LEARN measures
    analysis.process (buffer);

    // While learning, also record a 16 kHz mono copy for neural analysis
    if (analysis.isLearning() && buffer.getNumChannels() > 0)
    {
        const float* left  = buffer.getReadPointer (0);
        const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;
        const int numSamples = buffer.getNumSamples();

        float monoChunk[2048];
        int done = 0;
        while (done < numSamples)
        {
            const int chunk = std::min (2048, numSamples - done);
            for (int i = 0; i < chunk; ++i)
                monoChunk[i] = right != nullptr ? 0.5f * (left[done + i] + right[done + i])
                                                : left[done + i];

            const int outWanted = std::max (1, (int) ((double) chunk / resampleRatio) - 1);
            const int lenNow = learn16kLen.load (std::memory_order_relaxed);
            if (lenNow + outWanted < learn16kCapacity)
            {
                learnResampler.process (resampleRatio, monoChunk,
                                        learn16k.data() + lenNow, outWanted);
                learn16kLen.store (lenNow + outWanted, std::memory_order_relaxed);
            }
            done += chunk;
        }
    }

    // Finalise a learn pass on the audio thread if the GUI requested a stop
    if (learnStopRequested.exchange (false))
    {
        pendingSnapshot = analysis.finishLearning();
        snapshotFresh.store (true, std::memory_order_release);
    }

    // Process the fixed chain + rack in slices no larger than the block size we
    // prepared our internal buffers for. Some hosts (and certain FL Studio modes)
    // hand a plugin a bigger buffer than it announced in prepareToPlay; without
    // this guard the oversampled saturator and split-band buffers overrun, which
    // is heard as constant glitching regardless of settings. When the host behaves
    // (buffer <= prepared) this is a single pass with zero added overhead.
    const auto chainP  = readChainParams();
    const auto rackAuto = readRackAutomation();
    const int  total = buffer.getNumSamples();
    const int  nch   = buffer.getNumChannels();
    const int  maxB  = std::max (1, preparedBlockSize);
    for (int off = 0; off < total; off += maxB)
    {
        const int len = std::min (maxB, total - off);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), nch, off, len);
        chain.process (slice, chainP);
        rack.process  (slice, &rackAuto);
    }

    // Output safety net: if any DSP stage ever produces a NaN/Inf (bad filter
    // coefficient, runaway feedback, etc.) it would otherwise propagate and be
    // heard as continuous nasty glitching. Replace non-finite samples with
    // silence so one bad value can't poison the stream. Cheap; RT-safe.
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0, ns = buffer.getNumSamples(); i < ns; ++i)
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
    }
}

// ============================================================================
//  LEARN workflow
// ============================================================================
void VoxBrainProcessor::setLearning (bool shouldLearn)
{
    if (shouldLearn)
    {
        learn16kLen.store (0);
        learnResampler.reset();
        analysis.startLearning();
    }
    else if (analysis.isLearning())
        learnStopRequested.store (true);
}

void VoxBrainProcessor::timerCallback()
{
    // Keep the host's latency compensation in sync when the retune latency mode
    // (or the rack's latency) changes at runtime. Cheap comparison at 10 Hz;
    // setLatencySamples is a no-op when the value is unchanged.
    {
        const int want = chain.getLatencySamples() + rack.latencySamples();
        if (want != getLatencySamples())
            setLatencySamples (want);
    }

    if (! snapshotFresh.exchange (false))
        return;

    // Lazy-create the neural analyzer (loads the ONNX model if present)
    if (crepe == nullptr)
        crepe = std::make_unique<CrepeAnalyzer>();

    const int recorded = learn16kLen.load();

    if (crepe->isAvailable() && recorded > 16000 && pendingSnapshot.valid)
    {
        // Neural path: refine pitch/key stats on a background thread
        auto snapshot = pendingSnapshot;
        auto audio = std::make_shared<std::vector<float>> (
            learn16k.begin(), learn16k.begin() + recorded);

        aiPool.addJob ([this, snapshot, audio]() mutable
        {
            const auto frames = crepe->analyze (audio->data(), (int) audio->size());
            refineSnapshotWithPitchFrames (snapshot, frames);

            const juce::String note = "Analysis engine: CREPE neural pitch ("
                                    + juce::String ((int) frames.size()) + " frames)";
            juce::MessageManager::callAsync ([this, alive = aliveFlag, snapshot, note]
            {
                if (*alive)
                    finishAutoMix (snapshot, note);
            });
        });
    }
    else
    {
        finishAutoMix (pendingSnapshot,
                       crepe->isAvailable() ? "Analysis engine: DSP (recording too short)"
                                            : "Analysis engine: DSP (" + crepe->getStatus() + ")");
    }
}

void VoxBrainProcessor::finishAutoMix (const AnalysisSnapshot& snapshot,
                                         const juce::String& engineNote)
{
    if (snapshot.keyRoot >= 0)
    {
        autoKeyRoot.store (snapshot.keyRoot);
        autoKeyMajor.store (snapshot.keyIsMajor);
    }

    lastSnapshot = snapshot;           // keep for the module advisor (rack UI)
    analysis.setPitchDnaFromSnapshot (snapshot.dna);   // seed live-radar slow axes
    lastResult = AutoMixBrain::computeChain (snapshot);
    lastResult.summary << "\n" << engineNote << "\n";

    presets.pushUndo ("Auto-Mix");     // so a single Undo reverts the whole pass
    applyBrainResult (lastResult);
    autoBuildRackFromAnalysis();       // LEARN also builds a complementary rack

    // Note any locked modules the AI deliberately left alone (transparency).
    struct LM { const char* id; const char* name; };
    static const LM lm[] = {
        { pitchLock, "Pitch" }, { gateLock, "Gate" }, { eqLock, "EQ" },
        { dyneqLock, "Dynamic EQ" }, { deessLock, "De-Esser" }, { compLock, "Compressor" },
        { mbandLock, "Multiband" }, { satLock, "Saturation" }, { delayLock, "Delay" },
        { verbLock, "Reverb" }, { limitLock, "Limiter" } };
    juce::StringArray lockedNames;
    for (const auto& m : lm)
        if (auto* v = apvts.getRawParameterValue (m.id); v != nullptr && v->load() > 0.5f)
            lockedNames.add (m.name);
    if (! lockedNames.isEmpty())
        lastResult.summary << "\nLocked — left untouched: " << lockedNames.joinIntoString (", ") << "\n";

    if (onAutoMixApplied)
        onAutoMixApplied();
}

juce::String VoxBrainProcessor::applyChatMessage (const juce::String& message)
{
    presets.pushUndo ("Chat: " + message);
    return ChatEngine::handleMessage (message, apvts);
}

std::vector<mods::ModuleSuggestion> VoxBrainProcessor::suggestModules() const
{
    return mods::ModuleAdvisor::suggest (lastSnapshot);
}

void VoxBrainProcessor::syncRackMacros()
{
    const auto snap = rack.snapshot();
    const int n = juce::jmin ((int) snap.size(), mods::ModuleRack::Automation::Slots);
    for (int slot = 0; slot < n; ++slot)
    {
        auto* m = rack.find (snap[(size_t) slot].instanceId);
        if (m == nullptr) continue;
        auto& ps = m->params();
        const int np = juce::jmin ((int) ps.size(), mods::ModuleRack::Automation::Macros);
        for (int k = 0; k < np; ++k)
        {
            const float mn = ps[(size_t) k].min, mx = ps[(size_t) k].max;
            const float norm = mx > mn ? juce::jlimit (0.0f, 1.0f, (ps[(size_t) k].value - mn) / (mx - mn)) : 0.0f;
            if (auto* p = apvts.getParameter ("rack_s" + juce::String (slot) + "_m" + juce::String (k)))
                p->setValueNotifyingHost (norm);
        }
    }
}

void VoxBrainProcessor::autoBuildRackFromAnalysis()
{
    if (rack.size() > 0) return;              // never clobber a user-built rack
    const auto& s = lastSnapshot;
    if (! s.valid) return;

    auto addWith = [this] (const juce::String& id, std::vector<std::pair<const char*, float>> vals)
    {
        const auto iid = rack.addModule (id);
        if (iid.isEmpty()) return;
        if (auto* m = rack.find (iid))
            for (const auto& [k, v] : vals) m->setValue (k, v);
    };

    // Complementary polish that the fixed chain does NOT do (so nothing doubles):
    if (s.crestDb > 8.0f && s.crestDb < 18.0f)
        addWith ("transient_designer", { { "attack", 35.0f }, { "sustain", 0.0f } });   // punch
    if (s.brightness < 0.55f)
        addWith ("tape_sat", { { "drive", 22.0f }, { "tone", 55.0f }, { "mix", 30.0f } }); // warmth
    if (s.brightness < 0.42f)
        addWith ("exciter", { { "freq", 4000.0f }, { "amount", 45.0f }, { "mix", 28.0f } }); // air
    if (s.voicedRatio > 0.5f && s.pitchStabilityCents < 60.0f && s.transientDensity < 4.0f)
        addWith ("stereo_chorus", { { "rate", 0.5f }, { "depth", 40.0f }, { "mix", 26.0f }, { "width", 80.0f } }); // width

    syncRackMacros();
}

Preset VoxBrainProcessor::captureCurrentAsPreset()
{
    const auto rackX = rack.toXml();                 // include routing if any
    return Preset::captureFrom (apvts, rackX.get());
}

void VoxBrainProcessor::applyPreset (const Preset& p)
{
    presets.pushUndo ("Preset: " + p.meta.name);     // revertible with one Undo
    juce::XmlElement* rackXml = nullptr;
    p.applyTo (apvts, &rackXml);                      // params (+ optional rack)
    if (rackXml != nullptr)
    {
        rack.fromXml (rackXml);
        delete rackXml;
    }
}

// ============================================================================
//  Reference apply / A-B compare (phase 4). All message-thread.
// ============================================================================
bool VoxBrainProcessor::rackHasType (const juce::String& typeId) const
{
    for (const auto& n : rack.snapshot()) if (n.typeId == typeId) return true;
    return false;
}

VoxBrainProcessor::RefState VoxBrainProcessor::captureRefState() const
{
    RefState s;
    s.params = presets.capture();
    if (auto x = rack.toXml()) s.rackXml = x->toString();
    s.valid = true;
    return s;
}

void VoxBrainProcessor::restoreRefState (const RefState& s)
{
    if (! s.valid) return;
    presets.restore (s.params);
    if (s.rackXml.isNotEmpty())
        if (auto x = juce::parseXML (s.rackXml))
            rack.fromXml (x.get());
}

void VoxBrainProcessor::captureRefOriginalIfNeeded()
{
    if (! haveRefOriginal)
    {
        refOriginal = captureRefState();
        haveRefOriginal = true;
        refShowingOriginal = false;
    }
}

void VoxBrainProcessor::writeReferencePlan (const std::vector<refapply::Write>& plan)
{
    for (const auto& w : plan)
        if (auto* p = apvts.getParameter (w.parameterId))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (w.value));
            p->endChangeGesture();
        }
}

void VoxBrainProcessor::addRackInsertInternal (const ReferenceMatchBrain::RackInsertion& ins)
{
    if (rackHasType (ins.moduleTypeId)) return;   // never duplicate
    rack.addModule (ins.moduleTypeId);            // added at its defaults
}

void VoxBrainProcessor::applyReferenceDecision (const ReferenceMatchBrain::Decision& d)
{
    captureRefOriginalIfNeeded();
    presets.pushUndo ("Reference: " + d.area);
    const refapply::LockPredicate lk = [this] (const juce::String& id) { return isModuleLocked (id); };
    writeReferencePlan (refapply::planDecision (d, lk));
    refApplied = captureRefState();
    refShowingOriginal = false;
    if (presets.onStateChanged) presets.onStateChanged();
}

void VoxBrainProcessor::applyReferenceMatch (const ReferenceMatchBrain::Result& m)
{
    captureRefOriginalIfNeeded();
    presets.pushUndo ("Reference Match: " + m.presetName);
    const refapply::LockPredicate lk = [this] (const juce::String& id) { return isModuleLocked (id); };
    writeReferencePlan (refapply::planAll (m, lk));
    for (const auto& ins : m.rackInserts) addRackInsertInternal (ins);
    syncRackMacros();
    refApplied = captureRefState();
    refShowingOriginal = false;
    if (presets.onStateChanged) presets.onStateChanged();
}

void VoxBrainProcessor::applyReferenceRackInsert (const ReferenceMatchBrain::RackInsertion& ins)
{
    captureRefOriginalIfNeeded();
    presets.pushUndo ("Reference: add " + ins.name);
    addRackInsertInternal (ins);
    syncRackMacros();
    refApplied = captureRefState();
    refShowingOriginal = false;
    if (presets.onStateChanged) presets.onStateChanged();
}

void VoxBrainProcessor::toggleReferenceCompare()
{
    if (! haveRefOriginal || ! refApplied.valid) return;
    if (refShowingOriginal) { restoreRefState (refApplied);  refShowingOriginal = false; }
    else                    { restoreRefState (refOriginal); refShowingOriginal = true;  }
    if (presets.onStateChanged) presets.onStateChanged();
}

void VoxBrainProcessor::resetReferenceCompare() noexcept
{
    haveRefOriginal = false;
    refApplied.valid = false;
    refShowingOriginal = false;
}

// ---- Reference → preset / community share (phase 5) ------------------------
Preset VoxBrainProcessor::buildReferencePreset (const ReferenceResult& r) const
{
    Preset p;
    p.meta = refpreset::deriveMeta (r.profile, r.match, r.fileName);
    p.meta.presetId = juce::Uuid().toString();

    // Values = the AI's SUGGESTED chain (independent of what's applied right now).
    for (const auto& d : r.match.decisions)
        for (const auto& t : d.targets)
            p.values[t.parameterId] = t.value;

    // Rack = the suggested complementary modules (deduped), serialised to XML.
    if (! r.match.rackInserts.empty())
    {
        mods::ModuleRack tmp;
        for (const auto& ins : r.match.rackInserts)
        {
            bool has = false;
            for (const auto& n : tmp.snapshot()) if (n.typeId == ins.moduleTypeId) has = true;
            if (! has) tmp.addModule (ins.moduleTypeId);
        }
        if (auto x = tmp.toXml()) p.rackXml = std::move (x);
    }

    p.seal();   // content hash + timestamps (required by the marketplace)
    return p;
}

Preset VoxBrainProcessor::saveReferenceAsPreset (const ReferenceResult& r)
{
    Preset p = buildReferencePreset (r);
    presetLibrary.add (p);                                  // appears in the Preset Browser

    auto dir = presets.userPresetDir();
    dir.createDirectory();
    const auto file = dir.getChildFile (juce::File::createLegalFileName (p.meta.name) + ".vbpreset");
    p.save (file);                                          // persists across sessions
    return p;
}

bool VoxBrainProcessor::shareReferenceToCommunity (const ReferenceResult& r, juce::String& errorOut)
{
    const Preset p = buildReferencePreset (r);
    return marketplace.uploadPreset (p, errorOut);          // validates + adds as "community"
}

bool VoxBrainProcessor::isModuleLocked (const juce::String& paramId) const
{
    if (const char* lock = vf::param::lockParamFor (paramId))
        if (auto* lv = apvts.getRawParameterValue (lock))
            return lv->load() > 0.5f;
    return false;
}

void VoxBrainProcessor::applyBrainResult (const AutoMixBrain::Result& r)
{
    for (const auto& d : r.decisions)
    {
        if (isModuleLocked (d.parameterId))
            continue;   // locked module — AI leaves every parameter untouched

        if (auto* param = apvts.getParameter (d.parameterId))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (d.value));
            param->endChangeGesture();
        }
    }
}

// ============================================================================
//  State
// ============================================================================
void VoxBrainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        // Save the modular rack (which modules, order, params) alongside the
        // APVTS params so routing persists in sessions AND presets. The rack
        // lives as a "Rack" child of the state tree; old builds simply ignore it.
        if (auto rackXml = rack.toXml())
            xml->addChildElement (rackXml.release());   // xml adopts ownership
        copyXmlToBinary (*xml, destData);
    }
}

void VoxBrainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            // Restore the rack first, then strip its child so it never pollutes
            // the APVTS value tree (which would duplicate it on the next save).
            if (auto* rackXml = xml->getChildByName ("Rack"))
            {
                rack.fromXml (rackXml);
                xml->removeChildElement (rackXml, true);
            }
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        }
}

juce::AudioProcessorEditor* VoxBrainProcessor::createEditor()
{
    return new VoxBrainEditor (*this);
}
} // namespace vf

// ============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vf::VoxBrainProcessor();
}
