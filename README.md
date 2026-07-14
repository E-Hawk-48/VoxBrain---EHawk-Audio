# VocalForge — AI Vocal Engineer (VST3)

One-button intelligent vocal mixing. Insert on a vocal track, click **LEARN**, play the
vocal, click again — VocalForge analyzes the recording and builds a complete
professional chain with a full written explanation of every decision.

**V1 status:** working auto-mix core. See the roadmap below for what comes next.

---

## Building (Windows, Visual Studio 2022)

Requirements: Visual Studio 2022 (Desktop C++ workload), CMake 3.22+, Git, internet
on first configure (JUCE 8.0.4 is fetched automatically).

```
Scripts\build_windows.bat
```

or manually:

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Output: `build\VocalForge_artefacts\Release\VST3\VocalForge.vst3` — JUCE also copies it
to the system VST3 folder automatically. A Standalone .exe is built too (useful for
testing with your mic without a DAW). First build takes several minutes (JUCE download + compile).

Tested-target DAWs: Cakewalk/Sonar, FL Studio, Ableton, Reaper, Studio One, Cubase, Bitwig.

## Using it

1. Insert VocalForge on your vocal track.
2. Click **LEARN** — the button pulses while listening.
3. Play the loudest / most representative section of the vocal (8–30 s is ideal).
4. Click again. In under a second the AI engineer:
   - gain-stages the vocal to -18 dB internal level
   - sets the gate from the measured noise floor
   - EQs out rumble and mud, adds presence/air based on measured brightness
   - de-esses proportionally to measured sibilance
   - compresses according to measured crest factor (dynamics)
   - adds saturation, delay and reverb sized to the delivery style (melodic vs rhythmic)
   - limits to -1 dBTP
5. Read the full decision report (every move + why) and tweak anything — all
   parameters stay live, automatable, and host-savable.

## Architecture

```
Source/
  PluginProcessor.*      host integration, LEARN workflow, APVTS
  Parameters.*           every automatable parameter (stable IDs)
  Analysis/
    PitchDetector.*      YIN F0 estimation (voice-optimized)
    LoudnessMeter.*      EBU R128 / BS.1770-4 gated LUFS
    AnalysisEngine.*     FFT spectral bands, sibilance, noise floor,
                         dynamics, pitch stats → AnalysisSnapshot
  DSP/
    VocalChain.*         Gate → EQ → De-esser → Compressor → Saturation
                         → Delay → Reverb → Limiter (all real-time safe)
  Brain/
    AutoMixBrain.*       expert-system: AnalysisSnapshot → chain settings
                         + generated preset name + rationale report
  UI/                    dark-glass theme, live spectrum, module cards
```

Design rules used throughout: no allocation/locks on the audio thread, atomics for
cross-thread data, parameters read lock-free per block, analysis runs pre-chain.

## Phase 2 — Pitch correction (DONE)

- **PITCH module** at the head of the chain: TD-PSOLA-family pitch-synchronous
  resynthesis (formant-preserving), incremental YIN tracking, scale quantizer.
- **Key/scale**: set manually or leave on **Auto** — the LEARN pass detects the key
  via Krumhansl-Schmuckler analysis of the pitch-class histogram.
- **Retune Speed** (0 ms = hard-tune / T-Pain, 100+ ms = transparent) and
  **Correction amount**. The AI sets both from measured pitch drift.
- The engine adds ~30 ms of latency (reported to the host, so your DAW
  compensates automatically; timing stays aligned in the mix).

## Phase 3 — Neural analysis via ONNX Runtime (DONE)

- **ONNX Runtime 1.20.1** embedded (fetched automatically at configure time;
  onnxruntime.dll is delay-loaded and shipped inside the plugin bundle).
- **CREPE neural pitch model** (`Models/crepe-tiny.onnx`, 2.2 MB) analyzes the
  LEARN recording on a background thread: far more robust than DSP tracking on
  breathy/noisy vocals → sharper key detection and drift measurement. Verified
  in-repo: 110–880 Hz test tones detected within a few cents.
- The real-time retune path stays DSP (neural nets are too slow for per-sample
  latency); the AI improves the *decisions*, DSP does the *processing*.
- Fully graceful: if the model or DLL is missing, everything falls back to DSP.
  The analysis report states which engine was used.

## Phase 4 — Chat assistant (DONE)

Type instructions to the engineer in the box under the analysis report:
"darker", "way more air", "less autotune", "hard tune", "warmer", "punchier",
"more reverb", "intimate", "vintage", "radio ready", "telephone", "expensive",
"more emotion", "reset" — with intensity ("slightly", "way more") and negation
("less", "too", "remove") understood. Runs locally, instantly, fully offline;
every edit goes through the normal parameters so the host sees automation.

## Roadmap (agreed phasing)

- **P5 — More modules**: dynamic EQ, multiband comp, doubler, de-breath, de-reverb.
- **P6 — Product**: installer, presets library, licensing, auto-update, AU/AAX/CLAP.

## Known V1 limitations (honest list)

- Reverb/limiter use JUCE's built-in engines (solid, not boutique — custom engines are roadmap).
- Analysis requires the LEARN pass; no continuous background re-mixing yet.
- No pitch correction yet (P2).
- Mono/stereo only; no sidechain input.
