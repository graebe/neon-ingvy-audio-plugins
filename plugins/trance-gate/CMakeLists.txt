# The engine, compiled from the submodule. NOT copied in: this is the same
# translation unit Schwung builds for the Move module.
set(TG_ENGINE ${CMAKE_SOURCE_DIR}/external/schwung-trance-gate/src/dsp)

juce_add_plugin(TranceGate
    PLUGIN_MANUFACTURER_CODE  Grbe
    PLUGIN_CODE               TrGt
    COMPANY_NAME              "graebe"
    PRODUCT_NAME              "Trance Gate"
    BUNDLE_ID                 "com.graebe.trancegate"
    FORMATS                   AU VST3 Standalone
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE)

target_sources(TranceGate PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${TG_ENGINE}/trance_gate_core.c)

target_include_directories(TranceGate PRIVATE ${TG_ENGINE})

target_compile_definitions(TranceGate PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # No parameters in v1: the pattern, the slots and the macros all live in
    # the saved state and are edited in the plugin's own editor, exactly as
    # they do on Move. Automation is an additive change once the editor
    # exists; guessing at it on paper is how the 520-parameter design died.
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TranceGate
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
