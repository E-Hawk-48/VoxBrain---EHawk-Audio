#include "VocalChain.h"
#include <cmath>

namespace vf
{
namespace
{
    float dbToGain (float db)  { return juce::Decibels::decibelsToGain (db, -120.0f); }
    float gainToDb (float g)   { return juce::Decibels::gainToDecibels (g, -120.0f); }

    /** One-pole coefficient for a time constant in ms. */
    float tau (double fs, float ms)
    {
        return ms <= 0.0f ? 0.0f : std::exp (-1.0f / ((float) fs * ms * 0.001f));
    }

    /** Soft-knee peak compressor over one band buffer, in place. Tracks its own
        detector envelope (envDb, persisted by the caller) and applies makeup.
        Shared by the multiband compressor's three bands. */
    void compressBand (juce::AudioBuffer<float>& b, int numSamples, int numCh,
                       float threshDb, float ratio, float attCoef, float relCoef,
                       float makeupDb, float& envDb, float& maxGr)
    {
        const float kneeDb = 6.0f;
        const float makeup = dbToGain (makeupDb);
        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < numCh; ++c)
                peak = std::max (peak, std::abs (b.getSample (c, i)));
            const float inDb = gainToDb (std::max (peak, 1.0e-6f));

            envDb = inDb > envDb ? attCoef * envDb + (1.0f - attCoef) * inDb
                                 : relCoef * envDb + (1.0f - relCoef) * inDb;

            float grAmt = 0.0f;
            const float over = envDb - threshDb;
            if (over > kneeDb * 0.5f)
                grAmt = over * (1.0f - 1.0f / ratio);
            else if (over > -kneeDb * 0.5f)
            {
                const float x = over + kneeDb * 0.5f;
                grAmt = (1.0f - 1.0f / ratio) * x * x / (2.0f * kneeDb);
            }

            maxGr = std::max (maxGr, grAmt);
            const float g = dbToGain (-grAmt) * makeup;
            for (int c = 0; c < numCh; ++c)
                b.setSample (c, i, b.getSample (c, i) * g);
        }
    }
} // namespace

// ============================================================================
//  NoiseGate
// ============================================================================
void NoiseGate::prepare (double sampleRate)
{
    fs = sampleRate;
    envelope = 0.0f;
    gain = 1.0f;
}

void NoiseGate::process (juce::AudioBuffer<float>& buffer, const GateParams& p)
{
    if (! p.on) return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    const float threshold = dbToGain (p.thresholdDb);
    const float attCoef  = tau (fs, 0.5f);     // detector attack 0.5 ms
    const float relCoef  = tau (fs, 60.0f);    // detector release 60 ms
    const float gOpen    = tau (fs, 2.0f);     // gain opens fast
    const float gClose   = tau (fs, 120.0f);   // closes gently (breath-safe)

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int c = 0; c < numCh; ++c)
            peak = std::max (peak, std::abs (buffer.getSample (c, i)));

        envelope = peak > envelope ? attCoef * envelope + (1.0f - attCoef) * peak
                                   : relCoef * envelope + (1.0f - relCoef) * peak;

        // 4:1 downward expansion below threshold with soft floor at -60 dB
        float target = 1.0f;
        if (envelope < threshold)
        {
            const float underDb = gainToDb (std::max (envelope, 1.0e-6f)) - p.thresholdDb; // negative
            target = dbToGain (std::max (underDb * 3.0f, -60.0f));  // extra 3× attenuation
        }

        gain = target > gain ? gOpen  * gain + (1.0f - gOpen)  * target
                             : gClose * gain + (1.0f - gClose) * target;

        for (int c = 0; c < numCh; ++c)
            buffer.setSample (c, i, buffer.getSample (c, i) * gain);
    }
}

// ============================================================================
//  VocalEq
// ============================================================================
void VocalEq::prepare (const juce::dsp::ProcessSpec& spec)
{
    fs = spec.sampleRate;
    for (auto* f : { &hpf, &lowShelf, &mud, &presence, &air })
        f->prepare (spec);
    lastParams.hpfHz = -1.0f;   // force coefficient rebuild
}

void VocalEq::updateCoefficients (const EqParams& p)
{
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    *hpf.state      = *Coeffs::makeHighPass (fs, juce::jlimit (20.0f, 400.0f, p.hpfHz), 0.707f);
    *lowShelf.state = *Coeffs::makeLowShelf (fs, 180.0f, 0.707f, dbToGain (p.lowShelfDb));
    *mud.state      = *Coeffs::makePeakFilter (fs, juce::jlimit (150.0f, 800.0f, p.mudHz), 1.1f, dbToGain (p.mudDb));
    *presence.state = *Coeffs::makePeakFilter (fs, juce::jlimit (1500.0f, 8000.0f, p.presHz), 0.9f, dbToGain (p.presDb));
    *air.state      = *Coeffs::makeHighShelf (fs, 12000.0f, 0.707f, dbToGain (p.airDb));
    lastParams = p;
}

void VocalEq::process (juce::AudioBuffer<float>& buffer, const EqParams& p)
{
    if (! p.on) return;

    const bool changed = p.hpfHz != lastParams.hpfHz || p.lowShelfDb != lastParams.lowShelfDb
                      || p.mudDb != lastParams.mudDb || p.mudHz != lastParams.mudHz
                      || p.presDb != lastParams.presDb || p.presHz != lastParams.presHz
                      || p.airDb != lastParams.airDb;
    if (changed)
        updateCoefficients (p);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    hpf.process (ctx);
    lowShelf.process (ctx);
    mud.process (ctx);
    presence.process (ctx);
    air.process (ctx);
}

