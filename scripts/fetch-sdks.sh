#!/usr/bin/env bash
# Fetch the plugin SDKs iPlug2 builds against, at PINNED versions.
#
#   scripts/fetch-sdks.sh              download all three into the iPlug2 submodule
#   scripts/fetch-sdks.sh --verify     check that what is there is what is pinned
#   scripts/fetch-sdks.sh --cache-key  print a CI cache key for exactly these pins
#
# WHY THIS EXISTS. The SDKs are not in the iPlug2 submodule; its
# Dependencies/IPlug/download-*.sh scripts clone them, and with no argument
# they clone `master`/`main` -- so two builds of the same commit could compile
# against two different VST3 SDKs, and CI's cache (keyed on .gitmodules) would
# keep serving whichever one it saw first. The pins live here, on our side of
# the submodule boundary, rather than in an edited copy of iPlug2's scripts.
#
# To move a pin: change it below, run this, build, and run the suite. The
# licences of what is fetched are recorded in THIRD_PARTY_LICENSES.md, and
# scripts/check-licenses.mjs reads the pins from this file.
set -euo pipefail

# The VST3 SDK: MIT since 3.8.0. Tags are v<version>_build_<n>.
VST3_SDK_TAG=v3.8.1_build_84
VST3_SDK_VERSION=3.8.1
# The CLAP SDK: MIT.
CLAP_SDK_TAG=1.2.10
# clap-helpers: MIT. It publishes no tags at all, so it is pinned by commit.
CLAP_HELPERS_COMMIT=55a5dd5d1db9c87b32f407e387f64676d27e10b1

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/external/iPlug2/Dependencies/IPlug"

verify() {
    local ok=1
    local vst3
    vst3=$(sed -n 's/^#define kVstVersionString[[:space:]]*"VST \([0-9.]*\)".*/\1/p' \
             "$DEPS/VST3_SDK/pluginterfaces/vst/vsttypes.h" 2>/dev/null || true)
    [ "$vst3" = "$VST3_SDK_VERSION" ] || { echo "VST3_SDK is '${vst3:-missing}', pinned $VST3_SDK_VERSION" >&2; ok=0; }
    local clap
    clap=$(awk '/#define CLAP_VERSION_(MAJOR|MINOR|REVISION) /{printf "%s%s", sep, $3; sep="."}' \
             "$DEPS/CLAP_SDK/include/clap/version.h" 2>/dev/null || true)
    [ "$clap" = "$CLAP_SDK_TAG" ] || { echo "CLAP_SDK is '${clap:-missing}', pinned $CLAP_SDK_TAG" >&2; ok=0; }
    local helpers
    helpers=$(cat "$DEPS/CLAP_HELPERS/.ni-pinned-commit" 2>/dev/null || true)
    [ "$helpers" = "$CLAP_HELPERS_COMMIT" ] || { echo "CLAP_HELPERS is '${helpers:-unpinned}', pinned $CLAP_HELPERS_COMMIT" >&2; ok=0; }
    [ "$ok" = 1 ] && echo "SDKs match their pins: VST3 $VST3_SDK_VERSION, CLAP $CLAP_SDK_TAG, clap-helpers ${CLAP_HELPERS_COMMIT:0:12}"
    [ "$ok" = 1 ]
}

# A clone of one commit, however the upstream names it, laid out the way
# iPlug2's download-clap-sdks.sh leaves it: no .git, and the readme.txt that
# iPlug2 itself tracks in that directory restored.
fetch_at() {
    local url=$1 ref=$2 dir=$3
    rm -rf "$dir"
    git init -q "$dir"
    git -C "$dir" fetch -q --depth 1 "$url" "$ref"
    git -C "$dir" checkout -q FETCH_HEAD
    rm -rf "$dir/.git"
    git -C "$DEPS" checkout -- "$dir/readme.txt"
}

case "${1:-}" in
    --verify)
        verify
        ;;
    --cache-key)
        # The submodule's own commit, not .gitmodules: that file names the
        # submodule's URL and never changes when the pin moves.
        iplug=$(git -C "$ROOT" rev-parse HEAD:external/iPlug2)
        echo "iplug2-${iplug}-vst3-${VST3_SDK_TAG}-clap-${CLAP_SDK_TAG}-helpers-${CLAP_HELPERS_COMMIT}"
        ;;
    "")
        cd "$DEPS"
        # iPlug2's own script for the VST3 SDK: it takes a tag, and it knows
        # which of the SDK's submodules iPlug2 needs and what to strip.
        ./download-vst3-sdk.sh "$VST3_SDK_TAG"
        fetch_at https://github.com/free-audio/clap.git "refs/tags/$CLAP_SDK_TAG" CLAP_SDK
        fetch_at https://github.com/free-audio/clap-helpers.git "$CLAP_HELPERS_COMMIT" CLAP_HELPERS
        echo "$CLAP_HELPERS_COMMIT" > CLAP_HELPERS/.ni-pinned-commit
        verify
        ;;
    *)
        echo "usage: $0 [--verify | --cache-key]" >&2
        exit 2
        ;;
esac
