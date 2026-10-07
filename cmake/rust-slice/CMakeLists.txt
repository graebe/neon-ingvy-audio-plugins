# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# One further slice of a universal macOS build: the engines' crates, imported
# by Corrosion exactly as cmake/NiRust.cmake imports them, for one more target
# triple. NiRust.cmake runs this as a sub-build per slice and joins what it
# leaves in NI_RUST_SLICE_DIR with lipo; it is not meant to be configured by
# hand. Corrosion builds one triple per CMake project, which is the whole
# reason this is a project of its own.
cmake_minimum_required(VERSION 3.22)
project(ni_rust_slice LANGUAGES C)

foreach(var NI_SOURCE_DIR NI_RUST_CRATES NI_RUST_LIBS NI_RUST_SLICE_DIR Rust_CARGO_TARGET)
    if (NOT ${var})
        message(FATAL_ERROR "rust-slice: ${var} is not set -- this is cmake/NiRust.cmake's sub-build")
    endif()
endforeach()

include(${NI_SOURCE_DIR}/cmake/NiCorrosion.cmake)

corrosion_import_crate(MANIFEST_PATH ${NI_SOURCE_DIR}/Cargo.toml
    CRATES ${NI_RUST_CRATES}
    CRATE_TYPES staticlib
    PROFILE release)
foreach(lib IN LISTS NI_RUST_LIBS)
    set_target_properties(${lib} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${NI_RUST_SLICE_DIR})
    ni_rust_quiet_cargo(${lib})
endforeach()
