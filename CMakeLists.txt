cmake_minimum_required(VERSION 3.22)

project(VocalForge VERSION 0.1.0 LANGUAGES C CXX)

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
set(VF_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VF_FORMATS AU)
endif()

juce_add_plugin(VocalForge
    COMPANY_NAME                "EHawk Audio"
    BUNDLE_ID                   "com.ehawkaudio.vocalforge"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE  # copy handled by build script (needs admin)
    PLUGIN_MANUFACTURER_CODE    EHwk
    PLUGIN_CODE                 Vfge
    FORMATS                     ${VF_FORMATS}
    PRODUCT_NAME                "VocalForge"
    VST3_CATEGORIES             Fx Dynamics EQ Pitch
)

target_sources(VocalForge PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/Analysis/AnalysisEngine.cpp
    Source/Analysis/PitchDetector.cpp
    Source/Analysis/LoudnessMeter.cpp
    Source/DSP/VocalChain.cpp
    Source/DSP/RetuneEngine.cpp
    Source/Brain/AutoMixBrain.cpp
    Source/AI/CrepeAnalyzer.cpp
    Source/Chat/ChatEngine.cpp
    Source/Preset/PresetManager.cpp
    Source/Update/UpdateChecker.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/SpectrumDisplay.cpp
    Source/UI/ModuleStrip.cpp
    Source/UI/AnalysisPanel.cpp
    Source/UI/PresetBar.cpp
    Source/UI/UpdateBanner.cpp
)

target_include_directories(VocalForge PRIVATE Source)

target_compile_definitions(VocalForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # --- Auto-update: current version + where to check for new versions ---
    #  Reads the latest GitHub Release of your repo. Publish a Release (tag like
    #  v1.2.0) with the .exe + .pkg installers attached and installed copies pick
    #  it up automatically. (Works because the repo/Releases are public.)
    VOCALFORGE_VERSION="${PROJECT_VERSION}"
    VOCALFORGE_UPDATE_URL="https://api.github.com/repos/E-Hawk-48/Custom-Vocal-Plugin/releases/latest"
)

target_link_libraries(VocalForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography     # SHA-256 integrity check for downloaded updates
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# SIMD + fast float. AVX2 is x86-only — never pass -mavx2 on Apple Silicon (arm64).
if(MSVC)
    target_compile_options(VocalForge PRIVATE /arch:AVX2 /fp:fast)
elseif(APPLE)
    # Universal / Apple-Silicon safe: no x86 -mavx2. CMAKE_OSX_ARCHITECTURES
    # (set by build_macos.sh) controls arm64/x86_64; add AVX2 only for x86 slices.
    target_compile_options(VocalForge PRIVATE -ffast-math
        "$<$<STREQUAL:${CMAKE_OSX_ARCHITECTURES},x86_64>:-mavx2>")
else()
    target_compile_options(VocalForge PRIVATE -mavx2 -ffast-math)
endif()

# --------------------------------------------------------------------------
# ONNX Runtime wiring: headers, delay-loaded DLL, copy DLL + models next to
# every built binary (delay-load lets the plugin control the DLL search path
# at runtime — plugins can't rely on the host exe's directory).
# --------------------------------------------------------------------------
if(WIN32)
    target_compile_definitions(VocalForge PUBLIC VF_HAS_ONNX=1)
    target_include_directories(VocalForge PRIVATE "${ORT_ROOT}/include")
    target_link_libraries(VocalForge PRIVATE "${ORT_ROOT}/lib/onnxruntime.lib" delayimp)

    foreach(tgt IN ITEMS VocalForge_VST3 VocalForge_Standalone)
        # CRITICAL: the delay-load flag must be on the final linked binaries.
        # (On the shared-code static lib it is silently discarded, producing a
        # hard import of onnxruntime.dll that breaks plugin loading.)
        target_link_options(${tgt} PRIVATE "/DELAYLOAD:onnxruntime.dll")
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ORT_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:${tgt}>"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${tgt}>/VocalForgeModels"
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/Models" "$<TARGET_FILE_DIR:${tgt}>/VocalForgeModels"
        )
    endforeach()
else()
    target_compile_definitions(VocalForge PUBLIC VF_HAS_ONNX=0)
endif()
