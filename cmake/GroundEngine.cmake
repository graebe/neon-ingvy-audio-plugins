# The animated ground's kick detector -- a HEADER, and no archive of its own.
#
# WHY THERE IS NO CARGO TARGET IN THIS FILE, which is the only surprising thing
# about it. Every other cmake/*Engine.cmake here builds a Rust staticlib. This
# one deliberately does not, because a second staticlib is exactly what must not
# exist: a Rust staticlib carries its own copy of the Rust runtime, so linking
# two into one binary fails on duplicate symbols (`_rust_eh_personality` and
# friends). NI Listen-In stopped linking the moment it had both libbus_capi.a and
# a libground_capi.a, which is how this file came to look like this.
#
# The invariant this repository keeps instead is stated in
# engines/spectro/crates/spectro-capi/Cargo.toml: ONE static library per plugin.
# The Spectrogram's shell links only libspectro_capi.a and reaches bus-core by
# DEPENDING on spectro-recv rather than by linking a second archive.
#
# So the ground follows that pattern. `ground-capi` is an rlib, and each
# product's capi crate depends on it -- bus-capi, spectro-capi, tg-capi and
# sc-capi each name it in their Cargo.toml and link it with a `use ground_capi
# as _;`. The gnd_* symbols are therefore already inside whichever single archive
# a plugin was going to link anyway, and there is nothing here to build.
#
# What IS still needed is the header, since the C++ shells include it. That is
# all this target carries.
#
# THE CONSEQUENCE FOR REBUILDS, worth knowing: a change to ground-core rebuilds
# through each product's cargo target, not through one of its own. That is the
# same arrangement spectro-recv has, and `cargo` is the dependency scanner in
# every one of those targets, so an edit here does reach all four plugins.

set(GND_ROOT ${CMAKE_SOURCE_DIR}/engines/ground)

if (NOT EXISTS ${GND_ROOT}/include/ground_detect.h)
    message(FATAL_ERROR
        "engines/ground/include/ground_detect.h is missing -- it is the hand-written "
        "contract for the ground detector and every plugin's shell includes it.")
endif()

add_library(ground_engine INTERFACE)
target_include_directories(ground_engine INTERFACE ${GND_ROOT}/include)

# THE TOOLCHAIN, FOR THE TESTS AND FOR NOTHING ELSE.
#
# There is no cargo invocation in this file, but `gnd_core` in
# tests/CMakeLists.txt is a `cargo test` and needs the same two variables every
# other engine's tests use. Leaving them unset does not fail loudly: the test
# command degrades to `test --manifest-path ...`, `test` resolves to the SHELL
# BUILTIN, and ctest reports `--manifest-path: unexpected operator` -- which
# names neither cargo nor this file. It did exactly that once.
include(${CMAKE_SOURCE_DIR}/cmake/RustToolchain.cmake)
set(GND_CARGO ${RUST_CARGO})
set(GND_ENV ${RUST_ENV})

# The detector's own tests are cargo's and are registered in
# tests/CMakeLists.txt; the C round-trip links a product's archive, which is also
# how it proves the symbols survive into one.
