#include "PitchTracker.h"
#include <cmath>
#include <algorithm>

namespace vf
{
namespace
{
    // Transition model: how likely a move of `semis` semitones between adjacent
    // frames is. Musical moves are small; a full octave in one 6 ms hop is almost
    // always an ANALYSIS error rather than a performance. The heavy tail (the
    // added floor) is what still lets a genuine leap or a new note through once
    // the evidence for it persists for a few frames.
    float transitionLogProb (float semis)
    {
        const float a = std::abs (semis);
        const float local = std::exp (-0.5f * (a / 1.4f) * (a / 1.4f));   // smooth, near-by
        const float leap  = 0.012f;                                       // rare but possible
        return std::log (local + leap);
    }

    constexpr float kLogEps = -30.0f;
}

// ============================================================================
void PitchTracker::prepare (const Config& c)
{
    cfg = c;
    cfg.decodeLag  = juce::jlimit (0, kMaxLag, cfg.decodeLag);
    cfg.hopSize    = std::max (32, cfg.hopSize);
    cfg.minHz      = juce::jlimit (30.0f, 400.0f, cfg.minHz);
    cfg.maxHz      = juce::jlimit (cfg.minHz * 2.0f, 2000.0f, cfg.maxHz);

    // The window must hold at least two periods of the lowest pitch we chase,
    // otherwise the CMND dip for that pitch doesn't exist to be found.
    const int needed = (int) std::ceil (2.5 * cfg.sampleRate / cfg.minHz);
    cfg.windowSize = std::max (cfg.windowSize, needed);

    maxTau = std::min (cfg.windowSize / 2 - 1, (int) (cfg.sampleRate / cfg.minHz));
    minTau = std::max (2, (int) (cfg.sampleRate / cfg.maxHz));

    ring.assign ((size_t) (cfg.windowSize * 2), 0.0f);
    frameBuf.assign ((size_t) cfg.windowSize, 0.0f);
    diff.assign ((size_t) (maxTau + 2), 0.0f);
    cumulative.assign ((size_t) (maxTau + 2), 0.0f);
    slots.assign ((size_t) (cfg.decodeLag + 1), FrameSlot{});
    activeLag    = cfg.decodeLag;     // runtime values start at the allocated max
    activeMaxTau = maxTau;

    reset();
}

void PitchTracker::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringWrite = 0; ringFill = 0; hopCounter = 0;
    slotWrite = 0; slotsFilled = 0;
    for (auto& s : slots) { s.numCands = 0; s.unvoicedLogProb = 0.0f; s.rms = 0.0f; }
    noiseFloor = 1.0e-4f;
    levelEnv = 0.0f;
    decoded = Frame{};
    lastVoicedHz = 0.0f;
}

// ---------------------------------------------------------------------------
//  Real-time-safe mode change (see the header). Only narrows what is USED.
// ---------------------------------------------------------------------------
void PitchTracker::setRuntimeMode (float newMinHz, int newDecodeLag) noexcept
{
    // Raising the floor shortens the lag search; it can never need a bigger
    // buffer than prepare() already allocated.
    const int wantTau = (int) (cfg.sampleRate / juce::jmax (20.0f, newMinHz));
    const int newMaxTau = juce::jlimit (minTau + 2, maxTau, wantTau);

    const int newLag = juce::jlimit (0, (int) slots.size() - 1, newDecodeLag);

    if (newMaxTau == activeMaxTau && newLag == activeLag)
        return;                       // nothing to do

    activeMaxTau = newMaxTau;
    activeLag    = newLag;

    // The decode window's contents refer to the old configuration, so restart
    // accumulation cleanly rather than mixing two geometries in one Viterbi pass.
    slotWrite = 0;
    slotsFilled = 0;
    for (auto& sl : slots) { sl.numCands = 0; sl.unvoicedLogProb = 0.0f; sl.rms = 0.0f; }
}

// ============================================================================
bool PitchTracker::process (const float* samples, int numSamples)
{
    bool produced = false;
    const int ringSize = (int) ring.size();

    for (int i = 0; i < numSamples; ++i)
    {
        ring[(size_t) ringWrite] = samples[i];
        ringWrite = (ringWrite + 1) % ringSize;
        if (ringFill < ringSize) ++ringFill;

        if (++hopCounter >= cfg.hopSize)
        {
            hopCounter = 0;
            if (ringFill >= cfg.windowSize)
            {
                analyseFrame();
                decodeViterbi();
                produced = true;
            }
        }
    }
    return produced;
}

