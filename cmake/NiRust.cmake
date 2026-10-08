# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The engines: each product's Rust C ABI as a static library, built by Corrosion.
#
#   ni_add_rust_engine(<target> CRATE <crate> LIB <lib> [INCLUDE <dir>])
#   ni_add_rust_headers(<target> [INCLUDE <dir>])
#   ni_build_rust_engines()           once, after every ni_add_rust_engine
#
# <target> is an INTERFACE library: linking it brings the archive, the system
# libraries the Rust standard library needs, the C ABI's generated headers and
# INCLUDE's hand-written ones. Include it once, at the root, after project().
#
# THE C HEADERS ARE GENERATED. Each capi crate's build.rs writes its own with
# cbindgen (engines/shared/cbindgen/capi_header.rs) into NI_CAPI_INCLUDE_DIR,
# which the cargo runs below are given, so a header is the Rust it declares
# and is rewritten only when its text changed. All of them land in one
# directory because the ground's and the shell's crates are built inside every
# product's cargo run: one value for all of them is what keeps their build
# scripts from rerunning, and every bundle relinking, on every build.
# The directory sits beside cargo's target directory, under build/cargo, so the
# two go together.
#
# ONE STATIC LIBRARY PER PLUGIN. Each product's capi crate is a staticlib that
# absorbs the rlibs it depends on (engines/spectro/crates/spectro-capi's
# Cargo.toml states the rule): two Rust staticlibs in one binary each carry the
# Rust runtime and fail to link. So a plugin links exactly its own archive, and
# a crate with no product of its own -- ground, shell -- is headers only here.
#
# CARGO IS THE DEPENDENCY SCANNER. Corrosion runs it on every build, it is a
# no-op when nothing changed, and Corrosion copies an archive out with
# copy_if_different, so an unchanged one keeps its timestamp and nothing
# relinks.
#
# THE TARGET IS THE C++ BUILD'S, never the machine cargo runs on: Corrosion
# derives it from the toolchain (cmake/NiCorrosion.cmake), so a Linux or
# Windows build selects its triple through its CMake toolchain file. A macOS
# build is universal by default, and that is the one thing Corrosion does not
# do -- see "UNIVERSAL" below.

include_guard(GLOBAL)

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/Cargo.toml)
    message(FATAL_ERROR "No Cargo.toml at the repository root -- the workspace is missing.")
endif()

set(NI_RUST_LIB_DIR ${CMAKE_BINARY_DIR}/rust-lib)
set(NI_CAPI_INCLUDE_DIR ${CMAKE_BINARY_DIR}/cargo/include)
file(MAKE_DIRECTORY ${NI_CAPI_INCLUDE_DIR})

# THE macOS SLICES, as Rust target triples. CMake's Apple architecture names
# are not Rust's. The first slice is the one Corrosion builds in this project;
# any further one is a sub-build (below). Elsewhere the list is empty and
# Corrosion's own triple is the only one.
set(NI_RUST_TRIPLES "")
if (APPLE)
    foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
        if (arch STREQUAL "arm64")
            list(APPEND NI_RUST_TRIPLES aarch64-apple-darwin)
        elseif (arch STREQUAL "x86_64")
            list(APPEND NI_RUST_TRIPLES x86_64-apple-darwin)
        else()
            message(FATAL_ERROR "no Rust target known for OSX architecture '${arch}'")
        endif()
    endforeach()
endif()
set(NI_RUST_EXTRA_TRIPLES ${NI_RUST_TRIPLES})
if (NI_RUST_TRIPLES)
    list(POP_FRONT NI_RUST_EXTRA_TRIPLES Rust_CARGO_TARGET)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/NiCorrosion.cmake)

# Every product's archive, built and (when universal) merged: what a target
# that links one waits for.
add_custom_target(ni_rust_engines)

