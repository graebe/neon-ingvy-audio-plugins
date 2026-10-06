# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Put cargo on PATH, or say why it cannot be found. Source, do not execute:
#
#     . "$(dirname "$0")/../../scripts/rust-env.sh"
#
# The shell half of cmake/RustToolchain.cmake -- see the note there for why
# `command -v cargo` is not enough. The short version: rustup.rs, Homebrew and
# a bare toolchain each put the binary somewhere different, and Homebrew's is
# the one nothing used to look in (~/.cargo/bin exists in a Homebrew install
# but holds only what `cargo install` put there -- no cargo shim).
#
# This lived inline in engines/trance-gate/tests/run.sh and nowhere else, so
# the module's package.sh (now modules/_shared/package.sh) called bare `cargo`
# and failed on exactly the setups run.sh had already worked around.

if ! command -v cargo >/dev/null 2>&1; then
    _tg_cargo=""

    # rustup knows best when it is reachable, whatever the layout around it.
    if command -v rustup >/dev/null 2>&1; then
        _tg_cargo="$(rustup which cargo 2>/dev/null)" || _tg_cargo=""
    fi

    if [ -z "$_tg_cargo" ]; then
        for _tg_dir in \
            "${CARGO_HOME:-$HOME/.cargo}/bin" \
            /opt/homebrew/opt/rustup/bin \
            /usr/local/opt/rustup/bin \
            /opt/homebrew/bin \
            /usr/local/bin
        do
            if [ -x "$_tg_dir/cargo" ]; then _tg_cargo="$_tg_dir/cargo"; break; fi
        done
    fi

    if [ -z "$_tg_cargo" ]; then
        echo "cargo not found -- both engines in this repository are Rust." >&2
        echo "  Install:  https://rustup.rs" >&2
        echo "            or: brew install rustup && rustup default stable" >&2
        echo "  Installed already? Put its directory on PATH:" >&2
        echo "            rustup which cargo   # names the real one" >&2
        return 1 2>/dev/null || exit 1
    fi

    # The DIRECTORY, not the binary: cargo shells out to rustc and expects to
    # find it beside itself. Without this it fails with "could not execute
    # process `rustc -vV`", which names neither PATH nor rustup.
    PATH="$(dirname "$_tg_cargo"):$PATH"
    export PATH
    unset _tg_cargo _tg_dir
fi