// ============================================================================
//  One analysis frame: linearise → CMND → candidates → observation weights
// ============================================================================
void PitchTracker::analyseFrame()
{
    const int N = cfg.windowSize;
    const int ringSize = (int) ring.size();

    // Newest N samples, oldest first.
    int start = ringWrite - N;
    if (start < 0) start += ringSize;
    for (int i = 0; i < N; ++i)
        frameBuf[(size_t) i] = ring[(size_t) ((start + i) % ringSize)];

    // Level + adaptive noise floor (drives voicing, and lets a quiet falsetto
    // note still be found while a whisper is correctly called unvoiced).
    double e = 0.0;
    for (int i = 0; i < N; ++i) e += (double) frameBuf[(size_t) i] * frameBuf[(size_t) i];
    const float rms = (float) std::sqrt (e / (double) N);

    levelEnv = std::max (rms, 0.85f * levelEnv);
    // The floor tracks quiet passages quickly but only creeps upward, so a long
    // sustained note never gets mistaken for background.
    noiseFloor = rms < noiseFloor ? 0.90f * noiseFloor + 0.10f * rms
                                  : 0.9995f * noiseFloor + 0.0005f * rms;
    noiseFloor = std::max (noiseFloor, 1.0e-6f);

    auto& slot = slots[(size_t) slotWrite];
    slot.numCands = 0;
    slot.rms = rms;

    if (rms < 1.0e-5f)                       // digital silence — definitively unvoiced
    {
        slot.unvoicedLogProb = 0.0f;         // log(1)
        slotWrite = (slotWrite + 1) % (int) slots.size();
        if (slotsFilled < (int) slots.size()) ++slotsFilled;
        return;
    }

    // ---- difference function (YIN step 1-2) ---------------------------
    const int limit = N - activeMaxTau;
    diff[0] = 0.0f;
    for (int tau = 1; tau <= activeMaxTau; ++tau)
    {
        float sum = 0.0f;
        const float* a = frameBuf.data();
        const float* b = frameBuf.data() + tau;
        // Simple, vectorisable loop (the compiler auto-vectorises this with
        // -mavx2; kept branch-free deliberately).
        for (int k = 0; k < limit; ++k)
        {
            const float d = a[k] - b[k];
            sum += d * d;
        }
        diff[(size_t) tau] = sum;
    }

    // ---- cumulative mean normalisation (YIN step 3) -------------------
    cumulative[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= activeMaxTau; ++tau)
    {
        running += diff[(size_t) tau];
        cumulative[(size_t) tau] = running > 0.0f
                                 ? diff[(size_t) tau] * (float) tau / running
                                 : 1.0f;
    }

    extractCandidates();

    // ---- voicing (unvoiced state's observation probability) -----------
    // Combine periodicity (best candidate) with how far the frame sits above the
    // adaptive floor. Sensitivity biases the trade: low = only clear pitch.
    float bestCmnd = 1.0f;
    for (int i = 0; i < slot.numCands; ++i)
        bestCmnd = std::min (bestCmnd, slot.cand[i].cmnd);

    const float snr = rms / std::max (noiseFloor, 1.0e-6f);
    const float levelTerm = juce::jlimit (0.0f, 1.0f, (std::log10 (std::max (snr, 1.0f)) / 1.2f));
    const float periodicity = juce::jlimit (0.0f, 1.0f, 1.0f - bestCmnd);

    // pVoiced rises with both periodicity and level; sensitivity shifts the knee.
    const float knee = 0.62f - 0.22f * cfg.sensitivity;
    float pVoiced = juce::jlimit (0.001f, 0.999f,
                                  (periodicity * 0.75f + levelTerm * 0.25f - knee) * 3.2f + 0.5f);

    slot.unvoicedLogProb = std::log (1.0f - pVoiced);
    for (int i = 0; i < slot.numCands; ++i)
        slot.cand[i].logProb += std::log (pVoiced);

    slotWrite = (slotWrite + 1) % (int) slots.size();
    if (slotsFilled < (int) slots.size()) ++slotsFilled;
}

