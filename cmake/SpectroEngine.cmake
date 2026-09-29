# The Spectrogram's analyzer, built from engines/spectro by cargo.
#
# WHY IT IS RUST AND NOT C++. AGENTS.md: "Preferred language is rust. Use C++/C
# wherever absolutely necessary." An FFT is not absolutely necessary C++, and
# iPlug2's own ISpectrumSender (WDL's FFT, and perfectly good) would have put the
# analysis on the wrong side of that line. The crate has NO dependencies at all,
# so it adds nothing to THIRD_PARTY_LICENSES.md.
#
# WHY IT IS IN THIS REPOSITORY AND NOT A SUBMODULE. The Trance Gate's engine is a
# submodule because it has a SECOND consumer -- the Schwung module on the Move --
# and a second copy would drift. This one has no second consumer yet: the Move
# has no screen to draw a spectrogram on. It moves out to its own repository and
# becomes a pinned submodule on the day something else links it, and not before.
#
# Everything below is cmake/TgEngine.cmake's structure, including the two traps
# it documents: rustup's shims are often not on PATH when CMake is driven from an
# IDE, and cargo needs rustc BESIDE it on PATH when it was found through
# `rustup which`.

set(SPECTRO_ROOT ${CMAKE_SOURCE_DIR}/engines/spectro)

if (NOT EXISTS ${SPECTRO_ROOT}/Cargo.toml)
    message(FATAL_ERROR "engines/spectro is missing its Cargo.toml.")
endif()

find_program(SPECTRO_CARGO cargo HINTS $ENV{HOME}/.cargo/bin)
if (NOT SPECTRO_CARGO)
    find_program(SPECTRO_RUSTUP rustup HINTS $ENV{HOME}/.cargo/bin)
    if (SPECTRO_RUSTUP)
        execute_process(COMMAND ${SPECTRO_RUSTUP} which cargo
                        OUTPUT_VARIABLE SPECTRO_CARGO
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
    endif()
endif()
if (NOT SPECTRO_CARGO)
    message(FATAL_ERROR "cargo not found -- the analyzer is Rust. https://rustup.rs")
endif()

# CMake's Apple architecture names are not Rust's target triples.
set(SPECTRO_TRIPLES "")
foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    if (arch STREQUAL "arm64")
        list(APPEND SPECTRO_TRIPLES aarch64-apple-darwin)
    elseif (arch STREQUAL "x86_64")
        list(APPEND SPECTRO_TRIPLES x86_64-apple-darwin)
    else()
        message(FATAL_ERROR "no Rust target known for OSX architecture '${arch}'")
    endif()
endforeach()

set(SPECTRO_BUILD_DIR ${CMAKE_BINARY_DIR}/spectro-engine)
set(SPECTRO_LIB ${SPECTRO_BUILD_DIR}/libspectro_capi.a)

set(SPECTRO_CARGO_ARGS build --release -p spectro-capi
    --target-dir ${SPECTRO_BUILD_DIR}/target)
set(SPECTRO_SLICES "")
foreach(triple IN LISTS SPECTRO_TRIPLES)
    list(APPEND SPECTRO_CARGO_ARGS --target ${triple})
    list(APPEND SPECTRO_SLICES
         ${SPECTRO_BUILD_DIR}/target/${triple}/release/libspectro_capi.a)
endforeach()
if (NOT SPECTRO_SLICES)
    # No OSX_ARCHITECTURES: one host build, no triple in the path.
    set(SPECTRO_SLICES ${SPECTRO_BUILD_DIR}/target/release/libspectro_capi.a)
endif()

list(LENGTH SPECTRO_SLICES SPECTRO_SLICE_COUNT)
if (SPECTRO_SLICE_COUNT GREATER 1)
    set(SPECTRO_COMBINE COMMAND lipo -create ${SPECTRO_SLICES} -output ${SPECTRO_LIB}.new)
else()
    set(SPECTRO_COMBINE COMMAND ${CMAKE_COMMAND} -E copy ${SPECTRO_SLICES} ${SPECTRO_LIB}.new)
endif()

get_filename_component(SPECTRO_CARGO_DIR ${SPECTRO_CARGO} DIRECTORY)
set(SPECTRO_ENV ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS
                "PATH=${SPECTRO_CARGO_DIR}:$ENV{PATH}")
if (CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND SPECTRO_ENV MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()

# CARGO IS THE DEPENDENCY SCANNER, NOT CMAKE -- so this target always runs, and
# copy_if_different is what keeps an unchanged analyzer from relinking three
# plugin formats.
add_custom_target(spectro_engine_cargo ALL
    BYPRODUCTS ${SPECTRO_LIB}
    COMMAND ${SPECTRO_ENV} ${SPECTRO_CARGO} ${SPECTRO_CARGO_ARGS}
    ${SPECTRO_COMBINE}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPECTRO_LIB}.new ${SPECTRO_LIB}
    WORKING_DIRECTORY ${SPECTRO_ROOT}
    COMMENT "Building the Spectrogram analyzer (cargo, ${SPECTRO_TRIPLES})"
    VERBATIM)

add_library(spectro_engine INTERFACE)
target_link_libraries(spectro_engine INTERFACE ${SPECTRO_LIB})
target_include_directories(spectro_engine INTERFACE ${SPECTRO_ROOT}/include)
add_dependencies(spectro_engine spectro_engine_cargo)

# The analyzer's own cargo tests are registered with ctest in tests/CMakeLists.txt,
# which runs after enable_testing(); SPECTRO_ENV and SPECTRO_CARGO are what it
# needs to find the toolchain the same way this file did.
