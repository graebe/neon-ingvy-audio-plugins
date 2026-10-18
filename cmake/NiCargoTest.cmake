# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The Rust crates' tests, built once and run per product.
#
#   ni_cargo_test(<name> <package>...)
#
# registers <name> as a run of those packages' tests (their unit, integration
# and example tests; the doctests are rust_doctests', below).
#
# WHY NOT ONE `cargo test -p ...` PER TEST, as it was. Cargo's resolver
# unifies features over the packages named on its command line, so every
# different -p selection is a different build of the crates they share: the
# quick tier ran a dozen of them, each recompiling 4 to 24 crates the others
# had already built, all queued on the one target directory's lock (measured
# 2026-10-18: 70-140 s a suite, the tier's Rust suites about four minutes).
#
# WITH cargo-nextest (https://nexte.st, Apache-2.0 OR MIT, `brew install
# cargo-nextest`), the whole workspace's tests are built ONCE, with the
# workspace's features -- rust_tests_build, a fixture every suite requires --
# and each suite runs only its packages' tests from that build (`-E
# 'package(a) | package(b)'`), every test in its own process, in parallel. All
# of the workspace's 1202 tests then run in about 25 s.
#
# Features are the workspace's, not the selection's. A crate that must be
# tested as it ships ALONE -- a Move module's crate, a core without its default
# features -- keeps its own `cargo test` (vz_move_rs, fd_move_rs,
# vz_core_time_only, music_core_no_std), which is that build on purpose.
#
# Tests of different binaries now run at once, which `cargo test` never did:
# .config/nextest.toml keeps the audio bus's tests, whose slots are distinct
# only within a binary, to one at a time.
#
# Without cargo-nextest each suite is `cargo test -p ...` as before, and the
# configure says so: slower, never different in what it checks.
include_guard(GLOBAL)

find_program(NI_CARGO_NEXTEST cargo-nextest HINTS $ENV{HOME}/.cargo/bin /opt/homebrew/bin /usr/local/bin)
if (NI_CARGO_NEXTEST)
    message(STATUS "cargo-nextest: ${NI_CARGO_NEXTEST} (the Rust tests are built once)")
else()
    message(STATUS "cargo-nextest: not found, each Rust suite builds its own selection "
                   "(brew install cargo-nextest)")
endif()

set(_ni_cargo_manifest ${CMAKE_SOURCE_DIR}/Cargo.toml)

# EACH SUITE'S SHARE OF THE MACHINE, told to ctest. nextest runs a test per
# process, as many at once as it is given threads, and ctest runs up to -j
# suites side by side: left to the default (a thread per core each), ten
# suites asked for a hundred processes on ten cores. A suite takes
# NI_NEXTEST_THREADS, and says so (PROCESSORS), so ctest starts no more suites
# than the cores hold.
set(NI_NEXTEST_THREADS 4 CACHE STRING "Tests one Rust suite runs at once under cargo-nextest")

# The workspace's test programs, built once before any suite runs. A suite run
# alone (ctest -R) pulls it in as well.
if (NI_CARGO_NEXTEST)
    add_test(NAME rust_tests_build
             COMMAND ${RUST_ENV} ${NI_CARGO_NEXTEST} nextest run --manifest-path ${_ni_cargo_manifest}
                     --workspace --no-run --cargo-quiet)
    set_tests_properties(rust_tests_build PROPERTIES FIXTURES_SETUP ni_rust_tests)
endif()

# The doctests, which nextest does not run: once, over the workspace, with the
# features the build above has, so they compile nothing it did not.
add_test(NAME rust_doctests
         COMMAND ${RUST_ENV} ${RUST_CARGO} test --manifest-path ${_ni_cargo_manifest} --workspace --doc)

function(ni_cargo_test name)
    if (NOT ARGN)
        message(FATAL_ERROR "ni_cargo_test(${name}): name the packages it tests")
    endif()
    if (NI_CARGO_NEXTEST)
        list(TRANSFORM ARGN PREPEND "package(")
        list(TRANSFORM ARGN APPEND ")")
        list(JOIN ARGN " | " filter)
        add_test(NAME ${name}
                 COMMAND ${RUST_ENV} ${NI_CARGO_NEXTEST} nextest run --manifest-path ${_ni_cargo_manifest}
                         --workspace --cargo-quiet --no-fail-fast
                         --test-threads ${NI_NEXTEST_THREADS} -E "${filter}")
        set_tests_properties(${name} PROPERTIES FIXTURES_REQUIRED ni_rust_tests
                                                PROCESSORS ${NI_NEXTEST_THREADS})
    else()
        list(TRANSFORM ARGN PREPEND "-p;")
        add_test(NAME ${name}
                 COMMAND ${RUST_ENV} ${RUST_CARGO} test --manifest-path ${_ni_cargo_manifest} ${ARGN})
    endif()
endfunction()
