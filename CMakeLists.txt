# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# neon-ingvy-audio-plugins -- the Live plugins around the Rust engines.
#
# One engine, two shells: each product's DSP is the same Rust crate its Schwung
# module builds for the Move, so "it sounds different in Live" cannot happen by
# construction. docs/tech/architecture.md has the layers.
#
# GPL-3.0-or-later, around dependencies GPLv3 can take in: iPlug2 is zlib, the
# VST3 SDK and CLAP are MIT. THIRD_PARTY_LICENSES.md has their notices, and
# docs/adr/0001-gpl-3.0-or-later.md the reasons.
cmake_minimum_required(VERSION 3.22)

# DEFAULTS, NOT DECREES. Ableton Live is still commonly run as x86_64 under
# Rosetta and a universal binary loads either way, so that is the default --
# but a -D on the command line wins: a single-slice debug build, a sanitizer
# build (TSan has no universal runtime) or a slice-by-slice CI build all need
# to say otherwise.
#
# BEFORE project(), because project() is where CMake creates both as (empty)
# cache entries on Apple -- after it, NOT DEFINED is never true. The host test
# is CMAKE_HOST_APPLE because APPLE itself does not exist yet here, and because
# on Linux these would otherwise make the engine builds ask cargo for Apple
# target triples.
if (CMAKE_HOST_APPLE)
    if (NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING
            "Architectures to build: universal by default")
    endif()
    if (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
            "Oldest macOS the plugins load on")
    endif()
endif()

project(neon_ingvy_audio_plugins VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Coverage, when asked for. Must come before any target is defined: it works by
# adding compile options, which reach only what is declared after it.
include(cmake/Coverage.cmake)
# A sanitizer for the C/C++ tests, when asked for (-DNI_SANITIZE=thread).
include(cmake/Sanitize.cmake)
# ccache in front of every compile, when it is installed (-DNI_USE_CCACHE=OFF
# turns it off), with hits across build directories and worktrees. After
# Coverage.cmake, whose build it treats differently.
include(cmake/NiCcache.cmake)

# ni_add_rust_engine: the engines, through Corrosion. See the file.
include(cmake/NiRust.cmake)

# Each product's C ABI, one static library apiece, with the headers its
# build.rs generates. audio-bus is the house transport rather than a product,
# and Listen-In's whole engine.
ni_add_rust_engine(tg_engine CRATE tg-capi LIB tg_capi)
ni_add_rust_engine(spectro_engine CRATE spectro-capi LIB spectro_capi)
ni_add_rust_engine(audio_bus_engine CRATE bus-capi LIB bus_capi)
ni_add_rust_engine(sc_engine CRATE sc-capi LIB sc_capi)
# The ground's beat clock and the shell plumbing (command queue, snapshot,
# handoff) belong to no product: their C ABI rides inside every product's
# archive, so they are headers here.
ni_add_rust_headers(ground_engine)
ni_add_rust_headers(shell_engine)
ni_build_rust_engines()

# The Trance Gate's and the Side-Chain's OTHER shell: the Schwung module for the
# Move, an aarch64 Linux cross-build in a container -- see the note in the file.
include(cmake/Schwung.cmake)

# THE JUCE SHELL (docs/adr/0002): JUCE as a subproject, excluded from `all`
# so only what a plugin links is built; the native UI kit every editor is made
# of (plugins/_shared/ui); every product's editor as a library, so the kit's
# and the editors' tests (tests/ui) build without a plugin, and the gallery
# they draw their pages from; and ni_add_juce_plugin
# (cmake/NiJucePlugin.cmake), which each product calls in its own directory.
set(NI_ROOT ${CMAKE_SOURCE_DIR})
add_subdirectory(external/JUCE ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(plugins/_shared/ui ${CMAKE_BINARY_DIR}/ni_ui)
include(cmake/NiJucePlugin.cmake)

# THE PRODUCTS, the one list a new product joins: its plugin
# (plugins/<product>), its editor library and its tests (tests/<product>,
# when it has a directory of its own there).
set(NI_PRODUCTS trance-gate spectrogram listen-in side-chain)
foreach(product IN LISTS NI_PRODUCTS)
    ni_ui_add_editor(${product})
endforeach()
add_subdirectory(plugins/_shared/ui/gallery ${CMAKE_BINARY_DIR}/gallery)
foreach(product IN LISTS NI_PRODUCTS)
    add_subdirectory(plugins/${product})
endforeach()

enable_testing()
# ni_test_tiers: every test is quick or full, and says which.
include(cmake/NiTest.cmake)
add_subdirectory(tests)
