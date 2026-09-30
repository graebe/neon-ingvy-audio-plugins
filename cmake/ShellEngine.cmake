# The shell plumbing: how every plugin's threads reach its engine.
#
# A HEADER-ONLY TARGET, for the ground's reason (cmake/GroundEngine.cmake): the
# Rust half has no archive of its own. shell-core is compiled into each
# product's capi crate, and shell-capi's handoff symbols ride in the archive of
# every product that uses them -- one static library per plugin.
#
#   shell_handoff.h   an object the main thread owns, lent to the audio thread
#   shell_state.h     the header every plugin's state chunk starts with

set(SHELL_ROOT ${CMAKE_SOURCE_DIR}/engines/shell)

add_library(shell_engine INTERFACE)
target_include_directories(shell_engine INTERFACE ${SHELL_ROOT}/include)

# For the cargo test in tests/CMakeLists.txt; see GroundEngine.cmake for what
# happens when these are left unset.
include(${CMAKE_SOURCE_DIR}/cmake/RustToolchain.cmake)
set(SHELL_CARGO ${RUST_CARGO})
set(SHELL_ENV ${RUST_ENV})
