# SPDX-License-Identifier: AGPL-3.0-only
# MORPHO-PHS — two JUCE 8 plugins (instrument + FX; VST3 + Standalone, optional CLAP) and the headless morpho_cli.
#
#   cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/plugin --target MorphoPHS_VST3 MorphoPHS_Standalone MorphoPHSFX_VST3 MorphoPHSFX_Standalone morpho_cli
#
# Licensing (Phase 4 / D6): MORPHO-PHS is released under the GNU AGPL v3 (see LICENSE); JUCE 8 is used under its
# AGPLv3 option.  See docs/LICENSES_phase4.md.
cmake_minimum_required(VERSION 3.22)
project(MORPHO_PHS VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(MORPHO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(JUCE_DIR ${MORPHO_ROOT}/third_party/JUCE CACHE PATH "JUCE 8 source tree")
set(CLAP_JUCE_DIR ${MORPHO_ROOT}/third_party/clap-juce-extensions CACHE PATH "clap-juce-extensions source tree")
option(MORPHO_CLAP "Also build a CLAP via clap-juce-extensions" ON)
option(MORPHO_NATIVE "Tune for the build machine (-march=native / AVX2)" OFF)
set(MORPHO_EXTRA_SYSROOT "" CACHE PATH "Linux only: extra header/library prefix (box build without root)")

if (MORPHO_EXTRA_SYSROOT)
    include_directories(SYSTEM AFTER ${MORPHO_EXTRA_SYSROOT}/usr/include)
    link_directories(${MORPHO_EXTRA_SYSROOT}/usr/lib/x86_64-linux-gnu)
    add_link_options("-Wl,-rpath-link,${MORPHO_EXTRA_SYSROOT}/usr/lib/x86_64-linux-gnu")
endif()

add_subdirectory(${JUCE_DIR} JUCE)
if (MORPHO_CLAP AND EXISTS ${CLAP_JUCE_DIR}/CMakeLists.txt)
    add_subdirectory(${CLAP_JUCE_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)
    set(MORPHO_HAVE_CLAP ON)
endif()

# ---------------- shared compile settings ----------------
add_library(morpho_flags INTERFACE)
target_include_directories(morpho_flags INTERFACE ${MORPHO_ROOT}/include ${CMAKE_CURRENT_SOURCE_DIR}/Source)
if (MSVC)
    # /fp:precise keeps IEEE semantics (no reassociation) like the GCC build; /arch:AVX2 enables 256-bit SIMD.
    target_compile_options(morpho_flags INTERFACE /O2 /fp:precise /arch:AVX2 /permissive- /Zc:__cplusplus /utf-8 /bigobj)
else()
    # -fno-math-errno lets GCC/Clang map std::sqrt to vsqrtp* in the SAV pass (vectorisation); no -ffast-math.
    # -ffp-contract=off (Phase 5): no implicit FMA contraction, so the SIMD stencil kernel and the CSR reference give
    # bit-identical results on GCC/Clang exactly as on MSVC (/fp:precise does not contract).
    target_compile_options(morpho_flags INTERFACE -O3 -fno-math-errno -ffp-contract=off)
    if (MORPHO_NATIVE)
        target_compile_options(morpho_flags INTERFACE -march=native)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(morpho_flags INTERFACE -march=x86-64-v3)
    endif()
endif()

# ---------------- factory presets as binary data ----------------
file(GLOB MORPHO_PRESETS CONFIGURE_DEPENDS ${MORPHO_ROOT}/presets/*.morpho)
juce_add_binary_data(MorphoPresets NAMESPACE BinaryData SOURCES ${MORPHO_PRESETS})

# ---------------- Phase 6a: UI assets (IBM Plex, SIL OFL 1.1 -- embedded unmodified; see Assets/fonts/IBMPlex-OFL.txt) ----------------
juce_add_binary_data(MorphoUiAssets HEADER_NAME BinaryData_UiAssets.h NAMESPACE UiAssets SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets/fonts/IBMPlexSansCondensed-Medium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets/fonts/IBMPlexSansCondensed-SemiBold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets/fonts/IBMPlexMono-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets/fonts/IBMPlexMono-Medium.ttf)
set(MORPHO_UI_SOURCES Source/PluginEditor.cpp Source/LatticeView.cpp Source/ui/Theme.cpp Source/ui/Controls.cpp Source/ui/PresetBrowser.cpp)

# ---------------- plugins (Phase 4 / D2: one source tree, two plugins) ----------------
#   MorphoPHS    "MORPHO-PHS"     instrument / synth (mode fixed to Instrument)    code Mpsi  lab.morpho.phs.instrument
#   MorphoPHSFX  "MORPHO-PHS FX"  effect (FX Edge Disperser / FX Defect Freezer)  code Mpsx  lab.morpho.phs.fx
# Both: VST3 + Standalone (+ CLAP).  The Phase-3 combined plugin used code Mphs / lab.morpho.phs (retired).
function(morpho_add_plugin target product code bundle is_fx)
    if (is_fx)
        set(_synth FALSE)
        set(_midi FALSE)
        set(_cats Fx Reverb)
        set(_clap audio-effect reverb stereo)
        set(_icon fx)
    else()
        set(_synth TRUE)
        set(_midi TRUE)
        set(_cats Instrument Synth)
        set(_clap instrument synthesizer stereo)
        set(_icon inst)
    endif()
    juce_add_plugin(${target}
        PRODUCT_NAME "${product}"
        COMPANY_NAME "MorphoLab"
        COMPANY_WEBSITE "https://example.invalid/morpho-phs"
        BUNDLE_ID "${bundle}"
        PLUGIN_MANUFACTURER_CODE Mrph
        PLUGIN_CODE ${code}
        FORMATS VST3 Standalone
        IS_SYNTH ${_synth}
        NEEDS_MIDI_INPUT ${_midi}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES ${_cats}
        VST3_AUTO_MANIFEST FALSE
        ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Assets/brand/icon_${_icon}_512.png
        ICON_SMALL ${CMAKE_CURRENT_SOURCE_DIR}/Assets/brand/icon_${_icon}_256.png)
    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp Source/PresetIO.cpp ${MORPHO_UI_SOURCES})
    if (is_fx)
        set(_variant 1)
    else()
        set(_variant 0)
    endif()
    target_compile_definitions(${target} PUBLIC
        MORPHO_VARIANT_FX=${_variant}
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0 JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_STRICT_REFCOUNTEDPOINTER=1 DONT_SET_USING_JUCE_NAMESPACE=1)
    target_link_libraries(${target}
        PRIVATE MorphoPresets MorphoUiAssets morpho_flags juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
    if (MORPHO_HAVE_CLAP)
        clap_juce_extensions_plugin(TARGET ${target}
            CLAP_ID "${bundle}"
            CLAP_FEATURES ${_clap})
    endif()
endfunction()

morpho_add_plugin(MorphoPHS   "MORPHO-PHS"    Mpsi "lab.morpho.phs.instrument" FALSE)
morpho_add_plugin(MorphoPHSFX "MORPHO-PHS FX" Mpsx "lab.morpho.phs.fx"         TRUE)

# ---------------- headless CLI (render / bench / validate) ----------------
juce_add_console_app(morpho_cli PRODUCT_NAME "morpho_cli")
target_sources(morpho_cli PRIVATE Source/cli/main.cpp Source/PresetIO.cpp)
target_compile_definitions(morpho_cli PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 DONT_SET_USING_JUCE_NAMESPACE=1)
target_link_libraries(morpho_cli
    PRIVATE morpho_flags juce::juce_core juce::juce_audio_basics juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags)

# ---------------- Phase 5b: thread-pool stress test (no JUCE; start/stop/reconfigure storm with a hang watchdog) ----------------
#   morpho_stress [secs_per_part=20] [seed=1]   exit 0 = pass, 1 = failure, 2 = HANG (watchdog dump)
add_executable(morpho_stress ${MORPHO_ROOT}/tests/test_phase5_stress.cpp)
target_link_libraries(morpho_stress PRIVATE morpho_flags)
set_target_properties(morpho_stress PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/morpho_stress_artefacts/$<CONFIG>")
if (NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(morpho_stress PRIVATE Threads::Threads)
endif()

# ---------------- Phase 6a: UI snapshot / paint-time harness (dev tool, not shipped) ----------------
#   xvfb-run morpho_ui_snap_inst <out_dir>   /   morpho_ui_snap_fx <out_dir>
option(MORPHO_UI_HARNESS "Build the Phase 6a UI snapshot harness" ON)
if (MORPHO_UI_HARNESS AND NOT WIN32)
    foreach(_v inst fx)
        if (_v STREQUAL "fx")
            set(_fx 1)
            set(_nm "MORPHO-PHS FX")
            set(_midi 0)
        else()
            set(_fx 0)
            set(_nm "MORPHO-PHS")
            set(_midi 1)
        endif()
        juce_add_gui_app(morpho_ui_snap_${_v} PRODUCT_NAME "morpho_ui_snap_${_v}")
        target_sources(morpho_ui_snap_${_v} PRIVATE Source/harness/UiSnapHarness.cpp Source/PluginProcessor.cpp Source/PresetIO.cpp ${MORPHO_UI_SOURCES})
        target_compile_definitions(morpho_ui_snap_${_v} PRIVATE MORPHO_VARIANT_FX=${_fx} JucePlugin_Name="${_nm}" JucePlugin_VersionString="${PROJECT_VERSION}"
            JucePlugin_WantsMidiInput=${_midi} JucePlugin_IsSynth=${_midi} JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_STRICT_REFCOUNTEDPOINTER=1 DONT_SET_USING_JUCE_NAMESPACE=1)
        target_link_libraries(morpho_ui_snap_${_v} PRIVATE MorphoPresets MorphoUiAssets morpho_flags juce::juce_audio_utils
            juce::juce_audio_processors juce::juce_gui_extra PUBLIC juce::juce_recommended_config_flags)
    endforeach()
endif()
