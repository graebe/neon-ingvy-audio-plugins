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
#   cd_rt         its audio callback allocates nothing                 (quick)

set(NI_CD ${CMAKE_SOURCE_DIR}/plugins/chord-detector)
# What ni_add_juce_plugin gives the plugin's sources: its name and the
# product's versions.json key. MIDI in.
set(_cd_definitions
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    "JucePlugin_Name=\"NI Chord-Detector\"" JucePlugin_WantsMidiInput=1
    "NI_PRODUCT=\"chord-detector\"")

juce_add_console_app(cd_processor PRODUCT_NAME cd_processor)
target_sources(cd_processor PRIVATE cd_processor.cpp ${_ni_shell_sources} ${NI_CD}/ChordDetector.cpp)
target_include_directories(cd_processor PRIVATE
    ${NI_CD} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(cd_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
# Its frozen saved states (tests/state_fixture.h).
target_include_directories(cd_processor PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(cd_processor PRIVATE "NI_FIXTURES=\"${CMAKE_SOURCE_DIR}/tests/fixtures\"")
target_compile_definitions(cd_processor PRIVATE ${_cd_definitions} "NI_VERSION_STRING=\"cd_processor\"")
target_link_libraries(cd_processor PRIVATE
    cd_engine ni_editor_chord_detector juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME cd_processor COMMAND cd_processor)

# ITS AUDIO CALLBACK ALLOCATES NOTHING: the processor under alloc_guard.c
# (tests/CMakeLists.txt, ni_alloc_guard). A dyld interposer, so macOS only.
if (APPLE)
    juce_add_console_app(cd_rt PRODUCT_NAME cd_rt)
    target_sources(cd_rt PRIVATE cd_rt.cpp ${_ni_shell_sources} ${NI_CD}/ChordDetector.cpp)
    target_include_directories(cd_rt PRIVATE ${NI_CD} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
    target_compile_definitions(cd_rt PRIVATE ${_cd_definitions} "NI_VERSION_STRING=\"cd_rt\"")
    target_link_libraries(cd_rt PRIVATE
        cd_engine ni_editor_chord_detector juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(cd_rt ni_alloc_guard)
    add_test(NAME cd_rt COMMAND cd_rt)
    set_tests_properties(cd_rt PROPERTIES
        ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ni_alloc_guard>")
endif()

ni_test_tiers(
    PRODUCT chord-detector
        PROGRAMS cd_processor cd_rt
        QUICK cd_processor cd_rt)
