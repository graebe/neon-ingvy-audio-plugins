# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The native UI on its own: the Ultraviolet kit (plugins/_shared/ui), the four
# editors (plugins/<product>/editor), their tests (tests/ui) and the gallery,
# built against JUCE 9 with no plugin, no host and no engine.
#
#   cmake -S ui -B build-ui -G Ninja
#   cmake --build build-ui
#   ctest --test-dir build-ui -L quick       unit tests and the token guard
#   ctest --test-dir build-ui -L full        and the snapshot goldens
#   build-ui/gallery/ni_ui_gallery_artefacts/Debug/NI UI Gallery.app   (macOS)
#
# ITS OWN PROJECT, NOT AN OPTION OF THE ROOT ONE. The root build is still
# iPlug2's, and until the JUCE shell replaces it, nothing here may change what
# that build configures, builds or tests -- an option in it would still need
# its SDKs, its cargo and its configure to be healthy before the kit could be
# built at all. The parts are written to be taken over unchanged: the plugin
# build adds plugins/_shared/ui (ni_ui, ni_editor_<product>) and tests/ui (the
# test programs and their tiers) exactly as this file does. The gallery is
# this project's alone; nothing ships it.
#
# Each agent or developer working in parallel builds in a directory of its own
# (build-ui, build-ui-<what>); the sources are shared, the build trees never.
cmake_minimum_required(VERSION 3.22)

# The plugins' oldest macOS, before project() for the reason the root build
# gives. A single architecture -- the host's -- unless -D says otherwise: the
# kit's tests and gallery ship nowhere, and a universal build would compile
# JUCE twice for nothing.
if (CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS the editors run on")
endif()

project(ni_native_ui VERSION 2026.10.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Debug unless told otherwise: the tests are what this project is for, and
# JUCE's assertions are part of what they check.
get_property(_ni_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT _ni_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

get_filename_component(NI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

if (NOT EXISTS ${NI_ROOT}/external/JUCE/CMakeLists.txt)
    message(FATAL_ERROR
        "external/JUCE is empty: git submodule update --init external/JUCE")
endif()
add_subdirectory(${NI_ROOT}/external/JUCE ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

# The kit, then one library per editor.
add_subdirectory(${NI_ROOT}/plugins/_shared/ui ${CMAKE_BINARY_DIR}/ni_ui)
foreach(product trance-gate side-chain spectrogram listen-in chord-detector)
    ni_ui_add_editor(${product})
endforeach()
# NI Chord-Detector's model hands the engine's own structs over: its editor
# reads the header cbindgen writes (it links nothing of the engine).
target_include_directories(ni_editor_chord_detector INTERFACE ${NI_ROOT}/engines/chord-detector/include)

# The gallery: every component in its states, for a person to look at.
add_subdirectory(${NI_ROOT}/plugins/_shared/ui/gallery ${CMAKE_BINARY_DIR}/gallery)

# The tests, in the repository's two tiers (cmake/NiTest.cmake).
enable_testing()
include(${NI_ROOT}/cmake/NiTest.cmake)
add_subdirectory(${NI_ROOT}/tests/ui ${CMAKE_BINARY_DIR}/tests/ui)
