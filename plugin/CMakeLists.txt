# JUCE plugin: VST3 + AU (macOS) + Standalone app, all from the same sources.
include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.4
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SampleChopper
  PRODUCT_NAME                "Sample Chopper"
  COMPANY_NAME                "Seth Richardson"
  BUNDLE_ID                   "com.sethrichardson.samplechopper"
  PLUGIN_MANUFACTURER_CODE    Srch
  PLUGIN_CODE                 Schp
  FORMATS                     VST3 AU Standalone
  VST3_CATEGORIES             Fx Tools
  AU_MAIN_TYPE                kAudioUnitType_MusicEffect
  IS_SYNTH                    FALSE
  NEEDS_MIDI_INPUT            TRUE      # play slices from a MIDI clip after chopping
  NEEDS_MIDI_OUTPUT           FALSE
  IS_MIDI_EFFECT              FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD     ${SC_COPY_PLUGIN_AFTER_BUILD}
  MICROPHONE_PERMISSION_ENABLED TRUE    # standalone app records from the audio input
  MICROPHONE_PERMISSION_TEXT  "Sample Chopper needs audio input to capture loops."
  HARDENED_RUNTIME_ENABLED    TRUE)

target_sources(SampleChopper PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/WaveformView.cpp)

target_compile_definitions(SampleChopper PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(SampleChopper
  PRIVATE
    chopper_core
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Headless UI snapshot / smoke test (optional; used by CI and for README screenshots).
option(SC_BUILD_SNAPSHOT_TOOL "Build the headless editor snapshot tool" ON)
if(SC_BUILD_SNAPSHOT_TOOL)
  juce_add_console_app(SampleChopperSnapshot PRODUCT_NAME "SampleChopperSnapshot")
  target_sources(SampleChopperSnapshot PRIVATE Source/SnapshotMain.cpp)
  target_include_directories(SampleChopperSnapshot PRIVATE Source)
  target_link_libraries(SampleChopperSnapshot PRIVATE
    SampleChopper
    chopper_core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
endif()
