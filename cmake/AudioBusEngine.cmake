# The house audio transport, built from engines/audio-bus by cargo.
#
# NOT A PRODUCT ENGINE, and that is the one thing to know before reading on.
# cmake/SpectroEngine.cmake and cmake/TranceGateEngine.cmake each build the DSP
# of exactly one plugin. This one builds something BOTH of them may link: a
# shared-memory bus that a Listen-In publishes into and a Spectrogram reads out
# of. It is the Rust counterpart of ui-kit.
#
# docs/tech/structure.md's rule -- "a crate belongs to exactly one product" --
# is about product engines and still holds: audio-bus depends on no product, and
# that is the direction that matters. See the note in Cargo.toml.
#
# WHY IT IS RUST AND NOT C++. AGENTS.md: "Preferred language is rust. Use C++/C
# wherever absolutely necessary." A lock-free ring over shared memory is the
# opposite of absolutely necessary C++ -- it is precisely the code where an
# aliasing mistake is invisible until it is a click in someone's monitors. The
# crate has NO dependencies at all (it declares the six POSIX calls it needs
# rather than taking libc), so it adds nothing to THIRD_PARTY_LICENSES.md.
#
# Everything below is cmake/SpectroEngine.cmake's structure, including the two
# traps it documents: rustup's shims are often not on PATH when CMake is driven
# from an IDE, and cargo needs rustc BESIDE it on PATH when it was found through
# `rustup which`.

set(ABUS_ROOT ${CMAKE_SOURCE_DIR}/engines/audio-bus)

# THE WORKSPACE IS THE REPOSITORY ROOT, not this engine's directory: both
# engines' crates are members of one Cargo.toml there.
if (NOT EXISTS ${CMAKE_SOURCE_DIR}/Cargo.toml)
    message(FATAL_ERROR "No Cargo.toml at the repository root -- the workspace is missing.")
endif()

# cargo, found once for the whole repository -- rustup.rs, Homebrew and a bare
# toolchain each put it somewhere different, and this searched only one of them.
include(${CMAKE_SOURCE_DIR}/cmake/RustToolchain.cmake)
set(ABUS_CARGO ${RUST_CARGO})

# CMake's Apple architecture names are not Rust's target triples.
set(ABUS_TRIPLES "")
foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    if (arch STREQUAL "arm64")
        list(APPEND ABUS_TRIPLES aarch64-apple-darwin)
    elseif (arch STREQUAL "x86_64")
        list(APPEND ABUS_TRIPLES x86_64-apple-darwin)
    else()
        message(FATAL_ERROR "no Rust target known for OSX architecture '${arch}'")
    endif()
endforeach()

set(ABUS_BUILD_DIR ${CMAKE_BINARY_DIR}/audio-bus-engine)
set(ABUS_LIB ${ABUS_BUILD_DIR}/libbus_capi.a)

set(ABUS_CARGO_ARGS build --release -p bus-capi
    --target-dir ${ABUS_BUILD_DIR}/target)
set(ABUS_SLICES "")
foreach(triple IN LISTS ABUS_TRIPLES)
    list(APPEND ABUS_CARGO_ARGS --target ${triple})
    list(APPEND ABUS_SLICES
         ${ABUS_BUILD_DIR}/target/${triple}/release/libbus_capi.a)
endforeach()
if (NOT ABUS_SLICES)
    # No OSX_ARCHITECTURES: one host build, no triple in the path.
    set(ABUS_SLICES ${ABUS_BUILD_DIR}/target/release/libbus_capi.a)
endif()

list(LENGTH ABUS_SLICES ABUS_SLICE_COUNT)
if (ABUS_SLICE_COUNT GREATER 1)
    set(ABUS_COMBINE COMMAND lipo -create ${ABUS_SLICES} -output ${ABUS_LIB}.new)
else()
    set(ABUS_COMBINE COMMAND ${CMAKE_COMMAND} -E copy ${ABUS_SLICES} ${ABUS_LIB}.new)
endif()

# The environment cargo needs -- rustc beside it on PATH, no make jobserver,
# the deployment target carried through. Built once; see RustToolchain.cmake.
set(ABUS_ENV ${RUST_ENV})

# CARGO IS THE DEPENDENCY SCANNER, NOT CMAKE -- so this target always runs, and
# copy_if_different is what keeps an unchanged analyzer from relinking three
# plugin formats.
add_custom_target(audio_bus_engine_cargo ALL
    BYPRODUCTS ${ABUS_LIB}
    COMMAND ${ABUS_ENV} ${ABUS_CARGO} ${ABUS_CARGO_ARGS}
    ${ABUS_COMBINE}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ABUS_LIB}.new ${ABUS_LIB}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building the audio bus (cargo, ${ABUS_TRIPLES})"
    VERBATIM)

add_library(audio_bus_engine INTERFACE)
target_link_libraries(audio_bus_engine INTERFACE ${ABUS_LIB})
target_include_directories(audio_bus_engine INTERFACE ${ABUS_ROOT}/include)
add_dependencies(audio_bus_engine audio_bus_engine_cargo)

# The transport's own cargo tests are registered with ctest in
# tests/CMakeLists.txt, which runs after enable_testing(); ABUS_ENV and
# ABUS_CARGO are what it needs to find the toolchain the same way this file did.