// ============================================================================
//  DynamicEq
// ============================================================================
void DynamicEq::prepare (const juce::dsp::ProcessSpec& spec)
{
    fs = spec.sampleRate;
    for (auto* band : { &low, &mid, &high })
    {
        band->freq  = -1.0f;
        band->envDb = -100.0f;
        band->bpL.reset();
        band->bpR.reset();
    }
}

void DynamicEq::updateBand (Band& b, float freq)
{
    // Unity-gain (0 dB peak) band-pass detector; Q sets the tamed bandwidth.
    auto c = juce::dsp::IIR::Coefficients<float>::makeBandPass (fs, freq, 1.3f);
    b.bpL.coefficients = c;
    b.bpR.coefficients = c;
    b.freq = freq;
}

void DynamicEq::processBand (juce::AudioBuffer<float>& buffer, Band& b,
                             float threshDb, float rangeDb, float& maxGr)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    const float attCoef = tau (fs, 5.0f);     // detector attack
    const float relCoef = tau (fs, 90.0f);    // detector release
    const float ratio   = 4.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = buffer.getSample (0, i);
        const float bL  = b.bpL.processSample (inL);
        float inR = 0.0f, bR = 0.0f, mag = std::abs (bL);
        if (numCh > 1)
        {
            inR = buffer.getSample (1, i);
            bR  = b.bpR.processSample (inR);
            mag = std::max (mag, std::abs (bR));
        }

        const float magDb = gainToDb (std::max (mag, 1.0e-6f));
        b.envDb = magDb > b.envDb ? attCoef * b.envDb + (1.0f - attCoef) * magDb
                                  : relCoef * b.envDb + (1.0f - relCoef) * magDb;

        const float over = b.envDb - threshDb;
        const float redDb = over > 0.0f ? std::min (rangeDb, over * (1.0f - 1.0f / ratio)) : 0.0f;
        const float f = 1.0f - dbToGain (-redDb);   // fraction of band to remove (0..)

        buffer.setSample (0, i, inL - f * bL);
        if (numCh > 1)
            buffer.setSample (1, i, inR - f * bR);
        maxGr = std::max (maxGr, redDb);
    }
}

void DynamicEq::process (juce::AudioBuffer<float>& buffer, const DynEqParams& p)
{
    if (! p.on) { grDb.store (0.0f); return; }

    if (p.lowFreqHz  != low.freq)  updateBand (low,  p.lowFreqHz);
    if (p.midFreqHz  != mid.freq)  updateBand (mid,  p.midFreqHz);
    if (p.highFreqHz != high.freq) updateBand (high, p.highFreqHz);

    float maxGr = 0.0f;
    processBand (buffer, low,  p.lowThreshDb,  p.rangeDb, maxGr);
    processBand (buffer, mid,  p.midThreshDb,  p.rangeDb, maxGr);
    processBand (buffer, high, p.highThreshDb, p.rangeDb, maxGr);
    grDb.store (maxGr);
}

// ============================================================================
//  DeEsser
// ============================================================================
void DeEsser::prepare (const juce::dsp::ProcessSpec& spec)
{
    fs = spec.sampleRate;
    lowPass.prepare (spec);
    highPass.prepare (spec);
    lowPass.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);
    highPass.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    highBand.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);
    envelope = 0.0f;
    lastFreq = -1.0f;
}

void DeEsser::process (juce::AudioBuffer<float>& buffer, const DeEssParams& p)
{
    if (! p.on) { grDb.store (0.0f); return; }

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    if (p.freqHz != lastFreq)
    {
        lowPass.setCutoffFrequency (p.freqHz);
        highPass.setCutoffFrequency (p.freqHz);
        lastFreq = p.freqHz;
    }

    // Split: buffer keeps the low band, highBand gets the sibilant band
    for (int c = 0; c < numCh; ++c)
        highBand.copyFrom (c, 0, buffer, c, 0, numSamples);

    juce::dsp::AudioBlock<float> lowBlock (buffer);
    juce::dsp::AudioBlock<float> highBlock (highBand);
    auto highSub = highBlock.getSubBlock (0, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> lowCtx (lowBlock);
    juce::dsp::ProcessContextReplacing<float> highCtx (highSub);
    lowPass.process (lowCtx);
    highPass.process (highCtx);

    // Compress the high band: fast attack, 6:1
    const float threshold = dbToGain (p.thresholdDb);
    const float attCoef = tau (fs, 0.3f);
    const float relCoef = tau (fs, 40.0f);
    float maxGr = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int c = 0; c < numCh; ++c)
            peak = std::max (peak, std::abs (highBand.getSample (c, i)));

        envelope = peak > envelope ? attCoef * envelope + (1.0f - attCoef) * peak
                                   : relCoef * envelope + (1.0f - relCoef) * peak;

        float g = 1.0f;
        if (envelope > threshold)
        {
            const float overDb = gainToDb (envelope) - p.thresholdDb;
            const float grAmt  = overDb * (1.0f - 1.0f / 6.0f);   // 6:1
            g = dbToGain (-grAmt);
            maxGr = std::max (maxGr, grAmt);
        }

        for (int c = 0; c < numCh; ++c)
            buffer.addSample (c, i, highBand.getSample (c, i) * g);
    }
    grDb.store (maxGr);
}

