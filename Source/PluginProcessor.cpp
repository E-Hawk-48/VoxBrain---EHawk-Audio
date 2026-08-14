#include "PluginProcessor.h"
#include "DSP/VoiceCharacter.h"
#include "Modules/ModuleRegistry.h"
#include "Preset/FactoryPresetLibrary.h"
#include "Reference/ReferencePreset.h"
#include "Support/CrashLog.h"
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
    vf::crashlog::install();   // field-crash diagnostics → <appData>/VoxBrain/crash.log

    // Cache raw parameter pointers for lock-free audio-thread access
    for (const char* id : { inputGain, outputGain,
                            pitchOn, pitchKey, pitchScale, pitchSpeed, pitchAmount,
                            pitchHumanize, pitchFormant, pitchLatency,
                            pitchFlex, pitchVibrato, pitchTransition, pitchDrift,
                            pitchSensitivity, pitchHardTune, pitchSnap, pitchHQ,
                            pitchTranspose, voiceCharacter,
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

    // Unified chain: start in the historical order (stages, then rack) so a
    // fresh instance sounds exactly like it always did.
    chainOrderMaster = ChainOrder::defaultOrder (rack.size());

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

    // Initialise parameter smoothers (15 ms ramp)
    smoothed.prepareAll (sampleRate, 0.015);
    // Prime smoothers from current parameter values so they don't ramp on startup
    {
        auto get = [&](const char* id) -> float {
            if (auto p = apvts.getRawParameterValue (id))
                return p->load();
            return 0.0f;
        };
        using namespace vf::param;
        smoothed.inputGain.setCurrentAndTargetValue  (get (vf::param::inputGain));
        smoothed.outputGain.setCurrentAndTargetValue (get (vf::param::outputGain));

        smoothed.pitchOn.setCurrentAndTargetValue (get (vf::param::pitchOn)); smoothed.pitchKey.setCurrentAndTargetValue (get (vf::param::pitchKey));
        smoothed.pitchScale.setCurrentAndTargetValue (get (vf::param::pitchScale)); smoothed.pitchSpeed.setCurrentAndTargetValue (get (vf::param::pitchSpeed)); smoothed.pitchAmount.setCurrentAndTargetValue (get (vf::param::pitchAmount));
        smoothed.pitchHumanize.setCurrentAndTargetValue (get (vf::param::pitchHumanize)); smoothed.pitchFormant.setCurrentAndTargetValue (get (vf::param::pitchFormant)); smoothed.pitchLatency.setCurrentAndTargetValue (get (vf::param::pitchLatency));
        smoothed.pitchFlex.setCurrentAndTargetValue (get (vf::param::pitchFlex)); smoothed.pitchVibrato.setCurrentAndTargetValue (get (vf::param::pitchVibrato)); smoothed.pitchTransition.setCurrentAndTargetValue (get (vf::param::pitchTransition)); smoothed.pitchDrift.setCurrentAndTargetValue (get (vf::param::pitchDrift));
        smoothed.pitchSensitivity.setCurrentAndTargetValue (get (vf::param::pitchSensitivity)); smoothed.pitchHardTune.setCurrentAndTargetValue (get (vf::param::pitchHardTune)); smoothed.pitchSnap.setCurrentAndTargetValue (get (vf::param::pitchSnap)); smoothed.pitchHQ.setCurrentAndTargetValue (get (vf::param::pitchHQ));
        smoothed.pitchTranspose.setCurrentAndTargetValue (get (vf::param::pitchTranspose)); smoothed.voiceCharacter.setCurrentAndTargetValue (get (vf::param::voiceCharacter));

        smoothed.gateOn.setCurrentAndTargetValue (get (vf::param::gateOn)); smoothed.gateThreshold.setCurrentAndTargetValue (get (vf::param::gateThreshold));

        smoothed.eqOn.setCurrentAndTargetValue (get (vf::param::eqOn)); smoothed.eqHpfFreq.setCurrentAndTargetValue (get (vf::param::eqHpfFreq)); smoothed.eqLowShelfGain.setCurrentAndTargetValue (get (vf::param::eqLowShelfGain));
        smoothed.eqMudGain.setCurrentAndTargetValue (get (vf::param::eqMudGain)); smoothed.eqMudFreq.setCurrentAndTargetValue (get (vf::param::eqMudFreq)); smoothed.eqPresenceGain.setCurrentAndTargetValue (get (vf::param::eqPresenceGain));
        smoothed.eqPresenceFreq.setCurrentAndTargetValue (get (vf::param::eqPresenceFreq)); smoothed.eqAirGain.setCurrentAndTargetValue (get (vf::param::eqAirGain));

        smoothed.dyneqOn.setCurrentAndTargetValue (get (vf::param::dyneqOn)); smoothed.dyneqLowThresh.setCurrentAndTargetValue (get (vf::param::dyneqLowThresh)); smoothed.dyneqLowFreq.setCurrentAndTargetValue (get (vf::param::dyneqLowFreq));
        smoothed.dyneqMidThresh.setCurrentAndTargetValue (get (vf::param::dyneqMidThresh)); smoothed.dyneqMidFreq.setCurrentAndTargetValue (get (vf::param::dyneqMidFreq)); smoothed.dyneqHighThresh.setCurrentAndTargetValue (get (vf::param::dyneqHighThresh));
        smoothed.dyneqHighFreq.setCurrentAndTargetValue (get (vf::param::dyneqHighFreq)); smoothed.dyneqRange.setCurrentAndTargetValue (get (vf::param::dyneqRange));

        smoothed.deessOn.setCurrentAndTargetValue (get (vf::param::deessOn)); smoothed.deessThreshold.setCurrentAndTargetValue (get (vf::param::deessThreshold)); smoothed.deessFreq.setCurrentAndTargetValue (get (vf::param::deessFreq));

        smoothed.compOn.setCurrentAndTargetValue (get (vf::param::compOn)); smoothed.compThreshold.setCurrentAndTargetValue (get (vf::param::compThreshold)); smoothed.compRatio.setCurrentAndTargetValue (get (vf::param::compRatio));
        smoothed.compAttack.setCurrentAndTargetValue (get (vf::param::compAttack)); smoothed.compRelease.setCurrentAndTargetValue (get (vf::param::compRelease)); smoothed.compMakeup.setCurrentAndTargetValue (get (compMakeup)); smoothed.compMix.setCurrentAndTargetValue (get (compMix));

        smoothed.mbandOn.setCurrentAndTargetValue (get (mbandOn)); smoothed.mbandLowThresh.setCurrentAndTargetValue (get (mbandLowThresh)); smoothed.mbandMidThresh.setCurrentAndTargetValue (get (mbandMidThresh));
        smoothed.mbandHighThresh.setCurrentAndTargetValue (get (mbandHighThresh)); smoothed.mbandLowGain.setCurrentAndTargetValue (get (mbandLowGain)); smoothed.mbandMidGain.setCurrentAndTargetValue (get (mbandMidGain));
        smoothed.mbandHighGain.setCurrentAndTargetValue (get (mbandHighGain)); smoothed.mbandRatio.setCurrentAndTargetValue (get (mbandRatio)); smoothed.mbandLowXover.setCurrentAndTargetValue (get (mbandLowXover)); smoothed.mbandHighXover.setCurrentAndTargetValue (get (mbandHighXover));

        smoothed.satOn.setCurrentAndTargetValue (get (vf::param::satOn)); smoothed.satType.setCurrentAndTargetValue (get (vf::param::satType)); smoothed.satDrive.setCurrentAndTargetValue (get (satDrive));
        smoothed.satTone.setCurrentAndTargetValue (get (satTone)); smoothed.satBias.setCurrentAndTargetValue (get (satBias)); smoothed.satMix.setCurrentAndTargetValue (get (satMix)); smoothed.satHQ.setCurrentAndTargetValue (get (satHQ));

        smoothed.delayOn.setCurrentAndTargetValue (get (delayOn)); smoothed.delayTime.setCurrentAndTargetValue (get (delayTime)); smoothed.delayFeedback.setCurrentAndTargetValue (get (delayFeedback)); smoothed.delayMix.setCurrentAndTargetValue (get (delayMix));

        smoothed.verbOn.setCurrentAndTargetValue (get (vf::param::verbOn)); smoothed.verbType.setCurrentAndTargetValue (get (verbType)); smoothed.verbSize.setCurrentAndTargetValue (get (verbSize));
        smoothed.verbDecay.setCurrentAndTargetValue (get (vf::param::verbDecay)); smoothed.verbPredelay.setCurrentAndTargetValue (get (vf::param::verbPredelay)); smoothed.verbDamp.setCurrentAndTargetValue (get (verbDamp));
        smoothed.verbDiffusion.setCurrentAndTargetValue (get (verbDiffusion)); smoothed.verbLowCut.setCurrentAndTargetValue (get (verbLowCut)); smoothed.verbHighCut.setCurrentAndTargetValue (get (verbHighCut));
        smoothed.verbModDepth.setCurrentAndTargetValue (get (verbModDepth)); smoothed.verbWidth.setCurrentAndTargetValue (get (verbWidth)); smoothed.verbMix.setCurrentAndTargetValue (get (vf::param::verbMix));
        smoothed.verbDuck.setCurrentAndTargetValue (get (verbDuck)); smoothed.verbShimmer.setCurrentAndTargetValue (get (verbShimmer)); smoothed.verbFreeze.setCurrentAndTargetValue (get (verbFreeze));

        smoothed.limitOn.setCurrentAndTargetValue (get (limitOn)); smoothed.limitCeiling.setCurrentAndTargetValue (get (limitCeiling)); smoothed.limitGain.setCurrentAndTargetValue (get (limitGain));
    }
}

ChainParams VoxBrainProcessor::readChainParams()
{
    // Read smoothed parameter values (updated once per block in processBlock)
    auto v = [this] (juce::SmoothedValue<float>& sv) { return sv.getNextValue(); };
    auto b = [&v]   (juce::SmoothedValue<float>& sv) { return v (sv) > 0.5f; };

    ChainParams p;
    p.inputGainDb  = v (smoothed.inputGain);
    p.outputGainDb = v (smoothed.outputGain);

    // Pitch: resolve "Auto" key/scale from the last LEARN pass
    {
        RetuneParams rp;
        rp.on       = b (smoothed.pitchOn);
        rp.speedMs  = v (smoothed.pitchSpeed);
        rp.amount   = v (smoothed.pitchAmount) * 0.01f;

        const int keyChoice   = (int) v (smoothed.pitchKey);     // 0=Auto, 1=C … 12=B
        const int scaleChoice = (int) v (smoothed.pitchScale);   // 0=Auto,1=Chrom,2=Maj,3=Min,…10=Blues

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
        rp.humanize   = v (smoothed.pitchHumanize) * 0.01f;
        rp.formant    = v (smoothed.pitchFormant);
        rp.transposeSemis = v (smoothed.pitchTranspose);   // free interval, independent of correction
        rp.latencyMode = (int) v (smoothed.pitchLatency);   // 0=Live,1=Balanced,2=Studio

        // ---- pro controls (see DSP/NoteIntelligence.h) ----
        rp.flexTune            = v (smoothed.pitchFlex)        * 0.01f;
        rp.vibratoPreservation = v (smoothed.pitchVibrato)     * 0.01f;
        rp.transitionSmoothing = v (smoothed.pitchTransition)  * 0.01f;
        rp.driftCorrection     = v (smoothed.pitchDrift)       * 0.01f;
        rp.sensitivity         = v (smoothed.pitchSensitivity) * 0.01f;
        rp.hardTune            = v (smoothed.pitchHardTune)    * 0.01f;
        rp.snapThreshold       = v (smoothed.pitchSnap);              // cents
        rp.hqRender            = b (smoothed.pitchHQ);
        p.retune = rp;
    }

    p.gate  = { b (smoothed.gateOn), v (smoothed.gateThreshold) };
    p.eq    = { b (smoothed.eqOn), v (smoothed.eqHpfFreq), v (smoothed.eqLowShelfGain), v (smoothed.eqMudGain), v (smoothed.eqMudFreq),
                v (smoothed.eqPresenceGain), v (smoothed.eqPresenceFreq), v (smoothed.eqAirGain) };
    p.dyneq = { b (smoothed.dyneqOn), v (smoothed.dyneqLowThresh), v (smoothed.dyneqLowFreq), v (smoothed.dyneqMidThresh),
                v (smoothed.dyneqMidFreq), v (smoothed.dyneqHighThresh), v (smoothed.dyneqHighFreq), v (smoothed.dyneqRange) };
    p.deess = { b (smoothed.deessOn), v (smoothed.deessThreshold), v (smoothed.deessFreq) };
    p.comp  = { b (smoothed.compOn), v (smoothed.compThreshold), v (smoothed.compRatio), v (smoothed.compAttack),
                v (smoothed.compRelease), v (smoothed.compMakeup), v (smoothed.compMix) * 0.01f };
    p.mband = { b (smoothed.mbandOn), v (smoothed.mbandLowThresh), v (smoothed.mbandMidThresh), v (smoothed.mbandHighThresh),
                v (smoothed.mbandLowGain), v (smoothed.mbandMidGain), v (smoothed.mbandHighGain), v (smoothed.mbandRatio),
                v (smoothed.mbandLowXover), v (smoothed.mbandHighXover) };
    p.sat   = { b (smoothed.satOn), (int) v (smoothed.satType), v (smoothed.satDrive) * 0.01f, v (smoothed.satTone) * 0.01f,
                v (smoothed.satBias) * 0.01f, v (smoothed.satMix) * 0.01f, b (smoothed.satHQ) };
    p.delay = { b (smoothed.delayOn), v (smoothed.delayTime), v (smoothed.delayFeedback) * 0.01f, v (smoothed.delayMix) * 0.01f };
    p.verb  = { b (smoothed.verbOn), (int) v (smoothed.verbType), v (smoothed.verbSize) * 0.01f, v (smoothed.verbDecay) * 0.01f,
                v (smoothed.verbPredelay), v (smoothed.verbDamp) * 0.01f, v (smoothed.verbDiffusion) * 0.01f,
                v (smoothed.verbLowCut), v (smoothed.verbHighCut), v (smoothed.verbModDepth) * 0.01f, v (smoothed.verbWidth) * 0.01f,
                v (smoothed.verbMix) * 0.01f, v (smoothed.verbDuck) * 0.01f, v (smoothed.verbShimmer) * 0.01f, b (smoothed.verbFreeze) };
    p.limit = { b (smoothed.limitOn), v (smoothed.limitCeiling), v (smoothed.limitGain) };
    return p;
}

void VoxBrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Update all parameter smoothers from APVTS (lock-free reads)
    smoothed.updateAll (raw);

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

    // End of a LEARN pass. The audio thread ONLY stops the accumulation here —
    // real-time safe. Building the snapshot (sorting ~65k values + allocating)
    // happens on the message thread in timerCallback; doing it here blew the
    // audio deadline the instant LEARN finished.
    if (learnStopRequested.exchange (false))
    {
        analysis.stopLearning();
        learnFinalizePending.store (true, std::memory_order_release);
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

    refreshChainOrderCache();   // lock-free unless the user just reordered

    for (int off = 0; off < total; off += maxB)
    {
        const int len = std::min (maxB, total - off);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), nch, off, len);

        // ---- unified chain: fixed stages and rack modules in ONE order ----
        // Input/output trim always bookend the chain; everything between is
        // driven by chainOrderCache, so dragging a module in the UI genuinely
        // changes the processing order.
        chain.applyInputGain (slice, chainP);
        {
            mods::ModuleRack::ScopedProcess rackAccess (rack, &rackAuto);
            int rackCursor = 0;
            for (int i = 0; i < chainOrderCacheCount; ++i)
            {
                const auto& item = chainOrderCache[i];
                if (item.isStage())
                    chain.processStage (item.stage, slice, chainP);
                else
                    rackAccess.processNode (rackCursor++, slice);   // Nth token → Nth rack module
            }
        }
        chain.applyOutputGain (slice, chainP);
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
    vf::crashlog::step (shouldLearn ? "setLearning(start)" : "setLearning(stop)");
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

    // Keep the unified chain order in step with the rack. Modules can be added
    // or removed from several places (rack UI, LEARN auto-build, reference
    // accepts); healing here covers them all without each call site remembering.
    syncChainOrderWithRack();

    // Build the LEARN snapshot here (message thread) — the audio thread only
    // stopped the accumulation. Safe: accumulation has ceased, so nothing on the
    // audio thread is touching these buffers any more.
    if (learnFinalizePending.exchange (false, std::memory_order_acquire))
    {
        vf::crashlog::step ("finalizeLearning");
        pendingSnapshot = analysis.finalizeLearning();
        snapshotFresh.store (true, std::memory_order_release);
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
    vf::crashlog::step ("finishAutoMix");
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

    // Give the assistant access to the module rack + the last LEARN analysis so
    // it can also add/remove/dial modules ("add a de-esser", "set the compressor
    // up for my voice"), not just edit the fixed chain. One undo covers it all.
    ChatEngine::Context ctx;
    ctx.rack     = &rack;
    ctx.snapshot = &lastSnapshot;
    ctx.onRackChanged = [this]
    {
        syncRackMacros();            // new/re-dialled values become automatable
        syncChainOrderWithRack();    // heal the unified chain order
    };
    return ChatEngine::handleMessage (message, apvts, std::move (ctx));
}

std::vector<mods::ModuleSuggestion> VoxBrainProcessor::suggestModules() const
{
    return mods::ModuleAdvisor::suggest (lastSnapshot);
}

// ============================================================================
//  Unified chain order
// ============================================================================
void VoxBrainProcessor::refreshChainOrderCache()
{
    // Audio thread. Only touches the lock when the order actually changed, and
    // never blocks: if the message thread is mid-edit we simply keep playing the
    // previous order for one more block.
    const uint32_t v = chainOrderVersion.load (std::memory_order_acquire);
    if (v == chainOrderCacheVersion && chainOrderCacheCount > 0)
        return;

    const juce::SpinLock::ScopedTryLockType sl (chainOrderLock);
    if (! sl.isLocked())
        return;

    const int n = juce::jmin ((int) chainOrderMaster.size(), kMaxChainItems);
    for (int i = 0; i < n; ++i)
        chainOrderCache[i] = chainOrderMaster[(size_t) i];
    chainOrderCacheCount   = n;
    chainOrderCacheVersion = v;
}

std::vector<ChainItem> VoxBrainProcessor::getChainOrder() const
{
    const juce::SpinLock::ScopedLockType sl (chainOrderLock);
    return chainOrderMaster;
}

void VoxBrainProcessor::setChainOrder (std::vector<ChainItem> order)
{
    ChainOrder::repair (order, rack.size());
    {
        const juce::SpinLock::ScopedLockType sl (chainOrderLock);
        chainOrderMaster = std::move (order);
    }
    chainOrderVersion.fetch_add (1, std::memory_order_release);
}

void VoxBrainProcessor::moveChainItem (int from, int to)
{
    auto order = getChainOrder();
    const int n = (int) order.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    const auto item = order[(size_t) from];
    order.erase (order.begin() + from);
    order.insert (order.begin() + juce::jlimit (0, (int) order.size(), to), item);
    setChainOrder (std::move (order));
}

void VoxBrainProcessor::applyChainArrangement (std::vector<ChainItem> order,
                                               const juce::StringArray& rackIdsInOrder)
{
    // 1. Reorder rack NODES so their order matches the visual order of rack rows.
    //    Processing runs the Nth rack token onto the Nth rack node, so aligning
    //    the node list with the on-screen order is what makes a rack-vs-rack drag
    //    (or an insert-at-slot) genuinely change the sound. Placing each id at its
    //    final index in turn realises any permutation.
    for (int i = 0; i < rackIdsInOrder.size(); ++i)
        rack.moveModule (rackIdsInOrder[i], i);

    // 2. Keep every module's sound with its (possibly new) macro slot.
    syncRackMacros();

    // 3. Commit the stage/rack-token sequence (repaired against the rack).
    setChainOrder (std::move (order));
}

void VoxBrainProcessor::syncChainOrderWithRack()
{
    auto order = getChainOrder();
    if (ChainOrder::rackTokenCount (order) == rack.size())
        return;                            // already in step — nothing to do
    setChainOrder (std::move (order));     // repair() adds/removes rack tokens
}

void VoxBrainProcessor::autoBuildRack()
{
    autoBuildRackFromAnalysis();          // proven default front-end (only when empty)

    // …plus any advisor picks from the last LEARN that aren't in the rack yet.
    for (const auto& s : suggestModules())
    {
        if (! mods::ModuleRegistry::instance().isImplemented (s.moduleId)) continue;
        if (rackHasType (s.moduleId)) continue;
        rack.addModule (s.moduleId);
    }

    syncRackMacros();
    syncChainOrderWithRack();
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

    // The rack adds COMPLEMENTARY polish the fixed chain doesn't do, so nothing
    // doubles up. Which modules those are — and how each one is set — now comes
    // from the AI Engineer's knowledge base (Brain/ModuleIntelligence), which
    // scores every module against THIS analysis and returns calibrated settings
    // in natural units. Previously this was four modules with fixed numbers.
    static const char* kFixedChainHandles[] =
    {
        // Already covered by the fixed VocalChain stages — never duplicate them.
        "compressor", "gate", "de_esser", "dynamic_eq", "parametric_eq",
        "limiter", "highpass", "lowpass", "vintage_eq",
    };

    int added = 0;
    for (const auto& rec : ModuleIntelligence::recommend (s))
    {
        if (added >= 4) break;                       // keep the auto rack focused
        if (rec.need < ModuleIntelligence::kInsertThreshold) break;   // ranked: rest are weaker
        if (! mods::ModuleRegistry::instance().isImplemented (rec.moduleId)) continue;

        bool handledByChain = false;
        for (const char* h : kFixedChainHandles)
            if (rec.moduleId == h) { handledByChain = true; break; }
        if (handledByChain || rackHasType (rec.moduleId)) continue;

        const auto iid = rack.addModule (rec.moduleId);
        if (iid.isEmpty()) continue;
        if (auto* m = rack.find (iid))
            for (const auto& st : rec.settings)
                m->setValue (st.paramId, st.value);   // dialled to THIS vocal
        ++added;
    }

    syncRackMacros();
}

void VoxBrainProcessor::aiDialRackModule (const juce::String& instanceId)
{
    // "Set this module up for my voice" — works on ANY module in the rack,
    // including one the user inserted by hand, using the same knowledge base
    // LEARN uses. Undoable as a single step.
    if (! lastSnapshot.valid) return;

    juce::String typeId;
    for (const auto& n : rack.snapshot())
        if (n.instanceId == instanceId) { typeId = n.typeId; break; }
    if (typeId.isEmpty() || ! ModuleIntelligence::knows (typeId)) return;

    const auto rec = ModuleIntelligence::dialFor (typeId, lastSnapshot);
    if (rec.settings.empty()) return;

    presets.pushUndo ("AI dial: " + rec.moduleName);
    if (auto* m = rack.find (instanceId))
        for (const auto& st : rec.settings)
            m->setValue (st.paramId, st.value);
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

// ============================================================================
//  Voice Changer
// ============================================================================
//  A character is applied through exactly the same route as every other AI
//  decision — natural units converted to normalised and written with a host
//  gesture — so it is automatable, undoable in one step, savable as a preset,
//  and it respects a locked module. Nothing here touches the audio thread.
juce::String VoxBrainProcessor::applyVoiceCharacterById (const juce::String& id)
{
    const auto* prof = vf::VoiceCharacters::byId (id);
    if (prof == nullptr)
        return {};

    // Keep the menu parameter in step, whether the request came from the menu,
    // the chat assistant, or a preset. Written first so a single undo restores
    // both the selection and the settings it produced.
    presets.pushUndo ("Voice: " + prof->name);

    if (auto* sel = apvts.getParameter (vf::param::voiceCharacter))
    {
        const int idx = vf::VoiceCharacters::indexOf (id);
        sel->beginChangeGesture();
        sel->setValueNotifyingHost (sel->convertTo0to1 ((float) idx));
        sel->endChangeGesture();
    }

    for (const auto& t : prof->targets)
    {
        if (isModuleLocked (t.paramId))
            continue;   // locked module — the character leaves it alone

        if (auto* param = apvts.getParameter (t.paramId))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (t.value));
            param->endChangeGesture();
        }
    }

    if (presets.onStateChanged) presets.onStateChanged();
    return prof->sound;
}

