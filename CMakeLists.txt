cmake_minimum_required(VERSION 3.22)
project(KeroMixAI VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# JUCE path - CI sets this via -DJUCE_DIR, local dev can override
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_SOURCE_DIR}/JUCE")
endif()

add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(KeroMixAI
    COMPANY_NAME                "nadahong"
    PLUGIN_MANUFACTURER_CODE    Kero
    PLUGIN_CODE                 KrMA
    FORMATS                     VST3 AU
    PRODUCT_NAME                "KeroMixAI"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx
)

target_sources(KeroMixAI PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(KeroMixAI PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(KeroMixAI PRIVATE
    juce_audio_basics
    juce_audio_devices
    juce_audio_formats
    juce_audio_plugin_client
    juce_audio_processors
    juce_audio_utils
    juce_core
    juce_data_structures
    juce_dsp
    juce_events
    juce_graphics
    juce_gui_basics
    juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