// ============================================================================
float PitchTracker::refineTau (int tauInt) const
{
    // Parabolic vertex, clamped to one bin — an almost-flat parabola can
    // otherwise fling the estimate far away and spike the reported pitch.
    if (tauInt <= 0 || tauInt >= activeMaxTau) return (float) tauInt;
    const float s0 = cumulative[(size_t) (tauInt - 1)];
    const float s1 = cumulative[(size_t) tauInt];
    const float s2 = cumulative[(size_t) (tauInt + 1)];
    const float denom = 2.0f * s1 - s2 - s0;
    if (std::abs (denom) < 1.0e-12f) return (float) tauInt;
    const float off = 0.5f * (s2 - s0) / denom;
    return (float) tauInt + juce::jlimit (-1.0f, 1.0f, off);
}

void PitchTracker::extractCandidates()
{
    auto& slot = slots[(size_t) slotWrite];

    // ---- every local minimum is a candidate ---------------------------
    // Taking only the first threshold crossing (classic YIN) throws away the
    // true F0 whenever a sub-harmonic dips slightly lower — the root cause of
    // octave errors on rich voices.
    struct Raw { int tau; float v; };
    Raw raw[kMaxCandidates * 3];
    int nRaw = 0;

    for (int tau = minTau + 1; tau < activeMaxTau && nRaw < (int) (sizeof (raw) / sizeof (raw[0])); ++tau)
    {
        const float v = cumulative[(size_t) tau];
        if (v < 0.62f
            && v <= cumulative[(size_t) (tau - 1)]
            && v <= cumulative[(size_t) (tau + 1)])
        {
            raw[nRaw++] = { tau, v };
            // step past this dip so one wide minimum isn't counted repeatedly
            while (tau + 1 < activeMaxTau && cumulative[(size_t) (tau + 1)] <= cumulative[(size_t) tau])
                ++tau;
        }
    }

    if (nRaw == 0)
    {
        // Nothing convincing: keep the single global minimum as a weak candidate
        // so soft/breathy notes can still be carried by the temporal model.
        int bestTau = -1; float bestV = 1.0e9f;
        for (int tau = minTau; tau <= activeMaxTau; ++tau)
            if (cumulative[(size_t) tau] < bestV) { bestV = cumulative[(size_t) tau]; bestTau = tau; }
        if (bestTau > 0 && bestV < 0.85f)
            raw[nRaw++] = { bestTau, bestV };
    }

    // ---- sub-harmonic (octave-down) demotion ---------------------------
    // If a candidate's period is ~an integer multiple of a shorter candidate
    // that is nearly as periodic, the shorter one is the true fundamental and
    // this one is its octave/sub-harmonic image.
    for (int i = 0; i < nRaw; ++i)
    {
        for (int j = 0; j < nRaw; ++j)
        {
            if (i == j || raw[j].tau >= raw[i].tau) continue;
            const float ratio = (float) raw[i].tau / (float) raw[j].tau;
            const float nearest = std::round (ratio);
            if (nearest >= 2.0f && std::abs (ratio - nearest) < 0.06f
                && raw[j].v < raw[i].v + 0.16f)
            {
                raw[i].v += 0.28f;      // demote the sub-harmonic
                break;
            }
        }
    }

    // ---- keep the best few, as probabilities ---------------------------
    std::sort (raw, raw + nRaw, [] (const Raw& a, const Raw& b) { return a.v < b.v; });
    const int keep = std::min (nRaw, kMaxCandidates);

    for (int i = 0; i < keep; ++i)
    {
        const float tauF = refineTau (raw[i].tau);
        if (tauF <= 0.0f) continue;
        const float hz = (float) cfg.sampleRate / tauF;
        if (hz < cfg.minHz || hz > cfg.maxHz) continue;

        auto& c = slot.cand[slot.numCands];
        c.tau  = tauF;
        c.hz   = hz;
        c.cmnd = juce::jlimit (0.0f, 1.0f, raw[i].v);
        // Deeper dip = more periodic = higher observation probability.
        c.logProb = std::log (std::max (1.0e-6f, std::pow (1.0f - c.cmnd, 2.0f)));
        if (++slot.numCands >= kMaxCandidates) break;
    }
}