// ============================================================================
//  Compressor
// ============================================================================
void Compressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    fs = spec.sampleRate;
    dryCopy.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);
    envelopeDb = -100.0f;
    msEnv      = 0.0f;
    lastGrAmt  = 0.0f;
}

void Compressor::process (juce::AudioBuffer<float>& buffer, const CompParams& p)
{
    if (! p.on) { grDb.store (0.0f); return; }

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    const float mix = juce::jlimit (0.0f, 1.0f, p.mix);

    if (mix < 1.0f)
        for (int c = 0; c < numCh; ++c)
            dryCopy.copyFrom (c, 0, buffer, c, 0, numSamples);

    const float attCoef = tau (fs, p.attackMs);
    // Program-dependent release: interpolate between the user's release (shallow
    // GR → snappy) and 3× that (deep GR → smooth) using how hard we're clamping.
    const float relFast = tau (fs, p.releaseMs);
    const float relSlow = tau (fs, p.releaseMs * 3.0f);
    const float msCoef  = tau (fs, 5.0f);        // ~5 ms RMS detector window
    const float kneeDb  = 6.0f;
    const float makeup  = dbToGain (p.makeupDb);
    const float ratioSlope = 1.0f - 1.0f / juce::jmax (1.0f, p.ratio);
    float maxGr = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Program-dependent detector: blend a fast peak (transients) with a
        // short-window RMS (body). Peak alone chatters and over-reacts to single
        // samples; RMS alone is sluggish on consonants — the blend tracks vocal
        // dynamics far more musically for the same threshold/ratio.
        float peak = 0.0f, ms = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            const float s = buffer.getSample (c, i);
            peak = std::max (peak, std::abs (s));
            ms  += s * s;
        }
        ms /= (float) juce::jmax (1, numCh);
        msEnv = msCoef * msEnv + (1.0f - msCoef) * ms;
        const float rms    = std::sqrt (std::max (msEnv, 0.0f));
        const float detLin = 0.5f * peak + 0.5f * rms;
        const float inDb   = gainToDb (std::max (detLin, 1.0e-6f));

        if (inDb > envelopeDb)
        {
            envelopeDb = attCoef * envelopeDb + (1.0f - attCoef) * inDb;   // attack
        }
        else
        {
            const float depth   = juce::jlimit (0.0f, 1.0f, lastGrAmt / 12.0f);
            const float relCoef = relFast + (relSlow - relFast) * depth;   // deeper → slower
            envelopeDb = relCoef * envelopeDb + (1.0f - relCoef) * inDb;
        }

        // Soft-knee gain computer
        float grAmt = 0.0f;
        const float over = envelopeDb - p.thresholdDb;
        if (over > kneeDb * 0.5f)
            grAmt = over * ratioSlope;
        else if (over > -kneeDb * 0.5f)
        {
            const float x = over + kneeDb * 0.5f;
            grAmt = ratioSlope * x * x / (2.0f * kneeDb);
        }
        lastGrAmt = grAmt;

        maxGr = std::max (maxGr, grAmt);
        const float g = dbToGain (-grAmt) * makeup;
        for (int c = 0; c < numCh; ++c)
            buffer.setSample (c, i, buffer.getSample (c, i) * g);
    }

    if (mix < 1.0f)
        for (int c = 0; c < numCh; ++c)
        {
            buffer.applyGain (c, 0, numSamples, mix);
            buffer.addFrom (c, 0, dryCopy, c, 0, numSamples, 1.0f - mix);
        }

    grDb.store (maxGr);
}

// ============================================================================
//  MultibandCompressor
// ============================================================================
void MultibandCompressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    fs = spec.sampleRate;
    for (auto* f : { &lp1, &hp1, &lp2, &hp2 })
        f->prepare (spec);
    lp1.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    hp1.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lp2.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    hp2.setType (juce::dsp::LinkwitzRileyFilterType::highpass);

    lowBuf .setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);
    midBuf .setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);
    highBuf.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);

    envLowDb = envMidDb = envHighDb = -100.0f;
    lastLowXover = lastHighXover = -1.0f;
}

