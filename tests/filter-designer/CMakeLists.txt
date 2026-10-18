# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI FILTER DESIGNER ON THE JUCE SHELL: its processor, its audio callback and
# its built bundle, as Vocalize's are held. The engine's own claims are
# cargo's and C's (fd_core_rs, fd_core).
#
#   fd_processor  the processor on the real engine: the parameters from the
#                 engine's table, the buses, the sound, the state, the model
#                 and the window                                     (quick)
#   fd_rt         its audio callback allocates nothing                (quick)
#   fd_host       the built NIFilterDesigner.vst3 as a DAW hosts it   (full)

set(NI_FD ${CMAKE_SOURCE_DIR}/plugins/filter-designer)
# What ni_add_juce_plugin gives the plugin's sources: its name and the
# product's versions.json key. No MIDI in.
set(_fd_definitions
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    "JucePlugin_Name=\"NI Filter Designer\"" JucePlugin_WantsMidiInput=0
    "NI_PRODUCT=\"filter-designer\"")

juce_add_console_app(fd_processor PRODUCT_NAME fd_processor)
target_sources(fd_processor PRIVATE fd_processor.cpp ${_ni_shell_sources} ${NI_FD}/FilterDesigner.cpp)
target_include_directories(fd_processor PRIVATE ${NI_FD} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(fd_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
# Its frozen saved states (tests/state_fixture.h).
target_include_directories(fd_processor PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(fd_processor PRIVATE "NI_FIXTURES=\"${CMAKE_SOURCE_DIR}/tests/fixtures\"")
target_compile_definitions(fd_processor PRIVATE ${_fd_definitions}
    JUCE_MODAL_LOOPS_PERMITTED=1 "NI_VERSION_STRING=\"fd_processor\"")
target_link_libraries(fd_processor PRIVATE
    fd_engine ni_editor_filter_designer juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME fd_processor COMMAND fd_processor)

# ITS AUDIO CALLBACK ALLOCATES NOTHING: the processor under alloc_guard.c
# (tests/CMakeLists.txt, ni_alloc_guard). A dyld interposer, so macOS only.
if (APPLE)
    juce_add_console_app(fd_rt PRODUCT_NAME fd_rt)
    target_sources(fd_rt PRIVATE fd_rt.cpp ${_ni_shell_sources} ${NI_FD}/FilterDesigner.cpp)
    target_include_directories(fd_rt PRIVATE ${NI_FD} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
    target_compile_definitions(fd_rt PRIVATE ${_fd_definitions}
        JUCE_MODAL_LOOPS_PERMITTED=1 "NI_VERSION_STRING=\"fd_rt\"")
    target_link_libraries(fd_rt PRIVATE
        fd_engine ni_editor_filter_designer juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(fd_rt ni_alloc_guard)
    add_test(NAME fd_rt COMMAND fd_rt)
    set_tests_properties(fd_rt PROPERTIES
        ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ni_alloc_guard>")
endif()

# THE BUILT NIFilterDesigner.vst3, AS A DAW HOSTS IT.
juce_add_console_app(fd_host PRODUCT_NAME fd_host)
target_sources(fd_host PRIVATE fd_host.cpp)
target_compile_definitions(fd_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(fd_host PRIVATE
    juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME fd_host COMMAND fd_host ${NI_BUNDLES}/NIFilterDesigner.vst3)

ni_test_tiers(
    PRODUCT filter-designer
        PROGRAMS fd_processor fd_rt fd_host
        QUICK fd_processor fd_rt
        FULL fd_host:host)
