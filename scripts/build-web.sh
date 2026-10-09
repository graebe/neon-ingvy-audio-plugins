#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Build the Move modules' web editors: the page Schwung's web manager shows for
# a module's slot (web/<module>, docs/tech/move-web-editors.md).
#
#   scripts/build-web.sh                 every editor
#   scripts/build-web.sh trance-gate     one
#
# For each: the engine's browser build where the editor or its harness runs
# one (engines/<p>/crates/<p>-web, wasm32-unknown-unknown, release), then
# `vite build` into build/web/<module>/, which modules/_shared/package.sh puts
# in the module's tarball. Run on the host, before packaging: the module's
# container has no node, and the page is the same whatever the device.
#
# Every stage is timed into the timing log (scripts/timing.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck source=./timing.sh
. scripts/timing.sh
# shellcheck source=./rust-env.sh
. scripts/rust-env.sh

# The editors, and the engine each one's page or harness runs as wasm.
wasm_crate() {
    case "$1" in
        trance-gate) echo tg-web ;;
        side-chain)  echo sc-web ;;
        *) echo "no web editor called $1 (web/<module>)" >&2; return 2 ;;
    esac
}

modules=("$@")
[ ${#modules[@]} -gt 0 ] || modules=(trance-gate side-chain)

[ -d node_modules ] || { echo "node_modules is missing: run npm ci first" >&2; exit 1; }

for m in "${modules[@]}"; do
    crate=$(wasm_crate "$m")
    ni_time_stage "web wasm $m" -- \
        cargo build --quiet --release --target wasm32-unknown-unknown -p "$crate"
    ni_time_stage "web build $m" -- \
        npx vite build --config "web/$m/vite.config.js" --logLevel warn
    echo "built build/web/$m/"
done