void MultibandCompressor::process (juce::AudioBuffer<float>& buffer, const MultibandParams& p)
{
    if (! p.on) { grDb.store (0.0f); return; }

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    // Keep the two crossovers ordered so the split stays valid.
    const float loX = juce::jlimit (60.0f, 1200.0f, p.lowXoverHz);
    const float hiX = juce::jlimit (loX + 100.0f, 12000.0f, p.highXoverHz);
    if (loX != lastLowXover)  { lp1.setCutoffFrequency (loX); hp1.setCutoffFrequency (loX); lastLowXover = loX; }
    if (hiX != lastHighXover) { lp2.setCutoffFrequency (hiX); hp2.setCutoffFrequency (hiX); lastHighXover = hiX; }

    // Split: low = LP(x); rest = HP(x); mid = LP(rest); high = HP(rest).
    for (int c = 0; c < numCh; ++c)
    {
        lowBuf .copyFrom (c, 0, buffer, c, 0, numSamples);
        highBuf.copyFrom (c, 0, buffer, c, 0, numSamples);   // holds "rest" for now
    }
    {
        juce::dsp::AudioBlock<float> lowBlock  (lowBuf);
        juce::dsp::AudioBlock<float> restBlock (highBuf);
        auto lowSub  = lowBlock .getSubBlock (0, (size_t) numSamples);
        auto restSub = restBlock.getSubBlock (0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> lowCtx  (lowSub);
        juce::dsp::ProcessContextReplacing<float> restCtx (restSub);
        lp1.process (lowCtx);
        hp1.process (restCtx);
    }
    for (int c = 0; c < numCh; ++c)
        midBuf.copyFrom (c, 0, highBuf, c, 0, numSamples);   // mid starts from "rest"
    {
        juce::dsp::AudioBlock<float> midBlock  (midBuf);
        juce::dsp::AudioBlock<float> highBlock (highBuf);
        auto midSub  = midBlock .getSubBlock (0, (size_t) numSamples);
        auto highSub = highBlock.getSubBlock (0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> midCtx  (midSub);
        juce::dsp::ProcessContextReplacing<float> highCtx (highSub);
        lp2.process (midCtx);
        hp2.process (highCtx);
    }

    // Compress each band independently. Lower bands get slower time constants.
    const float ratio = juce::jlimit (1.0f, 20.0f, p.ratio);
    float maxGr = 0.0f;
    compressBand (lowBuf,  numSamples, numCh, p.lowThreshDb,  ratio,
                  tau (fs, 15.0f), tau (fs, 200.0f), p.lowGainDb,  envLowDb,  maxGr);
    compressBand (midBuf,  numSamples, numCh, p.midThreshDb,  ratio,
                  tau (fs, 8.0f),  tau (fs, 120.0f), p.midGainDb,  envMidDb,  maxGr);
    compressBand (highBuf, numSamples, numCh, p.highThreshDb, ratio,
                  tau (fs, 3.0f),  tau (fs, 80.0f),  p.highGainDb, envHighDb, maxGr);

    // Sum the bands back to the output.
    for (int c = 0; c < numCh; ++c)
    {
        float* out = buffer.getWritePointer (c);
        const float* lo = lowBuf .getReadPointer (c);
        const float* md = midBuf .getReadPointer (c);
        const float* hi = highBuf.getReadPointer (c);
        for (int i = 0; i < numSamples; ++i)
            out[i] = lo[i] + md[i] + hi[i];
    }
    grDb.store (maxGr);
}

// ============================================================================
//  Saturator
// ============================================================================
namespace
{
    inline float softAlg (float x) { return x / (1.0f + std::abs (x)); }

    inline float diodeShape (float x)
    {
        return (x >= 0.0f ? 1.0f : -1.0f) * (1.0f - std::exp (-std::abs (x)));
    }

    // Primary transfer curve per model (input already driven + biased).
    inline float shapeCore (int type, float x)
    {
        switch (type)
        {
            case Saturator::Tube:
                return std::tanh (x + 0.08f * x * x * (x > 0.0f ? 1.0f : 0.6f));
            case Saturator::Tape:
                return softAlg (1.5f * x);
            case Saturator::Console:
            {
                const float xx = juce::jlimit (-1.6f, 1.6f, x);
                return xx - xx * xx * xx * (1.0f / 3.0f);
            }
            case Saturator::Transformer:
                return 0.5f * (std::tanh (1.3f * x) + softAlg (1.3f * x));
            case Saturator::Germanium:
                return x >= 0.0f ? std::tanh (1.6f * x) : std::tanh (0.6f * x);
            case Saturator::Diode:
                return diodeShape (x);
            default:
                return std::tanh (x);
        }
    }

    // Rough output-level match so switching models keeps a similar level.
    inline float modelNorm (int type)
    {
        switch (type)
        {
            case Saturator::Tube:        return 0.78f;
            case Saturator::Tape:        return 0.90f;
            case Saturator::Console:     return 0.92f;
            case Saturator::Transformer: return 0.82f;
            case Saturator::Germanium:   return 0.80f;
            case Saturator::Diode:       return 0.88f;
            default:                     return 0.85f;
        }
    }
}

void Saturator::prepare (const juce::dsp::ProcessSpec& spec)
{
    fs = spec.sampleRate;
    maxBlockSize = (int) spec.maximumBlockSize;
    using OS = juce::dsp::Oversampling<float>;
    os2 = std::make_unique<OS> (spec.numChannels, 1, OS::filterHalfBandPolyphaseIIR, true);
    os4 = std::make_unique<OS> (spec.numChannels, 2, OS::filterHalfBandPolyphaseIIR, true);
    os2->initProcessing (spec.maximumBlockSize);
    os4->initProcessing (spec.maximumBlockSize);
    dryCopy.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);
    for (int c = 0; c < 2; ++c) { toneLp[c] = exHp[c] = lofiHold[c] = 0.0f; lofiCount[c] = 0; }
}

void Saturator::applyTone (juce::AudioBuffer<float>& buffer, float tone)
{
    if (std::abs (tone - 0.5f) < 0.001f) return;   // neutral → skip
    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min (buffer.getNumChannels(), 2);
    const float a = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 4000.0f / (float) fs);
    const float amt = (tone - 0.5f) * 2.0f;        // -1 dark .. +1 bright
    for (int c = 0; c < numCh; ++c)
    {
        float* d = buffer.getWritePointer (c);
        float lp = toneLp[c];
        for (int i = 0; i < numSamples; ++i)
        {
            lp += a * (d[i] - lp);
            const float hp = d[i] - lp;
            d[i] = lp + hp * (1.0f + amt);
        }
        toneLp[c] = lp;
    }
}

