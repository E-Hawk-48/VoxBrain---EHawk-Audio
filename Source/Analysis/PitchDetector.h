#pragma once
#include <vector>

namespace vf
{
// ============================================================================
//  PitchDetector — YIN fundamental-frequency estimator (de Cheveigné/Kawahara)
//  Optimised for monophonic vocals, 60–1200 Hz search range.
//  Feed mono samples continuously; when a full analysis window is available,
//  detect() returns the latest F0 estimate (0 = unvoiced/none).
// ============================================================================
class PitchDetector
{
public:
    void prepare (double sampleRate);

    /** Push a block of mono samples. Returns true when a fresh estimate exists. */
    bool push (const float* samples, int numSamples);

    /** Latest fundamental in Hz (0 if unvoiced). */
    float getFrequencyHz() const noexcept   { return currentHz; }

    /** Confidence 0..1 derived from the YIN aperiodicity measure. */
    float getConfidence() const noexcept    { return confidence; }

private:
    float runYin();

    double fs            = 44100.0;
    int    windowSize    = 2048;     // ~46 ms @ 44.1k — good for voice
    int    writePos      = 0;
    float  currentHz     = 0.0f;
    float  confidence    = 0.0f;

    static constexpr float yinThreshold = 0.15f;
    static constexpr float minHz        = 60.0f;
    static constexpr float maxHz        = 1200.0f;

    std::vector<float> buffer;       // circular accumulation
    std::vector<float> frame;        // linearised analysis frame
    std::vector<float> diff;         // YIN difference function
};
} // namespace vf
