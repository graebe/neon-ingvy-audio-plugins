# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# NI Cross Smoke: proof that the cross-build kit builds what the plugins will be.
#
# A JUCE 9.0.3 VST3 whose processBlock calls a Rust static library through a C
# ABI -- built natively on macOS, natively on Linux in tools/docker/linux and
# cross-compiled for Windows in tools/docker/windows, then validated by
# pluginval. It ships nowhere. docs/tech/cross-build.md is the walk-through.
#
#   cmake -S tools/cross/smoke -B <build> -G Ninja -DNI_JUCE_DIR=<JUCE 9.0.3>
#
# NI_JUCE_DIR is how the scripts/build-*.sh hand any project its JUCE: a
# checkout at tag 9.0.3, wherever it is. The repository's own is the
# submodule at external/JUCE.
cmake_minimum_required(VERSION 3.22)

# The repository's defaults (its root CMakeLists.txt says why): universal, and
# the oldest macOS Rust's arm64 target supports. Before project(), which is
# where CMake creates both.
if (CMAKE_HOST_APPLE)
    if (NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures to build")
    endif()
    if (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS the plugin loads on")
    endif()
endif()

project(ni_cross_smoke VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# THE STATIC C RUNTIME ON WINDOWS. A plugin is a DLL inside someone else's
# process: linking the CRT in means it loads without the Visual C++
# Redistributable installed. JUCE builds juceaide the same way.
# RustStaticLib.cmake reads this and builds the Rust side to match.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(NI_JUCE_VERSION 9.0.3)
set(NI_JUCE_DIR "" CACHE PATH "A JUCE checkout at tag ${NI_JUCE_VERSION}")
if (NOT EXISTS "${NI_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "NI_JUCE_DIR is '${NI_JUCE_DIR}', which is not a JUCE checkout. Pass "
                        "-DNI_JUCE_DIR=<dir> with JUCE at tag ${NI_JUCE_VERSION} "
                        "(scripts/build-*.sh take it as --juce <dir>).")
endif()
add_subdirectory(${NI_JUCE_DIR} JUCE)
get_directory_property(juce_version DIRECTORY ${NI_JUCE_DIR} DEFINITION JUCE_VERSION)
if (NOT juce_version VERSION_EQUAL NI_JUCE_VERSION)
    message(FATAL_ERROR "NI_JUCE_DIR holds JUCE ${juce_version}; this project is pinned to ${NI_JUCE_VERSION}")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../RustStaticLib.cmake)
set(NI_SMOKE_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.toml)
ni_add_rust_staticlib(ni_smoke_engine MANIFEST ${NI_SMOKE_MANIFEST} LIB ni_cross_smoke
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/rust/include)

# JUCE WRITES moduleinfo.json BY RUNNING A HELPER on the build machine, compiled
# with the plugin's own compiler. A cross build's helper is a Windows program,
# so a cross build has no manifest: hosts scan the plugin instead, and the
# native Windows build that publishes it writes one.
if (CMAKE_CROSSCOMPILING)
    set(vst3_manifest FALSE)
else()
    set(vst3_manifest TRUE)
endif()

# Never copied into the user's plug-in folders: validation loads the bundle
# from the build directory.
juce_add_plugin(NICrossSmoke
    COMPANY_NAME "Neon Ingvy"
    PRODUCT_NAME "NICrossSmoke"
    PLUGIN_NAME "NI Cross Smoke"
    BUNDLE_ID com.graebe.ni-cross-smoke
    PLUGIN_MANUFACTURER_CODE Grbe
    PLUGIN_CODE Xsmk
    FORMATS VST3
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST ${vst3_manifest})

target_sources(NICrossSmoke PRIVATE SmokePlugin.cpp)

target_compile_definitions(NICrossSmoke PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(NICrossSmoke
    PRIVATE
        ni_smoke_engine
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ctest: the C ABI from C++, and the crate's own tests, both for the platform
# being built -- on Windows, run under the emulator the build script names.
enable_testing()
add_executable(ni_smoke_abi_test abi_test.cpp)
target_link_libraries(ni_smoke_abi_test PRIVATE ni_smoke_engine)
add_test(NAME rust_abi COMMAND ni_smoke_abi_test)
ni_add_rust_test(rust_unit MANIFEST ${NI_SMOKE_MANIFEST})
