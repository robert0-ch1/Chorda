# ==============================================================================
#  Chorda - CMake build
#
#  Configure + build (JUCE is downloaded automatically):
#      cmake -B build -DCMAKE_BUILD_TYPE=Release
#      cmake --build build --config Release
#
#  To use a JUCE checkout you already have instead of downloading one:
#      cmake -B build -DCHORDA_JUCE_PATH=/path/to/JUCE
#
#  Build products land in build/Chorda_artefacts/<Config>/.
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

project(Chorda
    VERSION 1.0.0
    DESCRIPTION "Karplus-Strong string synthesiser"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CHORDA_BUILD_TESTS       "Build the offline render test"                          ON)
option(CHORDA_COPY_PLUGIN       "Copy the built plugins into the user plugin folders"    OFF)
set(CHORDA_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (empty = download JUCE ${CHORDA_JUCE_VERSION})")
set(CHORDA_JUCE_VERSION "8.0.6" CACHE STRING "JUCE tag to download when CHORDA_JUCE_PATH is empty")

# ------------------------------------------------------------------------------
# JUCE
# ------------------------------------------------------------------------------
if(CHORDA_JUCE_PATH)
    message(STATUS "Using local JUCE at ${CHORDA_JUCE_PATH}")
    add_subdirectory("${CHORDA_JUCE_PATH}" JUCE EXCLUDE_FROM_ALL)
else()
    message(STATUS "Downloading JUCE ${CHORDA_JUCE_VERSION}")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${CHORDA_JUCE_VERSION}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Sources shared by the plugin and the test executable
# ------------------------------------------------------------------------------
set(CHORDA_SOURCES
    Source/Parameters.h
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/DSP/KarplusSound.h
    Source/DSP/KarplusVoice.h
    Source/DSP/KarplusVoice.cpp
    Source/DSP/PluckSynth.h
    Source/DSP/PluckSynth.cpp
    Source/DSP/StereoWidener.h
    Source/Presets/FactoryPresets.h
    Source/Presets/FactoryPresets.cpp
    Source/Presets/PresetManager.h
    Source/Presets/PresetManager.cpp
    Source/UI/PluckLookAndFeel.h
    Source/UI/PluckLookAndFeel.cpp
    Source/UI/ParameterKnob.h
    Source/UI/StringView.h
    Source/UI/StringView.cpp
    Source/UI/ParameterBox.h
    Source/UI/SwitchButton.h
    Source/UI/SectionCard.h
    Source/UI/IconButton.h
    Source/UI/PresetBar.h
    Source/UI/PresetBar.cpp)

set(CHORDA_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

# ------------------------------------------------------------------------------
# Embedded assets: the title typeface (Bagnard, SIL Open Font License) and,
# if present, a hand icon for the pick marker. All other text uses the
# system UI font (SF Pro on macOS, Segoe UI on Windows). Drop one at
# Resources/hand.svg (or hand.png) and it replaces the hand drawn in code.
# ------------------------------------------------------------------------------
set(CHORDA_HAND_ICON "")
foreach(candidate Resources/hand.svg Resources/hand.png)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${candidate}")
        set(CHORDA_HAND_ICON "${candidate}")
        break()
    endif()
endforeach()

# The pick hand's plucking animation: three frames, played at each note-on.
juce_add_binary_data(PluckAssets
    SOURCES
        Resources/Bagnard.otf
        Resources/hand_pluck_1.png
        Resources/hand_pluck_2.png
        Resources/hand_pluck_3.png
        ${CHORDA_HAND_ICON})
set_target_properties(PluckAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------
# The plugin
# ------------------------------------------------------------------------------
set(CHORDA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND CHORDA_FORMATS AU)
endif()

juce_add_plugin(Chorda
    PRODUCT_NAME                "Chorda"
    COMPANY_NAME                "Roberto Chiurazzi"
    BUNDLE_ID                   "com.robertochiurazzi.chorda"
    PLUGIN_MANUFACTURER_CODE    Rchz
    PLUGIN_CODE                 Chrd
    FORMATS                     ${CHORDA_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${CHORDA_COPY_PLUGIN}
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

juce_generate_juce_header(Chorda)

target_sources(Chorda PRIVATE ${CHORDA_SOURCES})
target_include_directories(Chorda PRIVATE Source)
target_compile_definitions(Chorda PUBLIC ${CHORDA_COMMON_DEFINITIONS})

target_link_libraries(Chorda
    PRIVATE
        PluckAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Offline render test: drives the processor with MIDI, checks for clicks,
# NaNs, polyphony behaviour and state round-trips. Run with `ctest`.
# ------------------------------------------------------------------------------
if(CHORDA_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(PluckRenderTest PRODUCT_NAME "PluckRenderTest")
    juce_generate_juce_header(PluckRenderTest)

    target_sources(PluckRenderTest PRIVATE Tests/RenderTest.cpp ${CHORDA_SOURCES})
    target_include_directories(PluckRenderTest PRIVATE Source)

    # The processor sources expect these to come from the plugin wrapper.
    target_compile_definitions(PluckRenderTest PRIVATE
        ${CHORDA_COMMON_DEFINITIONS}
        JucePlugin_Name="Chorda"
        JucePlugin_Manufacturer="Roberto Chiurazzi"
        JucePlugin_VersionString="${PROJECT_VERSION}")

    target_link_libraries(PluckRenderTest
        PRIVATE
            PluckAssets
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME RenderTest
             COMMAND PluckRenderTest "${CMAKE_BINARY_DIR}/test-output")
endif()
