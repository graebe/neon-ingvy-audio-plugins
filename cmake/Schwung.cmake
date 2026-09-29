# The second target: the Trance Gate as a Schwung module for the Move.
#
#   cmake --build build --target schwung
#
# WHY THIS IS A CUSTOM TARGET AND NOT A CMAKE LIBRARY. It is an aarch64 LINUX
# cross-build, produced in a container with a pinned toolchain, and packaged as
# a tarball with a module.json beside it. CMake would have to be taught a
# second toolchain, a second sysroot and a packaging format to own that, for a
# product whose entire build is forty lines of shell. So CMake drives the
# script and the script stays runnable on its own -- which is what CI calls,
# and what anyone without this build directory calls.
#
# THE ENGINE IS THE SAME ENGINE. This builds `tg-move` where the plugin builds
# `tg-capi`; both are members of the one workspace at the repository root and
# both depend on `tg-core` by relative path. That is why there is no "keep them
# in sync" step here: there is nothing to sync.
#
# Only the Trance Gate has one. The Spectrogram draws a picture and the Move
# has no screen to draw it on.

find_program(SCHWUNG_DOCKER docker)

add_custom_target(schwung
    COMMAND ${CMAKE_SOURCE_DIR}/modules/trance-gate/package.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the Trance Gate module for Schwung (aarch64 Linux, via Docker)"
    USES_TERMINAL                 # it is slow and it has progress worth seeing
    VERBATIM)

if (NOT SCHWUNG_DOCKER)
    # Not fatal: a macOS-only build has no use for it, and failing configure
    # over a target nobody asked for would be its own kind of rude.
    message(STATUS
        "docker not found -- `--target schwung` will need it, or a cross "
        "toolchain and CROSS_PREFIX set by hand")
endif()
