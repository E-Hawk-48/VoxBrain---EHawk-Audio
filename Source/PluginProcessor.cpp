#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Chat/ChatEngine.h"

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

    learn16k.resize ((size_t) learn16kCapacity, 0.0f);   // preallocated: no RT alloc
    startTimerHz (10);   // message-thread poll for finished learn passes

    updater.startCheck();   // background, throttled; no-op until the update URL is set
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
    chain.prepare (spec);
    analysis.prepare (sampleRate, (int) spec.numChannels);
    setLatencySamples (chain.getLatencySamples());
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
        const int scaleChoice = (int) v (pitchScale);   // 0=Auto, 1=Chrom, 2=Maj, 3=Min

        int  root  = keyChoice == 0 ? autoKeyRoot.load() : keyChoice - 1;
        bool major = autoKeyMajor.load();
        if (scaleChoice == 2) major = true;
        if (scaleChoice == 3) major = false;

        const bool haveKey = root >= 0;
        rp.chromatic  = scaleChoice == 1 || ! haveKey;
        rp.keyRoot    = haveKey ? root : -1;
        rp.majorScale = major;
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

    chain.process (buffer, readChainParams());
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

    lastResult = AutoMixBrain::computeChain (snapshot);
    lastResult.summary << "\n" << engineNote << "\n";

    presets.pushUndo ("Auto-Mix");     // so a single Undo reverts the whole pass
    applyBrainResult (lastResult);

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
        copyXmlToBinary (*xml, destData);
}

void VoxBrainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
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
