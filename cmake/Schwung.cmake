# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The Schwung modules: the Move builds of the engines that have one.
#
#   cmake --build build --target schwung              NI Trance Gate
#   cmake --build build --target schwung-side-chain   NI Side-Chain
#   cmake --build build --target schwung-bit-crusher  NI Bit-Crusher
#   cmake --build build --target schwung-vocalize     NI Vocalize
#   cmake --build build --target schwung-filter-designer  NI Filter Designer
#   cmake --build build --target schwung-<single filter>  NI High Pass ... NI All Pass
#
# Each runs modules/_shared/package.sh with the module's directory name; what
# differs between modules is in modules/<name>/module.env. Their tests are
# modules/_shared/test.sh, which tests/CMakeLists.txt registers for every
# module in the full tier (`ctest -L move`).
#
# WHY THIS IS A CUSTOM TARGET AND NOT A CMAKE LIBRARY. It is an aarch64 LINUX
# cross-build, produced in a container with a pinned toolchain, and packaged as
# a tarball with a module.json beside it. CMake would have to be taught a
# second toolchain, a second sysroot and a packaging format to own that, for a
# product whose entire build is forty lines of shell. So CMake drives the
# script and the script stays runnable on its own -- which is what CI calls,
# and what anyone without this build directory calls.
#
# THE ENGINE IS THE SAME ENGINE. Each of these builds the `*-move` crate where
# the plugin builds the `*-capi` one; both are members of the one workspace at
# the repository root and both depend on the same `*-core` by relative path.
# That is why there is no "keep them in sync" step here: there is nothing to
# sync, and the render A/B tests prove it after the fact.
#
# The Spectrogram has no module: it draws a picture and the Move has no screen
# to draw it on.

find_program(SCHWUNG_DOCKER docker)

add_custom_target(schwung
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh trance-gate
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Trance Gate module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL                 # it is slow and it has progress worth seeing
    VERBATIM)

add_custom_target(schwung-side-chain
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh side-chain
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Side-Chain module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-bit-crusher
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh bit-crusher
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Bit-Crusher module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-vocalize
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh vocalize
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Vocalize module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-filter-designer
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh filter-designer
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Filter Designer module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

# THE SINGLE FILTERS: nine modules, one module crate (sf-move), a one-line
# crate each (sf-move-<product>).
add_custom_target(schwung-high-pass
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh high-pass
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI High Pass module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-low-pass
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh low-pass
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Low Pass module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-band-pass
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh band-pass
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Band Pass module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-notch
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh notch
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Notch module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-bell
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh bell
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Bell module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-low-shelf
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh low-shelf
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Low Shelf module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-high-shelf
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh high-shelf
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI High Shelf module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-tilt
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh tilt
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Tilt module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

add_custom_target(schwung-all-pass
    COMMAND ${CMAKE_SOURCE_DIR}/modules/_shared/package.sh all-pass
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI All Pass module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL
    VERBATIM)

if (NOT SCHWUNG_DOCKER)
    # Not fatal: a macOS-only build has no use for it, and failing configure
    # over a target nobody asked for would be its own kind of rude.
    message(STATUS
        "docker not found -- the schwung targets will need it, or a cross "
        "toolchain and CROSS_PREFIX set by hand")
endif()