void Saturator::processShaped (juce::AudioBuffer<float>& buffer, const SatParams& p)
{
    const int numSamples = buffer.getNumSamples();
    const float drive = 1.0f + p.drive * 9.0f;            // 1..10
    const float bias  = (p.bias - 0.5f) * 0.6f;           // asymmetry offset
    const float dcOff = shapeCore (p.type, bias);         // subtract DC from bias
    const float norm  = modelNorm (p.type);

    auto& os = p.hq ? *os4 : *os2;
    juce::dsp::AudioBlock<float> block (buffer);
    auto sub = block.getSubBlock (0, (size_t) numSamples);
    auto up  = os.processSamplesUp (sub);

    for (size_t c = 0; c < up.getNumChannels(); ++c)
    {
        float* data = up.getChannelPointer (c);
        const size_t n = up.getNumSamples();
        for (size_t i = 0; i < n; ++i)
            data[i] = (shapeCore (p.type, data[i] * drive + bias) - dcOff) * norm;
    }
    os.processSamplesDown (sub);
}

void Saturator::processExciter (juce::AudioBuffer<float>& buffer, const SatParams& p)
{
    const int numSamples = buffer.getNumSamples();
    const float drive = 1.0f + p.drive * 9.0f;
    auto& os = p.hq ? *os4 : *os2;
    const double osRate = fs * (p.hq ? 4.0 : 2.0);
    const float aHp = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 3000.0f / (float) osRate);

    juce::dsp::AudioBlock<float> block (buffer);
    auto sub = block.getSubBlock (0, (size_t) numSamples);
    auto up  = os.processSamplesUp (sub);

    for (size_t c = 0; c < up.getNumChannels() && c < 2; ++c)
    {
        float* data = up.getChannelPointer (c);
        const size_t n = up.getNumSamples();
        float hpState = exHp[c];
        for (size_t i = 0; i < n; ++i)
        {
            const float x = data[i];
            hpState += aHp * (x - hpState);
            const float hp = x - hpState;                    // high band only
            data[i] = x + std::tanh (drive * hp) * 0.6f;     // dry + generated harmonics
        }
        exHp[c] = hpState;
    }
    os.processSamplesDown (sub);
}

void Saturator::processLoFi (juce::AudioBuffer<float>& buffer, const SatParams& p)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min (buffer.getNumChannels(), 2);
    const int   decim  = 1 + (int) (p.drive * 7.0f);          // 1..8 sample-and-hold
    const float levels = 4.0f + (1.0f - p.drive) * 60.0f;     // fewer levels at high drive
    for (int c = 0; c < numCh; ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < numSamples; ++i)
        {
            if (lofiCount[c] <= 0) { lofiHold[c] = d[i]; lofiCount[c] = decim; }
            --lofiCount[c];
            const float q = std::round (lofiHold[c] * levels) / levels;   // bit-crush
            d[i] = juce::jlimit (-1.0f, 1.0f, q) * 0.9f;                    // hard clip
        }
    }
}

void Saturator::process (juce::AudioBuffer<float>& buffer, const SatParams& p)
{
    if (! p.on || p.mix <= 0.0001f) return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    const float mix = juce::jlimit (0.0f, 1.0f, p.mix);

    for (int c = 0; c < numCh; ++c)
        dryCopy.copyFrom (c, 0, buffer, c, 0, numSamples);

    if (p.type == LoFi)         processLoFi (buffer, p);
    else if (p.type == Exciter) processExciter (buffer, p);
    else                        processShaped (buffer, p);

    applyTone (buffer, p.tone);

    for (int c = 0; c < numCh; ++c)
    {
        buffer.applyGain (c, 0, numSamples, mix);
        buffer.addFrom (c, 0, dryCopy, c, 0, numSamples, 1.0f - mix);
    }
}

// ============================================================================
//  SlapDelay
// ============================================================================
void SlapDelay::prepare (const juce::dsp::ProcessSpec& spec)
{
    fs = spec.sampleRate;
    delayLine.reset();
    delayLine.setMaximumDelayInSamples ((int) (fs * 2.0) + 1);
    delayLine.prepare (spec);

    auto lp = juce::dsp::IIR::Coefficients<float>::makeLowPass  (fs, 6000.0f);
    auto hp = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, 250.0f);
    fbLowpassL.coefficients = lp;   fbLowpassR.coefficients = lp;
    fbHighpassL.coefficients = hp;  fbHighpassR.coefficients = hp;
    for (auto* f : { &fbLowpassL, &fbLowpassR, &fbHighpassL, &fbHighpassR })
        f->reset();
    smoothedDelaySamples = 0.0f;
}

