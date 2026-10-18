# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI VOCALIZE ON THE JUCE SHELL: its processor, its audio callback and its
# built bundle, as the Side-Chain's and the Bit-Crusher's are held. The
# engine's own claims are cargo's and C's (vz_core_rs, vz_core, vz_render_ab),
# the editor's are tests/ui's.
#
#   vz_processor  the processor on the real engine: the parameters from the
#                 engine's table, Mode and the latency it reports, the buses,
#                 the sound, the state, the model and the window     (quick)
#   vz_rt         its audio callback allocates nothing, a Mode switch and
#                 its latency request included                       (quick)
#   vz_host       the built NIVocalize.vst3 as a DAW hosts it        (full)

set(NI_VZ ${CMAKE_SOURCE_DIR}/plugins/vocalize)
# What ni_add_juce_plugin gives the plugin's sources: its name and the
# product's versions.json key. No MIDI in.
set(_vz_definitions
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    "JucePlugin_Name=\"NI Vocalize\"" JucePlugin_WantsMidiInput=0
    "NI_PRODUCT=\"vocalize\"")

juce_add_console_app(vz_processor PRODUCT_NAME vz_processor)
target_sources(vz_processor PRIVATE vz_processor.cpp ${_ni_shell_sources} ${NI_VZ}/Vocalize.cpp)
target_include_directories(vz_processor PRIVATE ${NI_VZ} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(vz_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
# Its frozen saved states (tests/state_fixture.h).
target_include_directories(vz_processor PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(vz_processor PRIVATE "NI_FIXTURES=\"${CMAKE_SOURCE_DIR}/tests/fixtures\"")
target_compile_definitions(vz_processor PRIVATE ${_vz_definitions}
    # A test lets the message loop run for a moment: the latency follows Mode
    # there (a timer), and a capture's answer comes back there.
    JUCE_MODAL_LOOPS_PERMITTED=1 "NI_VERSION_STRING=\"vz_processor\"")
target_link_libraries(vz_processor PRIVATE
    vz_engine vz_recordings ni_editor_vocalize juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME vz_processor COMMAND vz_processor)

# ITS AUDIO CALLBACK ALLOCATES NOTHING: the processor under alloc_guard.c
# (tests/CMakeLists.txt, ni_alloc_guard). A dyld interposer, so macOS only.
if (APPLE)
    juce_add_console_app(vz_rt PRODUCT_NAME vz_rt)
    target_sources(vz_rt PRIVATE vz_rt.cpp ${_ni_shell_sources} ${NI_VZ}/Vocalize.cpp)
    target_include_directories(vz_rt PRIVATE ${NI_VZ} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
    target_compile_definitions(vz_rt PRIVATE ${_vz_definitions}
        JUCE_MODAL_LOOPS_PERMITTED=1 "NI_VERSION_STRING=\"vz_rt\"")
    target_link_libraries(vz_rt PRIVATE
        vz_engine vz_recordings ni_editor_vocalize juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(vz_rt ni_alloc_guard)
    add_test(NAME vz_rt COMMAND vz_rt)
    set_tests_properties(vz_rt PROPERTIES
        ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ni_alloc_guard>")
endif()

# THE BUILT NIVocalize.vst3, AS A DAW HOSTS IT: vz_host.cpp says what only
# the bundle can show. The golden renders are the engine's own
# (engines/vocalize/tests/render_golden.h, as vz_render_ab holds them).
juce_add_console_app(vz_host PRODUCT_NAME vz_host)
target_sources(vz_host PRIVATE vz_host.cpp)
target_include_directories(vz_host PRIVATE ${CMAKE_SOURCE_DIR}/engines/vocalize/tests)
target_compile_definitions(vz_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(vz_host PRIVATE
    juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME vz_host COMMAND vz_host ${NI_BUNDLES}/NIVocalize.vst3)

# THE INTEL SLICE TOO. A universal bundle carries two builds of the engine,
# and the maths libraries under them are not the same: vz_host run under
# Rosetta loads the x86_64 slice and holds it to the same golden renders, bit
# for bit. Registered where that can run at all -- a universal build, on an
# arm64 Mac with Rosetta -- as the bus's cross-slice tests are.
set(_vz_rosetta_tier)
if (APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64"
    AND "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES
    AND "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND /usr/bin/arch -x86_64 /usr/bin/true
                    RESULT_VARIABLE _vz_rosetta OUTPUT_QUIET ERROR_QUIET)
    if (_vz_rosetta EQUAL 0)
        add_test(NAME vz_host_rosetta
                 COMMAND /usr/bin/arch -x86_64 $<TARGET_FILE:vz_host> ${NI_BUNDLES}/NIVocalize.vst3)
        set(_vz_rosetta_tier vz_host_rosetta:host)
    endif()
endif()

ni_test_tiers(
    PRODUCT vocalize
        PROGRAMS vz_processor vz_rt vz_host
        QUICK vz_processor vz_rt
        FULL vz_host:host ${_vz_rosetta_tier})
