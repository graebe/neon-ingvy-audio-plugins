#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The one entry point to the tests: the quick tier while you work, the full
# tier before you call it done, and one product's run for its release.
# docs/tech/testing.md has the whole picture.
#
#   scripts/test.sh quick [--build <dir>]
#       Builds the test programs (cmake --target ni_tests: no plugin bundle)
#       and runs `ctest -L quick` -- every Rust crate's tests, the C tests
#       against the engines' ABIs and their oracles, the C++ unit tests of the
#       processors, the native kit and the editors, and the lint-like checks
#       (SPDX, cargo-deny, the licences, the versions, the release plumbing,
#       the design paths, the token guards, the hint lengths, the timing log).
#       A new <dir> is configured with the dev preset: this machine's
#       architecture, RelWithDebInfo, no link-time optimisation. build-dev/ by
#       default.
#
#   scripts/test.sh full [--build <dir>] [--bundles <dir>] [--no-coverage]
#       Everything: the full build and the documentation site, `ctest -L
#       full` (quick, plus the render goldens, the hosted bundles against the
#       iPlug2 fixtures, the snapshot goldens, the bus across processes and
#       architectures, every bundle's notices and signature, the site's links,
#       and the Move modules built and tested on aarch64 in Docker), then scripts/coverage.sh with the 80% floor enforced, then the
#       validators over the bundles in <dir> (default <build>/out): pluginval
#       at strictness 10 with its editor tests, Steinberg's validator and
#       `codesign --verify --deep --strict`. A new <dir> is configured with the
#       release preset: universal, Release, link-time optimisation -- what
#       ships. build/ by default. --no-coverage skips the coverage stage, for
#       the gates between steps; the final gate runs it.
#
#   scripts/test.sh product <product> [--build <dir>]
#       One product, as its release builds and tests it: its bundle
#       (<Bundle>_VST3) and the test programs of that product and the shared
#       ones (ni_tests_<product>, ni_tests_shared: cmake/NiTest.cmake), then
#       `ctest -L '^product:(<product>|shared)$'` over the quick and full
#       tiers -- no other product's tests, no coverage -- then the validators
#       over that one bundle. <product> is the versions.json key (trance-gate,
#       side-chain ...). Configured with the release preset, as the full tier
#       is; build/ by default. A failing test of another product never stops
#       this one's release.
#
#   --build <dir> is the build directory: one per person or agent building at
#   the same time, so no two share a tree. One that exists is used as it was
#   configured.
#
# NOTHING IS READ FROM OR WRITTEN TO WHERE PLUGINS ARE INSTALLED. Every test
# that hosts a bundle loads it from <build>/out by path, and no preset copies a
# bundle anywhere else (NI_DEPLOY_PLUGINS is off). The validators are
# downloaded, pinned and checksummed, into <build>/validators
# (scripts/validate-plugins.sh).
#
# EVERY STAGE IS TIMED into the timing log outside the checkout
# (scripts/timing.sh), the whole run as the stage "total";
# scripts/build-timings.py summarises it.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

usage() {
    sed -n '/^#   scripts\/test.sh quick/,/^#   configured\./p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

tier="${1:-}"
case "$tier" in quick|full|product) ;; *) usage ;; esac
shift
name=""
if [ "$tier" = product ]; then
    [ $# -ge 1 ] || usage
    name=$1
    shift
fi
# The preset a new build directory is configured with, and where it is.
case "$tier" in
    quick)        preset=dev;     BUILD="$ROOT/build-dev" ;;
    full|product) preset=release; BUILD="$ROOT/build" ;;
esac
bundles=""
coverage=1
while [ $# -gt 0 ]; do
    case "$1" in
        --build) [ $# -ge 2 ] || usage; mkdir -p "$2"; BUILD="$(cd "$2" && pwd)"; shift 2 ;;
        --bundles) [ $# -ge 2 ] && [ "$tier" != product ] || usage; bundles="$(cd "$2" && pwd)"; shift 2 ;;
        --no-coverage) [ "$tier" = full ] || usage; coverage=0; shift ;;
        *) usage ;;
    esac
done
bundles="${bundles:-$BUILD/out}"

# shellcheck source=./rust-env.sh
. "$ROOT/scripts/rust-env.sh"
# shellcheck source=./timing.sh
. "$ROOT/scripts/timing.sh"
export NI_TIMING_PRESET="$preset"

