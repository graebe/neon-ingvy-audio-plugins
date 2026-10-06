#!/usr/bin/env bash
# Build a CMake plugin project natively on macOS as a universal binary (arm64
# and x86_64), run its ctest, and validate every VST3 it built with pluginval
# at strictness 10, editor tests included.
#
#   scripts/build-macos.sh [--juce <dir>] <cmake-project-dir>
#
# Build directory: <project>/build-macos-universal. pluginval is the release
# scripts/validate-plugins.sh pins for CI, read from there, downloaded into the
# build directory and checked against the same SHA-256. Nothing is installed:
# pluginval loads each bundle where the build left it, and nothing is copied
# into ~/Library. scripts/cross-common.sh has the contract every
# build-<platform>.sh keeps; docs/tech/cross-build.md the walk-through.
set -euo pipefail

# shellcheck source=scripts/cross-common.sh
. "$(dirname "$0")/cross-common.sh"
ni_parse_args "$@"
[ "$(uname -s)" = Darwin ] || ni_die "build-macos.sh builds on macOS"
BUILD="$NI_PROJECT/build-macos-universal"

pin() { sed -n "s/^$1=//p" "$NI_ROOT/scripts/validate-plugins.sh"; }
PLUGINVAL_VERSION=$(pin PLUGINVAL_VERSION)
PLUGINVAL_SHA256=$(pin PLUGINVAL_SHA256)
TOOLS="$BUILD/tools"
PLUGINVAL="$TOOLS/pluginval.app/Contents/MacOS/pluginval"

fetch_pluginval() {
    [ -x "$PLUGINVAL" ] && return 0
    mkdir -p "$TOOLS"
    curl -sSfL -o "$TOOLS/pluginval.zip" \
        "https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/pluginval_macOS.zip"
    echo "${PLUGINVAL_SHA256}  $TOOLS/pluginval.zip" | shasum -a 256 -c -
    unzip -q -o "$TOOLS/pluginval.zip" -d "$TOOLS"
    rm "$TOOLS/pluginval.zip"
}

# Universal means both slices in every bundle's binary, checked rather than
# assumed from what CMake was asked for.
check_universal() {
    local bundle binary archs
    ni_bundles "$BUILD" || return 1
    for bundle in "${NI_BUNDLES[@]}"; do
        binary="$bundle/Contents/MacOS/$(plutil -extract CFBundleExecutable raw "$bundle/Contents/Info.plist")"
        archs=$(lipo -archs "$binary")
        echo "$(basename "$bundle"): $archs"
        case " $archs " in *" arm64 "*) ;; *) return 1 ;; esac
        case " $archs " in *" x86_64 "*) ;; *) return 1 ;; esac
    done
}

start=$SECONDS
ni_result_begin "$BUILD"
if ni_build_and_test "$BUILD" -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"; then
    ni_stage universal check_universal || true
    if ni_stage pluginval-fetch fetch_pluginval; then
        ni_validate "$BUILD" ", editor tests included" "$PLUGINVAL" || true
    fi
fi
ni_record time total "$((SECONDS - start))"
ni_finish
