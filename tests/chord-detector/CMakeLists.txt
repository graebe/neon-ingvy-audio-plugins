# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI CHORD-DETECTOR ON THE JUCE SHELL: its processor, as tests/CMakeLists.txt
# holds NI Trance Gate's. The engine's own claims are cargo's and C's
# (cd_core_rs, cd_core), the editor's are tests/ui's, and the built bundle is
# juce_host_NIChordDetector's.
#
#   cd_processor  the processor on the real engine: a chord named, silence
#                 out, no bypass parameter of its own, and the lane heard
#                 while the host bypasses it                           (quick)

set(NI_CD ${CMAKE_SOURCE_DIR}/plugins/chord-detector)

juce_add_console_app(cd_processor PRODUCT_NAME cd_processor)
target_sources(cd_processor PRIVATE cd_processor.cpp ${_ni_shell_sources} ${NI_CD}/ChordDetector.cpp)
target_include_directories(cd_processor PRIVATE
    ${NI_CD} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(cd_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
target_compile_definitions(cd_processor PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    "JucePlugin_Name=\"NI Chord-Detector\"" JucePlugin_WantsMidiInput=1
    "NI_PRODUCT=\"chord-detector\"" "NI_VERSION_STRING=\"cd_processor\"")
target_link_libraries(cd_processor PRIVATE
    cd_engine ni_editor_chord_detector juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME cd_processor COMMAND cd_processor)

ni_test_tiers(QUICK cd_processor)
