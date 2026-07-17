# VoxBrain — Project Handoff / Session Context

AI-powered vocal processing VST3 plugin (Windows, JUCE 8, C++20). One-button
auto-mix: user clicks LEARN, sings, clicks again → analysis → expert-system
brain builds the whole chain and explains every decision. Long-term vision:
rival Auto-Tune Pro + Nectar + RX in one plugin. Built phase by phase.

## Who you're working with

The user is a music producer, NOT a programmer. They build by double-clicking
`Scripts\build_windows.bat` and test by ear in the Standalone app or their DAW.
Give plain-language instructions, one step at a time. They report results like
"it worked" / "build failed" / "sounds choppy" — read `build_log.txt` in this
folder yourself (they'll rerun the script when asked). Iterate fast.

## Status: Phases 1–5 COMPLETE & user-verified; Phases 6–7 code-complete

1. **Auto-mix core** — analysis engine + DSP chain + rule-based brain.
2. **Pitch correction** — real-time PSOLA-family retune, auto key detection.
3. **Neural analysis** — ONNX Runtime + CREPE model refines LEARN analysis.
4. **Chat assistant** — local production-vocabulary engine ("darker", "hard tune").
5. **Dynamic EQ + Multiband comp** — user-verified working (2026-07-13). Both
   default OFF so Phases 1–4 sound is unchanged until the brain (or user)
   enables them; brain enables conditionally with explained settings.
6. **Preset system + A/B + Undo** — code-complete, syntax-verified on Linux;
   **awaiting user test**. Workflow layer only (no DSP change).
7. **Lockable modules** — code-complete, syntax-verified on Linux. Per-module
   `<mod>_lock` bool params (default off). AI Auto-Mix (`applyBrainResult`) and
   AI Engineer/chat (`applyMove`, `reset`) skip locked modules; report/chat say
   what was left untouched. UI: LOCK pill per card + drawn padlock + accent
   border + knobs disabled (bypass stays live). Saved with state/presets
   automatically. `vf::param::lockParamFor(id)` maps any param → its lock.

## Flagship roadmap (user-provided, 21 items) — build in verified increments
DONE: #1 Lockable modules; #20 Saturation models (8 algorithms); #21 Reverb
(FDN, 7 algorithms); #12 Auto-update (in-plugin updater + server tooling +
Inno Setup installer); macOS port SCAFFOLDED (cross-platform CMake + build/
package scripts + cloud-Mac CI — needs a Mac/CI to produce binaries).
REMAINING (grouped):
- **Modular graph** (huge, foundational): #2 drag-drop order, #4 add/dup/delete
  modules, #3 signal-flow viz, #18 routing & sends. Converts the fixed serial
  chain into a dynamic module list/graph — do FIRST if going that direction.
- **DSP module expansions** (self-contained, audible): #13 BPM-sync delay,
  #14 ping-pong delay, #16 chorus, #5/#15 dedicated pitch module + units,
  #17 formant module.  (#20 saturation, #21 reverb DONE.)
- **UI/UX polish**: #6 AI Engineer chat panel, #7 spectrum analyzer (Pro-Q tier),
  #8 spectrum cursor readout, #9 universal typography, #10 simple/advanced modes,
  #11 built-in help.
