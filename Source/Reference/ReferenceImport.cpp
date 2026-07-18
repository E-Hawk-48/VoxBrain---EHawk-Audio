#include "ReferenceImport.h"
#include "ReferenceAnalyzer.h"
#include "VocalIsolation.h"
#include <cmath>

namespace vf
{
ReferenceImport::ReferenceImport() : juce::Thread ("VoxBrainReferenceImport") {}

ReferenceImport::~ReferenceImport()
{
    cancelFlag.store (true);
    stopThread (3000);
}

// ============================================================================
//  Decode — any common audio file → float buffer (≤ 2 channels) + sample rate
// ============================================================================
bool ReferenceImport::decode (const juce::File& file, juce::AudioBuffer<float>& out,
                              double& sampleRate, juce::String& error)
{
    if (! file.existsAsFile()) { error = "File not found."; return false; }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();   // WAV/AIFF/FLAC/OGG (+ MP3/AAC/WMA via the OS on Windows)
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { error = "Unsupported or unreadable audio format."; return false; }

    sampleRate = reader->sampleRate;
    if (sampleRate < 8000.0) { error = "Unusual sample rate."; return false; }

    const long long total = reader->lengthInSamples;
    if (total <= 0) { error = "The file contains no audio."; return false; }

    const int       ch  = (int) juce::jmax ((unsigned int) 1, reader->numChannels);
    const int       use = juce::jmin (ch, 2);                       // keep up to stereo
    const long long cap = (long long) (sampleRate * kMaxSeconds);
    const int       n   = (int) juce::jmin (total, cap);

    out.setSize (use, n);
    out.clear();
    reader->read (&out, 0, n, 0, true, use > 1);
    return true;
}

juce::String ReferenceImport::signatureOf (const juce::File& f)
{
    return f.getFullPathName() + "|"
         + juce::String (f.getLastModificationTime().toMilliseconds()) + "|"
         + juce::String (f.getSize());
}

// ============================================================================
//  Synchronous decode + analyze + match (reused by the thread AND by tests)
// ============================================================================
ReferenceResult ReferenceImport::analyzeFileSync (const juce::File& file,
                                                  std::function<void (float)> prog,
                                                  const std::atomic<bool>* cancel,
                                                  bool isolateVocal)
{
    ReferenceResult r;
    r.fileName = file.getFileName();
    r.filePath = file.getFullPathName();

    if (prog) prog (0.02f);

    juce::AudioBuffer<float> buf; double sr = 0.0; juce::String err;
    if (! decode (file, buf, sr, err)) { r.error = err; return r; }
    if (cancel != nullptr && cancel->load()) { r.error = "Cancelled."; return r; }

    // Optional centre-extraction so a full mix is measured more like its vocal.
    bool isolated = false;
    if (isolateVocal && buf.getNumChannels() >= 2)
    {
        if (prog) prog (0.06f);
        buf = VocalIsolation::isolateCenter (buf, sr);
        isolated = true;
    }

    auto ap = [&prog] (float f) { if (prog) prog (0.10f + 0.85f * f); };
    r.profile = ReferenceAnalyzer::analyze (buf, sr, ap, cancel);
    r.profile.vocalIsolated = isolated;
    if (cancel != nullptr && cancel->load()) { r.error = "Cancelled."; return r; }
    if (! r.profile.valid) { r.error = "Could not analyse this file (too short or silent)."; return r; }

    r.match = ReferenceMatchBrain::match (r.profile);
    r.ok = true;
    if (prog) prog (1.0f);
    return r;
}

// ============================================================================
//  Background job control
// ============================================================================
void ReferenceImport::analyzeFile (const juce::File& file, bool isolateVocal)
{
    cancelCurrent();          // stop any in-flight analysis quickly…
    stopThread (3000);        // …and join it
    cancelFlag.store (false);
    { std::lock_guard<std::mutex> l (jobMutex); pendingFile = file; pendingIsolate = isolateVocal; }
    progress.store (0.0f);
    state.store (State::Loading);
    startThread();
}

void ReferenceImport::cancelCurrent() { cancelFlag.store (true); }

void ReferenceImport::run()
{
    juce::File f; bool isolate = false;
    { std::lock_guard<std::mutex> l (jobMutex); f = pendingFile; isolate = pendingIsolate; }

    // Cache: identical file (path+modtime+size+isolate) → return the stored result.
    const auto key = signatureOf (f) + (isolate ? "|iso" : "");
    if (key == cacheKey && cacheResult.ok)
    {
        { std::lock_guard<std::mutex> l (resultMutex); finishedResult = cacheResult; resultReady = true; }
        progress.store (1.0f);
        state.store (State::Done);
        return;
    }

    state.store (State::Analyzing);
    auto prog = [this] (float p) { progress.store (juce::jlimit (0.0f, 1.0f, p)); };
    ReferenceResult res = analyzeFileSync (f, prog, &cancelFlag, isolate);

    if (cancelFlag.load()) { state.store (State::Cancelled); return; }
    if (res.ok) { cacheKey = key; cacheResult = res; }
    { std::lock_guard<std::mutex> l (resultMutex); finishedResult = res; resultReady = true; }
    state.store (res.ok ? State::Done : State::Error);
}

bool ReferenceImport::takeResult (ReferenceResult& out)
{
    std::lock_guard<std::mutex> l (resultMutex);
    if (! resultReady) return false;
    out = finishedResult;
    resultReady = false;
    return true;
}

juce::String ReferenceImport::statusText() const
{
    switch (state.load())
    {
        case State::Loading:   return "Loading reference…";
        case State::Analyzing: return "Analyzing reference… "
                                    + juce::String ((int) std::round (progress.load() * 100.0f)) + "%";
        case State::Done:      return "Analysis complete";
        case State::Error:     return "Analysis failed";
        case State::Cancelled: return "Analysis cancelled";
        default:               return {};
    }
}

// ============================================================================
//  Report formatting for the AI report panel
// ============================================================================
juce::String ReferenceImport::buildReport (const ReferenceResult& r)
{
    if (! r.ok)
        return r.error.isNotEmpty() ? r.error : juce::String ("Analysis failed.");

    const auto& m = r.match;
    juce::String s;
    s << m.summary << "\n\n";
    s << "SUGGESTED CHAIN (preview — nothing is applied yet):\n";
    for (const auto& d : m.decisions)
        s << "\n• " << d.area << "  [" << (int) std::round (d.confidence * 100.0f)
          << "% confidence]\n  " << d.rationale << "\n";

    if (! m.rackInserts.empty())
    {
        s << "\nCOMPLEMENTARY MODULES:\n";
        for (const auto& ins : m.rackInserts)
            s << "\n• " << ins.name << "  [" << (int) std::round (ins.confidence * 100.0f)
              << "%]\n  " << ins.rationale << "\n";
    }
    return s;
}
} // namespace vf
