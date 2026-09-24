# The Trance Gate engine, built from the submodule by cargo.
#
# NOT copied in and not rebuilt per consumer: five things link this engine --
# the plugin, the core tests, the render A/B, the AU host and the VST3 host --
# and they must all link the SAME bytes. "It sounds different in Live" is the
# hardest kind of bug to chase, and a second copy of the engine is how you get
# one.
#
# This replaces compiling ${TG_ROOT}/src/dsp/trance_gate_core.c, which no
# longer exists: the engine is Rust as of schwung-trance-gate 383998d. The C
# ABI is unchanged, so every consumer below still includes trance_gate_core.h
# and calls tg_core_*.

set(TG_ROOT ${CMAKE_SOURCE_DIR}/external/schwung-trance-gate)

if (NOT EXISTS ${TG_ROOT}/Cargo.toml)
    message(FATAL_ERROR
        "The engine submodule is empty or predates the Rust port.\n"
        "  git submodule update --init external/schwung-trance-gate")
endif()

# RUSTUP'S SHIMS ARE OFTEN NOT ON PATH when CMake is driven from an IDE or
# Xcode, which starts a login shell without the user's profile. The toolchain's
# own bin directory always works once found.
find_program(TG_CARGO cargo HINTS $ENV{HOME}/.cargo/bin)
if (NOT TG_CARGO)
    find_program(TG_RUSTUP rustup HINTS $ENV{HOME}/.cargo/bin)
    if (TG_RUSTUP)
        execute_process(COMMAND ${TG_RUSTUP} which cargo
                        OUTPUT_VARIABLE TG_CARGO
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
    endif()
endif()
if (NOT TG_CARGO)
    message(FATAL_ERROR "cargo not found -- the engine is Rust. https://rustup.rs")
endif()

# CMake's Apple architecture names are not Rust's target triples, and the
# mapping is the only place this build knows about either.
set(TG_TRIPLES "")
foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    if (arch STREQUAL "arm64")
        list(APPEND TG_TRIPLES aarch64-apple-darwin)
    elseif (arch STREQUAL "x86_64")
        list(APPEND TG_TRIPLES x86_64-apple-darwin)
    else()
        message(FATAL_ERROR "no Rust target known for OSX architecture '${arch}'")
    endif()
endforeach()

set(TG_BUILD_DIR ${CMAKE_BINARY_DIR}/tg-engine)
set(TG_LIB ${TG_BUILD_DIR}/libtg_capi.a)

set(TG_CARGO_ARGS build --release -p tg-capi --target-dir ${TG_BUILD_DIR}/target)
set(TG_SLICES "")
foreach(triple IN LISTS TG_TRIPLES)
    list(APPEND TG_CARGO_ARGS --target ${triple})
    list(APPEND TG_SLICES ${TG_BUILD_DIR}/target/${triple}/release/libtg_capi.a)
endforeach()
if (NOT TG_SLICES)
    # No OSX_ARCHITECTURES: one host build, no triple in the path.
    set(TG_SLICES ${TG_BUILD_DIR}/target/release/libtg_capi.a)
endif()

list(LENGTH TG_SLICES TG_SLICE_COUNT)
if (TG_SLICE_COUNT GREATER 1)
    set(TG_COMBINE COMMAND lipo -create ${TG_SLICES} -output ${TG_LIB}.new)
else()
    set(TG_COMBINE COMMAND ${CMAKE_COMMAND} -E copy ${TG_SLICES} ${TG_LIB}.new)
endif()

# CARGO NEEDS RUSTC BESIDE IT ON PATH. When cargo was found through `rustup
# which` rather than on PATH -- an IDE, or Xcode, or any build not started from
# the user's login shell -- its own directory is not on PATH either, and cargo
# fails with "could not execute process `rustc -vV`", which names neither PATH
# nor rustup.
#
# --unset=MAKEFLAGS because make exports a jobserver cargo cannot attach to
# from here, and the warning it prints looks like a real failure.
#
# MACOSX_DEPLOYMENT_TARGET has to reach rustc, or its objects carry a newer
# minimum than the C++ around them and every link prints "object file was
# built for newer macOS version".
get_filename_component(TG_CARGO_DIR ${TG_CARGO} DIRECTORY)
set(TG_ENV ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS
           "PATH=${TG_CARGO_DIR}:$ENV{PATH}")
if (CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND TG_ENV MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()

# CARGO IS THE DEPENDENCY SCANNER, NOT CMAKE. A file(GLOB) over the crates
# would be a second, worse answer to a question cargo already answers exactly,
# and a stale one the first time a file is added. So this target always runs;
# cargo is a no-op in well under a second when nothing changed.
#
# copy_if_different is what keeps that from relinking three plugin formats on
# every build: an unchanged engine leaves ${TG_LIB}'s timestamp alone.
add_custom_target(tg_engine_cargo ALL
    BYPRODUCTS ${TG_LIB}
    COMMAND ${TG_ENV} ${TG_CARGO} ${TG_CARGO_ARGS}
    ${TG_COMBINE}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TG_LIB}.new ${TG_LIB}
    WORKING_DIRECTORY ${TG_ROOT}
    COMMENT "Building the Trance Gate engine (cargo, ${TG_TRIPLES})"
    VERBATIM)

add_library(tg_engine INTERFACE)
target_link_libraries(tg_engine INTERFACE ${TG_LIB})
target_include_directories(tg_engine INTERFACE ${TG_ROOT}/src/dsp)
add_dependencies(tg_engine tg_engine_cargo)