juce::String VoxBrainProcessor::applyVoiceCharacter (int menuIndex)
{
    const auto* prof = vf::VoiceCharacters::byIndex (menuIndex);
    if (prof == nullptr || prof->id == "off")
        return {};
    return applyVoiceCharacterById (prof->id);
}

int VoxBrainProcessor::currentVoiceCharacter() const
{
    if (auto* p = apvts.getRawParameterValue (vf::param::voiceCharacter))
        return (int) p->load();
    return 0;
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

        // Save the unified chain order too, so a rearranged chain persists.
        // Old builds ignore this child; new builds fall back to the default
        // (historical) order when it's absent.
        if (auto orderXml = ChainOrder::toXml (getChainOrder()))
            xml->addChildElement (orderXml.release());

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

            // Restore the unified chain order (repaired against the rack we just
            // loaded). Absent → the historical order, so old sessions are safe.
            if (auto* orderXml = xml->getChildByName ("ChainOrder"))
            {
                setChainOrder (ChainOrder::fromXml (orderXml, rack.size()));
                xml->removeChildElement (orderXml, true);
            }
            else
            {
                setChainOrder (ChainOrder::defaultOrder (rack.size()));
            }

            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        }
}

// ============================================================================
//  SmoothedParams implementation
// ============================================================================
void VoxBrainProcessor::SmoothedParams::prepareAll (double sampleRate, double rampSeconds)
{
    auto prep = [&](auto& sv) { sv.reset (sampleRate, rampSeconds); };
    prep (inputGain); prep (outputGain);
    prep (pitchOn); prep (pitchKey); prep (pitchScale); prep (pitchSpeed); prep (pitchAmount);
    prep (pitchHumanize); prep (pitchFormant); prep (pitchLatency);
    prep (pitchFlex); prep (pitchVibrato); prep (pitchTransition); prep (pitchDrift);
    prep (pitchSensitivity); prep (pitchHardTune); prep (pitchSnap); prep (pitchHQ);
    prep (pitchTranspose); prep (voiceCharacter);
    prep (gateOn); prep (gateThreshold);
    prep (eqOn); prep (eqHpfFreq); prep (eqLowShelfGain); prep (eqMudGain); prep (eqMudFreq);
    prep (eqPresenceGain); prep (eqPresenceFreq); prep (eqAirGain);
    prep (dyneqOn); prep (dyneqLowThresh); prep (dyneqLowFreq); prep (dyneqMidThresh);
    prep (dyneqMidFreq); prep (dyneqHighThresh); prep (dyneqHighFreq); prep (dyneqRange);
    prep (deessOn); prep (deessThreshold); prep (deessFreq);
    prep (compOn); prep (compThreshold); prep (compRatio); prep (compAttack);
    prep (compRelease); prep (compMakeup); prep (compMix);
    prep (mbandOn); prep (mbandLowThresh); prep (mbandMidThresh); prep (mbandHighThresh);
    prep (mbandLowGain); prep (mbandMidGain); prep (mbandHighGain); prep (mbandRatio);
    prep (mbandLowXover); prep (mbandHighXover);
    prep (satOn); prep (satType); prep (satDrive); prep (satTone); prep (satBias); prep (satMix); prep (satHQ);
    prep (delayOn); prep (delayTime); prep (delayFeedback); prep (delayMix);
    prep (verbOn); prep (verbType); prep (verbSize); prep (verbDecay); prep (verbPredelay);
    prep (verbDamp); prep (verbDiffusion); prep (verbLowCut); prep (verbHighCut);
    prep (verbModDepth); prep (verbWidth); prep (verbMix); prep (verbDuck); prep (verbShimmer); prep (verbFreeze);
    
}

