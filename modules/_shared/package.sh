#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Build and package one Schwung module for the Move (aarch64 Linux).
#
#   ./modules/_shared/package.sh trance-gate
#   ./modules/_shared/package.sh side-chain
#   cmake --build build --target schwung              the same, for trance-gate
#   cmake --build build --target schwung-side-chain   the same, for side-chain
#
# ONE SCRIPT, EVERY MODULE. The argument is the module's directory under
# modules/, which is also its product name in versions.json and its tag prefix.
# Everything that differs between modules -- the catalog ID and the crate that
# is built -- is in modules/<name>/module.env; everything else is here, once.
# There used to be a copy per module, and the copies had already drifted: one
# packaged its LICENSE after an `&&` list that ended the script under `set -e`.
#
# THE SECOND OF TWO TARGETS AROUND ONE CORE. Each plugin links its engine's
# `*-capi` crate (see cmake/NiRust.cmake); this builds the `*-move`
# crate, the Schwung audio_fx v2 vtable. Both are members of the repository's
# single Cargo workspace and both reach the engine's `*-core` by relative path,
# so what ships here and what ships in the VST3 are the same DSP compiled
# twice, never two implementations.
#
# Docker does the cross-compile unless CROSS_PREFIX is already set, which is
# how this runs INSIDE the container (the image sets it).
set -euo pipefail

SHARED_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SHARED_DIR/../.." && pwd)"

MODULE="${1:-}"
MODULE_DIR="$REPO_ROOT/modules/$MODULE"
if [ -z "$MODULE" ] || [ ! -f "$MODULE_DIR/module.env" ]; then
    echo "usage: $0 <module>   (a directory under modules/ with a module.env)" >&2
    exit 2
fi
# MODULE_ID, MODULE_TITLE, MODULE_CRATE -- see the file for what each is.
. "$MODULE_DIR/module.env"
# Each stage is timed into the timing log (scripts/timing.sh); inside the
# container nothing is, the host having timed the container's run as a whole.
# shellcheck source=../../scripts/timing.sh
. "$REPO_ROOT/scripts/timing.sh"
# shellcheck source=./container.sh
. "$SHARED_DIR/container.sh"

if ! ni_in_module_container; then
    echo "=== $MODULE_TITLE module (via Docker) ==="
    ni_module_container "$MODULE" modules/_shared/package.sh "$MODULE"
    echo "=== Done ==="
    exit 0
fi

# cargo, wherever it is installed. See scripts/rust-env.sh -- a bare `cargo`
# fails on any setup that does not put a shim in ~/.cargo/bin, which includes
# every Homebrew install.
. "$REPO_ROOT/scripts/rust-env.sh"

cd "$REPO_ROOT"
echo "=== Building $MODULE_TITLE module ($MODULE_ID) ==="
echo "Cross prefix: $CROSS_PREFIX"

OUT="dist/$MODULE_ID"
rm -rf "$OUT"
mkdir -p "$OUT"

echo "Compiling the Schwung wrapper (Rust)..."
# CARGO RUNS AT THE REPOSITORY ROOT, which is where the one workspace and its
# .cargo/config.toml live -- that config is what sets target-cpu=cortex-a72 for
# this triple, and cargo only finds it by walking UP from the working directory.
#
# -Ofast IS GONE AND CANNOT COME BACK. The C build used it, which let clang
# contract `a - b*c` into a fused multiply-add -- so the shipped .so and the
# suite that tested it were never bit-identical to each other. Rust does not
# contract, so the module and its tests compute the same numbers, and the
# golden render pins the algorithm rather than a compiler flag.
#
# --locked: the module links exactly the crates Cargo.lock names, the ones the
# licence gate checked and the notices below list. A manifest edited past its
# lock stops the build here, where cargo would otherwise re-resolve, rewrite
# the lock and ship crates nothing has checked.
ni_time_stage "cargo $MODULE_CRATE" -- \
    cargo build --locked --release -p "$MODULE_CRATE" --target aarch64-unknown-linux-gnu

# THE .so NAME IS LOAD-BEARING. For component_type audio_fx the chain host
# builds the path itself as modules/audio_fx/<id>/<id>.so and never reads
# module.json's "dsp" field. Name it anything else and the module simply does
# not load, with no error on screen -- one line in debug.log and nothing else.
#
# Straight into dist/, not via build/: that is CMake's build directory, and a
# script CMake runs has no business leaving files in it.
LIB="target/aarch64-unknown-linux-gnu/release/lib${MODULE_CRATE//-/_}.so"
cp "$LIB" "$OUT/${MODULE_ID}.so"
"${CROSS_PREFIX}strip" --strip-unneeded "$OUT/${MODULE_ID}.so"
echo "  size: $(wc -c < "$OUT/${MODULE_ID}.so") bytes"

echo "Packaging..."
cp "$MODULE_DIR/module.json"   "$OUT/module.json"
# Optional per module: the Side-Chain deliberately ships no ui_chain.js (the
# host draws its knob grid from chain_params). `if`, not `[ -f ] && cp`, which
# ends the script under `set -e` when the file is absent.
for optional in ui_chain.js help.json; do
    if [ -f "$MODULE_DIR/$optional" ]; then
        cp "$MODULE_DIR/$optional" "$OUT/$optional"
    fi
done
# THE NOTICES SHIP WITH THE BINARY. What lands on a device is a .so (and a .js)
# with no repository anywhere near it, and the GPL asks that every copy come
# with the licence -- so it travels in the tarball or it does not reach the
# person it is addressed to at all. THIRD_PARTY_LICENSES.md too: the .so statically links
# the Rust standard library and the crates from crates.io its engine uses
# (wmidi, lexical-core, serde_json and the rest), and the Side-Chain's MIDI
# trigger is a port of MIT-licensed code; every one's notice is recorded there.
cp LICENSE                     "$OUT/LICENSE"
cp THIRD_PARTY_LICENSES.md     "$OUT/THIRD_PARTY_LICENSES.md"
chmod 755 "$OUT/${MODULE_ID}.so"

tar -C dist -czf "dist/${MODULE_ID}-module.tar.gz" "$MODULE_ID/"

echo ""
echo "=== Build Complete ==="
echo "Output:  $OUT/"
echo "Tarball: dist/${MODULE_ID}-module.tar.gz"
ls -la "$OUT/"
