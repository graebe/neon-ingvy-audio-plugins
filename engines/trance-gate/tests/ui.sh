#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The Move editor's test: modules/trance-gate/ui_chain.js RUN by
# tests/smoke_ui.mjs against Schwung's own shared modules and this engine's
# real chain_params.
#
#   engines/trance-gate/tests/ui.sh            finds a Schwung checkout
#   SCHWUNG_SHARED=<schwung>/src/shared ...    names one
#
# ctest runs it as tg_move_ui (quick tier), and tests/run.sh runs it after
# the C suites. Exit 77 is "skipped": no Schwung checkout, or no node.
#
# WHY A SCHWUNG CHECKOUT. ui_chain.js imports the host's page controller,
# input filter and screen reader by their paths on the device
# (/data/UserData/schwung/shared/...), which this repository does not carry.
# The test points those imports at real sources rather than at stubs, so it
# fails when the module and the host it runs on stop agreeing.
#
# SKIPPED ALOUD, NOT SILENTLY. The search below used to sit at the end of
# run.sh as four fixed relative paths, written when the engine was its own
# repository; none of them matches this checkout or its worktrees, so every
# check in smoke_ui.mjs was skipped, with one line of output, inside a run
# that passed -- and no ctest ran it at all. Exit 77 makes ctest say Skipped.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
cd "$HERE/.."

# A Schwung checkout beside this one, or beside any directory above it -- the
# worktrees sit a few levels below the checkout's own parent, so the search
# walks up rather than naming depths. `schwung/schwung` is the layout with
# the clone inside a directory of the same name.
SHARED="${SCHWUNG_SHARED:-}"
if [ -z "$SHARED" ]; then
    dir="$ROOT"
    while [ -z "$SHARED" ] && [ "$dir" != "/" ]; do
        dir=$(dirname "$dir")
        for c in "$dir/schwung" "$dir/schwung/schwung"; do
            if [ -d "$c/src/shared/param_pages" ]; then
                SHARED="$c/src/shared"
                break
            fi
        done
    done
fi
if [ ! -d "$SHARED/param_pages" ]; then
    echo "ui smoke test skipped: no Schwung checkout found above $ROOT (set SCHWUNG_SHARED)"
    exit 77
fi
if ! command -v node >/dev/null 2>&1; then
    echo "ui smoke test skipped: node is not installed"
    exit 77
fi
echo "ui smoke test against $SHARED"

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
