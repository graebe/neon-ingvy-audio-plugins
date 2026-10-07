# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The C++ tests, on doctest.
#
# WHY THEY LINK SO LITTLE. iplug_configure_target refuses any target that is
# not a plugin format, and IPlug_include_in_plug_hdr.h stops with `#error "No
# API defined!"` outside one -- so a test cannot link a plugin class, or the
# shared ni::WebPlugin. What it links is what was lifted out of them: the shell's
# wire and editor protocol (plugins/_shared/ni), each product's Wire, Params,
# Patch and State, iPlug2's IPlugParameter.cpp and, where the seam is the point,
# the real engine.
#
# doctest is MIT and one header, vendored at external/doctest. Nothing that
# ships links it, so THIRD_PARTY_LICENSES.md lists it as test-only.

set(DOCTEST_DIR ${CMAKE_SOURCE_DIR}/external/doctest)

if (NOT EXISTS ${DOCTEST_DIR}/doctest.h)
    message(FATAL_ERROR
        "external/doctest/doctest.h is missing -- it is vendored, not fetched. "
        "See THIRD_PARTY_LICENSES.md for the version and provenance.")
endif()

set(NI_SHARED ${CMAKE_SOURCE_DIR}/plugins/_shared)
set(NI_WIRE_SRC ${NI_SHARED}/ni/Wire.cpp)

# ------------------------------------------------------------ the shell

# What every shell does to a buffer or a message: the split, the quantisers,
# the height, the transport, the chunking, the passthrough, the scope capture
# and the numbers -- the last under a comma-decimal locale too.
add_executable(ni_wire ni_wire.cpp ${NI_WIRE_SRC})
target_include_directories(ni_wire PRIVATE ${DOCTEST_DIR} ${NI_SHARED})
add_test(NAME ni_wire COMMAND ni_wire)

# THE EDITOR PROTOCOL, over a recording Port: ready, defaults, typed text and
# the height, as ni::WebPlugin speaks them without a host.
add_executable(ni_editor ni_editor.cpp ${NI_SHARED}/ni/Editor.cpp ${NI_WIRE_SRC})
target_include_directories(ni_editor PRIVATE ${DOCTEST_DIR} ${NI_SHARED})
add_test(NAME ni_editor COMMAND ni_editor)

# ------------------------------------------------------------- Trance Gate

# The Trance Gate's state and parameters are tested on the JUCE shell now,
# with its processor in the program (tests/tg_processor.cpp): the iPlug2
# product these two linked is gone. iPlug2's parameter class stays for the
# three products still on it.
set(IPLUG2_DIR ${CMAKE_SOURCE_DIR}/external/iPlug2)
set(IPLUG_PARAM_SRC ${IPLUG2_DIR}/IPlug/IPlugParameter.cpp)

# THE STATE HEADER against every plugin's older chunk layout, byte for byte.
add_executable(shell_state shell_state.cpp ${IPLUG_PARAM_SRC})
target_include_directories(shell_state PRIVATE
    ${DOCTEST_DIR} ${IPLUG2_DIR}/IPlug ${IPLUG2_DIR}/WDL)
target_link_libraries(shell_state PRIVATE shell_engine)
add_test(NAME shell_state COMMAND shell_state)

# THE AUDIO THREAD'S FLUSH-TO-ZERO GUARD, which all four ProcessBlocks open with.
add_executable(shell_denormals shell_denormals.cpp)
target_include_directories(shell_denormals PRIVATE ${DOCTEST_DIR})
target_link_libraries(shell_denormals PRIVATE shell_engine)
add_test(NAME shell_denormals COMMAND shell_denormals)

# --------------------------------------------------------------- Listen-In

add_executable(listenin_wire listenin_wire.cpp
               ${CMAKE_SOURCE_DIR}/plugins/listen-in/Wire.cpp ${NI_WIRE_SRC})
# audio_bus.h for ABUS_MAX_SLOT only -- a macro, so nothing is linked; the
# header is generated, so the target waits for the cargo run that writes it.
target_include_directories(listenin_wire PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/listen-in ${NI_SHARED}
    ${NI_CAPI_INCLUDE_DIR})
add_dependencies(listenin_wire ni_rust_engines)

# THE WIRE ORACLE, both halves -- the Spectrogram's arrangement, applied to the
# state string and the label sanitiser.
#
#   listenin_wire      Wire.cpp still agrees with ui/test/wire_table.txt
#   listenin_wire_js   ui/src/lib/state.js still agrees with the same file
#
# Regenerate with `listenin_wire --dump > ui/test/wire_table.txt`, and only when
# the format is MEANT to change -- which means the decoder changes in the same
# commit.
set(LISTENIN_WIRE_TABLE
    ${CMAKE_SOURCE_DIR}/plugins/listen-in/ui/test/wire_table.txt)

add_test(NAME listenin_wire
         COMMAND listenin_wire --verify ${LISTENIN_WIRE_TABLE})
# Listen-In's chunk, saved and reloaded as clap-validator's state tests do.
add_executable(listenin_state listenin_state.cpp
               ${CMAKE_SOURCE_DIR}/plugins/listen-in/State.cpp
               ${CMAKE_SOURCE_DIR}/plugins/listen-in/Wire.cpp
               ${NI_WIRE_SRC}
               ${IPLUG_PARAM_SRC})
target_include_directories(listenin_state PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/listen-in ${NI_SHARED}
    ${IPLUG2_DIR}/IPlug ${IPLUG2_DIR}/WDL)
target_link_libraries(listenin_state PRIVATE audio_bus_engine shell_engine)
add_test(NAME listenin_state COMMAND listenin_state)

# All of them quick: each is a doctest binary over code lifted out of a plugin,
# with no bundle, host or second process. cmake/NiTest.cmake.
ni_test_tiers(QUICK
    ni_wire ni_editor shell_state shell_denormals
    listenin_wire listenin_state)
