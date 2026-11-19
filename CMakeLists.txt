cmake_minimum_required(VERSION 3.22)

project(UltimateVocal VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Runtime estático en Windows (sin depender de redistribuibles de VC++).
# Debe definirse ANTES de añadir JUCE para que afecte a todos sus módulos.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- JUCE (descargado automáticamente) -------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(JUCE)

# --- Plugin -----------------------------------------------------------------
juce_add_plugin(UltimateVocal
    COMPANY_NAME "Manuel J. Rodriguez"
    PLUGIN_MANUFACTURER_CODE MJRd
    PLUGIN_CODE UVoc
    FORMATS VST3 Standalone
    PRODUCT_NAME "Ultimate Vocal"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Pitch\ Shift
    BUNDLE_ID com.mjrodriguez.ultimatevocal
)

target_sources(UltimateVocal PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(UltimateVocal PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(UltimateVocal
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Test del motor DSP (sin JUCE) ------------------------------------------
option(UV_BUILD_DSP_TEST "Compila el test del motor DSP" OFF)
if(UV_BUILD_DSP_TEST)
    add_executable(dsp_test Tests/dsp_test.cpp)
    target_include_directories(dsp_test PRIVATE Source)
endif()