void SlapDelay::process (juce::AudioBuffer<float>& buffer, const DelayParams& p)
{
    if (! p.on || p.mix <= 0.0001f) return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min (buffer.getNumChannels(), 2);
    const float targetSamples = juce::jlimit (1.0f, (float) (fs * 2.0 - 2.0),
                                              p.timeMs * 0.001f * (float) fs);
    const float fb  = juce::jlimit (0.0f, 0.85f, p.feedback);
    const float mix = juce::jlimit (0.0f, 1.0f, p.mix);
    if (smoothedDelaySamples <= 0.0f)
        smoothedDelaySamples = targetSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        smoothedDelaySamples += 0.0005f * (targetSamples - smoothedDelaySamples);

        for (int c = 0; c < numCh; ++c)
        {
            const float in = buffer.getSample (c, i);
            const float delayed = delayLine.popSample (c, smoothedDelaySamples, true);

            float fbSample = delayed * fb;
            fbSample = c == 0 ? fbLowpassL.processSample (fbHighpassL.processSample (fbSample))
                              : fbLowpassR.processSample (fbHighpassR.processSample (fbSample));

            delayLine.pushSample (c, in + fbSample);
            buffer.setSample (c, i, in + delayed * mix);
        }
    }
}

// ============================================================================
//  ReverbEngine — feedback delay network
// ============================================================================
namespace
{
    // Fractional (linearly-interpolated) circular-buffer read at an absolute
    // read position (may be negative or beyond cap — wrapped here).
    inline float fracRead (const std::vector<float>& buf, int cap, float readPos)
    {
        while (readPos < 0.0f)          readPos += (float) cap;
        while (readPos >= (float) cap)  readPos -= (float) cap;
        const int i0 = (int) readPos;
        const float fr = readPos - (float) i0;
        int i1 = i0 + 1; if (i1 >= cap) i1 -= cap;
        return buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
    }
}

void ReverbEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    fs = spec.sampleRate;
    const double sr = fs / 48000.0;

    preCap = (int) (0.205 * fs) + 4;
    preBuf.assign ((size_t) preCap, 0.0f);
    preWrite = 0;

    lowCut.reset();  highCut.reset();
    lastLowCut = lastHighCut = -1.0f;

    const int apBase[N] = { 149, 211, 271, 353 };
    for (int k = 0; k < N; ++k)
    {
        diff[k].len = juce::jmax (4, (int) (apBase[k] * sr));
        diff[k].buf.assign ((size_t) diff[k].len, 0.0f);
        diff[k].idx = 0;
    }

    const int   base[N] = { 1531, 1789, 2053, 2311 };
    const float rate[N] = { 0.83f, 1.07f, 0.61f, 1.31f };
    for (int k = 0; k < N; ++k)
    {
        fdnBaseLen[k] = juce::jmax (16, (int) (base[k] * sr));
        fdnCap[k] = (int) (fdnBaseLen[k] * 3.2) + (int) (0.014 * fs) + 8;
        fdn[k].assign ((size_t) fdnCap[k], 0.0f);
        fdnWrite[k] = 0;
        dampState[k] = 0.0f;
        lfoPhase[k] = (float) k * 0.5f;
        lfoInc[k] = juce::MathConstants<float>::twoPi * rate[k] / (float) fs;
    }

    shCap = (int) (0.12 * fs) + 4;
    shBuf.assign ((size_t) shCap, 0.0f);
    shWrite = 0;
    shRamp = 0.0f;

    duckEnv = bloomEnv = 0.0f;
}

