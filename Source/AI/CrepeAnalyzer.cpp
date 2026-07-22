#include "CrepeAnalyzer.h"
#include <cmath>
#include <algorithm>
#include <array>

#if VB_HAS_ONNX
  // Manual API init: without this, the ORT C++ header binds the API during
  // static initialization (i.e. at plugin DLL load), which fires the
  // delay-loaded onnxruntime.dll BEFORE we can set the search path — the
  // plugin would then fail to load in hosts and in juce_vst3_helper.
  #define ORT_API_MANUAL_INIT
  #include <onnxruntime_cxx_api.h>
  #if JUCE_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX          // keep windows.h from clobbering std::min/max
    #include <windows.h>
  #endif
#endif

namespace vf
{
namespace
{
    constexpr int   kFrameLen  = 1024;   // CREPE input length @ 16 kHz
    constexpr int   kNumBins   = 360;
    constexpr float kCentsBase = 1997.3794084376191f;   // bin 0, cents rel. 10 Hz
    constexpr float kConfidenceGate = 0.5f;

    float binToHz (float bin)
    {
        const float cents = kCentsBase + 20.0f * bin;
        return 10.0f * std::pow (2.0f, cents / 1200.0f);
    }

#if VB_HAS_ONNX && JUCE_WINDOWS
    /** Directory containing THIS module (the plugin DLL, not the host exe). */
    juce::File getModuleDirectory()
    {
        HMODULE handle = nullptr;
        GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR> (&getModuleDirectory), &handle);
        wchar_t path[MAX_PATH] {};
        GetModuleFileNameW (handle, path, MAX_PATH);
        return juce::File (juce::String (path)).getParentDirectory();
    }