- **Platform/quality**: #19 DSP quality audit.  (#12 auto-update DONE.)
  Still TODO for full macOS: enable ONNX/CREPE on macOS (currently VB_HAS_ONNX=0
  → DSP-only analysis on Mac); code-signing + notarisation for a warning-free
  install; actually building the Mac binaries (no Mac in this project — use the
  friend's Mac or the GitHub Actions macOS runner).
NOTE: NEVER dump many of these at once (user forbids placeholders); one complete,
verified feature per session, same as #1.

## Architecture (Source/)

- `PluginProcessor.*` — host glue. LEARN flow: GUI `setLearning(true/false)` →
  audio thread accumulates → `finishLearning()` snapshot → 10 Hz timer picks it
  up on message thread → CREPE refinement on `aiPool` background thread (if
  model available) → `finishAutoMix()` runs brain → writes params via APVTS.
  Also records LEARN audio at 16 kHz mono (`learn16k`, preallocated) for CREPE.
- `Parameters.*` — every parameter ID (stable strings, never rename). APVTS.
- `Analysis/AnalysisEngine.*` — FFT bands, sibilance, noise floor (10th
  percentile frame RMS), crest, brightness (log centroid), YIN pitch stats,
  Krumhansl-Schmuckler key detection. Produces `AnalysisSnapshot`.
- `Analysis/PitchDetector.*` — YIN (46 ms window) for UI/learn.
- `Analysis/LoudnessMeter.*` — EBU R128 / BS.1770 gated LUFS.
- `DSP/VocalChain.*` — Retune → Gate → EQ (HPF/low shelf/mud/presence/air) →
  **DynamicEq** → De-esser (Linkwitz-Riley split, 6:1) → Compressor (soft knee,
  parallel mix) → **MultibandCompressor** → Saturator (8-model, see below) →
  SlapDelay → ReverbEngine (FDN, see below) → Limiter (juce). Params read
  lock-free per block from APVTS raw pointers.
  - **Saturator** (P#20): 8 models via `satType` choice — Tube/Tape/Console/
    Transformer/Germanium/Diode (waveshapers), Exciter (HP→tanh→add), Lo-Fi
    (sample-hold decimate + bit-crush + clip). `satBias` adds asymmetry (DC
    removed by subtracting shapeCore(bias)); `satTone` is a one-pole tilt;
    `satHQ` picks 4× vs 2× oversampling (two prepared `Oversampling` objects);
    Lo-Fi runs at BASE rate so decimation aliases on purpose. All curves bounded
    (verified: 0 NaN, maxAbs<14 over drive/bias/freq/amp sweep). Brain picks the
    model from brightness/crest; chat: "tube","tape machine","console","fuzz",
    "exciter","cassette", etc. SAT card gained a Model combo + Tone knob.
  - **ReverbEngine** (P#21): replaced the juce::dsp::Reverb wrapper with a real
    FDN. Signal: input HPF/LPF (`verbLowCut`/`verbHighCut`) → pre-delay
    (`verbPredelay` ms) → 4 series Schroeder diffusion allpasses (`verbDiffusion`)
    → 4-line FDN with a 0.5·Hadamard feedback matrix (energy-preserving → stable
    for decay<1), per-line sine-modulated fractional delay (`verbModDepth`) and
    an in-loop one-pole damping (`verbDamp`) → mid/side `verbWidth`. `verbType`
    (Room/Hall/Plate/Spring/Cathedral/Shimmer/Bloom) is a recipe of size/diff/
    damp/mod/decay/predelay multipliers. `verbShimmer` = octave-up two-tap
    crossfade pitch-shifter fed back (decay auto-reduced to `0.88−0.30·shim` for
    loop headroom). `verbFreeze` = infinite (g≈0.9995, input muted). `verbDuck`
    ducks the wet under the dry envelope; Bloom type adds an attack swell. All
    buffers preallocated in prepare (RT-safe). VERIFIED: compiles vs juce_dsp;
    offline — 0 NaN, bounded, impulse decays to 0, freeze sustains (no runaway).
    Brain picks type + send tone from delivery/brightness; REVERB card gained a
    Type combo + Decay knob.
  - **DynamicEq** (P5): 3 downward bands (low-mid boom / mid honk / presence
    harsh). Each isolates its band with a unity-gain (0 dB peak) band-pass
    detector and outputs `input − f·band` where f rises with how far the band
    env exceeds threshold (capped at `rangeDb`). Phase-coherent, transparent
    when idle, and NEVER recomputes IIR coeffs on the audio thread (only when a
    band freq param changes). Zero added latency.
  - **MultibandCompressor** (P5): 3-way Linkwitz-Riley split (lp1/hp1 @ lowXover,
    lp2/hp2 @ highXover), each band soft-knee compressed (shared ratio, per-band
    attack/release: low slow → high fast) with per-band makeup/tonal gain, then
    summed. Naive 3-way LR has minor crossover phase ripple — acceptable, module
    is OFF by default. Scratch buffers preallocated in prepare. Zero latency.
  - Brain enables both CONDITIONALLY (boomy/nasal/harsh → dyneq; very dynamic or
    spectrally uneven → multiband) with calibrated, explained settings. Thresholds
    are calibrated from `stagedRms + <band>Db` approximations; user-verified to
    sound good with the default gentle ranges.
  - Chat vocab added: "glue"/"multiband", "resonance"/"nasal"/"honky",
    "smoother"/"tame highs". UI: two new cards (DYN EQ, MULTIBAND); ModuleStrip
    now lays out in TWO weighted rows; editor default window 1120×760.
- `DSP/RetuneEngine.*` — THE most delicate code. Pitch-synchronous OLA
  (TD-PSOLA family): incremental YIN (30 ms window, hop/4) with octave-jump
  debounce + voicing hysteresis; scale quantizer; grains 2 periods long,
  FRACTIONAL centers, respaced by period/ratio; window-sum normalization;
  unvoiced audio passes through the SAME grain pipeline at ratio 1 with source
  locked to ideal (NEVER crossfade wet against dry — phase misalignment combs;
  this caused "choppy" bugs, fixed). Fixed latency ~32 ms reported to host;
  bypass still delays dry so toggling never shifts timing. Grains must never
  write behind the emission point (stale-slot crackle bug, fixed).
  UPGRADE (verified): (1) 10 scales via `scaleMaskFor` + `RetuneParams.scaleType`
  (Chromatic, Major, Nat/Harm Minor, Dorian, Mixolydian, Phrygian, Maj/Min
  Pentatonic, Blues) — the Scale menu now has all of them, mapped in
  readChainParams. (2) `humanize` (pitch_humanize %): quantizes a blend of the
  instantaneous pitch and a slow pitch centre (f0Center EMA, slower than
  vibrato), then re-adds the natural deviation scaled by humanize → hard-tune at
  0, vibrato-preserving at 1. (3) `formant` (pitch_formant, ±5 st): grain content
  resampled around its centre in fireGrain (window stays output-domain) → shifts
  the spectral envelope while pitch stays put; availability clamp widened by
  formantSpan. (4) Better tracking: median-of-3 over VOICED-only YIN estimates
  (unvoiced zeros are NOT pushed into the history — that pollution glitched the
  pitch on consonant transitions = the "choppy/glitchy" bug; primed on note
  onset, `histPrimed`). VERIFIED: offline glitch test — a continuous tone stays
  continuous (out sample-jump 0.029 = natural) and faded voiced/unvoiced
  transitions add ZERO discontinuity; plus 445 Hz→440 (A4) chromatic snap,
  off-scale note→nearest C-major tone, all scales/humanize/formant finite,
  bypass + latency intact.
- `UI/PitchDisplay.*` — live auto-tune visualizer (30 Hz): scrolling detected
  (dim) vs corrected (accent) pitch traces on a semitone grid with C-octave
  labels + a big current-note read-out. Reads RetuneEngine's lock-free
  getInputPitchHz/getTargetPitchHz. Sits between the spectrum and preset bar
  (120 px strip). Compiles clean vs JUCE.
- GENRE-AWARE BRAIN (verified): `AutoMixBrain` now infers a vocal Style
  (`inferStyle`: Rap/Pop/R&B/Rock/Ballad/Spoken from voicedRatio + pitch
  stability + crest + brightness — no tempo needed) and biases the chain toward
  it: retune speed/amount + the new `pitch_humanize` (Pop/Rap tight, R&B/Ballad/
  Rock keep vibrato), reverb type/size/mix + delay (later same-param decisions
  override the generic ones), and saturation. Style shows in the preset name +
  report. VERIFIED: 8/8 style-classification cases correct; AutoMixBrain.cpp
  object-compiles vs JUCE. Implemented rack modules now number 24 (added
  `SpaceModules.*`: Hall Reverb [juce::dsp::Reverb], Digital Delay [tone+fb],
  Ping-Pong Delay [cross-feed], Doubler [detuned L/R short delays] — all
  runtime-verified finite through their tails).
- `Brain/AutoMixBrain.*` — pure function `AnalysisSnapshot → decisions +
  rationale strings + generated preset name`. Encodes real engineering rules
  (gain-stage to -18 dB RMS, gate 6 dB above noise floor, mud cut, brightness-
  driven presence/air, sibilance-ratio de-ess depth, crest-driven compression,
  melodic-vs-rhythmic space, key/stability-driven retune settings).
- `AI/CrepeAnalyzer.*` — ONNX Runtime wrapper (pimpl hides ORT types). Model:
  `Models/crepe-tiny.onnx` (2.2 MB, input {n,1024} @16 kHz normalized frames,
  output {n,360} sigmoid bins; cents = 1997.3794 + 20·bin, f = 10·2^(c/1200),
  weighted-avg decode ±4 bins, confidence gate 0.5). Verified accurate
  110–880 Hz. `refineSnapshotWithPitchFrames()` overwrites pitch/key stats.
- `Chat/ChatEngine.*` — data-driven rule table: synonyms → param deltas/macros,
  intensity ("slightly"=0.5×, "way"=1.7×), negation ("less/too/remove"),
  longest-synonym-first matching. Applied via APVTS (automation-visible).
  EXPANDED (verified compile): new auto-tune controls ("more human"/"humanize"
  → pitch_humanize; "deeper/higher voice", "chipmunk" → pitch_formant; "minor
  key"/"harmonic minor"/"pentatonic"/"blues" → pitch_scale index; "robotic tune"
  → hard-lock); richer natural phrases ("make it pop"/"3d", "tuck it back",
  "thin", "de-breath", "double it"); and GENRE one-shot presets (absolute
  FULL-SENTENCE PARSING (verified): handleMessage now segments the request into
  clauses (split on commas/periods/"and"/"then"/"but"/"with"/…) and matches +
  scopes modifiers PER CLAUSE, so compound commands like "make it more emo-trap,
  less reverb, better autotune and brighter" apply all four correctly (pure-logic
  test: 4 clauses → EMO + REVERB(inverted) + AUTOTUNE(boosted) + BRIGHT). Richer
  modifiers: "better/more/tighter/cleaner/stronger" → 1.35× boost; added
  gentler/softer, drop/ease/pull to the down set. Genre one-shots (absolute
  multi-param): "rap"/"trap", "drill", "pop vocal", "rnb"/"neo soul", "rock",
  "country"/"folk", "gospel". Genre presets set eq/comp/deess/sat/pitch/reverb
  targets in one command. (Genre here = user-driven chat presets; a genre-aware
  LEARN brain that auto-detects style is a future step.)
- `Preset/PresetManager.*` (P6) — session workflow layer over the APVTS.
  Snapshots = `map<paramID, normalised 0..1>` (round-trips every param type via
  getValue/setValueNotifyingHost). Undo/redo stacks (cap 32): processor calls
  `pushUndo()` BEFORE each auto-mix (`finishAutoMix`) and each chat batch
  (`applyChatMessage`), so one Undo reverts the whole change. A/B slots
  auto-clone on first divergence. Factory presets = partial natural-unit
  overrides applied on top of `defaults()` (via convertTo0to1). User presets =
  `<userAppData>/VoxBrain/Presets/*.vbpreset` (flat XML of normalised values;
  unspecified params reset to default on load). `onStateChanged` → UI refresh.
  All message-thread. NOTE: chat now routes through `processor.applyChatMessage`
  (not ChatEngine directly) so undo is captured — keep that path.
- PRESET ECOSYSTEM (new — `Preset/PresetMeta.*`, `PresetApply.cpp`,
  `PresetMorph.*`, `FactoryPresetLibrary.*`, `AiPresetGenerator.*`,
  `PresetLibrary.*`, `Marketplace.h`). Layers on top of PresetManager without
  changing it. `PresetMeta` = rich first-class metadata (id/creator/version/
  compat/genre/mood/energy/vocal type/mic+rec recs/cpu+latency est/tags/AI
  confidence+summary/downloads/likes/rating/featured/contentHash/signature).
  `Preset` = meta + sparse NATURAL-unit `values` + optional rack XML; `.vbpreset`
  v2 = `<VoxBrainPreset><Meta/><Values/><Rack/></>`. SECURITY: SHA-256
  `computeContentHash` (juce_cryptography) → `seal()`/`verifyIntegrity()` (tamper
  detection), duplicate detection by hash, safe XmlDocument parse, semver
  `compareVersions`/`compatibleWith`. The APVTS glue (`captureFrom`/`applyTo`)
  lives in `PresetApply.cpp` so `PresetMeta.cpp` stays pure (juce_core+crypto)
  and unit-testable. `FactoryPresetLibrary::build()` = 130 presets across 26
  genres (Clean/Broadcast/Podcast/Pop/Hip-Hop/Trap/Emo Trap/Hyperpop/R&B/Rock/
  Country/EDM/Lo-Fi/Metal/Gospel/Cinematic/…) from per-genre templates ×5
  variations + 10 official Collections. `AiPresetGenerator::generate(snapshot)`
  wraps AutoMixBrain → a complete AI preset (detected genre, confidence, plain-
  language summary). `PresetMorph` blends presets (A→B slider + weighted multi).
  `PresetLibrary` = fast in-memory index: fuzzy search (name/creator/genre/tags/
  mood), filters (genre/mood/creator/rating/cpu/source/ai/verified/official/
  favorites), sorts (relevance/name/newest/downloads/rating/trending), favorites,
  collections, discovery (trending/mostDownloaded/highestRated/newArrivals/
  editorsPicks/recommendedFor). `Marketplace.h` = abstract `MarketplaceClient`
  (discover/search/fetch/creators/ratings/reviews/report/upload + premium seams,
  no payments) + offline-first `LocalMarketplace` over a PresetLibrary — a real
  server implements the SAME interface and drops in unchanged. Processor wires
  it: `presetLibrary` member loaded with the factory library at ctor;
  `getPresetLibrary()`, `generateAiPreset()`, `captureCurrentAsPreset()`,
  `applyPreset()` (pushes undo, restores params + rack). VERIFIED: all 6 TUs
  object-compile vs JUCE; the integration-API probe compiles vs full JUCE;
  runtime tests — metadata round-trip/hash/tamper/dup/version (16), factory
  130+morph (23), library search/filter/sort/favorites/collections/discovery +
  AI-gen + LocalMarketplace upload-validation/creator/ratings (29) — ALL PASS.
  PRESET BROWSER UI (done — `UI/PresetBrowser.*`): a full-window overlay
  (header PRESETS button) over `getPresetLibrary()` — search box, genre + sort
  ComboBoxes, discovery tabs (All/Trending/New/AI Picks/Favorites), and a ListBox
  of rich preset rows (name, creator·genre·mood, favourite star gutter,
  AI/OFFICIAL/COMMUNITY/USER badge, rating + downloads, verified tick). Actions:
  Load (applyPreset), Save Current (captureCurrentAsPreset → library), ✨ AI
  Preset (generateAiPreset → library + load), Favorite. Wired in the editor via
  the processor accessors. VERIFIED: PresetBrowser.cpp syntax-compiles vs JUCE.
  NEXT PHASE: preset cards grid view + morph slider UI, and a real networked
  backend behind the same MarketplaceClient interface (cloud sync).
- MORE VOCAL MODULES (`Modules/AnalogModules.*`, +6 = 30 implemented total):
  Tape Saturation, Console Saturation, Transient Designer (fast/slow env attack+
  sustain), Telephone (band-limit + drive), Vintage EQ (broad shelves + presence
  bell), Optical Compressor (slow program-dependent). Registered (plan→register),
  and the ModuleAdvisor now suggests Transient Designer (moderate crest), Tape
  Saturation (not-bright tone), and Optical Compressor (dynamic+sustained).
  VERIFIED: all 6 create/process finite; advisor suggests them.
- `Update/UpdateChecker.*` (#12) — cross-platform (Win+mac) auto-update client,
  a `juce::Thread`. On load (throttled ≤ once/4h via `update.check` stamp file)
  it fetches `VOXBRAIN_UPDATE_URL` via `juce::URL` (WinINet/NSURL — no curl;
  sends a `User-Agent` header, required by GitHub), semver-compares to
  `VOXBRAIN_VERSION`, and silently downloads the platform installer to
  `<appdata>/VoxBrain/Updates`, verifying SHA-256 (juce_cryptography) when
  provided. **Auto-detects two manifest formats**: GitHub Releases API
  (`hasProperty("tag_name")` → tag_name/body/html_url + picks the `.exe`/`.pkg`
  from `assets[]`) or a self-hosted appcast.json (version/notes/windows|macos).
  Current URL points at the repo's `releases/latest` (verified: real GitHub JSON
  parses to the correct per-OS asset). Publishing = push a `v*` tag →
  `.github/workflows/release.yml` builds both installers and cuts the Release. Results marshaled to the message thread
  via `onStateChanged`; alive-guard (shared_ptr<bool>) for async safety. Never
  self-replaces (a loaded VST3 can't). `UI/UpdateBanner.*` shows the state and
  an Install button that launches the downloaded installer (`File::startAsProcess`)
  — user closes the DAW to apply. Both defines live in CMakeLists; the URL is a
  `YOUR-DOMAIN` placeholder and the updater NO-OPs until it's set (no phoning
  home by default). Server side: `update-server/` (appcast.json + README),
  `Scripts/publish_update.ps1`, `installer/voxbrain.iss` (Inno Setup).
- `Modules/` (modular workstation foundation — WIRED into the live audio path).
  `Module.h`: base class + `Param` (beginner/advanced, unit, tooltip) +
  `Descriptor` (id/name/`Category`/tags/description) + ProcessMode
  (Stereo/Mono/M-S). `ModuleRegistry` = factory + 78-entry searchable catalog
  (8 implemented, ~70 roadmap) + Smart Search (ranked over name/tags/category/
  desc — verified: "warm"→Tube, "reverb","pitch","vintage","compress","airy"
  all return relevant hits). `ModuleRack` = hot-swap ordered chain:
  add/remove/duplicate/move/bypass/solo/lock, RT-safe process via NON-BLOCKING
  `SpinLock` try-lock (edits on message thread; ≤1 block passthrough during an
  edit), summed latency + per-module CPU metering, `toXml`/`fromXml` state,
  and an `Automation` macro pool. `BuiltinModules.*` = 8 real modules (Gain,
  Trim, Phase Flip, Stereo Width M/S, Mono Maker LR-crossover, Highpass,
  Lowpass, Tube Saturation). `ModuleAdvisor` = AnalysisSnapshot → module insert
  suggestions + rationale ("low-mid buildup → Dynamic EQ", etc.).
  RACK WIRE-IN (done): the rack runs in `processBlock` AFTER the fixed
  `VocalChain` — empty by default = clean passthrough, so existing behaviour is
  unchanged until the user inserts modules. Because VST3 fixes its parameter
  list at load, `Parameters.cpp` exposes a FIXED generic pool the host can
  automate: `rack_on` + 8 slots × 6 macros (`rack_s{0..7}_m{0..5}`); the
  processor caches these pointers and each block maps slot i's macros onto the
  i-th rack module's first params (`readRackAutomation`). Latency =
  `chain.getLatencySamples() + rack.latencySamples()`. Rack routing (modules,
  order, params, bypass/solo/lock) serialises as a `"Rack"` child of the plugin
  state in `get/setStateInformation`, so it persists in sessions AND presets;
  old sessions with no Rack child simply load an empty rack. VERIFIED: all
  Module TUs object-compile vs JUCE; full `PluginProcessor.cpp` integration
  syntax-compiles vs JUCE + all project headers; offline runtime test — XML
  round-trip (modules/order/params/bypass restored), unknown-type skip, macro
  automation drives params + audio, `rack_on=false` passthrough — 14/14 PASS.
  RACK UI (done — `UI/RackView.*`): a full-window overlay toggled by the header
  MODULES button. Left column = palette: a Smart-Search box (live
  `registry.search`) over a `ListBox` of the whole catalog (implemented rows add
  on click; roadmap rows greyed "SOON"). Right column = the live rack as
  scrollable `RackModuleCard`s driven by `rack.snapshot()`: slot badge, name +
  category + latency, ▲▼ reorder, Bypass/Solo/Lock pills (rack node flags),
  Dup, ✕ remove, live CPU% (8 Hz timer), and up to 6 rotary macro knobs bound
  via `SliderAttachment` to the `rack_s{slot}_m{k}` params (so every knob is
  DAW-automatable), labelled by the module's own param names. On every
  structural edit the view calls `syncMacros()` — reads each module's current
  param values, normalises, and writes them to that slot's macro params — so a
  freshly added module sits at its defaults (not min) and modules keep their
  sound across reorders. Top of the right column = AI advisor chips from
  `processor.suggestModules()` (ModuleAdvisor on the last LEARN snapshot),
  refreshed after each LEARN; implemented suggestions insert on click, roadmap
  ones show "(soon)". VERIFIED: RackView.cpp + PluginEditor.cpp + the updated
  PluginProcessor.cpp all syntax-compile vs JUCE + all project headers, and
  RackView.cpp object-compiles clean.
  IMPLEMENTED MODULES (20 total now — every advisor suggestion is creatable):
  `BuiltinModules.*` (8: Gain, Trim, Phase Flip, Stereo Width, Mono Maker,
  Highpass, Lowpass, Tube Sat) + `DynamicsModules.*` (Compressor, Gate/Expander,
  Parallel Comp, Limiter, Soft Clipper — plus Restoration: De-Esser, De-Plosive,
  Noise Reduction; all share a branching `EnvFollower`) + `ToneModules.*`
  (Parametric Vocal EQ 4-band, Dynamic EQ ducker, Exciter) + `ModulationModules.*`
  (Stereo Chorus). Every new module is RT-safe, ≤6 params (so all knobs are
  macro-automatable), and had its factory moved from plan()→registerType() in
  ModuleCatalog (dup roadmap entries removed). VERIFIED: all module TUs
  object-compile vs JUCE; runtime smoke test — create/prepare/process finite for
  all 12, plus directional checks (limiter caps at ceiling, compressor reduces,
  gate attenuates quiet, soft-clip bounds hot input) — ALL PASS.
  AI ENGINEER — auto-insert (done): RackView has a "✨ Auto-Build" button
  (`autoBuild()`) that constructs a full rack — a proven default front-end
  (gate→de_plosive→de_esser→parametric_eq→compressor→dynamic_eq) plus any
  implemented advisor suggestions from `suggestModules()`, de-duplicated via
  `hasType()`, then `syncMacros()`. NEXT (AI Engineer, still pending): genre-aware
  brain moves + richer ChatEngine vocabulary; and the auto-tune upgrade
  (better pitch tracking, smarter key/scale, formant preservation, tighter
  hard-tune + humanize). Also: creative/space + pitch rack modules, drag-to-
  reorder, per-module editors, marketplace. The fixed VocalChain stays as the
  default/AI path; the rack is the "advanced modular" layer.
- `UI/` — dark glass theme (`theme::` colors), SpectrumDisplay (lock-free FIFO
  from analysis), ModuleStrip (11 cards in TWO weighted rows, knobs + combo
  support), AnalysisPanel (preset name + report + chat input), PresetBar (P6:
  preset menu + Save + A/B/Copy + Undo/Redo, delegates to PresetManager), pulsing
  LEARN button, and RackView (the modular-workstation overlay — see Modules
  above; toggled by the header MODULES button). Editor default 1120×800.
  PresetBar's Save uses an async AlertWindow (deleteWhenDismissed) name prompt.
  NEON REDESIGN (v0.2.6): `theme::` colours are now MUTABLE inline globals
  (one shared def; change one → whole plugin recolours on repaint). Default =
  Neon Red (accentWarm ff1e56) + Neon Purple (accent b14dff) on near-black violet
  glass. Theme API (LookAndFeel.cpp): 8 built-in `schemes()` (Neon Red/Purple,
  Ultraviolet, Vaporwave, Cyber Blue, Inferno, Toxic, Sunset, Mono),
  `applyScheme`, `setPrimary/Secondary/Background` (bg derives panel/outline),
  `setGlow`, `neonGradient` (purple→red), and persistence to
  `<userAppData>/VoxBrain/theme.xml` (`load()/save()`). LookAndFeel draws with
  layered neon glow (scaled by `theme::glow`) + dual-neon gradients on knobs/
  toggles/buttons; SpectrumDisplay graph uses the neon gradient + glow + gradient
  backdrop. `UI/ThemePanel.*` = live customizer (scheme presets, juce::
  ColourSelector pickers for Primary/Secondary/Background via CallOutBox, glow
  slider, reset) toggled by a header THEME button; on change → `refreshColours()`
  + repaint + `theme::save()`. Editor calls `theme::load()` before styling.
  Needs `juce::juce_gui_extra` (linked in CMake) for ColourSelector. VERIFIED:
  LookAndFeel.cpp + ThemePanel.cpp + SpectrumDisplay.cpp syntax-compile vs JUCE.
  UI POLISH (verified): `VoxBrainLookAndFeel` refreshed — rotary knobs now have a
  gradient value arc + soft glow + domed radial-gradient cap + glowing pointer
  tip; buttons are glassy vertical gradients with a top highlight; toggle pills
  gradient-fill when on; ComboBox/PopupMenu themed. This lifts the whole plugin
  AND the mixer at once. Editor `paint` adds a vertical gradient backdrop, a
  header bar with an accent underline, and an accent-dot logo. Rack polish:
  `RackModuleCard` is now drag-to-reorder (drag the card body; the card follows
  the mouse, commits `rack.moveModule` on drop, and the rebuild is deferred via
  `MessageManager::callAsync` + `SafePointer` so a card is never deleted inside
  its own mouseUp). ▲▼ buttons remain. All verified: LookAndFeel.cpp object-
  compiles; RackView.cpp + PluginEditor.cpp syntax-compile vs JUCE.

## Build system — CRITICAL knowledge (each item was a real failure)

- `Scripts\build_windows.bat` → wraps `build_windows.ps1` (bat is 3 lines +
  pause; complex batch scripts silently die — never put logic in .bat).
- PS script auto-detects VS via **vswhere** — user has **VS Community 2026
  (v18)** → generator "Visual Studio 18 2026". Do NOT hardcode VS 2022.
- CMake 4.4 at `C:\Program Files\CMake`. JUCE 8.0.4 via FetchContent (git).
- ONNX Runtime 1.20.1: pre-downloaded by the ps1 with curl.exe into
  `ThirdParty/onnxruntime-win-x64-1.20.1.zip` (CMake's downloader failed
  silently); CMakeLists prefers the local zip.
- **`/DELAYLOAD:onnxruntime.dll` MUST be set on VoxBrain_VST3 and
  VoxBrain_Standalone targets** — on the shared-code static lib it is
  silently dropped → hard import → plugin fails to load (error 193 via wrong
  DLL on PATH). Also **`ORT_API_MANUAL_INIT`** before including
  onnxruntime_cxx_api.h (else static init fires the delay-load at DLL load,
  breaking juce_vst3_helper), and `ensureOrtLoaded()` explicitly LoadLibrary's
  the DLL from the plugin's own dir (via GetModuleHandleExW) before InitApi.
- **NOMINMAX** + WIN32_LEAN_AND_MEAN before windows.h (std::min/max clash).
- `COPY_PLUGIN_AFTER_BUILD FALSE` — the ps1 installs to
  `C:\Program Files\Common Files\VST3` with a UAC elevation prompt instead
  (plain copy is Permission denied).
- Post-build copies onnxruntime.dll + `Models/` → `VoxBrainModels/` into the
  .vst3 bundle and next to the Standalone exe.
- `/arch:AVX2` is on → needs 2014+ CPUs.
- `Scripts\package_plugin.bat` → verified shareable zip with INSTALL.txt.
- `Scripts\diagnose.bat` → writes environment report to `diag.txt`.
- **Cross-platform / macOS** (scaffolded): `FORMATS` = `${VB_FORMATS}` (VST3 +
  Standalone everywhere, + AU on Apple). AVX2 is guarded — `/arch:AVX2` on MSVC,
  `-mavx2` only on x86 Linux and on Apple ONLY for a pure `x86_64` slice (genex
  on `CMAKE_OSX_ARCHITECTURES`) so Apple-Silicon/universal builds never get it.
  ONNX/CREPE stays Windows-only (`VB_HAS_ONNX=0` elsewhere → DSP-only analysis;
  CrepeAnalyzer.cpp verified to compile with it off). `juce_cryptography` linked
  for update SHA-256. Mac build: `Scripts/build_macos.sh` (Xcode, universal) →
  `Scripts/package_macos.sh <ver>` (pkgbuild .pkg, optional codesign/notarize).
  No Mac in this project → use a Mac or the `.github/workflows/build-macos.yml`
  cloud-Mac runner (workflow_dispatch / v* tag → downloadable .pkg artifact).
- Build output/log: `build_log.txt` (mixed UTF-8/UTF-16 — use `strings` and
  `strings -e l` when reading it programmatically).

## Verification workflow used so far

No Windows toolchain in the Claude sandbox. Verify by syntax-checking each
translation unit on Linux against real JUCE (already fetched at
`build/_deps/juce-src` and `/tmp/juce`; modules dir = `/tmp/juce/modules`) and
real ORT Linux headers, with `-fsyntax-only -std=c++20 -mavx2`. Required flag:
`-I/tmp/juce/modules -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1` (silences JUCE's
"No global header file was included!" guard) plus `-DJUCE_WEB_BROWSER=0
-DJUCE_USE_CURL=0`. X11 dev headers ARE present, so juce_gui_basics /
juce_audio_processors TUs compile. When the mount view of just-edited files is
stale (see below), compile a standalone harness containing the new code, or
reconstruct the file into /tmp — that's how P5 DSP was verified. Note: the sandbox's mounted view of recently-edited files can lag
several minutes and appear truncated at the file's OLD length — the file on
the user's disk is fine (Read tool is authoritative); wait or reconstruct the
file in /tmp to verify. MSVC-only issues (NOMINMAX etc.) won't show on Linux —
think about Windows specifics when touching platform code.

## Conventions

- Real-time safety: no allocation/locks/logging on the audio thread; atomics
  for cross-thread; parameters via cached raw pointers.
- Parameter IDs are stable API — never rename (breaks sessions).
- All AI/brain decisions carry human-readable rationale strings shown in the
  report panel; keep that transparency for every new feature.
- The user tests by ear — after DSP changes, tell them exactly what to listen
  for and what sequence of clicks to test with.