void ReverbEngine::process (juce::AudioBuffer<float>& buffer, const ReverbParams& p)
{
    if (! p.on || (p.mix <= 0.0001f && ! p.freeze)) return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    // ---- algorithm recipe ----
    float sizeMul, diffMul, dampMul, modMul, decayMul, preBias, shimBias;
    bool bloomMode = false;
    switch (p.type)
    {
        case Room:      sizeMul=0.65f; diffMul=0.90f; dampMul=1.30f; modMul=0.5f; decayMul=0.70f; preBias=0.0f;  shimBias=0.0f;  break;
        case Plate:     sizeMul=0.90f; diffMul=1.35f; dampMul=0.70f; modMul=0.6f; decayMul=1.00f; preBias=0.0f;  shimBias=0.0f;  break;
        case Spring:    sizeMul=0.80f; diffMul=1.40f; dampMul=0.90f; modMul=1.8f; decayMul=0.95f; preBias=0.0f;  shimBias=0.0f;  break;
        case Cathedral: sizeMul=2.00f; diffMul=1.15f; dampMul=0.80f; modMul=1.3f; decayMul=1.30f; preBias=35.0f; shimBias=0.0f;  break;
        case Shimmer:   sizeMul=1.30f; diffMul=1.00f; dampMul=0.90f; modMul=1.0f; decayMul=1.15f; preBias=10.0f; shimBias=0.55f; break;
        case Bloom:     sizeMul=1.40f; diffMul=1.00f; dampMul=0.90f; modMul=1.1f; decayMul=1.15f; preBias=20.0f; shimBias=0.0f;  bloomMode=true; break;
        case Hall:
        default:        sizeMul=1.30f; diffMul=1.00f; dampMul=1.00f; modMul=1.0f; decayMul=1.10f; preBias=10.0f; shimBias=0.0f;  break;
    }

    // ---- derived per-block ----
    const float sizeScale = juce::jlimit (0.35f, 3.0f, (0.5f + p.size * 1.5f) * sizeMul);
    int L[N];
    for (int k = 0; k < N; ++k)
        L[k] = juce::jlimit (8, fdnCap[k] - 4, (int) (fdnBaseLen[k] * sizeScale));

    const float decay01 = juce::jlimit (0.0f, 1.0f, p.decay * decayMul);
    const float shim = juce::jlimit (0.0f, 1.0f, p.shimmer + (p.type == Shimmer ? shimBias : 0.0f));
    const bool  shEnable = (shim > 0.001f && ! p.freeze);
    float g = p.freeze ? 0.9995f : (0.5f + 0.492f * decay01);
    if (shEnable) g = juce::jmin (g, 0.88f - 0.30f * shim);       // headroom for regen
    const float shFbGain = shEnable ? shim * 0.20f : 0.0f;
    const float inGain = p.freeze ? 0.0f : 1.0f;

    const float dampFc = juce::jlimit (600.0f, 18000.0f,
                                       18000.0f - juce::jlimit (0.0f, 1.0f, p.damp * dampMul) * 16000.0f);
    const float dampCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * dampFc / (float) fs);

    const float apg = juce::jlimit (0.0f, 0.78f, juce::jlimit (0.0f, 1.0f, p.diffusion * diffMul) * 0.78f);
    for (int k = 0; k < N; ++k) diff[k].g = apg;

    const float lc = juce::jlimit (20.0f, 1000.0f, p.lowCutHz);
    const float hc = juce::jlimit (1000.0f, 20000.0f, p.highCutHz);
    if (lc != lastLowCut)  { lowCut.coefficients  = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, lc, 0.707f); lastLowCut = lc; }
    if (hc != lastHighCut) { highCut.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass  (fs, hc, 0.707f); lastHighCut = hc; }

    const int   preSamp = juce::jlimit (0, preCap - 2, (int) ((p.predelayMs + preBias) * 0.001f * (float) fs));
    const float modSamp = juce::jlimit (0.0f, 1.0f, p.modDepth * modMul) * (0.006f * (float) fs);
    const float width = juce::jlimit (0.0f, 1.0f, p.width);
    const float mix = juce::jlimit (0.0f, 1.0f, p.mix);
    const float duckAmt = juce::jlimit (0.0f, 1.0f, p.duck);
    const float shimDepth = (float) (shCap / 2 - 2);

    const float dAtt = 1.0f - std::exp (-1.0f / (0.005f * (float) fs));
    const float dRel = 1.0f - std::exp (-1.0f / (0.25f  * (float) fs));
    const float bAtt = 1.0f - std::exp (-1.0f / (0.40f  * (float) fs));
    const float bRel = 1.0f - std::exp (-1.0f / (0.60f  * (float) fs));

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = buffer.getSample (0, i);
        const float dryR = numCh > 1 ? buffer.getSample (1, i) : dryL;
        const float in = 0.5f * (dryL + dryR);

        const float rect = std::abs (in);
        duckEnv += (rect > duckEnv ? dAtt : dRel) * (rect - duckEnv);

        const float s = highCut.processSample (lowCut.processSample (in));

        // pre-delay
        preBuf[(size_t) preWrite] = s;
        int prp = preWrite - preSamp; if (prp < 0) prp += preCap;
        const float pd = preBuf[(size_t) prp];
        if (++preWrite >= preCap) preWrite = 0;

        // input diffusion
        float dif = pd;
        for (int k = 0; k < N; ++k) dif = diff[k].process (dif);
        const float inject = dif * inGain;

        // FDN read + in-loop damping
        float y[N];
        for (int k = 0; k < N; ++k)
        {
            const float mo = modSamp * std::sin (lfoPhase[k]);
            lfoPhase[k] += lfoInc[k];
            if (lfoPhase[k] > juce::MathConstants<float>::twoPi) lfoPhase[k] -= juce::MathConstants<float>::twoPi;
            const float rd = fracRead (fdn[k], fdnCap[k], (float) fdnWrite[k] - ((float) L[k] + mo));
            dampState[k] += dampCoef * (rd - dampState[k]);
            y[k] = dampState[k];
        }

        // Hadamard (energy-preserving) feedback
        const float fb[N] = {
            0.5f * ( y[0] + y[1] + y[2] + y[3]),
            0.5f * ( y[0] - y[1] + y[2] - y[3]),
            0.5f * ( y[0] + y[1] - y[2] - y[3]),
            0.5f * ( y[0] - y[1] - y[2] + y[3]) };

        // octave-up shimmer regeneration (two-tap crossfade pitch shifter)
        float shOut = 0.0f;
        if (shEnable)
        {
            shBuf[(size_t) shWrite] = 0.5f * (y[0] + y[1]);
            shRamp -= 1.0f; if (shRamp < 0.0f) shRamp += shimDepth;
            float dB = shRamp + shimDepth * 0.5f; if (dB >= shimDepth) dB -= shimDepth;
            const float a = fracRead (shBuf, shCap, (float) shWrite - shRamp);
            const float b = fracRead (shBuf, shCap, (float) shWrite - dB);
            const float gA = 1.0f - std::abs (2.0f * (shRamp / shimDepth) - 1.0f);
            const float gB = 1.0f - std::abs (2.0f * (dB    / shimDepth) - 1.0f);
            shOut = a * gA + b * gB;
            if (++shWrite >= shCap) shWrite = 0;
        }

        // write back into the network
        for (int k = 0; k < N; ++k)
        {
            fdn[k][(size_t) fdnWrite[k]] = inject * 0.25f + g * fb[k] + shFbGain * shOut;
            if (++fdnWrite[k] >= fdnCap[k]) fdnWrite[k] = 0;
        }

        // stereo output + width (mid/side)
        float wetL = y[0] + y[2];
        float wetR = y[1] + y[3];
        const float mid  = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * (0.3f + 1.4f * width);
        wetL = mid + side;
        wetR = mid - side;

        float wetGain = 1.0f - duckAmt * juce::jlimit (0.0f, 1.0f, duckEnv * 4.0f);
        if (bloomMode)
        {
            const float target = rect > 0.0005f ? 1.0f : 0.0f;
            bloomEnv += (target > bloomEnv ? bAtt : bRel) * (target - bloomEnv);
            wetGain *= bloomEnv;
        }

        buffer.setSample (0, i, dryL + wetL * mix * wetGain);
        if (numCh > 1) buffer.setSample (1, i, dryR + wetR * mix * wetGain);
    }
}

