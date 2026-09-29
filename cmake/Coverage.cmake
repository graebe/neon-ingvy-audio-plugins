# Coverage, for the C, the C++ and the Rust at once.
#
# AGENTS.md has asked for this from the start -- "use coverage tests with human
# & machine readable outputs", "target coverage: >80%" -- and until now the word
# did not appear anywhere in the repository.
#
# ONE ENGINE FOR THREE LANGUAGES. Rust compiles through LLVM, so the same
# -fprofile-instr-generate/-fcoverage-mapping instrumentation, the same .profraw
# files and the same llvm-cov reader serve the C tests, the C++ tests and both
# cargo suites. gcov would have meant a second toolchain for the Rust half and a
# second set of numbers that could disagree with the first.
#
# WHY NOT lcov/genhtml. They are installed here and they are GPL-2.0. There is
# no obligation -- they are developer tools that never link into an artefact --
# but llvm-cov already renders HTML, and THIRD_PARTY_LICENSES.md opens with "the
# licence is uniform: MIT throughout". One fewer thing to have to explain is
# worth more than the nicer stylesheet.
#
# THIS IS NOT A SHIPPABLE CONFIGURATION and is not meant to be built in `build`.
# scripts/coverage.sh uses build-coverage precisely so an instrumented bundle
# can never be mistaken for a release one.

option(VST_COVERAGE "Instrument first-party code for llvm-cov" OFF)

if (NOT VST_COVERAGE)
    return()
endif()

if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "VST_COVERAGE needs clang: source-based coverage is an LLVM feature, and "
        "matching it to the Rust half is the whole reason for choosing it.")
endif()

# ONE ARCHITECTURE. The shipping build is a universal binary because Live is
# still commonly run as x86_64 under Rosetta -- but llvm-cov cannot read a
# coverage mapping out of a fat object without being told which slice, and the
# second slice buys a coverage run nothing at all. Forced rather than warned
# about: a half-instrumented universal binary fails later, in llvm-cov, with an
# error that names neither architectures nor this file.
if (APPLE)
    set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}"
        CACHE STRING "" FORCE)
    message(STATUS "coverage: one slice only (${CMAKE_OSX_ARCHITECTURES})")
endif()

# -O0 so a line maps to the code that was written rather than to whatever
# survived inlining, and no NDEBUG so the assert()s are live while the tests
# run. The shipping build sets -DNDEBUG, which is exactly why the payload-size
# guards in the plugin shells needed a static_assert instead.
set(VST_COVERAGE_FLAGS -fprofile-instr-generate -fcoverage-mapping -O0 -g)

add_compile_options(${VST_COVERAGE_FLAGS})
add_link_options(-fprofile-instr-generate)

# Deliberately global rather than per-target: iPlug2's sources arrive through an
# INTERFACE target that attaches them to whatever links it, so there is no
# first-party target to scope this to. The filtering happens at report time
# instead -- llvm-cov is told which paths to count, which is also where the
# exclusions can be read and argued with rather than being invisible in a flag.
message(STATUS "coverage: instrumented (report via scripts/coverage.sh)")
