# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Corrosion, pinned, and pointed at the cargo cmake/RustToolchain.cmake found.
#
# Included by cmake/NiRust.cmake for the build itself and by
# cmake/rust-slice/CMakeLists.txt for each further macOS slice, so both import
# the crates with the same Corrosion and the same toolchain.
#
# CORROSION IS A BUILD TOOL. It is CMake code that runs cargo; nothing of it is
# compiled into anything, so it adds no notice to a bundle
# (THIRD_PARTY_LICENSES.md, "Build tools"). MIT.
#
# THE TARGET TRIPLE. Corrosion builds one per CMake project, Rust_CARGO_TARGET.
# Unset, it derives the triple from the C++ toolchain -- which is how a Linux
# or Windows build selects its own through a CMake toolchain file, with or
# without setting Rust_CARGO_TARGET there. A macOS build sets it from
# CMAKE_OSX_ARCHITECTURES before this file is included (cmake/NiRust.cmake).

include_guard(GLOBAL)

include(${CMAKE_CURRENT_LIST_DIR}/RustToolchain.cmake)

# THE TOOLCHAIN IS THE ONE THE TESTS USE. Corrosion would otherwise search on
# its own, and could settle on a different rustc than the `cargo test` targets
# run -- or on none, from an IDE whose PATH has no Rust on it (RustToolchain.cmake
# says why). So it is handed the cargo found there and the rustc beside it.
#
# A RUSTUP PROXY IS RESOLVED FIRST. Corrosion discards a Rust_COMPILER that is
# rustup's proxy, with a warning, and goes looking for rustup itself -- in
# ~/.cargo/bin, which is not where Homebrew's rustup lives. `rustup which`,
# asked from the source tree, names the binaries the proxies run there, so a
# rust-toolchain file would be honoured as the proxies honour it.
get_filename_component(_ni_rust_bin ${RUST_CARGO} DIRECTORY)
find_program(_ni_rustup rustup HINTS ${_ni_rust_bin} NO_DEFAULT_PATH)
if (_ni_rustup)
    foreach(tool cargo rustc)
        execute_process(COMMAND ${_ni_rustup} which ${tool}
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        OUTPUT_VARIABLE _ni_rust_${tool}
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        COMMAND_ERROR_IS_FATAL ANY)
    endforeach()
    set(Rust_CARGO ${_ni_rust_cargo})
    set(Rust_COMPILER ${_ni_rust_rustc})
else()
    set(Rust_CARGO ${RUST_CARGO})
    set(Rust_COMPILER ${_ni_rust_bin}/rustc)
endif()

# A RELEASE TARBALL, PINNED BY ITS HASH. The tag names the version; the hash is
# what makes a tag that moved fail the download rather than build something
# else. A sub-build (cmake/rust-slice) is handed this download through
# FETCHCONTENT_SOURCE_DIR_CORROSION rather than fetching it again.
include(FetchContent)
FetchContent_Declare(Corrosion
    URL https://github.com/corrosion-rs/corrosion/archive/refs/tags/v0.6.1.tar.gz
    URL_HASH SHA256=e9e95b1ee2bad52681f347993fb1a5af5cce458c5ce8a2636c9e476e4babf8e3
    DOWNLOAD_EXTRACT_TIMESTAMP OFF)
FetchContent_MakeAvailable(Corrosion)

# CARGO UNDER MAKE, WITHOUT MAKE'S JOBSERVER. A Makefile build hands every
# recipe MAKEFLAGS naming the jobserver's descriptors, but closes the
# descriptors for any recipe that is not itself a make -- so cargo, finding
# the variable, warns once per crate that it cannot open them, which reads
# like a failure. Emptied, cargo schedules its own jobs, as it does from a
# shell. Corrosion's targets take environment variables per crate; both
# importers call this for each one.
function(ni_rust_quiet_cargo lib)
    corrosion_set_env_vars(${lib} MAKEFLAGS= MFLAGS=)
endfunction()
