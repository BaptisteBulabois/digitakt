cmake_minimum_required(VERSION 3.22)
project(TaktII VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(TAKT_BUILD_PLUGIN "Build the JUCE VST3 instrument and standalone application" ON)
set(TAKT_JUCE_PATH "" CACHE PATH "Existing checkout of JUCE 8.0.6 (optional)")

add_library(takt_engine STATIC src/engine/Engine.cpp src/engine/PatternChain.cpp src/engine/SequencerRules.cpp src/engine/AmpFilter.cpp)
target_include_directories(takt_engine PUBLIC src src/engine)
set_target_properties(takt_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(takt_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(takt_engine_tests tests/engine_tests.cpp)
    target_link_libraries(takt_engine_tests PRIVATE takt_engine)
    add_test(NAME engine COMMAND takt_engine_tests)
    add_executable(takt_pattern_chain_tests tests/pattern_chain_tests.cpp)
    target_link_libraries(takt_pattern_chain_tests PRIVATE takt_engine)
    add_test(NAME pattern_chain COMMAND takt_pattern_chain_tests)
    add_executable(takt_sequencer_rules_tests tests/sequencer_rules_tests.cpp)
    target_link_libraries(takt_sequencer_rules_tests PRIVATE takt_engine)
    add_test(NAME sequencer_rules COMMAND takt_sequencer_rules_tests)
    add_executable(takt_amp_filter_tests tests/amp_filter_tests.cpp)
    target_link_libraries(takt_amp_filter_tests PRIVATE takt_engine)
    add_test(NAME amp_filter COMMAND takt_amp_filter_tests)
endif()

if(TAKT_BUILD_PLUGIN)
    if(TAKT_JUCE_PATH)
        add_subdirectory("${TAKT_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 51a8a6d7aeae7326956d747737ccf1575e61e209
            GIT_SHALLOW FALSE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(TaktII
        COMPANY_NAME "Baptiste Bulabois"
        BUNDLE_ID "fr.baptistebulabois.taktii"
        PLUGIN_MANUFACTURER_CODE Bbui
        PLUGIN_CODE TkII
        FORMATS VST3 Standalone
        PRODUCT_NAME "Takt II"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Sampler)
    target_sources(TaktII PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
    target_include_directories(TaktII PUBLIC src)
    target_compile_definitions(TaktII PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_JACK=0
        JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_OGGVORBIS=0
        JUCE_USE_MP3AUDIOFORMAT=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(TaktII PRIVATE takt_engine juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    if(BUILD_TESTING)
        juce_add_console_app(TaktTests PRODUCT_NAME "Takt integration tests")
        target_sources(TaktTests PRIVATE tests/plugin_tests.cpp)
        target_compile_definitions(TaktTests PRIVATE JUCE_PLUGINHOST_VST3=1)
        target_link_libraries(TaktTests PRIVATE TaktII takt_engine juce::juce_audio_utils juce::juce_gui_extra)
        add_test(NAME plugin_processor COMMAND TaktTests)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            juce_add_console_app(TaktRealtimeTests PRODUCT_NAME "Takt realtime allocation tests")
            target_sources(TaktRealtimeTests PRIVATE tests/processor_realtime_tests.cpp)
            target_link_libraries(TaktRealtimeTests PRIVATE TaktII takt_engine juce::juce_audio_utils juce::juce_gui_extra)
            target_link_options(TaktRealtimeTests PRIVATE
                "-Wl,--wrap=malloc" "-Wl,--wrap=free"
                "-Wl,--wrap=calloc" "-Wl,--wrap=realloc")
            add_test(NAME processor_realtime COMMAND TaktRealtimeTests)
        endif()
    endif()
endif()
