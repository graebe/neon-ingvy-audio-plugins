# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Trance Gate on JUCE 9: the spike that proves a JUCE build can take over
# the Live sets the iPlug2 VST3 saved. spike/UT2.md is the manual Live test.
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# ITS OWN PROJECT, NOT PART OF THE ROOT BUILD. The root build is iPlug2's, and
# until the refactor replaces it nothing here may change what it builds,
# installs or tests:
#
#   cmake -S spike/juce-trance-gate -B build-spike/juce-trance-gate -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-spike/juce-trance-gate -j
#   ctest --test-dir build-spike/juce-trance-gate --output-on-failure
#
# Nothing is installed: COPY_PLUGIN_AFTER_BUILD is off, and the `spike_out`
# target copies the bundle to spike/out for the manual test, nowhere else.
cmake_minimum_required(VERSION 3.22)

# The root build's defaults, for the root build's reasons: Live runs native or
# under Rosetta, and a universal bundle loads either way. Before project(), as
# there; a -D still wins.
if (CMAKE_HOST_APPLE)
    if (NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures to build: universal by default")
    endif()
    if (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS the plugin loads on")
    endif()
endif()

project(ni_juce_trance_gate_spike VERSION 2026.10.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(NI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(NI_FIXTURES ${NI_ROOT}/tests/fixtures/iplug2)

add_subdirectory(${NI_ROOT}/external/JUCE ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

# ------------------------------------------------------------ the engine
#
# THE SAME RUST ENGINE THE iPlug2 BUILD LINKS: tg-capi's static library, from
# the repository's one cargo workspace, built here into this build's own
# target directory. Cargo is the dependency scanner, so the target always runs
# and is a no-op when nothing changed. The workspace's release profile aborts
# on panic, which stays: the audio callback must not unwind.
include(${NI_ROOT}/cmake/RustToolchain.cmake)

set(NI_RUST_TARGET_DIR ${CMAKE_BINARY_DIR}/rust)
set(NI_TG_CAPI ${CMAKE_BINARY_DIR}/rust-lib/libtg_capi.a)
set(cargo_args build --release -p tg-capi --target-dir ${NI_RUST_TARGET_DIR})
set(slices)
foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    if (arch STREQUAL "arm64")
        set(triple aarch64-apple-darwin)
    elseif (arch STREQUAL "x86_64")
        set(triple x86_64-apple-darwin)
    else()
        message(FATAL_ERROR "no Rust target known for OSX architecture '${arch}'")
    endif()
    list(APPEND cargo_args --target ${triple})
    list(APPEND slices ${NI_RUST_TARGET_DIR}/${triple}/release/libtg_capi.a)
endforeach()
if (NOT slices)
    set(slices ${NI_RUST_TARGET_DIR}/release/libtg_capi.a)
endif()
list(LENGTH slices n_slices)
if (n_slices GREATER 1)
    set(combine COMMAND lipo -create ${slices} -output ${NI_TG_CAPI}.new)
else()
    set(combine COMMAND ${CMAKE_COMMAND} -E copy ${slices} ${NI_TG_CAPI}.new)
endif()
add_custom_target(tg_capi_build ALL
    BYPRODUCTS ${NI_TG_CAPI}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/rust-lib
    COMMAND ${RUST_ENV} ${RUST_CARGO} ${cargo_args}
    ${combine}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NI_TG_CAPI}.new ${NI_TG_CAPI}
    WORKING_DIRECTORY ${NI_ROOT}
    COMMENT "Building the Trance Gate engine (cargo: tg-capi)"
    VERBATIM)
add_library(tg_capi STATIC IMPORTED)
set_target_properties(tg_capi PROPERTIES IMPORTED_LOCATION ${NI_TG_CAPI})
target_include_directories(tg_capi INTERFACE ${NI_ROOT}/engines/trance-gate/include)
add_dependencies(tg_capi tg_capi_build)

# ------------------------------------------------------------- the plugin
#
# THE iPlug2 TRANCE GATE'S CLASS, which this build replaces in a saved set.
# tests/fixtures/iplug2/ids.json has it as the factory reports it: the FUID
# F2AEE70D 00DE4F4E 'Grbe' 'TrGt'. JUCE names a class by its TUID bytes,
# which on Windows are in GUID order -- the same class, other bytes.
if (WIN32)
    set(NI_IPLUG2_TG_CLASS 0DE7AEF2DE004E4F4772626554724774)
else()
    set(NI_IPLUG2_TG_CLASS F2AEE70D00DE4F4E4772626554724774)
endif()

# TWO WAYS FOR A SET TO FIND THIS BUILD, and the spike builds either:
#
#   OFF (default)  JUCE's own class ID, and the iPlug2 class declared
#                  compatible: moduleinfo.json's Compatibility and the
#                  factory's IPluginCompatibility. It works only in a host that
#                  reads them, and Live's release notes up to 12.4.6 say
#                  nothing of either.
#   ON             the iPlug2 class ID itself (JUCE_VST3_COMPONENT_CLASS, JUCE's
#                  own switch for replacing a VST3 not built with JUCE), so a
#                  set finds this build exactly as it found the old one. Bypass
#                  keeps JUCE's ID (15, not 65536), and the two bundles must
#                  never be installed side by side.
#
# spike/UT2.md tests both in Live.
option(NI_SPIKE_SAME_CLASS "Use the iPlug2 build's VST3 class ID instead of declaring it compatible" OFF)

juce_add_plugin(NITranceGateSpike
    PRODUCT_NAME "NI Trance Gate"
    COMPANY_NAME "Neon Ingvy"
    # Not the iPlug2 bundle's identifier (com.graebe.vst3.NITranceGate): that
    # bundle finds its own resources by it, and both may sit on one disk.
    BUNDLE_ID "com.graebe.vst3.NITranceGate.juce-spike"
    PLUGIN_MANUFACTURER_CODE Grbe
    PLUGIN_CODE TrGt
    FORMATS VST3
    VST3_CATEGORIES Fx Modulation
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(NITranceGateSpike PRIVATE
    Source/Processor.cpp
    Source/Params.cpp
    Source/Chunk.cpp
    # The shell's locale-free numbers and transport, shared with iPlug2's.
    ${NI_ROOT}/plugins/_shared/ni/Wire.cpp)
target_include_directories(NITranceGateSpike PRIVATE Source ${NI_ROOT}/plugins/_shared)

target_compile_definitions(NITranceGateSpike PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # The index is the VST3 parameter ID, as it was in iPlug2.
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
    "NI_IPLUG2_TG_CLASS=\"${NI_IPLUG2_TG_CLASS}\"")
if (NI_SPIKE_SAME_CLASS)
    target_compile_definitions(NITranceGateSpike PUBLIC
        "JUCE_VST3_COMPONENT_CLASS=\"${NI_IPLUG2_TG_CLASS}\""
        NI_SPIKE_SAME_CLASS=1)
else()
    # moduleinfo.json's Compatibility and the factory's IPluginCompatibility
    # class both come from this: New is this build's class, Old the iPlug2 one.
    target_compile_definitions(NITranceGateSpike PUBLIC
        "JUCE_VST3_COMPATIBLE_CLASSES=VST3Interface::hexStringToId(\"${NI_IPLUG2_TG_CLASS}\")"
        NI_SPIKE_SAME_CLASS=0)
endif()

target_link_libraries(NITranceGateSpike
    PRIVATE tg_capi
            juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

# For the manual Live test (spike/UT2.md): the bundle, copied to spike/out --
# the same-class variant to spike/out/same-class, so the two never meet.
get_target_property(NI_SPIKE_BUNDLE NITranceGateSpike_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
if (NI_SPIKE_SAME_CLASS)
    set(NI_SPIKE_OUT ${NI_ROOT}/spike/out/same-class)
else()
    set(NI_SPIKE_OUT ${NI_ROOT}/spike/out)
endif()
add_custom_target(spike_out
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${NI_SPIKE_OUT}/NI Trance Gate.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${NI_SPIKE_BUNDLE}" "${NI_SPIKE_OUT}/NI Trance Gate.vst3"
    DEPENDS NITranceGateSpike_VST3
    COMMENT "Copying the spike to ${NI_SPIKE_OUT}"
    VERBATIM)

# -------------------------------------------------------------- the tests

enable_testing()

# THE CHUNK, ALONE: every iPlug2 fixture read, the legacy layouts, refusals,
# and a round trip. No JUCE, no engine.
add_executable(spike_chunk test/ChunkTest.cpp Source/Chunk.cpp Source/Params.cpp)
target_include_directories(spike_chunk PRIVATE Source ${NI_ROOT}/external/doctest
    ${NI_ROOT}/engines/trance-gate/include)
target_compile_definitions(spike_chunk PRIVATE "NI_FIXTURES=\"${NI_FIXTURES}\"")
add_test(NAME spike_chunk COMMAND spike_chunk)

# THE BUILT VST3, HOSTED: JUCE's own VST3 host loads the bundle as a DAW
# would and checks the class, the parameters, every Trance Gate fixture, the
# reverse trip into the iPlug2 build, and that the engine gates audio the way
# the iPlug2 build does. The iPlug2 bundle comes from the root build.
set(NI_IPLUG2_OUT ${NI_ROOT}/build-spike/out CACHE PATH "The iPlug2 build's bundles (root build, build-spike/out)")
juce_add_console_app(spike_host PRODUCT_NAME "spike_host")
target_sources(spike_host PRIVATE test/HostTest.cpp Source/Chunk.cpp Source/Params.cpp)
target_include_directories(spike_host PRIVATE Source ${NI_ROOT}/engines/trance-gate/include)
target_compile_definitions(spike_host PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    "NI_IPLUG2_TG_CLASS=\"${NI_IPLUG2_TG_CLASS}\""
    NI_SPIKE_SAME_CLASS=$<BOOL:${NI_SPIKE_SAME_CLASS}>)
target_link_libraries(spike_host PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_dependencies(spike_host NITranceGateSpike_VST3)
add_test(NAME spike_host
         COMMAND spike_host "${NI_SPIKE_BUNDLE}" ${NI_FIXTURES} "${NI_IPLUG2_OUT}/NITranceGate.vst3")

# THE AUDIO CALLBACK ALLOCATES NOTHING: the processor itself, its sources
# compiled in, under alloc_guard, which counts every malloc of the calling
# thread -- C++, JUCE and the Rust engine alike. test/alloc_guard.c says why
# it is an interposer and not a global allocator.
add_library(spike_alloc_guard SHARED test/alloc_guard.c)
juce_add_console_app(spike_rt PRODUCT_NAME "spike_rt")
target_sources(spike_rt PRIVATE test/RtTest.cpp
    Source/Processor.cpp Source/Params.cpp Source/Chunk.cpp ${NI_ROOT}/plugins/_shared/ni/Wire.cpp)
target_include_directories(spike_rt PRIVATE Source ${NI_ROOT}/plugins/_shared)
target_compile_definitions(spike_rt PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    "JucePlugin_Name=\"NI Trance Gate\""
    "NI_IPLUG2_TG_CLASS=\"${NI_IPLUG2_TG_CLASS}\"")
target_link_libraries(spike_rt PRIVATE
    tg_capi
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME spike_rt COMMAND spike_rt ${NI_FIXTURES})
set_tests_properties(spike_rt PROPERTIES
    ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:spike_alloc_guard>")

# PLUGINVAL AT STRICTNESS 10, the pinned v1.0.4 scripts/validate-plugins.sh
# fetches (`scripts/validate-plugins.sh fetch build-spike/validators`), with
# its editor tests. A missing pluginval fails; it never skips.
set(NI_PLUGINVAL ${NI_ROOT}/build-spike/validators/pluginval.app/Contents/MacOS/pluginval
    CACHE FILEPATH "pluginval v1.0.4")
add_test(NAME spike_pluginval
         COMMAND ${NI_PLUGINVAL} --strictness-level 10 --timeout-ms 300000 --validate "${NI_SPIKE_BUNDLE}")
