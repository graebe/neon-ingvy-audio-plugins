# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI SPECTROGRAM ON THE JUCE SHELL: its session, its processor, its audio
# callback and its built bundle, as tests/CMakeLists.txt holds NI Trance
# Gate's.
#
#   spectro_state  the chunk and the Session, plain C++: the fixtures, the
#                  layouts every build wrote, loads on a host's thread beside
#                  the receiver's service                             (quick)
#   spectro_wire   the session's strings read back                    (quick)
#   sg_processor   the processor, its model and its editor in one program,
#                  on the real analyzer and a real bus                (quick)
#   sg_rt          its audio callback allocates nothing               (quick)
#   sg_host        the built NISpectrogram.vst3 as a DAW hosts it     (full)
#
# Each names a bus namespace of its own (NIA_BUS_NS), so the buses it probes
# and claims are never a Live session's, another checkout's, or another
# test's running at the same time.

set(NI_SG ${CMAKE_SOURCE_DIR}/plugins/spectrogram)
set(_sg_sources
    ${NI_SG}/SpectrogramProcessor.cpp ${NI_SG}/EngineModel.cpp ${NI_SG}/State.cpp ${NI_SG}/Wire.cpp
    ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
set(_sg_definitions
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0
    "JucePlugin_Name=\"NI Spectrogram\"" JucePlugin_WantsMidiInput=0
    "NI_PRODUCT=\"spectrogram\"")

# THE SESSION, ON ITS OWN: State.cpp and Wire.cpp with the shell's codec, and
# the receiver the Session tests drive (spectro_engine, the one Rust archive).
add_executable(spectro_state spectro_state.cpp
    ${NI_SG}/State.cpp ${NI_SG}/Wire.cpp ${NI_JUCE_SHELL}/Nist.cpp
    ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
target_include_directories(spectro_state PRIVATE
    ${NI_SG} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(spectro_state SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
target_compile_definitions(spectro_state PRIVATE "NI_FIXTURES=\"${NI_FIXTURES}\"")
target_link_libraries(spectro_state PRIVATE spectro_engine)
add_test(NAME spectro_state COMMAND spectro_state)

add_executable(spectro_wire spectro_wire.cpp ${NI_SG}/Wire.cpp ${CMAKE_SOURCE_DIR}/plugins/_shared/ni/Wire.cpp)
target_include_directories(spectro_wire PRIVATE ${NI_SG} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(spectro_wire SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
add_test(NAME spectro_wire COMMAND spectro_wire)

# A Listen-In's sending half, as a process of its own: the bus's C ABI is
# another Rust archive than the receiver's, and one binary takes one.
add_executable(sg_bus_sender sg_bus_sender.c)
target_link_libraries(sg_bus_sender PRIVATE audio_bus_engine)
if (NOT WIN32)
    target_link_libraries(sg_bus_sender PRIVATE m)
endif()

# THE PROCESSOR, ITS MODEL AND ITS EDITOR, IN ONE PROGRAM: the product's
# sources and the shell's compiled in, as the plugin compiles them, around
# the real analyzer. tests/ui holds the editor on a fake.
juce_add_console_app(sg_processor PRODUCT_NAME sg_processor)
target_sources(sg_processor PRIVATE sg_processor.cpp ${_ni_shell_sources} ${_sg_sources})
target_include_directories(sg_processor PRIVATE
    ${NI_SG} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
target_include_directories(sg_processor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/doctest)
target_compile_definitions(sg_processor PRIVATE ${_sg_definitions}
    "NI_VERSION_STRING=\"sg_processor\"" "NI_FIXTURES=\"${NI_FIXTURES}\""
    "NI_BUS_SENDER=\"$<TARGET_FILE:sg_bus_sender>\"")
target_link_libraries(sg_processor PRIVATE
    spectro_engine ni_editor_spectrogram juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_dependencies(sg_processor sg_bus_sender)
add_test(NAME sg_processor COMMAND sg_processor)

# ITS AUDIO CALLBACK ALLOCATES NOTHING: the processor under alloc_guard.c
# (tests/CMakeLists.txt, ni_alloc_guard), which counts every malloc of the
# calling thread. A dyld interposer, so macOS only.
set(_sg_quick spectro_state spectro_wire sg_processor)
if (APPLE)
    juce_add_console_app(sg_rt PRODUCT_NAME sg_rt)
    target_sources(sg_rt PRIVATE sg_rt.cpp ${_ni_shell_sources} ${_sg_sources})
    target_include_directories(sg_rt PRIVATE
        ${NI_SG} ${NI_JUCE_SHELL} ${CMAKE_SOURCE_DIR}/plugins/_shared)
    target_compile_definitions(sg_rt PRIVATE ${_sg_definitions} "NI_VERSION_STRING=\"sg_rt\"")
    target_link_libraries(sg_rt PRIVATE
        spectro_engine ni_editor_spectrogram juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(sg_rt ni_alloc_guard)
    add_test(NAME sg_rt COMMAND sg_rt ${NI_FIXTURES})
    set_tests_properties(sg_rt PROPERTIES
        ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ni_alloc_guard>")
    list(APPEND _sg_quick sg_rt)
endif()

# THE BUILT NISpectrogram.vst3, AS A DAW HOSTS IT: sg_host.cpp says what only
# the bundle can show.
juce_add_console_app(sg_host PRODUCT_NAME sg_host)
target_sources(sg_host PRIVATE sg_host.cpp)
target_compile_definitions(sg_host PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(sg_host PRIVATE
    juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME sg_host COMMAND sg_host ${NI_BUNDLES}/NISpectrogram.vst3 ${NI_FIXTURES})
set_tests_properties(sg_host PROPERTIES ENVIRONMENT "NIA_BUS_NS=${CMAKE_BINARY_DIR}")

ni_test_tiers(
    QUICK ${_sg_quick}
    FULL sg_host:host)
