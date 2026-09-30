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
# doctest is MIT and one header, vendored at external/doctest, which keeps
# THIRD_PARTY_LICENSES.md's "MIT throughout" literally true.

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

# THE PATTERN'S SAVE AND RELOAD, through Patch.cpp -- the plugin's own code for
# it -- against the real engine. The one test here that links an engine: what it
# guards is exactly the seam between the shell and the engine's state, and a
# stub on either side would be asserting on the stub. iPlug2's IByteChunk is
# header-only, so the chunk is the one the plugin writes.
#
# THE PARAMETERS ARE REAL. iplug::IParam links on its own with
# IPlugParameter.cpp, so the chunk's parameter block comes from the plugin's own
# declarations (Params.cpp) rather than fifteen stand-in doubles -- which is what
# lets these tests do what clap-validator's state-reproducibility does: random
# values, save, a fresh instance, load, save again, byte for byte.
set(IPLUG2_DIR ${CMAKE_SOURCE_DIR}/external/iPlug2)
set(IPLUG_PARAM_SRC ${IPLUG2_DIR}/IPlug/IPlugParameter.cpp)
add_executable(tg_state tg_state.cpp
               ${CMAKE_SOURCE_DIR}/plugins/trance-gate/Params.cpp
               ${CMAKE_SOURCE_DIR}/plugins/trance-gate/Patch.cpp
               ${NI_WIRE_SRC}
               ${IPLUG_PARAM_SRC})
target_include_directories(tg_state PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/trance-gate ${NI_SHARED}
    ${IPLUG2_DIR}/IPlug ${IPLUG2_DIR}/WDL)
target_link_libraries(tg_state PRIVATE tg_engine shell_engine)
add_test(NAME tg_state COMMAND tg_state)

# EVERY PARAMETER'S TEXT PARSES BACK TO ITS VALUE -- clap-validator's
# param-conversions, on the plugin's declarations and the CLAP wrapper's own
# conversions (param_host.h).
add_executable(tg_params tg_params.cpp
               ${CMAKE_SOURCE_DIR}/plugins/trance-gate/Params.cpp
               ${NI_WIRE_SRC}
               ${IPLUG_PARAM_SRC})
target_include_directories(tg_params PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/trance-gate ${NI_SHARED}
    ${IPLUG2_DIR}/IPlug ${IPLUG2_DIR}/WDL)
target_link_libraries(tg_params PRIVATE tg_engine)
add_test(NAME tg_params COMMAND tg_params)

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

# -------------------------------------------------------------- Spectrogram

add_executable(spectro_wire spectro_wire.cpp
               ${CMAKE_SOURCE_DIR}/plugins/spectrogram/Wire.cpp ${NI_WIRE_SRC})
target_include_directories(spectro_wire PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/spectrogram ${NI_SHARED})

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
               ${CMAKE_SOURCE_DIR}/plugins/listen-in/Wire.cpp ${NI_WIRE_SRC})
# audio_bus.h for ABUS_MAX_SLOT only -- a macro, so nothing is linked.
target_include_directories(listenin_wire PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/listen-in ${NI_SHARED}
    ${CMAKE_SOURCE_DIR}/engines/audio-bus/include)

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

# THE PARAMETERS AS A HOST SEES THEM -- their text and the state chunk, on the
# plugin's own Params.cpp and real IParams. See tg_params and tg_state above.
# The engine, because the Rate options are its own table.
add_executable(sc_params sc_params.cpp
               ${CMAKE_SOURCE_DIR}/plugins/side-chain/Params.cpp
               ${IPLUG_PARAM_SRC})
target_include_directories(sc_params PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/side-chain
    ${IPLUG2_DIR}/IPlug ${IPLUG2_DIR}/WDL)
target_link_libraries(sc_params PRIVATE sc_engine shell_engine)
add_test(NAME sc_params COMMAND sc_params)

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

# The Spectrogram's chunk, saved and reloaded as clap-validator's state tests do.
add_executable(spectro_state spectro_state.cpp
               ${CMAKE_SOURCE_DIR}/plugins/spectrogram/State.cpp
               ${CMAKE_SOURCE_DIR}/plugins/spectrogram/Wire.cpp
               ${NI_WIRE_SRC}
               ${IPLUG_PARAM_SRC})
target_include_directories(spectro_state PRIVATE
    ${DOCTEST_DIR} ${CMAKE_SOURCE_DIR}/plugins/spectrogram ${NI_SHARED}
    ${IPLUG2_DIR}/IPlug ${IPLUG2_DIR}/WDL)
target_link_libraries(spectro_state PRIVATE shell_engine)
add_test(NAME spectro_state COMMAND spectro_state)
