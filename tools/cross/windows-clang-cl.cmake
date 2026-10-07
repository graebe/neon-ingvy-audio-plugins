# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Cross-compiling for Windows x64 (the MSVC ABI) from Linux: clang-cl compiles,
# lld-link links, llvm-lib archives, llvm-rc compiles resources and llvm-mt
# handles manifests, against the MSVC C runtime and the Windows SDK as xwin
# lays them out ("splats" them). tools/docker/windows is the image all of that
# is installed in; scripts/build-windows.sh is what uses this file.
#
#   cmake -DCMAKE_TOOLCHAIN_FILE=tools/cross/windows-clang-cl.cmake \
#         -DNI_XWIN_DIR=<splat> ...
#
# NI_XWIN_DIR is the splat's root (crt/ and sdk/); without -D it comes from the
# environment, which the Windows image sets. NI_MSVC_VERSION, likewise, is the
# compiler version the splat's CRT belongs to (19.44 for the 14.44 toolset):
# clang-cl reports it as _MSC_VER, so headers that test it see the compiler
# they were written for.
#
# WHY /imsvc AND /libpath, spelled out, rather than clang-cl's /winsysroot:
# the splat's default layout (lower-case, with symlinks for every casing the
# SDK's own headers use) is the one that works on a case-sensitive file
# system, and it is not the Visual Studio layout /winsysroot expects.
# /imsvc makes the CRT and SDK SYSTEM headers -- their warnings are not ours.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# try_compile() runs a fresh CMake that reads this file again: pass both on.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES NI_XWIN_DIR NI_MSVC_VERSION)
if (NOT NI_XWIN_DIR)
    set(NI_XWIN_DIR "$ENV{NI_XWIN_DIR}")
endif()
if (NOT NI_MSVC_VERSION)
    set(NI_MSVC_VERSION "$ENV{NI_MSVC_VERSION}")
endif()
if (NOT IS_DIRECTORY "${NI_XWIN_DIR}/crt/include" OR NOT IS_DIRECTORY "${NI_XWIN_DIR}/sdk/include/um")
    message(FATAL_ERROR "NI_XWIN_DIR is '${NI_XWIN_DIR}', which is not an xwin splat (crt/ and sdk/) "
                        "-- see tools/docker/windows/README.md")
endif()
if (NOT NI_MSVC_VERSION MATCHES "^19\\.[0-9]+$")
    message(FATAL_ERROR "NI_MSVC_VERSION is '${NI_MSVC_VERSION}': expected the splat's compiler "
                        "version, like 19.44")
endif()

# The tools by their unversioned names: the Windows image puts its LLVM's bin
# directory first on PATH. Absolute paths in the cache, found once.
find_program(NI_CLANG_CL clang-cl REQUIRED)
find_program(NI_LLD_LINK lld-link REQUIRED)
find_program(NI_LLVM_LIB llvm-lib REQUIRED)
find_program(NI_LLVM_RC llvm-rc REQUIRED)
find_program(NI_LLVM_MT llvm-mt REQUIRED)

set(CMAKE_C_COMPILER ${NI_CLANG_CL})
set(CMAKE_CXX_COMPILER ${NI_CLANG_CL})
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_LINKER ${NI_LLD_LINK})
set(CMAKE_AR ${NI_LLVM_LIB})
set(CMAKE_RC_COMPILER ${NI_LLVM_RC})
set(CMAKE_MT ${NI_LLVM_MT})

set(_ni_includes
    ${NI_XWIN_DIR}/crt/include
    ${NI_XWIN_DIR}/sdk/include/ucrt
    ${NI_XWIN_DIR}/sdk/include/um
    ${NI_XWIN_DIR}/sdk/include/shared
    ${NI_XWIN_DIR}/sdk/include/winrt)
set(_ni_libdirs
    ${NI_XWIN_DIR}/crt/lib/x86_64
    ${NI_XWIN_DIR}/sdk/lib/um/x86_64
    ${NI_XWIN_DIR}/sdk/lib/ucrt/x86_64)

list(TRANSFORM _ni_includes PREPEND "/imsvc" OUTPUT_VARIABLE _ni_compile)
list(APPEND _ni_compile -fms-compatibility-version=${NI_MSVC_VERSION})
list(JOIN _ni_compile " " _ni_compile)
set(CMAKE_C_FLAGS_INIT "${_ni_compile}")
set(CMAKE_CXX_FLAGS_INIT "${_ni_compile}")

# llvm-rc preprocesses with clang, which takes plain -I: the version resource
# JUCE writes includes <windows.h>.
list(TRANSFORM _ni_includes PREPEND "-I" OUTPUT_VARIABLE _ni_rc)
list(JOIN _ni_rc " " _ni_rc)
set(CMAKE_RC_FLAGS_INIT "${_ni_rc}")

list(TRANSFORM _ni_libdirs PREPEND "/libpath:" OUTPUT_VARIABLE _ni_link)
list(JOIN _ni_link " " _ni_link)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_ni_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_ni_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_ni_link}")

# Libraries and headers come from the splat, never from the Linux host;
# programs (the tools above, cargo, juceaide) from the host.
set(CMAKE_FIND_ROOT_PATH ${NI_XWIN_DIR})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
