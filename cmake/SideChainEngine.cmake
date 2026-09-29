# NI Side-Chain's engine C ABI, built by cargo for the PLUGIN's targets.
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
# THIS IS ONE OF TWO WRAPPERS AROUND ONE CORE. `sc-capi` is the C ABI, which
# is what a plugin format needs; `sc-move` is the Schwung audio_fx vtable,
# and cmake/Schwung.cmake builds that one. Both depend on `sc-core` by
# relative path inside the repository's single Cargo workspace, so neither can
# drift from the other -- not by policy, by construction. The render A/B is
# what keeps that honest: it renders through the plugin's audio path and
# matches the Move module's reference render byte for byte.

set(SC_ROOT ${CMAKE_SOURCE_DIR}/engines/side-chain)

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
set(SC_CARGO ${RUST_CARGO})

# CMake's Apple architecture names are not Rust's target triples, and the
# mapping is the only place this build knows about either.
set(SC_TRIPLES "")
foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    if (arch STREQUAL "arm64")
        list(APPEND SC_TRIPLES aarch64-apple-darwin)
    elseif (arch STREQUAL "x86_64")
        list(APPEND SC_TRIPLES x86_64-apple-darwin)
    else()
        message(FATAL_ERROR "no Rust target known for OSX architecture '${arch}'")
    endif()
endforeach()

set(SC_BUILD_DIR ${CMAKE_BINARY_DIR}/sc-engine)
set(SC_LIB ${SC_BUILD_DIR}/libsc_capi.a)

set(SC_CARGO_ARGS build --release -p sc-capi --target-dir ${SC_BUILD_DIR}/target)
set(SC_SLICES "")
foreach(triple IN LISTS SC_TRIPLES)
    list(APPEND SC_CARGO_ARGS --target ${triple})
    list(APPEND SC_SLICES ${SC_BUILD_DIR}/target/${triple}/release/libsc_capi.a)
endforeach()
if (NOT SC_SLICES)
    # No OSX_ARCHITECTURES: one host build, no triple in the path.
    set(SC_SLICES ${SC_BUILD_DIR}/target/release/libsc_capi.a)
endif()

list(LENGTH SC_SLICES SC_SLICE_COUNT)
if (SC_SLICE_COUNT GREATER 1)
    set(SC_COMBINE COMMAND lipo -create ${SC_SLICES} -output ${SC_LIB}.new)
else()
    set(SC_COMBINE COMMAND ${CMAKE_COMMAND} -E copy ${SC_SLICES} ${SC_LIB}.new)
endif()

# The environment cargo needs -- rustc beside it on PATH, no make jobserver,
# the deployment target carried through. Built once; see RustToolchain.cmake.
set(SC_ENV ${RUST_ENV})

# CARGO IS THE DEPENDENCY SCANNER, NOT CMAKE. A file(GLOB) over the crates
# would be a second, worse answer to a question cargo already answers exactly,
# and a stale one the first time a file is added. So this target always runs;
# cargo is a no-op in well under a second when nothing changed.
#
# copy_if_different is what keeps that from relinking three plugin formats on
# every build: an unchanged engine leaves ${SC_LIB}'s timestamp alone.
add_custom_target(sc_engine_cargo ALL
    BYPRODUCTS ${SC_LIB}
    COMMAND ${SC_ENV} ${SC_CARGO} ${SC_CARGO_ARGS}
    ${SC_COMBINE}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SC_LIB}.new ${SC_LIB}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the NI Side-Chain engine (cargo, ${SC_TRIPLES})"
    VERBATIM)

add_library(sc_engine INTERFACE)
target_link_libraries(sc_engine INTERFACE ${SC_LIB})
target_include_directories(sc_engine INTERFACE ${SC_ROOT}/include)
add_dependencies(sc_engine sc_engine_cargo)
