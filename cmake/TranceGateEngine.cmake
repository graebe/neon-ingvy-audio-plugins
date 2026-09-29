# The Trance Gate engine's C ABI, built by cargo for the PLUGIN's targets.
#
# NOT copied and not rebuilt per consumer: five things link this -- the plugin,
# the core tests, the render A/B, the AU host and the VST3 host -- and they
# must all link the SAME bytes. "It sounds different in Live" is the hardest
# kind of bug to chase, and a second copy of the engine is how you get one.
#
# THIS IS ONE OF TWO WRAPPERS AROUND ONE CORE. `tg-capi` is the C ABI, which
# is what a plugin format needs; `tg-move` is the Schwung audio_fx vtable, and
# cmake/Schwung.cmake builds that one. Both depend on `tg-core` by relative
# path inside engine/'s single Cargo workspace, so neither can drift from the
# other -- not by policy, by construction. tests/render_plugin.c is what keeps
# that honest: it renders through the plugin's audio path and matches the Move
# module's reference render byte for byte.
#
# It replaces compiling trance_gate_core.c, which no longer exists: the engine
# is Rust. The C ABI is unchanged, so every consumer still includes
# trance_gate_core.h and calls tg_core_*.

set(TG_ROOT ${CMAKE_SOURCE_DIR}/engines/trance-gate)

# THE WORKSPACE IS THE REPOSITORY ROOT, not this engine's directory: both
# engines' crates are members of one Cargo.toml there, so that is what cargo
# must find and what .cargo/config.toml sits beside.
if (NOT EXISTS ${CMAKE_SOURCE_DIR}/Cargo.toml)
    message(FATAL_ERROR
        "No Cargo.toml at the repository root -- the workspace is missing.")
endif()

# cargo, found once for the whole repository -- rustup.rs, Homebrew and a bare
# toolchain each put it somewhere different, and this searched only one of them.
include(${CMAKE_SOURCE_DIR}/cmake/RustToolchain.cmake)
set(TG_CARGO ${RUST_CARGO})

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

# The environment cargo needs -- rustc beside it on PATH, no make jobserver,
# the deployment target carried through. Built once; see RustToolchain.cmake.
set(TG_ENV ${RUST_ENV})

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
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the Trance Gate engine (cargo, ${TG_TRIPLES})"
    VERBATIM)

add_library(tg_engine INTERFACE)
target_link_libraries(tg_engine INTERFACE ${TG_LIB})
target_include_directories(tg_engine INTERFACE ${TG_ROOT}/include)
add_dependencies(tg_engine tg_engine_cargo)
