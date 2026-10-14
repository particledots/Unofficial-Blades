cmake_minimum_required(VERSION 3.22)
project(UnofficialBladesPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  # The GitHub workflow passes -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal build.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(UnofficialBlades
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE UBld
  FORMATS ${FORMATS}
  PRODUCT_NAME "Unofficial Blades"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(UnofficialBlades PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(UnofficialBlades PRIVATE src)
target_compile_definitions(UnofficialBlades PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(UnofficialBlades PRIVATE
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# --- Test of the filter model (no JUCE):  cmake --build build --target blades_test && build/blades_test ---
add_executable(blades_test EXCLUDE_FROM_ALL tests/blades_test.cpp)
target_include_directories(blades_test PRIVATE src)

# --- Picture of the panel (used by the build):  cmake --build build --target blades_snapshot ---
juce_add_console_app(blades_snapshot PRODUCT_NAME "blades_snapshot")
set_target_properties(blades_snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(blades_snapshot PRIVATE tools/snapshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(blades_snapshot PRIVATE src)
target_compile_definitions(blades_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(blades_snapshot PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