function(ni_add_rust_engine name)
    cmake_parse_arguments(ARG "" "CRATE;LIB" "INCLUDE" ${ARGN})
    if (NOT ARG_CRATE OR NOT ARG_LIB)
        message(FATAL_ERROR "ni_add_rust_engine(${name}) needs CRATE and LIB")
    endif()
    set_property(GLOBAL APPEND PROPERTY NI_RUST_CRATES ${ARG_CRATE})
    set_property(GLOBAL APPEND PROPERTY NI_RUST_LIBS ${ARG_LIB})
    add_library(${name} INTERFACE)
    if (NI_RUST_EXTRA_TRIPLES)
        # The merged archive, and what the Rust standard library links against,
        # which Corrosion found for the first slice -- the same for every
        # Apple one.
        target_link_libraries(${name} INTERFACE
            ${NI_RUST_LIB_DIR}/universal/lib${ARG_LIB}.a
            $<TARGET_PROPERTY:${ARG_LIB}-static,INTERFACE_LINK_LIBRARIES>)
        target_link_options(${name} INTERFACE
            $<TARGET_PROPERTY:${ARG_LIB}-static,INTERFACE_LINK_OPTIONS>)
    else()
        # Corrosion's own target (the crate's [lib] name), once
        # ni_build_rust_engines has imported it.
        target_link_libraries(${name} INTERFACE ${ARG_LIB})
    endif()
    target_include_directories(${name} INTERFACE ${NI_CAPI_INCLUDE_DIR} ${ARG_INCLUDE})
    add_dependencies(${name} ni_rust_engines)
endfunction()

# A crate whose C ABI rides inside every product's archive: its header, and no
# archive of its own. The header is written by the products' cargo runs, so a
# target that includes it waits for them as well.
function(ni_add_rust_headers name)
    cmake_parse_arguments(ARG "" "" "INCLUDE" ${ARGN})
    add_library(${name} INTERFACE)
    target_include_directories(${name} INTERFACE ${NI_CAPI_INCLUDE_DIR} ${ARG_INCLUDE})
    add_dependencies(${name} ni_rust_engines)
endfunction()

