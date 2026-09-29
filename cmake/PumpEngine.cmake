# NI Pump's engine C ABI, built by cargo for the PLUGIN's targets.
#
# A DELIBERATE COPY OF cmake/TranceGateEngine.cmake, not a shared macro, and
# SpectroEngine.cmake already made the same call for the same reason: the
# per-engine parts are the crate name, the library name, the build directory
# and the include path, which is most of the file. A macro taking four
# arguments would be a macro whose body is the file, read at one remove.
#
# NOT copied per consumer, though: five things link this library -- the plugin,
# the core tests, the render A/B, the AU host and the wire tests -- and they
# must all link the SAME bytes. "It sounds different in Live" is the hardest
# kind of bug to chase, and a second copy of the engine is how you get one.
#
# THIS IS ONE OF TWO WRAPPERS AROUND ONE CORE. `pump-capi` is the C ABI, which
# is what a plugin format needs; `pump-move` is the Schwung audio_fx vtable,
# and cmake/Schwung.cmake builds that one. Both depend on `pump-core` by
# relative path inside the repository's single Cargo workspace, so neither can
# drift from the other -- not by policy, by construction. The render A/B is
# what keeps that honest: it renders through the plugin's audio path and
# matches the Move module's reference render byte for byte.

set(PUMP_ROOT ${CMAKE_SOURCE_DIR}/engines/pump)

# THE WORKSPACE IS THE REPOSITORY ROOT, not this engine's directory: every
# engine's crates are members of one Cargo.toml there, so that is what cargo
# must find and what .cargo/config.toml sits beside.
if (NOT EXISTS ${CMAKE_SOURCE_DIR}/Cargo.toml)
    message(FATAL_ERROR
        "No Cargo.toml at the repository root -- the workspace is missing.")
endif()

# cargo, found once for the whole repository -- rustup.rs, Homebrew and a bare
# toolchain each put it somewhere different, and this searched only one of them.
include(${CMAKE_SOURCE_DIR}/cmake/RustToolchain.cmake)
set(PUMP_CARGO ${RUST_CARGO})

# CMake's Apple architecture names are not Rust's target triples, and the
# mapping is the only place this build knows about either.
set(PUMP_TRIPLES "")
foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    if (arch STREQUAL "arm64")
        list(APPEND PUMP_TRIPLES aarch64-apple-darwin)
    elseif (arch STREQUAL "x86_64")
        list(APPEND PUMP_TRIPLES x86_64-apple-darwin)
    else()
        message(FATAL_ERROR "no Rust target known for OSX architecture '${arch}'")
    endif()
endforeach()

set(PUMP_BUILD_DIR ${CMAKE_BINARY_DIR}/pump-engine)
set(PUMP_LIB ${PUMP_BUILD_DIR}/libpump_capi.a)

set(PUMP_CARGO_ARGS build --release -p pump-capi --target-dir ${PUMP_BUILD_DIR}/target)
set(PUMP_SLICES "")
foreach(triple IN LISTS PUMP_TRIPLES)
    list(APPEND PUMP_CARGO_ARGS --target ${triple})
    list(APPEND PUMP_SLICES ${PUMP_BUILD_DIR}/target/${triple}/release/libpump_capi.a)
endforeach()
if (NOT PUMP_SLICES)
    # No OSX_ARCHITECTURES: one host build, no triple in the path.
    set(PUMP_SLICES ${PUMP_BUILD_DIR}/target/release/libpump_capi.a)
endif()

list(LENGTH PUMP_SLICES PUMP_SLICE_COUNT)
if (PUMP_SLICE_COUNT GREATER 1)
    set(PUMP_COMBINE COMMAND lipo -create ${PUMP_SLICES} -output ${PUMP_LIB}.new)
else()
    set(PUMP_COMBINE COMMAND ${CMAKE_COMMAND} -E copy ${PUMP_SLICES} ${PUMP_LIB}.new)
endif()

# The environment cargo needs -- rustc beside it on PATH, no make jobserver,
# the deployment target carried through. Built once; see RustToolchain.cmake.
set(PUMP_ENV ${RUST_ENV})

# CARGO IS THE DEPENDENCY SCANNER, NOT CMAKE. A file(GLOB) over the crates
# would be a second, worse answer to a question cargo already answers exactly,
# and a stale one the first time a file is added. So this target always runs;
# cargo is a no-op in well under a second when nothing changed.
#
# copy_if_different is what keeps that from relinking three plugin formats on
# every build: an unchanged engine leaves ${PUMP_LIB}'s timestamp alone.
add_custom_target(pump_engine_cargo ALL
    BYPRODUCTS ${PUMP_LIB}
    COMMAND ${PUMP_ENV} ${PUMP_CARGO} ${PUMP_CARGO_ARGS}
    ${PUMP_COMBINE}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PUMP_LIB}.new ${PUMP_LIB}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Pump engine (cargo, ${PUMP_TRIPLES})"
    VERBATIM)

add_library(pump_engine INTERFACE)
target_link_libraries(pump_engine INTERFACE ${PUMP_LIB})
target_include_directories(pump_engine INTERFACE ${PUMP_ROOT}/include)
add_dependencies(pump_engine pump_engine_cargo)
