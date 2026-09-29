#!/usr/bin/env bash
#
# The coverage run: build instrumented, run everything, report once.
# Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
#
# AGENTS.md asks for "coverage tests with human & machine readable outputs" and
# a target above 80%. This is the one command that produces them:
#
#   build/coverage/coverage.json       the machine's copy -- per file, per unit,
#                                      per language, with totals and the verdict
#   build/coverage/lcov.info           the merged tracefile, for editor gutters
#   build/coverage/html/index.html     the human's copy
#   build/coverage/summary.txt         the same, for a terminal
#
# It does NOT build into `build`. An instrumented plugin is -O0, single-slice
# and NDEBUG-less; keeping it in build-coverage means it can never be picked up
# and shipped by mistake.
#
# Missing tools are NAMED, with the command that installs them, rather than
# failing somewhere inside llvm-cov -- the same courtesy scripts/rust-env.sh
# does for cargo, and for the same reason.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-coverage"
OUT="$ROOT/build/coverage"
PROF="$BUILD/profraw"

cd "$ROOT"

# ------------------------------------------------------------------- tools

missing=0
need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "$1 not found -- $2" >&2
        missing=1
    fi
}

need cmake  "install CMake, or use the one your IDE ships"
need ctest  "it comes with CMake"
need node   "the UI's tests and the report generator are node's (22+)"

# xcrun finds the toolchain's llvm-* on macOS, where they are not on PATH.
if command -v xcrun >/dev/null 2>&1; then
    LLVM_PROFDATA="$(xcrun --find llvm-profdata 2>/dev/null || true)"
    LLVM_COV="$(xcrun --find llvm-cov 2>/dev/null || true)"
else
    LLVM_PROFDATA="$(command -v llvm-profdata || true)"
    LLVM_COV="$(command -v llvm-cov || true)"
fi

if [ -z "$LLVM_PROFDATA" ] || [ -z "$LLVM_COV" ]; then
    echo "llvm-profdata/llvm-cov not found -- they carry the C and C++ half." >&2
    echo "  macOS:  xcode-select --install" >&2
    echo "  Linux:  install the llvm package for your clang" >&2
    missing=1
fi

# shellcheck source=./rust-env.sh
. "$ROOT/scripts/rust-env.sh"

if ! cargo llvm-cov --version >/dev/null 2>&1; then
    echo "cargo-llvm-cov not found -- it carries the Rust half." >&2
    echo "  cargo install cargo-llvm-cov" >&2
    echo "  rustup component add llvm-tools-preview" >&2
    echo "" >&2
    echo "It is used rather than llvm-profdata directly because rustc carries" >&2
    echo "its own LLVM: a .profraw it writes is refused by Xcode's reader with" >&2
    echo "an error that names neither toolchain." >&2
    missing=1
fi

[ "$missing" -eq 0 ] || exit 1

FLOOR="$(node -e 'process.stdout.write(String(JSON.parse(require("fs").readFileSync("tests/coverage.floors.json","utf8")).floor))')"

rm -rf "$OUT" "$PROF"
mkdir -p "$OUT" "$PROF"

# ------------------------------------------------------- the C and the C++

echo "==> building instrumented (build-coverage)"
cmake -S "$ROOT" -B "$BUILD" -DVST_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >/dev/null

echo "==> running the suite"
# %p keeps one file per process and %m one per binary image, so the test
# binaries cannot overwrite each other's profile.
#
# NOT `set -e`-fatal: a failing test still produced coverage, and a run that
# reports nothing because one assertion went red is a run nobody will repeat.
# The suite's own exit code is reported at the end instead.
#
# -E coverage_floor: that test reads the report THIS run is about to produce,
# so including it here would check the previous run's file, or fail on a
# missing one. It is registered with ctest so `ctest -R coverage_floor` works
# once a report exists, and it is run below, after there is one.
suite_status=0
LLVM_PROFILE_FILE="$PROF/%p-%m.profraw" \
    ctest --test-dir "$BUILD" -E coverage_floor --output-on-failure \
        > "$OUT/ctest.log" 2>&1 || suite_status=$?
tail -3 "$OUT/ctest.log" | sed 's/^/    /'

echo "==> reducing the native profile"
shopt -s nullglob
profraw=( "$PROF"/*.profraw )
if [ ${#profraw[@]} -eq 0 ]; then
    echo "no .profraw was written -- nothing native was instrumented." >&2
    echo "  Check that cmake/Coverage.cmake ran: it is included from the root" >&2
    echo "  CMakeLists.txt BEFORE any target, because it works by adding" >&2
    echo "  compile options and those reach only what follows." >&2
    exit 1
fi

"$LLVM_PROFDATA" merge -sparse "${profraw[@]}" -o "$PROF/merged.profdata"

# One -object per instrumented binary. The test executables are what ran, so
# they are what carries the mapping for the code under test.
objects=()
for f in "$BUILD"/tests/* "$BUILD"/tests/cpp/*; do
    [ -f "$f" ] && [ -x "$f" ] && objects+=( -object "$f" )
done

"$LLVM_COV" export "${objects[@]}" \
    -instr-profile="$PROF/merged.profdata" \
    -format=lcov > "$OUT/native.info" 2>/dev/null

# ------------------------------------------------------------------- Rust

echo "==> rust"
cargo llvm-cov --workspace --lcov --output-path "$OUT/rust.info" \
    >/dev/null 2>&1 || {
        echo "cargo llvm-cov failed -- rerunning verbosely so the error is visible" >&2
        cargo llvm-cov --workspace --lcov --output-path "$OUT/rust.info"
    }

# --------------------------------------------------------------------- JS

echo "==> the editors and the kit"
# Node's own runner and its own coverage: no c8, no nyc, nothing added to a
# dependency tree that the licence audit is the reason for keeping small.
js_status=0
node --test --experimental-test-coverage \
     --test-reporter=lcov --test-reporter-destination="$OUT/js.info" \
     --test-reporter=spec --test-reporter-destination=/dev/null \
     ui-kit/test/*.test.mjs plugins/*/ui/test/*.test.mjs >/dev/null 2>&1 || js_status=$?

# ------------------------------------------------------------------ report

echo "==> report"
cat "$OUT/native.info" "$OUT/rust.info" "$OUT/js.info" > "$OUT/lcov.info" 2>/dev/null || true

COVERAGE_ROOT="$ROOT" COVERAGE_OUT="$OUT" COVERAGE_FLOOR="$FLOOR" \
    node "$ROOT/scripts/coverage-report.mjs" \
        "$OUT/native.info" "$OUT/rust.info" "$OUT/js.info"

# ------------------------------------------------------------------- floor

COVERAGE_JSON="$OUT/coverage.json" node --test "$ROOT/tests/coverage_floor.test.mjs"
floor_status=$?

if [ "$suite_status" -ne 0 ]; then
    echo "  NOTE: the suite itself failed (ctest exit $suite_status); see build/coverage/ctest.log" >&2
fi
if [ "$js_status" -ne 0 ]; then
    echo "  NOTE: a UI test failed (node exit $js_status)" >&2
fi

exit $(( floor_status != 0 ? floor_status : (suite_status != 0 ? suite_status : js_status) ))
