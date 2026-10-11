#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The Move editor's test: modules/trance-gate/ui_chain.js RUN by
# tests/smoke_ui.mjs against Schwung's own shared modules and this engine's
# real chain_params.
#
# ctest runs it as tg_move_ui (quick tier), and tests/run.sh runs it after
# the C suites. Exit 77 is "skipped": no node, or the pinned Schwung is not
# cached and cannot be fetched (no git, no network).
#
# WHY A SCHWUNG CHECKOUT. ui_chain.js imports the host's page controller,
# input filter and screen reader by their paths on the device
# (/data/UserData/schwung/shared/...), which this repository does not carry.
# The test points those imports at real sources rather than at stubs, so it
# fails when the module and the host it runs on stop agreeing.
#
# THE PINNED SCHWUNG, AND ONLY IT. modules/_shared/schwung.version names the
# version the modules are tested against, and modules/_shared/schwung.sh
# fetches it once into a cache outside the checkout and verifies it on every
# run -- a moved tag or an edited cache fails here rather than testing some
# other host. This used to take any schwung/ checkout it found above the
# repository, so the test ran on whatever version that checkout was on.
#
# SKIPPED ALOUD, NOT SILENTLY. Exit 77 makes ctest say Skipped: a test that
# could not run must not read as one that passed.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
cd "$HERE/.."

# Node first: without it there is nothing to run the editor in, and nothing
# to fetch Schwung for (the module image has none).
if ! command -v node >/dev/null 2>&1; then
    echo "ui smoke test skipped: node is not installed"
    exit 77
fi
status=0
SCHWUNG=$("$ROOT/modules/_shared/schwung.sh" checkout) || status=$?
if [ "$status" -eq 77 ]; then
    echo "ui smoke test skipped: the pinned Schwung is not cached and could not be fetched"
    exit 77
elif [ "$status" -ne 0 ]; then
    exit "$status"
fi
SHARED="$SCHWUNG/src/shared"
echo "ui smoke test against Schwung $(sed -n 's/^SCHWUNG_TAG=//p' "$ROOT/modules/_shared/schwung.version"), $SHARED"

# The contract the editor plans its pages from, as the module's own vtable
# answers it. ALWAYS REBUILT: this used to try an existing binary first, so
# an edit to chain_params was tested against the PREVIOUS build's JSON for as
# long as the old binary kept working -- a stale fixture reports the old
# contract as the current one, which is worse than none.
TRIPLE="${CARGO_BUILD_TARGET:-}"
OUT="build${TRIPLE:+/$TRIPLE}"
mkdir -p "$OUT"
. "$ROOT/scripts/rust-env.sh"
CAPI="$ROOT/target/capi-include"
NI_CAPI_INCLUDE_DIR="$CAPI" cargo build --quiet --release -p tg-move
ENGINE=$ROOT/target/${TRIPLE:+$TRIPLE/}release/libtg_move.a
cc -std=gnu11 -Iinclude -I"$CAPI" tests/dump_params.c "$ENGINE" \
   -o "$OUT/dump_params" -lm
"./$OUT/dump_params" > "$OUT/chain_params.json"

TG_PARAMS="$OUT/chain_params.json" node tests/smoke_ui.mjs "$SHARED" "$OUT/.smoke"