# The bundle a product ships, as its release names it (scripts/release.mjs):
# a product with no plugin, or no such product, stops here.
bundle=""
if [ "$tier" = product ]; then
    bundle="$(node "$ROOT/scripts/release.mjs" bundle "$name")"
fi

jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
stage() { printf '==> %s\n' "$*"; }
# A stage, timed into the log and on the terminal. A failure ends the run.
timed() {
    local name=$1 s=$SECONDS rc=0
    shift
    stage "$name"
    ni_time_stage "$name" "$@" || rc=$?
    printf '    %ss\n' "$(( SECONDS - s ))"
    [ "$rc" = 0 ] || { printf '\n%s failed (exit %s)\n' "$name" "$rc" >&2; exit "$rc"; }
}

# A command whose output goes to a log in the build directory, shown in part
# when it fails: a build's thousand lines are noise until one of them is not.
quietly() {
    local log="$BUILD/$1.log"
    shift
    "$@" > "$log" 2>&1 || { local rc=$?; tail -n 60 "$log" >&2; echo "(all of it: $log)" >&2; return "$rc"; }
}

quick() {
    timed "build test programs" --build "$BUILD" -- \
        quietly build-tests cmake --build "$BUILD" --target ni_tests -j"$jobs"
    timed "ctest quick" -- ctest --test-dir "$BUILD" -L quick -j"$jobs" --output-on-failure
}

full() {
    timed "build everything" --build "$BUILD" -- quietly build cmake --build "$BUILD" -j"$jobs"

    # The site's link check reads site/dist; built here so it runs rather than
    # skipping, as it does in a checkout that never built the site.
    timed "build the documentation site" -- quietly site npm run build --workspace site

    timed "ctest full" -- ctest --test-dir "$BUILD" -L full -j"$jobs" --output-on-failure

    if [ "$coverage" = 1 ]; then
        timed "coverage" -- "$ROOT/scripts/coverage.sh"
    else
        stage "coverage skipped (--no-coverage): the final gate runs it"
    fi

    validators "validators over $bundles" "$bundles"
}

# The product's bundle and its tests, and nothing of another product's: a
# build of only those targets, and a ctest of only those labels. Every test
# carries `full`, so the tier needs no -L of its own; coverage and e2e are the
# kinds a product's run leaves out.
product() {
    timed "build $bundle and the $name and shared tests" --build "$BUILD" -- \
        quietly "build-$name" cmake --build "$BUILD" \
            --target "${bundle}_VST3" "ni_tests_$name" ni_tests_shared -j"$jobs"
    timed "ctest $name and shared" -- ctest --test-dir "$BUILD" \
        -L "^product:($name|shared)\$" -LE '^(coverage|e2e)$' -j"$jobs" --output-on-failure
    validators "validators over $bundle" "$BUILD/out" "$bundle"
}

# The validators, fetched into the build directory once, over <dir> -- every
# bundle in it, or the ones named.
validators() {
    local stage_name=$1 tools="$BUILD/validators"
    shift
    if ! "$ROOT/scripts/validate-plugins.sh" fetched "$tools"; then
        timed "fetch the validators" -- "$ROOT/scripts/validate-plugins.sh" fetch "$tools"
    fi
    timed "$stage_name" -- "$ROOT/scripts/validate-plugins.sh" run "$tools" "$@"
}

main() {
    local t0=$SECONDS
    # THE BUILD DIRECTORY IS CONFIGURED ONCE, FROM ITS TIER'S PRESET. One that
    # exists is used as it is.
    if [ ! -f "$BUILD/CMakeCache.txt" ]; then
        mkdir -p "$BUILD"
        timed "configure (preset $preset)" --build "$BUILD" -- \
            quietly configure cmake --preset "$preset" -B "$BUILD"
    fi
    "$tier"
    if [ "$tier" = product ]; then
        printf '\n%s green in %ss: %s.vst3 and the %s and shared tests\n' \
            "$name" "$(( SECONDS - t0 ))" "$bundle" "$name"
    elif [ "$tier" = full ] && [ "$coverage" = 0 ]; then
        printf '\nfull tier green in %ss, without coverage\n' "$(( SECONDS - t0 ))"
    else
        printf '\n%s tier green in %ss\n' "$tier" "$(( SECONDS - t0 ))"
    fi
}

ni_time_stage total -- main
