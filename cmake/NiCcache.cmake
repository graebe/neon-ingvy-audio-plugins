# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# ccache in front of every C, C++, Objective-C and Objective-C++ compile.
#
#   -DNI_USE_CCACHE=OFF     compile without it (on by default when found)
#
# WHY. Every build directory compiles JUCE from scratch, and JUCE's modules
# are compiled again into every target that links them -- each plugin, each
# test program. A new build directory, a new worktree or a wiped one used to
# cost the whole of that again; with the cache it costs a lookup per object.
# docs/tech/testing.md has the measurements.
#
# HITS ACROSS BUILD DIRECTORIES AND WORKTREES. A compile line names its files
# by absolute path, and an absolute path differs from one checkout to the next,
# so ccache would key the same source differently in each. Two settings make
# the key the same wherever the tree is:
#
#   base_dir=<this checkout>   ccache hands the compiler, and hashes, every path
#                              under the checkout as relative to the directory
#                              it compiles in -- the same in every worktree and
#                              in every build directory inside one
#   -fdebug-prefix-map         the one absolute path left is the directory a
#                              -g build compiles in, which goes into its debug
#                              info, and so into ccache's key (hash_dir).
#                              ccache hashes it as the map rewrites it, so
#                              mapping the build directory to "." makes it the
#                              same in every build directory
#
# base_dir is given on ccache's own command line rather than in its
# configuration file, so it holds for this build and changes nothing else the
# cache serves.
# What they cost: a debugger finds the sources relative to the build directory
# (`settings set target.source-map . <build dir>` in lldb), and __FILE__ is a
# relative path -- nothing here prints one in a build that ships, which has no
# debug info either.
#
# NOT IN THE COVERAGE BUILD. Its coverage mapping records the compile directory
# too, and llvm-cov reads the sources back through it, so an object compiled in
# another checkout would report that checkout's files. There ccache runs
# without base_dir: hits only where the build directory is the same one, which
# is still every rebuild after a clean.
#
# A build directory OUTSIDE the checkout still works; only its hits across
# directories are lost, since its paths are not under base_dir.
#
# Included from the root CMakeLists.txt after project() and Coverage.cmake,
# before any target: a launcher reaches the targets declared after it. JUCE
# hands the launchers on to the juceaide it builds while configuring.

include_guard(GLOBAL)

option(NI_USE_CCACHE "Compile C, C++ and Objective-C through ccache when it is installed" ON)

if (NOT NI_USE_CCACHE)
    return()
endif()

find_program(NI_CCACHE ccache)
if (NOT NI_CCACHE)
    message(STATUS "ccache: not found, compiling without it (brew install ccache)")
    return()
endif()

set(_ni_ccache ${NI_CCACHE})
if (NOT VST_COVERAGE)
    list(APPEND _ni_ccache base_dir=${CMAKE_SOURCE_DIR})
    if (NOT MSVC)
        add_compile_options(-fdebug-prefix-map=${CMAKE_BINARY_DIR}=.)
    endif()
endif()

get_property(_ni_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
foreach(lang IN ITEMS C CXX OBJC OBJCXX)
    if (lang IN_LIST _ni_languages AND NOT CMAKE_${lang}_COMPILER_LAUNCHER)
        set(CMAKE_${lang}_COMPILER_LAUNCHER ${_ni_ccache})
    endif()
endforeach()

message(STATUS "ccache: ${NI_CCACHE}")
