#!/usr/bin/env bash
#
# The one entry point to the tests: the quick tier while you work, the full
# tier before you call it done. docs/tech/testing.md has the whole picture.
# Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
#
#   scripts/test.sh quick
#       Builds the test programs (cmake --target ni_tests: no plugin bundle)
#       and runs `ctest -L quick` -- every Rust crate's unit tests, the C/C++
#       wire, state and parameter tests, the oracles, all of the kit's and the
#       editors' JavaScript, and the lint-like checks. Under a minute warm.
#
#   scripts/test.sh full [--bundles <dir>]
#       Everything: the full build and the documentation site, `ctest -L full`
#       (quick, plus the render goldens, the AU renders and state stress, the bus across
#       processes and across architectures, the bundles' notices, the site's
#       links and the Playwright e2e suite), then scripts/coverage.sh with the
#       floor enforced, then auval, pluginval and clap-validator over the
#       bundles in <dir> (default build/out).
#
# WHAT IT READS OUTSIDE THE CHECKOUT, and why. Nothing under ~/Library, with
# one exception in the validator stage: auval and pluginval's AU pass find their
# component through the system's registry, which lists INSTALLED components,
# so they validate the installed AU, not <dir>'s. The ctest bundle tests load
# build/out's bundles by path and never look at what is installed: the AU tests
# (tg_au, sc_au, au_stress_*, au_ground_*) and editor_host_*'s AU runs register
# theirs in their own process only (tests/au_bundle.h), and editor_host_*'s
# VST3 and CLAP runs dlopen theirs. The VST3 and CLAP bundles are validated
# from <dir>.
# Nothing is ever written there: a build directory this script configures has
# -DIPLUG_DEPLOY_PLUGINS=OFF. The validators themselves are downloaded, pinned
# and checksummed, into build/validators (scripts/validate-plugins.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
cd "$ROOT"

usage() {
    sed -n '/^#   scripts\/test.sh quick/,/^#       bundles in/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

tier="${1:-}"
[ "$tier" = quick ] || [ "$tier" = full ] || usage
shift
bundles="$BUILD/out"
while [ $# -gt 0 ]; do
    case "$1" in
        --bundles) [ $# -ge 2 ] || usage; bundles="$(cd "$2" && pwd)"; shift 2 ;;
        *) usage ;;
    esac
done

# shellcheck source=./rust-env.sh
. "$ROOT/scripts/rust-env.sh"

jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
t0=$SECONDS
stage() { printf '==> %s\n' "$*"; }
took() { printf '    %ss\n' "$(( SECONDS - $1 ))"; }

# THE BUILD DIRECTORY IS CONFIGURED ONCE, WITH DEPLOYMENT OFF. One that exists
# is used as it is -- CI configures its own with deployment on, because its
# runner is where auval's components have to be installed -- but it is
# named if it would deploy, so nobody is surprised by a plugin in ~/Library.
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
    stage "configuring build (deployment off)"
    cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DIPLUG_DEPLOY_PLUGINS=OFF >/dev/null
elif grep -q '^IPLUG_DEPLOY_PLUGINS:BOOL=ON' "$BUILD/CMakeCache.txt"; then
    echo "note: $BUILD deploys plugins into ~/Library on build (IPLUG_DEPLOY_PLUGINS=ON)" >&2
fi

if [ "$tier" = quick ]; then
    s=$SECONDS; stage "building the test programs"
    cmake --build "$BUILD" --target ni_tests -j"$jobs" >/dev/null
    took $s
    s=$SECONDS; stage "ctest -L quick"
    ctest --test-dir "$BUILD" -L quick -j"$jobs" --output-on-failure
    took $s
    printf '\nquick tier green in %ss\n' "$(( SECONDS - t0 ))"
    exit 0
fi

# ---------------------------------------------------------------- full

s=$SECONDS; stage "building everything"
cmake --build "$BUILD" -j"$jobs" >/dev/null
took $s

# The site's link check reads site/dist; built here so it runs rather than
# skipping, as it does in a checkout that never built the site.
s=$SECONDS; stage "building the documentation site"
npm run build --workspace site >/dev/null
took $s

# -j4, not -j<ncpu>: the e2e test takes four processors of its own.
s=$SECONDS; stage "ctest -L full"
ctest --test-dir "$BUILD" -L full -j4 --output-on-failure
took $s

s=$SECONDS; stage "coverage, with the floor enforced"
"$ROOT/scripts/coverage.sh"
took $s

s=$SECONDS; stage "validators over $bundles"
tools="$BUILD/validators"
if [ ! -x "$tools/binaries/clap-validator" ] || [ ! -d "$tools/pluginval.app" ]; then
    "$ROOT/scripts/validate-plugins.sh" fetch "$tools" >/dev/null
fi
"$ROOT/scripts/validate-plugins.sh" run "$tools" "$bundles"
took $s

printf '\nfull tier green in %ss\n' "$(( SECONDS - t0 ))"