// ============================================================================
//  Viterbi over the stored window (candidates + one unvoiced state per frame).
//  This is what turns per-frame guesses into a stable melodic line: an octave
//  slip or a one-frame flip must out-score the continuity of everything around
//  it, which it almost never can.
// ============================================================================
void PitchTracker::decodeViterbi()
{
    const int L = activeLag + 1;
    if (slotsFilled < L)
    {
        // Warm-up: fall back to the best current candidate so the engine has
        // something usable immediately after a reset.
        const int cur = (slotWrite - 1 + (int) slots.size()) % (int) slots.size();
        const auto& s = slots[(size_t) cur];
        if (s.numCands > 0 && s.unvoicedLogProb < std::log (0.5f))
        {
            decoded.hz = s.cand[0].hz;
            decoded.confidence = 1.0f - s.cand[0].cmnd;
            decoded.voiced = true;
            lastVoicedHz = decoded.hz;
        }
        else decoded = Frame{};
        return;
    }

    // Oldest → newest order of the ring.
    int order[kMaxLag + 1];
    const int ns_ = (int) slots.size();
    for (int i = 0; i < L; ++i)
        order[i] = (slotWrite + ns_ - L + i) % ns_;

    // States per frame: [0] = unvoiced, [1..n] = candidates.
    float score[kMaxLag + 1][kMaxCandidates + 1];
    int   back [kMaxLag + 1][kMaxCandidates + 1];

    auto stateHz = [&] (int f, int st) -> float
    {
        const auto& s = slots[(size_t) order[f]];
        return st == 0 ? 0.0f : s.cand[st - 1].hz;
    };
    auto stateObs = [&] (int f, int st) -> float
    {
        const auto& s = slots[(size_t) order[f]];
        return st == 0 ? s.unvoicedLogProb : s.cand[st - 1].logProb;
    };
    auto numStates = [&] (int f) { return slots[(size_t) order[f]].numCands + 1; };

    for (int st = 0; st < numStates (0); ++st)
    {
        score[0][st] = stateObs (0, st);
        back[0][st]  = 0;
    }

    for (int f = 1; f < L; ++f)
    {
        const int ns = numStates (f), ps = numStates (f - 1);
        for (int st = 0; st < ns; ++st)
        {
            float best = -1.0e30f; int bestPrev = 0;
            const float hzNow = stateHz (f, st);

            for (int pv = 0; pv < ps; ++pv)
            {
                const float hzPrev = stateHz (f - 1, pv);
                float trans;

                if (st == 0 && pv == 0)       trans = std::log (0.90f);  // stay unvoiced
                else if (st == 0 || pv == 0)  trans = std::log (0.10f);  // voicing change
                else
                {
                    const float semis = 12.0f * std::log2 (std::max (hzNow, 1.0f)
                                                         / std::max (hzPrev, 1.0f));
                    trans = transitionLogProb (semis);
                }

                const float v = score[f - 1][pv] + trans;
                if (v > best) { best = v; bestPrev = pv; }
            }

            score[f][st] = best + stateObs (f, st);
            back[f][st]  = bestPrev;
        }
    }

    // Best path end, then walk back to the frame we are reporting (the oldest in
    // the window) — that frame's state has now been chosen with full hindsight.
    int bestEnd = 0; float bestVal = -1.0e30f;
    for (int st = 0; st < numStates (L - 1); ++st)
        if (score[L - 1][st] > bestVal) { bestVal = score[L - 1][st]; bestEnd = st; }

    int path[kMaxLag + 1];
    path[L - 1] = bestEnd;
    for (int f = L - 1; f > 0; --f)
        path[f - 1] = back[f][path[f]];

    const int reportFrame = 0;                     // oldest in the window
    const int st = path[reportFrame];
    const auto& s = slots[(size_t) order[reportFrame]];

    if (st == 0 || s.numCands == 0)
    {
        decoded.hz = 0.0f;
        decoded.confidence = 0.0f;
        decoded.voiced = false;
    }
    else
    {
        const auto& c = s.cand[st - 1];
        decoded.hz = c.hz;
        decoded.confidence = juce::jlimit (0.0f, 1.0f, 1.0f - c.cmnd);
        decoded.voiced = true;
        lastVoicedHz = c.hz;
    }
}
} // namespace vf
