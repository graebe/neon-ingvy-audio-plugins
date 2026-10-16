# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI BIT-CRUSHER ON THE JUCE SHELL: its processor, its audio callback and its
# built bundle, as the Side-Chain's are held. The engine's own claims are
# cargo's and C's (bc_core_rs, bc_core, bc_render_ab), the editor's are
# tests/ui's.
#
#   bc_processor  the processor on the real engine: the parameters from the
#                 engine's table, the buses, the sound, the state, the model
#                 and the window                                     (quick)
#   bc_rt         its audio callback allocates nothing               (quick)
#   bc_host       the built NIBitCrusher.vst3 as a DAW hosts it      (full)

set(NI_BC ${CMAKE_SOURCE_DIR}/plugins/bit-crusher)
# What ni_add_juce_plugin gives the plugin's sources: its name and the
# product's versions.json key. No MIDI in.
set(_bc_definitions
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    "JucePlugin_Name=\"NI Bit-Crusher\"" JucePlugin_WantsMidiInput=0
    "NI_PRODUCT=\"bit-crusher\"")

juce_add_console_app(bc_processor PRODUCT_NAME bc_processor)
target_sources(bc_processor PRIVATE bc_processor.cpp ${_ni_shell_sources} ${NI_BC}/BitCrusher.cpp)
target_include_directories(bc_processor PRIVATE ${NI_BC} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(bc_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
# Its frozen saved states (tests/state_fixture.h).
target_include_directories(bc_processor PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(bc_processor PRIVATE "NI_FIXTURES=\"${CMAKE_SOURCE_DIR}/tests/fixtures\"")
target_compile_definitions(bc_processor PRIVATE ${_bc_definitions}
    JUCE_MODAL_LOOPS_PERMITTED=1 "NI_VERSION_STRING=\"bc_processor\"")
target_link_libraries(bc_processor PRIVATE
    bc_engine ni_editor_bit_crusher juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME bc_processor COMMAND bc_processor)

# ITS AUDIO CALLBACK ALLOCATES NOTHING: the processor under alloc_guard.c
# (tests/CMakeLists.txt, ni_alloc_guard). A dyld interposer, so macOS only.
if (APPLE)
    juce_add_console_app(bc_rt PRODUCT_NAME bc_rt)
    target_sources(bc_rt PRIVATE bc_rt.cpp ${_ni_shell_sources} ${NI_BC}/BitCrusher.cpp)
    target_include_directories(bc_rt PRIVATE ${NI_BC} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
    target_compile_definitions(bc_rt PRIVATE ${_bc_definitions} "NI_VERSION_STRING=\"bc_rt\"")
    target_link_libraries(bc_rt PRIVATE
        bc_engine ni_editor_bit_crusher juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(bc_rt ni_alloc_guard)
    add_test(NAME bc_rt COMMAND bc_rt)
    set_tests_properties(bc_rt PROPERTIES
        ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ni_alloc_guard>")
endif()

# THE BUILT NIBitCrusher.vst3, AS A DAW HOSTS IT: bc_host.cpp says what only
# the bundle can show. The golden render is the engine's own
# (engines/bit-crusher/tests/render_golden.h, as bc_render_ab holds it).
juce_add_console_app(bc_host PRODUCT_NAME bc_host)
target_sources(bc_host PRIVATE bc_host.cpp)
target_include_directories(bc_host PRIVATE ${CMAKE_SOURCE_DIR}/engines/bit-crusher/tests)
target_compile_definitions(bc_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(bc_host PRIVATE
    juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME bc_host COMMAND bc_host ${NI_BUNDLES}/NIBitCrusher.vst3)

ni_test_tiers(
    PRODUCT bit-crusher
        PROGRAMS bc_processor bc_rt bc_host
        QUICK bc_processor bc_rt
        FULL bc_host:host)
