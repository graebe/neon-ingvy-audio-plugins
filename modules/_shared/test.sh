#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Build one Schwung module and test it as the Move will run it: on aarch64
# Linux, in the image it ships from.
#
#   ./modules/_shared/test.sh trance-gate
#   ./modules/_shared/test.sh side-chain
#   ctest -L move                       both, as part of the full tier
#
# In the container (container.sh), after package.sh has built the tarball:
#
#   1. THE TARBALL, as a device receives it: an aarch64 .so that exports the
#      vtable and the engine's C ABI and nothing of the plugin's (no
#      tg_shell_*, sc_shell_* or gnd_*: the capi crates' `shell` feature is
#      off for the Move); module.json under GPL-3.0-or-later; LICENSE; and a
#      THIRD_PARTY_LICENSES.md naming every crates.io crate this .so links.
#   2. MODULE_TEST_CRATES' cargo tests on aarch64 Linux: the vtable, the
#      chain_params contract and the host panic (CC 120/123), on the
#      architecture they ship to rather than only on the Mac.
#   3. MODULE_TEST_SCRIPT, when the module has one: the Trance Gate's
#      engines/trance-gate/tests/run.sh, whose golden render is held to its
#      recorded md5 here, on arm64.
#
# AARCH64 OR NOTHING. Docker on Apple Silicon runs the image as arm64, which is
# the point; on an x86_64 host it would run x86_64 and test a machine the Move
# is not, so the run stops and says so instead of passing on the wrong ISA.
#
# The cargo builds go to target/aarch64-unknown-linux-gnu/, where package.sh
# builds the .so, never to the host's target/debug or target/release.
set -euo pipefail

SHARED_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SHARED_DIR/../.." && pwd)"

MODULE="${1:-}"
MODULE_DIR="$REPO_ROOT/modules/$MODULE"
if [ -z "$MODULE" ] || [ ! -f "$MODULE_DIR/module.env" ]; then
    echo "usage: $0 <module>   (a directory under modules/ with a module.env)" >&2
    exit 2
fi
# MODULE_ID, MODULE_CRATE, MODULE_TEST_CRATES, MODULE_TEST_SCRIPT.
. "$MODULE_DIR/module.env"
# shellcheck source=../../scripts/timing.sh
. "$REPO_ROOT/scripts/timing.sh"
# shellcheck source=./container.sh
. "$SHARED_DIR/container.sh"

if ! ni_in_module_container; then
    ni_module_container "test $MODULE" modules/_shared/test.sh "$MODULE"
    exit 0
fi

fail() { echo "FAIL: $*" >&2; exit 1; }

TRIPLE=aarch64-unknown-linux-gnu
[ "$(uname -m)" = aarch64 ] || fail "this container is $(uname -m), not aarch64: the module's tests and its golden render must run on the Move's architecture (Docker on Apple Silicon, or an arm64 Linux host)"

. "$REPO_ROOT/scripts/rust-env.sh"
cd "$REPO_ROOT"

"$SHARED_DIR/package.sh" "$MODULE"

echo ""
echo "=== Testing the $MODULE_TITLE tarball ==="
TARBALL="dist/${MODULE_ID}-module.tar.gz"
UNPACKED="$(mktemp -d)"
trap 'rm -rf "$UNPACKED"' EXIT
tar -xzf "$TARBALL" -C "$UNPACKED"
PKG="$UNPACKED/$MODULE_ID"
SO="$PKG/$MODULE_ID.so"

for f in "$MODULE_ID.so" module.json LICENSE THIRD_PARTY_LICENSES.md; do
    [ -f "$PKG/$f" ] || fail "$TARBALL carries no $f"
done
grep -q '"license": *"GPL-3.0-or-later"' "$PKG/module.json" \
    || fail "module.json does not declare GPL-3.0-or-later"
# A ui_chain the module declares must travel with it, or the device draws
# the bare knob grid with no error anywhere.
ui="$(sed -n 's/.*"ui_chain": *"\([^"]*\)".*/\1/p' "$PKG/module.json")"
[ -z "$ui" ] || [ -f "$PKG/$ui" ] || fail "module.json names ui_chain $ui, which is not in $TARBALL"

"${CROSS_PREFIX}readelf" -h "$SO" | grep -q 'Machine: *AArch64' \
    || fail "$MODULE_ID.so is not an aarch64 build"
exports="$("${CROSS_PREFIX}nm" -D --defined-only "$SO" | awk '{print $3}')"
grep -qx move_audio_fx_init_v2 <<<"$exports" || fail "$MODULE_ID.so does not export move_audio_fx_init_v2"
plugin_only="$(grep -E '^[a-z]+_shell_|^gnd_' <<<"$exports" || true)"
[ -z "$plugin_only" ] || fail "$MODULE_ID.so exports the plugin's shell or ground:
$plugin_only"
echo "  $MODULE_ID.so: aarch64, $(wc -c < "$SO") bytes, $(grep -c . <<<"$exports") exports, none of them the plugin's"

# What `cargo tree` says the .so links from crates.io, each by the name and
# version the notices file gives it ("`serde_json` 1.0.151"). The workspace's
# own crates carry a path and are the engines table's.
crates="$(cargo tree --locked -p "$MODULE_CRATE" -e normal --target "$TRIPLE" --prefix none \
          | grep -v ' (/' | sed 's/ (\*)$//' | sort -u)"
[ -n "$crates" ] || fail "cargo tree listed no crate for $MODULE_CRATE"
while read -r name version; do
    grep -qF "\`$name\` ${version#v}" "$PKG/THIRD_PARTY_LICENSES.md" \
        || fail "$MODULE_ID links $name ${version#v}, and its THIRD_PARTY_LICENSES.md has no notice for it"
done <<<"$crates"
echo "  THIRD_PARTY_LICENSES.md: a notice for each of the $(grep -c . <<<"$crates") crates.io crates it links"

echo ""
echo "=== $MODULE_TEST_CRATES on $TRIPLE ==="
args=()
for c in $MODULE_TEST_CRATES; do args+=(-p "$c"); done
CARGO_BUILD_TARGET="$TRIPLE" cargo test --locked "${args[@]}"

if [ -n "${MODULE_TEST_SCRIPT:-}" ]; then
    echo ""
    echo "=== $MODULE_TEST_SCRIPT on $TRIPLE ==="
    CARGO_BUILD_TARGET="$TRIPLE" "./$MODULE_TEST_SCRIPT"
fi

echo ""
echo "=== $MODULE_TITLE: every check passed on $TRIPLE ==="
