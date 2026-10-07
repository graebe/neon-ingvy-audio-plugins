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

# OBJC/OBJCXX on Apple because iPlug2's macOS layer is Objective-C++ -- the
# AUv2 entry, the WKWebView editor and the standalone's Cocoa shell are all
# .mm. Without them CMake fails at generate time with a bare
# "CMAKE_OBJCXX_COMPILE_OBJECT is not set", which names neither iPlug2 nor
# Objective-C++.
if (APPLE)
    project(neon_ingvy_audio_plugins VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(neon_ingvy_audio_plugins VERSION 0.1.0 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Coverage, when asked for. Must come before any target is defined: it works by
# adding compile options, which reach only what is declared after it.
include(cmake/Coverage.cmake)
# A sanitizer for the C/C++ tests, when asked for (-DNI_SANITIZE=thread).
include(cmake/Sanitize.cmake)

# iPlug2, once for every plugin.
set(IPLUG2_DIR ${CMAKE_SOURCE_DIR}/external/iPlug2 CACHE PATH "iPlug2 root")
include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

# ni_add_rust_engine and ni_add_plugin. See the file.
include(cmake/NiPlugin.cmake)

# Each product's C ABI, one static library apiece. audio-bus is the house
# transport rather than a product, and Listen-In's whole engine.
ni_add_rust_engine(tg_engine CRATE tg-capi LIB tg_capi
    INCLUDE ${CMAKE_SOURCE_DIR}/engines/trance-gate/include)
ni_add_rust_engine(spectro_engine CRATE spectro-capi LIB spectro_capi
    INCLUDE ${CMAKE_SOURCE_DIR}/engines/spectro/include)
ni_add_rust_engine(audio_bus_engine CRATE bus-capi LIB bus_capi
    INCLUDE ${CMAKE_SOURCE_DIR}/engines/audio-bus/include)
ni_add_rust_engine(sc_engine CRATE sc-capi LIB sc_capi
    INCLUDE ${CMAKE_SOURCE_DIR}/engines/side-chain/include)
# NI Chord-Detector's header is cbindgen's (cd-capi/build.rs), checked in.
ni_add_rust_engine(cd_engine CRATE cd-capi LIB cd_capi
    INCLUDE ${CMAKE_SOURCE_DIR}/engines/chord-detector/include)
# The ground's beat clock and the shell plumbing (command queue, snapshot,
# handoff, state header, denormal guard) belong to no product: their C ABI
# rides inside every product's archive, so they are headers here.
ni_add_rust_headers(ground_engine INCLUDE ${CMAKE_SOURCE_DIR}/engines/ground/include)
ni_add_rust_headers(shell_engine INCLUDE ${CMAKE_SOURCE_DIR}/engines/shell/include)
ni_build_rust_engines()

# The Trance Gate's and the Side-Chain's OTHER shell: the Schwung module for the
# Move, an aarch64 Linux cross-build in a container -- see the note in the file.
include(cmake/Schwung.cmake)

add_subdirectory(plugins/trance-gate)
add_subdirectory(plugins/spectrogram)
add_subdirectory(plugins/listen-in)
add_subdirectory(plugins/side-chain)

# THE JUCE SHELL, beside iPlug2 while the refactor moves the four products
# onto it (docs/adr/0002). JUCE as a subproject, excluded from `all` so only
# what a plugin links is built; the native UI kit every editor is made of
# (plugins/_shared/ui); and ni_add_juce_plugin (cmake/NiJucePlugin.cmake).
set(NI_ROOT ${CMAKE_SOURCE_DIR})
add_subdirectory(external/JUCE ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(plugins/_shared/ui ${CMAKE_BINARY_DIR}/ni_ui)
include(cmake/NiJucePlugin.cmake)

add_subdirectory(plugins/chord-detector)


enable_testing()
# ni_test_tiers: every test is quick or full, and says which.
include(cmake/NiTest.cmake)
add_subdirectory(tests)
