juce_add_plugin(TranceGate
    # ITS OWN VERSION, not the repository's. project(vst_library VERSION 0.1.0)
    # names the repo, which is built to hold more than one plugin; this is what
    # the AU and the VST3 report to a host.
    VERSION                   1.0.0
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

# JetBrains Mono, the design system's one family, bundled so the plugin looks
# the same on a machine that has never heard of it. SIL Open Font License 1.1
# (Resources/OFL.txt) -- the OFL permits bundling in any software, GPLv3
# included, provided the licence ships with it.
juce_add_binary_data(TranceGateResources SOURCES
    Resources/JetBrainsMono-Regular.ttf
    Resources/JetBrainsMono-Medium.ttf)
set_target_properties(TranceGateResources PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(TranceGate PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Uv.cpp
    Source/UvLookAndFeel.cpp
    Source/UvComponents.cpp)

target_compile_definitions(TranceGate PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # The MACROS are host parameters; the PATTERN is not. Rate, Length,
    # Amount, Gate, the envelope, Slot and Legato automate; the 128 steps and
    # their per-step amounts stay in the saved state, because exposing them is
    # 128 x 8 = 1024 parameters -- or 256 that get silently rewritten every
    # time Slot moves, which is worse than not having them.
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TranceGate
    # tg_engine is the submodule's Rust crate, built once at the top level and
    # linked by the plugin and every test target -- see cmake/TgEngine.cmake.
    # It carries the include directory for trance_gate_core.h with it.
    PRIVATE tg_engine
            TranceGateResources
            juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
