#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The Move web editors' tests, as ctest runs them (tests/CMakeLists.txt):
#
#   scripts/test-web.sh unit kit            the kit's node tests (quick)
#   scripts/test-web.sh unit <module>       an editor's node tests, after its
#                                           engine's wasm build (quick)
#   scripts/test-web.sh e2e <module>        the editor built (scripts/build-web.sh)
#                                           and its Playwright suite, in Chrome,
#                                           on the editor's own ports (full)
#
# Exits 77 -- ctest's skip -- when there is no node_modules: a C++ developer's
# run should not fail for want of `npm ci`, and says what is missing.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

kind="${1:-}"; what="${2:-}"
case "$kind:$what" in
    unit:kit|unit:trance-gate|unit:side-chain|e2e:trance-gate|e2e:side-chain) ;;
    *) sed -n '5,15p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
esac
if [ ! -d node_modules/solid-js ]; then
    echo "web tests skipped: no node_modules (run npm ci)"
    exit 77
fi

wasm() {
    . scripts/rust-env.sh
    cargo build --quiet --release --target wasm32-unknown-unknown -p "$1"
}

case "$kind:$what" in
    unit:kit)
        exec node --conditions=browser --test web/kit/test/*.test.mjs ;;
    unit:trance-gate)
        wasm tg-web
        exec node --conditions=browser --test web/trance-gate/test/*.test.mjs ;;
    unit:side-chain)
        wasm sc-web
        exec node --conditions=browser --test web/side-chain/test/*.test.mjs ;;
    e2e:*)
        scripts/build-web.sh "$what"
        exec npx playwright test --config "web/$what/playwright.config.mjs" ;;
esac
