# The C++ tests, on doctest.
#
# WHY THERE ARE ANY AGAIN. Three C++ targets -- tg_params, tg_vst3 and tg_shot
# -- hosted JUCE classes directly and went with JUCE; see the note at the top
# of ../CMakeLists.txt. Nothing replaced them, so the plugin shells have been
# the one part of this repository with no framework pointed at them at all.
#
# WHY THEY LINK ALMOST NOTHING. iplug_configure_target refuses any target that
# is not a plugin format, and IPlug_include_in_plug_hdr.h stops with
# `#error "No API defined!"` outside one -- so a test CANNOT link the plugin's
# own translation unit. What it can link is the arithmetic that was lifted out
# of it into Wire.cpp, which includes <string> and nothing else. Each binary
# below is two files and builds in about a second.
#
# doctest is MIT and one header, vendored at external/doctest. Catch2 (BSL-1.0)
# and GoogleTest (BSD-3) are both permissive and both would have put the first
# exception into THIRD_PARTY_LICENSES.md's "the licence is uniform: MIT
# throughout" -- which is a sentence worth keeping literally true.

set(DOCTEST_DIR ${CMAKE_SOURCE_DIR}/external/doctest)

if (NOT EXISTS ${DOCTEST_DIR}/doctest.h)
    message(FATAL_ERROR
        "external/doctest/doctest.h is missing -- it is vendored, not fetched. "
        "See THIRD_PARTY_LICENSES.md for the version and provenance.")
endif()

# ------------------------------------------------------------- Trance Gate

add_executable(tg_wire tg_wire.cpp
               ${CMAKE_SOURCE_DIR}/plugins/trance-gate/Wire.cpp)
target_include_directories(tg_wire PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/trance-gate)
add_test(NAME tg_wire COMMAND tg_wire)

# THE PATTERN'S SAVE AND RELOAD, through Patch.cpp -- the plugin's own code for
# it -- against the real engine. The one test here that links an engine: what it
# guards is exactly the seam between the shell and the engine's state, and a
# stub on either side would be asserting on the stub. iPlug2's IByteChunk is
# header-only, so the chunk is the one the plugin writes.
set(IPLUG2_DIR ${CMAKE_SOURCE_DIR}/external/iPlug2)
add_executable(tg_state tg_state.cpp
               ${CMAKE_SOURCE_DIR}/plugins/trance-gate/Patch.cpp
               ${CMAKE_SOURCE_DIR}/plugins/trance-gate/Wire.cpp)
target_include_directories(tg_state PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/trance-gate
    ${IPLUG2_DIR}/IPlug ${IPLUG2_DIR}/WDL)
target_link_libraries(tg_state PRIVATE tg_engine shell_engine)
add_test(NAME tg_state COMMAND tg_state)

# THE STATE HEADER against every plugin's older chunk layout, byte for byte.
add_executable(shell_state shell_state.cpp)
target_include_directories(shell_state PRIVATE
    ${DOCTEST_DIR} ${IPLUG2_DIR}/IPlug ${IPLUG2_DIR}/WDL)
target_link_libraries(shell_state PRIVATE shell_engine)
add_test(NAME shell_state COMMAND shell_state)

# -------------------------------------------------------------- Spectrogram

add_executable(spectro_wire spectro_wire.cpp
               ${CMAKE_SOURCE_DIR}/plugins/spectrogram/Wire.cpp)
target_include_directories(spectro_wire PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/spectrogram)

# THE WIRE ORACLE, both halves -- the curve table's arrangement, applied to the
# format the editor decodes.
#
# The editor's test used to carry its own transcription of the encoder and said
# so in a comment. A transcription agrees with its source until somebody edits
# one of them, which is how the S-curve stayed wrong in curves.js.
#
#   spectro_wire        Wire.cpp still agrees with ui/test/wire_table.txt
#   spectro_columns_js  ui/src/lib/columns.js still agrees with the same file
#
# Regenerate with `spectro_wire --dump > ui/test/wire_table.txt`, and only when
# the wire format is MEANT to change -- which means the decoder changes in the
# same commit.
set(SPECTRO_WIRE_TABLE
    ${CMAKE_SOURCE_DIR}/plugins/spectrogram/ui/test/wire_table.txt)

add_test(NAME spectro_wire
         COMMAND spectro_wire --verify ${SPECTRO_WIRE_TABLE})

# --------------------------------------------------------------- Listen-In

add_executable(listenin_wire listenin_wire.cpp
               ${CMAKE_SOURCE_DIR}/plugins/listen-in/Wire.cpp)
target_include_directories(listenin_wire PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/listen-in)

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
# ------------------------------------------------------------ NI Side-Chain

add_executable(sc_wire sc_wire.cpp
               ${CMAKE_SOURCE_DIR}/plugins/side-chain/Wire.cpp)
target_include_directories(sc_wire PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/side-chain)
add_test(NAME sc_wire COMMAND sc_wire)