    /** SEH-guard the ORT API bind. A mismatched or corrupt onnxruntime.dll can
        make InitApi() (or the API it returns) ACCESS-VIOLATE instead of failing
        cleanly — and an access violation is NOT a C++ exception, so try/catch is
        useless against it. __try/__except catches it so we fall back to DSP. This
        helper holds no C++ objects that need unwinding (required for __try). */
    static bool sehInitApi() noexcept
    {
        __try { Ort::InitApi(); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    /** Loads onnxruntime.dll from the plugin's own folder and binds the ORT
        API. Returns false (never throws/crashes) when the DLL is missing or a
        version the plugin can't talk to. */
    bool ensureOrtLoaded()
    {
        static bool attempted = false, loaded = false;
        if (attempted)
            return loaded;
        attempted = true;

        const auto moduleDir = getModuleDirectory();
        const auto dllPath = moduleDir.getChildFile ("onnxruntime.dll");

        HMODULE h = LoadLibraryW (dllPath.getFullPathName().toWideCharPointer());
        if (h == nullptr)
            h = LoadLibraryW (L"onnxruntime.dll");     // PATH / host-dir fallback
        if (h == nullptr)
            return false;

        // DLL is now in memory: the delay-load thunk will bind to it. If that
        // binding faults (wrong ABI/version), bail cleanly to DSP analysis.
        if (! sehInitApi())
            return false;
        loaded = true;
        return true;
    }
#endif
} // namespace

// ============================================================================
juce::File CrepeAnalyzer::findModelFile()
{
#if VB_HAS_ONNX && JUCE_WINDOWS
    const auto moduleDir = getModuleDirectory();
    for (const auto& candidate :
         { moduleDir.getChildFile ("VoxBrainModels").getChildFile ("crepe-tiny.onnx"),
           juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("VoxBrain").getChildFile ("Models")
               .getChildFile ("crepe-tiny.onnx") })
    {
        if (candidate.existsAsFile())
            return candidate;
    }
#endif
    return {};
}

// ============================================================================
#if VB_HAS_ONNX
struct CrepeAnalyzer::Impl
{
    Ort::Env env { ORT_LOGGING_LEVEL_ERROR, "VoxBrain" };
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions allocator;
    std::string inputName, outputName;

    bool load (const juce::File& modelFile)
    {
       #if JUCE_WINDOWS
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads (2);
        opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        session = std::make_unique<Ort::Session> (
            env, modelFile.getFullPathName().toWideCharPointer(), opts);

        inputName  = session->GetInputNameAllocated (0, allocator).get();
        outputName = session->GetOutputNameAllocated (0, allocator).get();
        return true;
       #else
        juce::ignoreUnused (modelFile);
        return false;
       #endif
    }
};
#else
struct CrepeAnalyzer::Impl {};
#endif

#if VB_HAS_ONNX && JUCE_WINDOWS
// Build the ORT session. Catches C++ exceptions internally; only a hard SEH
// fault (incompatible DLL) escapes — caught by buildSessionSafe. Raw pointer so
// it can be called from a __try function. Member → can see the private Impl.
CrepeAnalyzer::Impl* CrepeAnalyzer::buildSessionChecked (const juce::File& modelFile) noexcept
{
    try
    {
        auto* p = new Impl();                    // constructs Ort::Env
        if (p->load (modelFile))                 // constructs Ort::Session
            return p;
        delete p;
    }
    catch (...) {}                               // Ort::Exception etc.
    return nullptr;
}

// SEH wrapper: an access violation from a corrupt/mismatched onnxruntime.dll is
// caught here and turned into a clean "no session" instead of crashing the host.
// No C++ unwinding objects live in this scope (required for __try).
CrepeAnalyzer::Impl* CrepeAnalyzer::buildSessionSafe (const juce::File& modelFile) noexcept
{
    __try { return buildSessionChecked (modelFile); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
#endif

// ============================================================================
CrepeAnalyzer::CrepeAnalyzer()
{
#if VB_HAS_ONNX
    const auto modelFile = findModelFile();
    if (! modelFile.existsAsFile())
    {
        status = "Model file not found - using DSP analysis";
        return;
    }
   #if JUCE_WINDOWS
    if (! ensureOrtLoaded())
    {
        status = "onnxruntime.dll missing/incompatible - using DSP analysis";
        return;
    }
    // Fully SEH-guarded: a bad DLL can never crash the host — worst case we run
    // the (perfectly good) DSP pitch analysis instead.
    if (auto* built = buildSessionSafe (modelFile))
    {
        impl.reset (built);
        status = "CREPE neural pitch model loaded";
    }
    else
    {
        status = "CREPE unavailable (onnxruntime.dll incompatible) - using DSP analysis";
    }
   #else
    try
    {
        auto candidate = std::make_unique<Impl>();
        if (candidate->load (modelFile))
        {
            impl = std::move (candidate);
            status = "CREPE neural pitch model loaded";
        }
    }
    catch (const std::exception& e)
    {
        impl.reset();
        status = juce::String ("ONNX init failed: ") + e.what();
    }
   #endif
#else
    status = "Built without ONNX support";
#endif
}

CrepeAnalyzer::~CrepeAnalyzer() = default;

bool CrepeAnalyzer::isAvailable() const noexcept
{
#if VB_HAS_ONNX
    return impl != nullptr && impl->session != nullptr;
#else
    return false;
#endif
}

// ============================================================================
std::vector<PitchFrame> CrepeAnalyzer::analyze (const float* audio16k, int numSamples,
                                                int hopSamples)
{
    std::vector<PitchFrame> result;
#if VB_HAS_ONNX
    if (! isAvailable() || numSamples < kFrameLen)
        return result;

    const int numFrames = 1 + (numSamples - kFrameLen) / hopSamples;
    result.reserve ((size_t) numFrames);

    constexpr int batchSize = 128;
    std::vector<float> batch ((size_t) batchSize * kFrameLen);

    const auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    const char* inNames[]  = { impl->inputName.c_str() };
    const char* outNames[] = { impl->outputName.c_str() };

    for (int start = 0; start < numFrames; start += batchSize)
    {
        const int count = std::min (batchSize, numFrames - start);

        // Preprocess: per-frame zero-mean / unit-std (CREPE convention)
        for (int f = 0; f < count; ++f)
        {
            const float* src = audio16k + (size_t) (start + f) * (size_t) hopSamples;
            float* dst = batch.data() + (size_t) f * kFrameLen;

            double mean = 0.0;
            for (int i = 0; i < kFrameLen; ++i) mean += src[i];
            mean /= kFrameLen;
            double var = 0.0;
            for (int i = 0; i < kFrameLen; ++i)
            {
                const double d = src[i] - mean;
                var += d * d;
            }
            const float invStd = (float) (1.0 / std::sqrt (var / kFrameLen + 1.0e-8));
            for (int i = 0; i < kFrameLen; ++i)
                dst[i] = (float) (src[i] - mean) * invStd;
        }

        const std::array<int64_t, 2> inShape { (int64_t) count, (int64_t) kFrameLen };
        auto inTensor = Ort::Value::CreateTensor<float> (
            memInfo, batch.data(), (size_t) count * kFrameLen,
            inShape.data(), inShape.size());

        auto outputs = impl->session->Run (Ort::RunOptions { nullptr },
                                           inNames, &inTensor, 1, outNames, 1);
        const float* probs = outputs[0].GetTensorData<float>();

        // Decode: weighted average of cents around the argmax bin
        for (int f = 0; f < count; ++f)
        {
            const float* p = probs + (size_t) f * kNumBins;
            int   best = 0;
            float bestV = p[0];
            for (int b = 1; b < kNumBins; ++b)
                if (p[b] > bestV) { bestV = p[b]; best = b; }

            PitchFrame frame;
            frame.timeSec    = (float) ((start + f) * hopSamples) / 16000.0f;
            frame.confidence = bestV;

            if (bestV >= kConfidenceGate)
            {
                const int lo = std::max (0, best - 4), hi = std::min (kNumBins - 1, best + 4);
                float wSum = 0.0f, wBin = 0.0f;
                for (int b = lo; b <= hi; ++b) { wSum += p[b]; wBin += p[b] * (float) b; }
                frame.f0Hz = wSum > 0.0f ? binToHz (wBin / wSum) : 0.0f;
            }
            result.push_back (frame);
        }
    }
#else
    juce::ignoreUnused (audio16k, numSamples, hopSamples);
#endif
    return result;
}

// ============================================================================
//  Snapshot refinement — replaces YIN-derived pitch/key stats with neural ones
// ============================================================================
void refineSnapshotWithPitchFrames (AnalysisSnapshot& s,
                                    const std::vector<PitchFrame>& frames)
{
    std::vector<float> voiced;
    voiced.reserve (frames.size());
    for (const auto& f : frames)
        if (f.f0Hz > 0.0f)
            voiced.push_back (f.f0Hz);

    if (voiced.size() < 16 || frames.empty())
        return;

    // Median pitch
    std::vector<float> sorted (voiced);
    std::sort (sorted.begin(), sorted.end());
    s.pitchMedianHz = sorted[sorted.size() / 2];
    s.voicedRatio   = (float) voiced.size() / (float) frames.size();

    // Stability: median absolute frame-to-frame deviation in cents
    std::vector<float> devs;
    devs.reserve (voiced.size());
    for (size_t i = 1; i < voiced.size(); ++i)
        devs.push_back (std::abs (1200.0f * std::log2 (voiced[i] / voiced[i - 1])));
    if (! devs.empty())
    {
        std::sort (devs.begin(), devs.end());
        s.pitchStabilityCents = devs[devs.size() / 2];
    }

    // Key detection (Krumhansl-Kessler), duration-weighted by frame count
    std::array<double, 12> hist {};
    for (float hz : voiced)
    {
        const int midi = (int) std::round (69.0f + 12.0f * std::log2 (hz / 440.0f));
        hist[(size_t) (((midi % 12) + 12) % 12)] += 1.0;
    }

    static constexpr std::array<double, 12> majP { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                                   2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr std::array<double, 12> minP { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                                   2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    auto correlate = [&hist] (const std::array<double, 12>& prof, int rot)
    {
        double hx = 0, px = 0;
        for (int i = 0; i < 12; ++i) { hx += hist[(size_t) i]; px += prof[(size_t) i]; }
        hx /= 12.0; px /= 12.0;
        double num = 0, dh = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = hist[(size_t) i] - hx;
            const double b = prof[(size_t) (((i - rot) % 12 + 12) % 12)] - px;
            num += a * b; dh += a * a; dp += b * b;
        }
        return (dh > 0 && dp > 0) ? num / std::sqrt (dh * dp) : 0.0;
    };

    double best = -2.0, second = -2.0;
    for (int root = 0; root < 12; ++root)
        for (int isMaj = 0; isMaj < 2; ++isMaj)
        {
            const double c = correlate (isMaj ? majP : minP, root);
            if (c > best)
            {
                second = best; best = c;
                s.keyRoot = root; s.keyIsMajor = (isMaj == 1);
            }
            else if (c > second)
                second = c;
        }
    s.keyConfidence = (float) juce::jlimit (0.0, 1.0, (best - second) * 4.0 + 0.2);
}
} // namespace vf
