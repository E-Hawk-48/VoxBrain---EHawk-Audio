cmake_minimum_required(VERSION 3.22)

project(VoxBrain VERSION 0.4.9 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --------------------------------------------------------------------------
# JUCE (fetched automatically; pinned for reproducible builds)
# --------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# --------------------------------------------------------------------------
# ONNX Runtime (prebuilt, Windows x64) — local neural-network inference
# --------------------------------------------------------------------------
if(WIN32)
    # Prefer a pre-downloaded archive (build script fetches it with curl,
    # which is more robust than CMake's built-in downloader)
    set(ORT_LOCAL_ZIP "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/onnxruntime-win-x64-1.20.1.zip")
    if(EXISTS "${ORT_LOCAL_ZIP}")
        set(ORT_URL "${ORT_LOCAL_ZIP}")
    else()
        set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip")
    endif()

    FetchContent_Declare(
        onnxruntime
        URL "${ORT_URL}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(onnxruntime)
    set(ORT_ROOT "${onnxruntime_SOURCE_DIR}")
endif()

# --------------------------------------------------------------------------
# Plugin target
# --------------------------------------------------------------------------
# Formats: VST3 + Standalone on every platform; add Audio Unit on macOS.
set(VB_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VB_FORMATS AU)
endif()

juce_add_plugin(VoxBrain
    COMPANY_NAME                "VoxBrain Audio"
    BUNDLE_ID                   "com.voxbrainaudio.voxbrain"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE  # copy handled by build script (needs admin)
    # NOTE: the 4-char PLUGIN_MANUFACTURER_CODE / PLUGIN_CODE are the plugin's
    # binary VST3/AU identity. They are intentionally NOT changed by the VoxBrain
    # rebrand — altering them makes hosts treat this as a brand-new plugin and
    # breaks any existing project that already loaded it. They are never shown to
    # users, so they carry no visible branding.
    PLUGIN_MANUFACTURER_CODE    EHwk
    PLUGIN_CODE                 Vfge
    FORMATS                     ${VB_FORMATS}
    PRODUCT_NAME                "VoxBrain"
    DESCRIPTION                 "AI Vocal Mixing Suite"
    COMPANY_COPYRIGHT           "© 2026 VoxBrain Audio. All Rights Reserved."
    VST3_CATEGORIES             Fx Dynamics EQ Pitch
)

target_sources(VoxBrain PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/Analysis/AnalysisEngine.cpp
    Source/Analysis/PitchDetector.cpp
    Source/Analysis/KeyDetector.cpp
    Source/Analysis/LoudnessMeter.cpp
    Source/DSP/VocalChain.cpp
    Source/DSP/RetuneEngine.cpp
    Source/DSP/PitchTracker.cpp
    Source/DSP/NoteIntelligence.cpp
    Source/DSP/VoiceCharacter.cpp
    Source/Brain/AutoMixBrain.cpp
    Source/Brain/ModuleIntelligence.cpp
    Source/Brain/GenreProfiles.cpp
    Source/AI/CrepeAnalyzer.cpp
    Source/Chat/ChatEngine.cpp
    Source/Chat/ModuleCommands.cpp
    Source/Preset/PresetManager.cpp
    Source/Preset/PresetMeta.cpp
    Source/Preset/PresetApply.cpp
    Source/Preset/PresetMorph.cpp
    Source/Preset/FactoryPresetLibrary.cpp
    Source/Preset/AiPresetGenerator.cpp
    Source/Preset/PresetLibrary.cpp
    Source/Update/UpdateChecker.cpp
    Source/Modules/ModuleRegistry.cpp
    Source/Modules/ModuleRack.cpp
    Source/Modules/BuiltinModules.cpp
    Source/Modules/ModuleCatalog.cpp
    Source/Modules/ModuleAdvisor.cpp
    Source/Modules/DynamicsModules.cpp
    Source/Modules/ToneModules.cpp
    Source/Modules/ModulationModules.cpp
    Source/Modules/SpaceModules.cpp
    Source/Modules/AnalogModules.cpp
    Source/DSP/ChainOrder.cpp
    Source/Reference/ReferenceAnalyzer.cpp
    Source/Reference/ReferenceMatchBrain.cpp
    Source/Reference/ReferenceImport.cpp
    Source/Reference/VocalIsolation.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/SpectrumDisplay.cpp
    Source/UI/ModuleStrip.cpp
    Source/UI/ModuleSpecs.cpp
    Source/UI/ChainView.cpp
    Source/UI/AnalysisPanel.cpp
    Source/UI/PresetBar.cpp
    Source/UI/UpdateBanner.cpp
    Source/UI/RackView.cpp
    Source/UI/PitchDisplay.cpp
    Source/UI/ThemePanel.cpp
    Source/UI/PresetBrowser.cpp
    Source/UI/VocalDnaPanel.cpp
    Source/UI/UiPrefs.cpp
    Source/UI/ReferencePanel.cpp
)

target_include_directories(VoxBrain PRIVATE Source)

target_compile_definitions(VoxBrain PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # --- Auto-update: current version + where to check for new versions ---
    #  Reads the latest GitHub Release of your repo. Publish a Release (tag like
    #  v1.2.0) with the .exe + .pkg installers attached and installed copies pick
    #  it up automatically. (Works because the repo/Releases are public.)
    #  NOTE: the repo is still named Custom-Vocal-Plugin on GitHub — this URL
    #  matches the real repo. If you rename the repo to VoxBrain, update it here.
    VOXBRAIN_VERSION="${PROJECT_VERSION}"
    VOXBRAIN_UPDATE_URL="https://api.github.com/repos/E-Hawk-48/Custom-Vocal-Plugin/releases/latest"
)

target_link_libraries(VoxBrain
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra        # juce::ColourSelector for the theme customizer
        juce::juce_dsp
        juce::juce_cryptography     # SHA-256 integrity check for downloaded updates
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# SIMD + fast float. AVX2 is x86-only — never pass -mavx2 on Apple Silicon (arm64).
if(MSVC)
    target_compile_options(VoxBrain PRIVATE /arch:AVX2 /fp:fast)
    # Emit a .pdb even in Release. /Zi adds symbol info to the shared-code objects;
    # /DEBUG makes the linker write the .pdb; /OPT:REF,ICF undo the size bloat
    # /DEBUG otherwise causes so the shipped binary stays just as lean. This does
    # NOT change runtime behaviour — it only means a future crash report names the
    # exact function + line instead of a bare address, turning the next diagnosis
    # into a one-line fix. NOTE: the /DEBUG *link* flag must live on the final
    # linked binaries (VST3/Standalone) — on the shared static lib it is silently
    # discarded (same reason the /DELAYLOAD flag below is applied per-target).
    target_compile_options(VoxBrain PRIVATE /Zi)
    foreach(dbgtgt IN ITEMS VoxBrain_VST3 VoxBrain_Standalone)
        if(TARGET ${dbgtgt})
            target_compile_options(${dbgtgt} PRIVATE /Zi)
            target_link_options(${dbgtgt} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
        endif()
    endforeach()
elseif(APPLE)
    # Universal / Apple-Silicon safe: no x86 -mavx2. CMAKE_OSX_ARCHITECTURES
    # (set by build_macos.sh) controls arm64/x86_64; add AVX2 only for x86 slices.
    target_compile_options(VoxBrain PRIVATE -ffast-math
        "$<$<STREQUAL:${CMAKE_OSX_ARCHITECTURES},x86_64>:-mavx2>")
else()
    target_compile_options(VoxBrain PRIVATE -mavx2 -ffast-math)
endif()

# --------------------------------------------------------------------------
# ONNX Runtime wiring: headers, delay-loaded DLL, copy DLL + models next to
# every built binary (delay-load lets the plugin control the DLL search path
# at runtime — plugins can't rely on the host exe's directory).
# --------------------------------------------------------------------------
if(WIN32)
    target_compile_definitions(VoxBrain PUBLIC VB_HAS_ONNX=1)
    target_include_directories(VoxBrain PRIVATE "${ORT_ROOT}/include")
    target_link_libraries(VoxBrain PRIVATE "${ORT_ROOT}/lib/onnxruntime.lib" delayimp)

    foreach(tgt IN ITEMS VoxBrain_VST3 VoxBrain_Standalone)
        # CRITICAL: the delay-load flag must be on the final linked binaries.
        # (On the shared-code static lib it is silently discarded, producing a
        # hard import of onnxruntime.dll that breaks plugin loading.)
        target_link_options(${tgt} PRIVATE "/DELAYLOAD:onnxruntime.dll")
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ORT_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:${tgt}>"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${tgt}>/VoxBrainModels"
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/Models" "$<TARGET_FILE_DIR:${tgt}>/VoxBrainModels"
        )
    endforeach()
else()
    target_compile_definitions(VoxBrain PUBLIC VB_HAS_ONNX=0)
endif()

# ============================================================================
#  TEST SUITE  (Phase 0 of the architecture review)
# ----------------------------------------------------------------------------
#  Every DSP claim in this project used to be proved by a harness compiled in a
#  temp folder, run once and discarded — roughly three hundred assertions that
#  could never be re-run, so a later regression would have been invisible. These
#  targets make that verification permanent and repeatable.
#
#  OFF by default: the normal plugin build a non-programmer runs must stay fast
#  and must never be broken by test code. `Scripts\run_tests.bat` configures a
#  SEPARATE build directory with this ON, so the two never interfere.
# ============================================================================
option(VB_BUILD_TESTS "Build the VoxBrain test suite" OFF)

if(VB_BUILD_TESTS)
    enable_testing()

    add_executable(VoxBrainTests
        Tests/TestMain.cpp
        Tests/Test_PitchTracker.cpp
        Tests/Test_NoteIntelligence.cpp
        Tests/Test_KeyDetector.cpp
        Tests/Test_RetuneEngine.cpp
        Tests/Test_VoiceChanger.cpp
        Tests/Test_Compressor.cpp
        Tests/Test_Smoothing.cpp

        # The engine code under test. Deliberately compiled INTO the test binary
        # rather than linked from the plugin: these units are pure DSP with no
        # APVTS/GUI dependency, which is exactly why they are testable at all.
        Source/DSP/PitchTracker.cpp
        Source/DSP/NoteIntelligence.cpp
        Source/DSP/RetuneEngine.cpp
        Source/DSP/VoiceCharacter.cpp
        Source/DSP/VocalChain.cpp
        Source/Analysis/KeyDetector.cpp
    )

    target_compile_definitions(VoxBrainTests PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        VB_HAS_ONNX=0
    )

    target_link_libraries(VoxBrainTests
        PRIVATE
            juce::juce_dsp
            juce::juce_audio_basics
        PUBLIC
            juce::juce_recommended_config_flags
    )

    if(MSVC)
        target_compile_options(VoxBrainTests PRIVATE /arch:AVX2 /fp:fast)
    endif()

    add_test(NAME VoxBrainTests COMMAND VoxBrainTests)
endif()