// ============================================================================
//  OutputLimiter
// ============================================================================
void OutputLimiter::prepare (const juce::dsp::ProcessSpec& spec)
{
    limiter.prepare (spec);
}

void OutputLimiter::process (juce::AudioBuffer<float>& buffer, const LimitParams& p)
{
    if (! p.on) return;

    if (p.gainDb > 0.01f)
        buffer.applyGain (dbToGain (p.gainDb));

    limiter.setThreshold (p.ceilingDb);
    limiter.setRelease (60.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    limiter.process (ctx);
}

// ============================================================================
//  VocalChain
// ============================================================================
void VocalChain::prepare (const juce::dsp::ProcessSpec& spec)
{
    currentSpec = spec;
    retune.prepare (spec.sampleRate, (int) spec.maximumBlockSize, (int) spec.numChannels);
    gate.prepare (spec.sampleRate);
    eq.prepare (spec);
    dyneq.prepare (spec);
    deess.prepare (spec);
    comp.prepare (spec);
    mband.prepare (spec);
    sat.prepare (spec);
    delay.prepare (spec);
    verb.prepare (spec);
    limiter.prepare (spec);
}

void VocalChain::reset()
{
    prepare (currentSpec);
}

// ---------------------------------------------------------------------------
//  Stage identity (stable ids are saved in state — never rename)
// ---------------------------------------------------------------------------
const char* VocalChain::stageId (Stage s) noexcept
{
    switch (s)
    {
        case Stage::Retune: return "retune";
        case Stage::Gate:   return "gate";
        case Stage::Eq:     return "eq";
        case Stage::DynEq:  return "dyneq";
        case Stage::DeEss:  return "deess";
        case Stage::Comp:   return "comp";
        case Stage::MBand:  return "mband";
        case Stage::Sat:    return "sat";
        case Stage::Delay:  return "delay";
        case Stage::Verb:   return "verb";
        case Stage::Limit:  return "limit";
    }
    return "?";
}

const char* VocalChain::stageName (Stage s) noexcept
{
    switch (s)
    {
        case Stage::Retune: return "Pitch";
        case Stage::Gate:   return "Gate";
        case Stage::Eq:     return "EQ";
        case Stage::DynEq:  return "Dynamic EQ";
        case Stage::DeEss:  return "De-Esser";
        case Stage::Comp:   return "Compressor";
        case Stage::MBand:  return "Multiband";
        case Stage::Sat:    return "Saturation";
        case Stage::Delay:  return "Delay";
        case Stage::Verb:   return "Reverb";
        case Stage::Limit:  return "Limiter";
    }
    return "?";
}

bool VocalChain::stageFromId (const juce::String& id, Stage& out) noexcept
{
    for (int i = 0; i < kStageCount; ++i)
    {
        const auto s = (Stage) i;
        if (id == stageId (s)) { out = s; return true; }
    }
    return false;
}

void VocalChain::processStage (Stage s, juce::AudioBuffer<float>& buffer, const ChainParams& p)
{
    switch (s)
    {
        case Stage::Retune: retune.process  (buffer, p.retune); break;
        case Stage::Gate:   gate.process    (buffer, p.gate);   break;
        case Stage::Eq:     eq.process      (buffer, p.eq);     break;
        case Stage::DynEq:  dyneq.process   (buffer, p.dyneq);  break;
        case Stage::DeEss:  deess.process   (buffer, p.deess);  break;
        case Stage::Comp:   comp.process    (buffer, p.comp);   break;
        case Stage::MBand:  mband.process   (buffer, p.mband);  break;
        case Stage::Sat:    sat.process     (buffer, p.sat);    break;
        case Stage::Delay:  delay.process   (buffer, p.delay);  break;
        case Stage::Verb:   verb.process    (buffer, p.verb);   break;
        case Stage::Limit:  limiter.process (buffer, p.limit);  break;
    }
}

void VocalChain::applyInputGain (juce::AudioBuffer<float>& buffer, const ChainParams& p)
{
    if (p.inputGainDb != 0.0f)
        buffer.applyGain (dbToGain (p.inputGainDb));
}

void VocalChain::applyOutputGain (juce::AudioBuffer<float>& buffer, const ChainParams& p)
{
    if (p.outputGainDb != 0.0f)
        buffer.applyGain (dbToGain (p.outputGainDb));
}

void VocalChain::process (juce::AudioBuffer<float>& buffer, const ChainParams& p)
{
    // Default (historical) order == the enum order, so this is byte-identical
    // to the original hard-wired sequence.
    applyInputGain (buffer, p);
    for (int i = 0; i < kStageCount; ++i)
        processStage ((Stage) i, buffer, p);
    applyOutputGain (buffer, p);
}
} // namespace vf

// (end of file)
