# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The Schwung modules: the Move builds of the engines that have one.
#
#   cmake --build build --target schwung              NI Trance Gate
#   cmake --build build --target schwung-side-chain   NI Side-Chain
#
# Both run modules/_shared/package.sh with the module's directory name; what
# differs between modules is in modules/<name>/module.env.
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

if (NOT SCHWUNG_DOCKER)
    # Not fatal: a macOS-only build has no use for it, and failing configure
    # over a target nobody asked for would be its own kind of rude.
    message(STATUS
        "docker not found -- the schwung targets will need it, or a cross "
        "toolchain and CROSS_PREFIX set by hand")
endif()
