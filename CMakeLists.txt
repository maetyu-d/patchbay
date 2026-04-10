cmake_minimum_required(VERSION 3.22)

project(PatchBayDAW VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(JUCE_SOURCE_DIR "" CACHE PATH "Path to a local JUCE checkout")

if(NOT JUCE_SOURCE_DIR)
    message(FATAL_ERROR "Set JUCE_SOURCE_DIR to a local JUCE checkout before configuring this project.")
endif()

add_subdirectory(${JUCE_SOURCE_DIR} JUCE)

set(PATCHBAY_PLUGIN_FORMATS VST3 AU Standalone)

if(APPLE AND CMAKE_GENERATOR MATCHES "Xcode")
    list(APPEND PATCHBAY_PLUGIN_FORMATS AUv3)
endif()

add_library(patchbay_modules
    Source/Modules/ModuleCores.cpp
    Source/Modules/ModuleCores.h
)

target_link_libraries(patchbay_modules
    PUBLIC
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_events
)

juce_add_gui_app(PatchBayDAW
    PRODUCT_NAME "PatchBay DAW"
    COMPANY_NAME "Codex Audio"
    BUNDLE_ID "space.codex.patchbaydaw"
)

target_sources(PatchBayDAW
    PRIVATE
        Source/Main.cpp
        Source/App/MainWindow.cpp
        Source/App/MainWindow.h
        Source/Engine/ModuleNode.h
        Source/Engine/NodeFactory.cpp
        Source/Engine/NodeFactory.h
        Source/Engine/PatchGraph.cpp
        Source/Engine/PatchGraph.h
        Source/Modules/ExampleModules.cpp
        Source/Modules/ExampleModules.h
        Source/Modules/ModuleCores.h
        Source/UI/MainComponent.cpp
        Source/UI/MainComponent.h
        Source/UI/PatchCanvas.cpp
        Source/UI/PatchCanvas.h
        Source/UI/TrackView.cpp
        Source/UI/TrackView.h
)

target_compile_definitions(PatchBayDAW
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(PatchBayDAW
    PRIVATE
        patchbay_modules
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_generate_juce_header(PatchBayDAW)

function(add_patchbay_plugin target_name bundle_id plugin_code product_name plugin_macro is_synth)
    juce_add_plugin(${target_name}
        COMPANY_NAME "Codex Audio"
        BUNDLE_ID "${bundle_id}"
        VERSION ${PROJECT_VERSION}
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE CdxA
        PLUGIN_CODE ${plugin_code}
        FORMATS ${PATCHBAY_PLUGIN_FORMATS}
        PRODUCT_NAME "${product_name}"
    )

    target_sources(${target_name}
        PRIVATE
            Source/Plugin/ModulePluginProcessor.cpp
            Source/Plugin/ModulePluginProcessor.h
            Source/Plugin/ModulePluginEditor.cpp
            Source/Plugin/ModulePluginEditor.h
            Source/Modules/ModuleCores.h
    )

    target_compile_definitions(${target_name}
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            ${plugin_macro}=1
    )

    target_link_libraries(${target_name}
        PRIVATE
            patchbay_modules
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    juce_generate_juce_header(${target_name})
endfunction()

add_patchbay_plugin(PatchBayOscillatorPlugin "space.codex.patchbay.oscillator" Posc "PatchBay Oscillator" PATCHBAY_PLUGIN_OSCILLATOR TRUE)
add_patchbay_plugin(PatchBayLfoPlugin "space.codex.patchbay.lfo" Plfo "PatchBay LFO" PATCHBAY_PLUGIN_LFO FALSE)
add_patchbay_plugin(PatchBayGainPlugin "space.codex.patchbay.gain" Pgai "PatchBay Gain" PATCHBAY_PLUGIN_GAIN FALSE)
add_patchbay_plugin(PatchBayOutputPlugin "space.codex.patchbay.output" Pout "PatchBay Output" PATCHBAY_PLUGIN_OUTPUT FALSE)
