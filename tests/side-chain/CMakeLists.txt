# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI SIDE-CHAIN ON THE JUCE SHELL: its processor, its audio callback and its
# built bundle, as tests/CMakeLists.txt holds NI Trance Gate's.
#
#   sc_processor  the processor, its model and its editor in one program,
#                 around the real engine: the parameters and the state the
#                 iPlug2 build had, the buses, the MIDI and its panic, the
#                 model's snapshots and the shape, the editor on them  (quick)
#   sc_rt         its audio callback allocates nothing               (quick)
#   sc_host       the built NISideChain.vst3 as a DAW hosts it       (full)

set(NI_SC ${CMAKE_SOURCE_DIR}/plugins/side-chain)
set(_sc_sources
    ${NI_SC}/SideChain.cpp ${NI_SC}/Params.cpp ${NI_SC}/EngineModel.cpp ${NI_SC}/Kick.cpp
    ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
# What ni_add_juce_plugin gives the plugin's sources: its name, MIDI in, and
# the product's versions.json key.
set(_sc_definitions
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    "JucePlugin_Name=\"NI Side-Chain\"" JucePlugin_WantsMidiInput=1
    "NI_PRODUCT=\"side-chain\"")

# THE PROCESSOR, ITS MODEL AND ITS EDITOR, IN ONE PROGRAM: the product's
# sources and the shell's compiled in, as the plugin compiles them, around
# the real engine. tests/ui holds the editor on fakes.
juce_add_console_app(sc_processor PRODUCT_NAME sc_processor)
target_sources(sc_processor PRIVATE sc_processor.cpp ${_ni_shell_sources} ${_sc_sources})
target_include_directories(sc_processor PRIVATE
    ${NI_SC} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(sc_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
target_compile_definitions(sc_processor PRIVATE ${_sc_definitions}
    # A test lets the message loop run for a moment, for the parameters' news.
    JUCE_MODAL_LOOPS_PERMITTED=1
    "NI_VERSION_STRING=\"sc_processor\"" "NI_FIXTURES=\"${NI_FIXTURES}\"")
target_link_libraries(sc_processor PRIVATE
    sc_engine ni_editor_side_chain juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME sc_processor COMMAND sc_processor)

# ITS AUDIO CALLBACK ALLOCATES NOTHING: the processor under alloc_guard.c
# (tests/CMakeLists.txt, ni_alloc_guard), which counts every malloc of the
# calling thread. A dyld interposer, so macOS only.
if (APPLE)
    juce_add_console_app(sc_rt PRODUCT_NAME sc_rt)
    target_sources(sc_rt PRIVATE sc_rt.cpp ${_ni_shell_sources} ${_sc_sources})
    target_include_directories(sc_rt PRIVATE
        ${NI_SC} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
    target_compile_definitions(sc_rt PRIVATE ${_sc_definitions} "NI_VERSION_STRING=\"sc_rt\"")
    target_link_libraries(sc_rt PRIVATE
        sc_engine ni_editor_side_chain juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(sc_rt ni_alloc_guard)
    add_test(NAME sc_rt COMMAND sc_rt ${NI_FIXTURES})
    set_tests_properties(sc_rt PROPERTIES
        ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ni_alloc_guard>")
endif()

# THE BUILT NISideChain.vst3, AS A DAW HOSTS IT: sc_host.cpp says what only
# the bundle can show. The golden render is the engine's own
# (engines/side-chain/tests/render_golden.h, as sc_render_ab holds it).
juce_add_console_app(sc_host PRODUCT_NAME sc_host)
target_sources(sc_host PRIVATE sc_host.cpp)
target_include_directories(sc_host PRIVATE ${CMAKE_SOURCE_DIR}/engines/side-chain/tests)
target_compile_definitions(sc_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(sc_host PRIVATE
    juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME sc_host COMMAND sc_host ${NI_BUNDLES}/NISideChain.vst3 ${NI_FIXTURES})

ni_test_tiers(
    PRODUCT side-chain
        PROGRAMS sc_processor sc_rt sc_host
        QUICK sc_processor sc_rt
        FULL sc_host:host)