function(ni_build_rust_engines)
    get_property(crates GLOBAL PROPERTY NI_RUST_CRATES)
    get_property(libs GLOBAL PROPERTY NI_RUST_LIBS)

    # One cargo run per crate, all in one target directory, so the crates they
    # share -- ni-dsp, ground, shell, audio-bus -- compile once rather than
    # once per product. Release always: it is the profile whose panic strategy
    # is abort (the root Cargo.toml says why), and the only one that ships.
    #
    # LOCKED: the crates linked are the ones Cargo.lock names, which are the
    # ones cargo-deny held to the allowlist and cargo-about wrote notices for
    # (both read the lock with --locked). Without it a manifest edited past its
    # lock is re-resolved here, silently, and the lock rewritten before ctest's
    # licence gate reads it -- so the gate would approve what the build chose.
    # With it the build stops and says the lock is stale.
    corrosion_import_crate(MANIFEST_PATH ${CMAKE_SOURCE_DIR}/Cargo.toml
        CRATES ${crates}
        CRATE_TYPES staticlib
        PROFILE release
        LOCKED)
    foreach(lib IN LISTS libs)
        set_target_properties(${lib} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${NI_RUST_LIB_DIR}/${Rust_CARGO_TARGET})
        ni_rust_quiet_cargo(${lib})
        corrosion_set_env_vars(${lib} NI_CAPI_INCLUDE_DIR=${NI_CAPI_INCLUDE_DIR})
        add_dependencies(ni_rust_engines cargo-build_${lib})

        # WHAT libSystem ALREADY IS. On Apple, the native libraries rustc
        # names for a static library are System, c and m -- all three
        # libSystem, which the compiler driver links into every binary. Named
        # again, ld warns of a duplicate -lSystem on every plugin and test it
        # links. Anything else rustc names is kept.
        if (APPLE)
            get_target_property(native ${lib}-static INTERFACE_LINK_LIBRARIES)
            if (native)
                list(REMOVE_ITEM native System c m)
                set_target_properties(${lib}-static PROPERTIES INTERFACE_LINK_LIBRARIES "${native}")
            endif()
        endif()
    endforeach()

    if (NOT NI_RUST_EXTRA_TRIPLES)
        return()
    endif()

    # UNIVERSAL. Corrosion builds one target triple per CMake project and has
    # no notion of a fat archive (v0.6.1, and its main branch as of this
    # writing). So each further slice is a sub-build -- cmake/rust-slice, the
    # same Corrosion import for that slice's triple -- and lipo joins the
    # slices, as the hand-written glue this replaced did. The sub-build is
    # BUILD_ALWAYS for the reason above: cargo decides whether anything is to
    # be done, and its archives are copied out only when they changed.
    include(ExternalProject)
    # Lists cross into the sub-build joined by LIST_SEPARATOR's comma.
    string(REPLACE ";" "," crates_arg "${crates}")
    string(REPLACE ";" "," libs_arg "${libs}")
    set(slice_targets "")
    foreach(triple IN LISTS NI_RUST_EXTRA_TRIPLES)
        string(REGEX REPLACE "-apple-darwin$" "" arch ${triple})
        if (arch STREQUAL "aarch64")
            set(arch arm64)
        endif()
        set(byproducts "")
        foreach(lib IN LISTS libs)
            list(APPEND byproducts ${NI_RUST_LIB_DIR}/${triple}/lib${lib}.a)
        endforeach()
        ExternalProject_Add(ni_rust_slice_${triple}
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/cmake/rust-slice
            BINARY_DIR ${CMAKE_BINARY_DIR}/rust-slice/${triple}
            CMAKE_ARGS
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_OSX_ARCHITECTURES=${arch}
                -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
                -DRUST_CARGO=${RUST_CARGO}
                -DRust_CARGO_TARGET=${triple}
                -DFETCHCONTENT_SOURCE_DIR_CORROSION=${corrosion_SOURCE_DIR}
                -DNI_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                -DNI_RUST_CRATES=${crates_arg}
                -DNI_RUST_LIBS=${libs_arg}
                -DNI_RUST_SLICE_DIR=${NI_RUST_LIB_DIR}/${triple}
            LIST_SEPARATOR ","
            BUILD_ALWAYS ON
            BUILD_BYPRODUCTS ${byproducts}
            INSTALL_COMMAND "")
        list(APPEND slice_targets ni_rust_slice_${triple})
    endforeach()

    # THE MERGE RUNS ONLY WHEN A SLICE CHANGED: its inputs keep their
    # timestamps while cargo had nothing to do, so the merged archive keeps
    # its own and no plugin relinks.
    set(merged "")
    foreach(lib IN LISTS libs)
        set(slices "")
        foreach(triple IN LISTS NI_RUST_TRIPLES)
            list(APPEND slices ${NI_RUST_LIB_DIR}/${triple}/lib${lib}.a)
        endforeach()
        set(out ${NI_RUST_LIB_DIR}/universal/lib${lib}.a)
        add_custom_command(OUTPUT ${out}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${NI_RUST_LIB_DIR}/universal
            COMMAND lipo -create ${slices} -output ${out}
            DEPENDS ${slices}
            COMMENT "Joining the ${NI_RUST_TRIPLES} slices of lib${lib}.a"
            VERBATIM)
        list(APPEND merged ${out})
    endforeach()
    add_custom_target(ni_rust_universal DEPENDS ${merged})
    foreach(lib IN LISTS libs)
        add_dependencies(ni_rust_universal cargo-build_${lib})
    endforeach()
    add_dependencies(ni_rust_universal ${slice_targets})
    add_dependencies(ni_rust_engines ni_rust_universal)
endfunction()
