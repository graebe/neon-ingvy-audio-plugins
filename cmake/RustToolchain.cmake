# Finding cargo, once, for everything in this repository that needs it.
#
# WHY THIS IS NOT JUST `find_program(cargo)`.
#
# Rust has no single install location, and the three common ones put the
# binary somewhere different:
#
#   rustup.rs      ~/.cargo/bin/cargo            (a shim)
#   Homebrew       /opt/homebrew/opt/rustup/bin  (its own shim dir; ~/.cargo/bin
#                                                 exists but holds only tools
#                                                 installed by `cargo install`)
#   any of them    ~/.rustup/toolchains/<tc>/bin (the real binary)
#
# On top of that, CMake driven from an IDE or from Xcode starts a login shell
# WITHOUT the user's profile, so even a correctly installed toolchain is often
# absent from PATH at configure time.
#
# This searched only ~/.cargo/bin, in two files with different variable
# prefixes, plus a third copy in a shell script -- so a Homebrew install failed
# in all three and the error named none of them. One list, one place.
#
# Sets RUST_CARGO (the binary) and RUST_ENV (a command prefix that runs it with
# a usable environment). scripts/rust-env.sh is the same search for shell.

# NO EARLY RETURN HERE, and that is a correctness point rather than a style one.
#
# RUST_CARGO is a CACHE variable, so on the second and every later configure it
# is already defined -- and a `return()` on that condition skipped the RUST_ENV
# construction at the bottom of this file, leaving it empty. cargo then ran
# without rustc on PATH and failed with
#
#     error: could not execute process `rustc -vV` (never executed)
#
# which names neither PATH nor this file. The search below is already cheap on
# a re-configure: find_program answers from the cache.

set(_rust_hints
    $ENV{CARGO_HOME}/bin
    $ENV{HOME}/.cargo/bin
    /opt/homebrew/opt/rustup/bin  # Homebrew's rustup keeps its shims here
    /usr/local/opt/rustup/bin     # ... and here on Intel
    /opt/homebrew/bin
    /usr/local/bin)

find_program(RUST_CARGO cargo HINTS ${_rust_hints})

# NOT CACHED AS A FAILURE. find_program writes NOTFOUND into the cache and then
# never looks again, so installing Rust afterwards left a stale NOTFOUND that
# only a wiped build directory cleared -- while the rustup fallback below
# quietly made it work anyway, leaving a cache entry that said the opposite of
# the truth.
if (NOT RUST_CARGO)
    unset(RUST_CARGO CACHE)
    find_program(_rust_rustup rustup HINTS ${_rust_hints})
    if (_rust_rustup)
        execute_process(COMMAND ${_rust_rustup} which cargo
                        OUTPUT_VARIABLE RUST_CARGO
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
    endif()
endif()

if (NOT RUST_CARGO)
    message(FATAL_ERROR
        "cargo not found -- both engines in this repository are Rust.\n"
        "  Install:  https://rustup.rs   (or: brew install rustup && rustup default stable)\n"
        "  Installed already? Its directory is not on PATH. Searched:\n"
        "    ${_rust_hints}\n"
        "  `rustup which cargo` names the real one; put its directory on PATH.")
endif()

# CARGO NEEDS RUSTC BESIDE IT ON PATH. When cargo was found through `rustup
# which` -- an IDE, or Xcode, or any build not started from the user's login
# shell -- its own directory is not on PATH either, and cargo fails with
# "could not execute process `rustc -vV`", which names neither PATH nor rustup.
#
# --unset=MAKEFLAGS because make exports a jobserver cargo cannot attach to
# from here, and the warning it prints looks like a real failure.
#
# MACOSX_DEPLOYMENT_TARGET has to reach rustc, or its objects carry a newer
# minimum than the C++ around them and every link prints "object file was built
# for newer macOS version".
get_filename_component(_rust_cargo_dir ${RUST_CARGO} DIRECTORY)
set(RUST_ENV ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS
             "PATH=${_rust_cargo_dir}:$ENV{PATH}")
if (CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND RUST_ENV MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()

message(STATUS "cargo: ${RUST_CARGO}")
