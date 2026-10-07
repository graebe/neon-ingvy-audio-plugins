# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI LISTEN-IN ON THE JUCE SHELL: its processor, its audio callback and its
# built bundle, as tests/CMakeLists.txt holds NI Trance Gate's.
#
#   li_processor  the processor, its model and its editor in one program, on
#                 the real bus: the parameter and the state the iPlug2 build
#                 had, the claim and its release, the audio through bit for
#                 bit and on the bus, the name kept and published   (quick)
#   li_rt         its audio callback allocates nothing               (quick)
#   li_host       the built NIListenIn.vst3 as a DAW hosts it        (full)
#
# Each one names a bus namespace of its own (NIA_BUS_NS, from its process id),
# so it never meets a Live session's buses, another checkout's, or another
# test's running at the same time.

set(NI_LI ${CMAKE_SOURCE_DIR}/plugins/listen-in)
set(_li_sources
    ${NI_LI}/ListenIn.cpp ${NI_LI}/EngineModel.cpp ${NI_LI}/State.cpp ${NI_LI}/Wire.cpp
    ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
set(_li_definitions
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    "JucePlugin_Name=\"NI Listen-In\"" JucePlugin_WantsMidiInput=0
    "NI_PRODUCT=\"listen-in\"")

# THE PROCESSOR, ITS MODEL AND ITS EDITOR, IN ONE PROGRAM: the product's
# sources and the shell's compiled in, as the plugin compiles them, around
# the real bus. tests/ui holds the editor on fakes.
juce_add_console_app(li_processor PRODUCT_NAME li_processor)
target_sources(li_processor PRIVATE li_processor.cpp ${_ni_shell_sources} ${_li_sources})
target_include_directories(li_processor PRIVATE
    ${NI_LI} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(li_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
target_compile_definitions(li_processor PRIVATE ${_li_definitions}
    # A test lets the message loop run for a moment, for the bus service.
    JUCE_MODAL_LOOPS_PERMITTED=1
    "NI_VERSION_STRING=\"li_processor\"" "NI_FIXTURES=\"${NI_FIXTURES}\"")
target_link_libraries(li_processor PRIVATE
    audio_bus_engine ni_editor_listen_in juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME li_processor COMMAND li_processor)

# ITS AUDIO CALLBACK ALLOCATES NOTHING: the processor under alloc_guard.c
# (tests/CMakeLists.txt, ni_alloc_guard), which counts every malloc of the
# calling thread. A dyld interposer, so macOS only.
if (APPLE)
    juce_add_console_app(li_rt PRODUCT_NAME li_rt)
    target_sources(li_rt PRIVATE li_rt.cpp ${_ni_shell_sources} ${_li_sources})
    target_include_directories(li_rt PRIVATE
        ${NI_LI} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
    target_compile_definitions(li_rt PRIVATE ${_li_definitions} "NI_VERSION_STRING=\"li_rt\"")
    target_link_libraries(li_rt PRIVATE
        audio_bus_engine ni_editor_listen_in juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(li_rt ni_alloc_guard)
    add_test(NAME li_rt COMMAND li_rt ${NI_FIXTURES})
    set_tests_properties(li_rt PROPERTIES
        ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ni_alloc_guard>")
endif()

# THE BUILT NIListenIn.vst3, AS A DAW HOSTS IT, read back through the bus's own
# C ABI: li_host.cpp says what only the bundle can show.
juce_add_console_app(li_host PRODUCT_NAME li_host)
target_sources(li_host PRIVATE li_host.cpp)
target_compile_definitions(li_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(li_host PRIVATE
    audio_bus_engine juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME li_host COMMAND li_host ${NI_BUNDLES}/NIListenIn.vst3 ${NI_FIXTURES})

ni_test_tiers(
    QUICK li_processor li_rt
    FULL li_host:host)