void VoxBrainProcessor::SmoothedParams::updateAll (const std::unordered_map<juce::String, std::atomic<float>*>& raw)
{
    auto get = [&](const char* id) -> float {
        auto it = raw.find (juce::String (id));
        if (it != raw.end() && it->second)
            return it->second->load();
        return 0.0f;
    };

    using namespace vf::param; // refer to ParameterIDs by qualified names

    inputGain.setTargetValue  (get (vf::param::inputGain));
    outputGain.setTargetValue (get (vf::param::outputGain));

    pitchOn.setTargetValue (get (vf::param::pitchOn)); pitchKey.setTargetValue (get (vf::param::pitchKey));
    pitchScale.setTargetValue (get (vf::param::pitchScale)); pitchSpeed.setTargetValue (get (vf::param::pitchSpeed)); pitchAmount.setTargetValue (get (vf::param::pitchAmount));
    pitchHumanize.setTargetValue (get (vf::param::pitchHumanize)); pitchFormant.setTargetValue (get (vf::param::pitchFormant)); pitchLatency.setTargetValue (get (vf::param::pitchLatency));
    pitchFlex.setTargetValue (get (vf::param::pitchFlex)); pitchVibrato.setTargetValue (get (vf::param::pitchVibrato)); pitchTransition.setTargetValue (get (vf::param::pitchTransition)); pitchDrift.setTargetValue (get (vf::param::pitchDrift));
    pitchSensitivity.setTargetValue (get (vf::param::pitchSensitivity)); pitchHardTune.setTargetValue (get (vf::param::pitchHardTune)); pitchSnap.setTargetValue (get (vf::param::pitchSnap)); pitchHQ.setTargetValue (get (vf::param::pitchHQ));
    pitchTranspose.setTargetValue (get (vf::param::pitchTranspose)); voiceCharacter.setTargetValue (get (vf::param::voiceCharacter));

    gateOn.setTargetValue (get (vf::param::gateOn)); gateThreshold.setTargetValue (get (vf::param::gateThreshold));

    eqOn.setTargetValue (get (vf::param::eqOn)); eqHpfFreq.setTargetValue (get (vf::param::eqHpfFreq)); eqLowShelfGain.setTargetValue (get (vf::param::eqLowShelfGain));
        eqMudGain.setTargetValue (get (vf::param::eqMudGain)); eqMudFreq.setTargetValue (get (vf::param::eqMudFreq)); eqPresenceGain.setTargetValue (get (vf::param::eqPresenceGain));
        eqPresenceFreq.setTargetValue (get (vf::param::eqPresenceFreq)); eqAirGain.setTargetValue (get (vf::param::eqAirGain));

    dyneqOn.setTargetValue (get (vf::param::dyneqOn)); dyneqLowThresh.setTargetValue (get (vf::param::dyneqLowThresh)); dyneqLowFreq.setTargetValue (get (vf::param::dyneqLowFreq));
        dyneqMidThresh.setTargetValue (get (vf::param::dyneqMidThresh)); dyneqMidFreq.setTargetValue (get (vf::param::dyneqMidFreq)); dyneqHighThresh.setTargetValue (get (vf::param::dyneqHighThresh));
        dyneqHighFreq.setTargetValue (get (vf::param::dyneqHighFreq)); dyneqRange.setTargetValue (get (vf::param::dyneqRange));

    deessOn.setTargetValue (get (vf::param::deessOn)); deessThreshold.setTargetValue (get (vf::param::deessThreshold)); deessFreq.setTargetValue (get (vf::param::deessFreq));

    compOn.setTargetValue (get (vf::param::compOn)); compThreshold.setTargetValue (get (vf::param::compThreshold)); compRatio.setTargetValue (get (vf::param::compRatio));
        compAttack.setTargetValue (get (vf::param::compAttack)); compRelease.setTargetValue (get (vf::param::compRelease)); compMakeup.setTargetValue (get (vf::param::compMakeup)); compMix.setTargetValue (get (vf::param::compMix));

    mbandOn.setTargetValue (get (vf::param::mbandOn)); mbandLowThresh.setTargetValue (get (vf::param::mbandLowThresh)); mbandMidThresh.setTargetValue (get (vf::param::mbandMidThresh));
        mbandHighThresh.setTargetValue (get (vf::param::mbandHighThresh)); mbandLowGain.setTargetValue (get (vf::param::mbandLowGain)); mbandMidGain.setTargetValue (get (vf::param::mbandMidGain));
        mbandHighGain.setTargetValue (get (vf::param::mbandHighGain)); mbandRatio.setTargetValue (get (vf::param::mbandRatio)); mbandLowXover.setTargetValue (get (vf::param::mbandLowXover)); mbandHighXover.setTargetValue (get (vf::param::mbandHighXover));

    satOn.setTargetValue (get (vf::param::satOn)); satType.setTargetValue (get (vf::param::satType)); satDrive.setTargetValue (get (vf::param::satDrive));
        satTone.setTargetValue (get (vf::param::satTone)); satBias.setTargetValue (get (vf::param::satBias)); satMix.setTargetValue (get (vf::param::satMix)); satHQ.setTargetValue (get (vf::param::satHQ));

    delayOn.setTargetValue (get (vf::param::delayOn)); delayTime.setTargetValue (get (vf::param::delayTime)); delayFeedback.setTargetValue (get (vf::param::delayFeedback)); delayMix.setTargetValue (get (vf::param::delayMix));

    verbOn.setTargetValue (get (vf::param::verbOn)); verbType.setTargetValue (get (vf::param::verbType)); verbSize.setTargetValue (get (vf::param::verbSize));
        verbDecay.setTargetValue (get (vf::param::verbDecay)); verbPredelay.setTargetValue (get (vf::param::verbPredelay)); verbDamp.setTargetValue (get (vf::param::verbDamp));
        verbDiffusion.setTargetValue (get (vf::param::verbDiffusion)); verbLowCut.setTargetValue (get (vf::param::verbLowCut)); verbHighCut.setTargetValue (get (vf::param::verbHighCut));
        verbModDepth.setTargetValue (get (vf::param::verbModDepth)); verbWidth.setTargetValue (get (vf::param::verbWidth)); verbMix.setTargetValue (get (vf::param::verbMix));
        verbDuck.setTargetValue (get (vf::param::verbDuck)); verbShimmer.setTargetValue (get (vf::param::verbShimmer)); verbFreeze.setTargetValue (get (vf::param::verbFreeze));

    limitOn.setTargetValue (get (vf::param::limitOn)); limitCeiling.setTargetValue (get (vf::param::limitCeiling)); limitGain.setTargetValue (get (vf::param::limitGain));
}

void VoxBrainProcessor::SmoothedParams::primeAll() {}

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

