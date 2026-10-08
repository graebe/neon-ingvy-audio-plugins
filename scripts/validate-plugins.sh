#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Run the hosts' own validators over the built VST3 bundles.
#
#   scripts/validate-plugins.sh fetch <tools-dir>
#       Download pluginval at its pinned release for this platform, checked
#       against the pinned SHA-256, and build Steinberg's VST3 validator from
#       the VST3 SDK at a pinned tag and commit.
#
#   scripts/validate-plugins.sh run <tools-dir> [bundle-dir]
#       Validate every .vst3 in bundle-dir (default build/out): pluginval at
#       strictness 10 with its editor tests, Steinberg's validator, and -- on
#       macOS -- `codesign --verify --deep --strict`, the check Live's scanner
#       makes. Exits non-zero if any of them failed, after running all of them.
#
# macOS, Linux and Windows (Git Bash, as GitHub's runners have it). On Linux
# without a display, pluginval's editor tests run under xvfb-run, which has to
# be installed (the xvfb package).
#
# WHAT EACH ONE IS FOR. The suite checks what these plugins DO -- the render
# A/B, the oracles, the hosted bundles. It does not check that they follow the
# format's rules: that a host can instantiate them, save and restore their
# state, drive every parameter from any thread, change the block size. Those
# are the validators' job, and they are what a host vendor runs before blaming
# the plugin. The signature is the one check neither makes: a bundle written
# into after it was signed loads in both and is refused by Live ("a sealed
# resource is missing or invalid").
#
# THE STEINBERG VST3 VALIDATOR is the SDK's own sample host
# (public.sdk/samples/vst-hosting/validator), built here from the SDK at the
# tag JUCE 9.0.3 vendors (3.8.0) with CMake's default generator. The clone is
# checked against the tag's commit; the SDK's submodules are the ones that
# commit pins. MIT, run and not shipped (THIRD_PARTY_LICENSES.md, build tools).
#
# Every validator run is one stage of the timing log (scripts/timing.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/timing.sh
. "$ROOT/scripts/timing.sh"

# One pluginval release for every platform; the cross-build images pin the
# same release by the same digests (tools/docker/*/Dockerfile).
PLUGINVAL_VERSION=v1.0.4
PLUGINVAL_SHA256_MACOS=3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f
PLUGINVAL_SHA256_LINUX=c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352
PLUGINVAL_SHA256_WINDOWS=c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab
VST3_SDK_TAG=v3.8.0_build_66
VST3_SDK_COMMIT=9fad9770f2ae8542ab1a548a68c1ad1ac690abe0

case "$(uname -s)" in
    Darwin) OS=macos ;;
    Linux) OS=linux ;;
    MINGW*|MSYS*|CYGWIN*) OS=windows ;;
    *) echo "validate-plugins.sh: no validators for $(uname -s)" >&2; exit 2 ;;
esac

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

pluginval_of() {
    case "$OS" in
        macos) echo "$1/pluginval.app/Contents/MacOS/pluginval" ;;
        linux) echo "$1/pluginval" ;;
        windows) echo "$1/pluginval.exe" ;;
    esac
}

vst3val_of() {
    case "$OS" in
        windows) echo "$1/binaries/vst3-validator.exe" ;;
        *) echo "$1/binaries/vst3-validator" ;;
    esac
}

fetch() {
    local dir=$1 asset want
    mkdir -p "$dir"
    dir="$(cd "$dir" && pwd)"
    case "$OS" in
        macos) asset=pluginval_macOS.zip; want=$PLUGINVAL_SHA256_MACOS ;;
        linux) asset=pluginval_Linux.zip; want=$PLUGINVAL_SHA256_LINUX ;;
        windows) asset=pluginval_Windows.zip; want=$PLUGINVAL_SHA256_WINDOWS ;;
    esac
    curl --proto '=https' --tlsv1.2 -sSfL -o "$dir/pluginval.zip" \
        "https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/${asset}"
    local got
    got=$(sha256 "$dir/pluginval.zip")
    [ "$got" = "$want" ] || { echo "pluginval.zip is $got, pinned $want" >&2; exit 1; }
    unzip -q -o "$dir/pluginval.zip" -d "$dir"
    rm "$dir/pluginval.zip"
    chmod +x "$(pluginval_of "$dir")" 2>/dev/null || true
    "$(pluginval_of "$dir")" --version

    rm -rf "$dir/vst3sdk" "$dir/vst3sdk-build"
    git clone -q --depth 1 --branch "$VST3_SDK_TAG" https://github.com/steinbergmedia/vst3sdk "$dir/vst3sdk"
    [ "$(git -C "$dir/vst3sdk" rev-parse HEAD)" = "$VST3_SDK_COMMIT" ] \
        || { echo "vst3sdk $VST3_SDK_TAG is not $VST3_SDK_COMMIT" >&2; exit 1; }
    git -C "$dir/vst3sdk" submodule update -q --init --depth 1 base cmake pluginterfaces public.sdk
    # The SDK's build talks a lot; its log is kept, and shown when it fails.
    { cmake -S "$dir/vst3sdk" -B "$dir/vst3sdk-build" -DCMAKE_BUILD_TYPE=Release \
          -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
          -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON -DSMTG_RUN_VST_VALIDATOR=OFF \
          -DSMTG_CREATE_PLUGIN_LINK=OFF \
      && cmake --build "$dir/vst3sdk-build" --config Release --target validator -j; } \
        > "$dir/vst3sdk-build.log" 2>&1 \
        || { tail -30 "$dir/vst3sdk-build.log" >&2; exit 1; }
    mkdir -p "$dir/binaries"
    local built
    built=$(find "$dir/vst3sdk-build/bin" -type f \( -name validator -o -name validator.exe \) | head -n 1)
    [ -n "$built" ] || { echo "the VST3 SDK built no validator" >&2; exit 1; }
    cp "$built" "$(vst3val_of "$dir")"
}

# The tools this checkout already fetched, or nothing to do.
fetched() {
    [ -x "$(pluginval_of "$1")" ] && [ -x "$(vst3val_of "$1")" ]
}

run() {
    local tools=$1 out=${2:-$ROOT/build/out}
    local pluginval vst3val failed=()
    pluginval=$(pluginval_of "$tools")
    vst3val=$(vst3val_of "$tools")
    fetched "$tools" || { echo "no validators in $tools: run '$0 fetch $tools' first" >&2; exit 2; }

    # pluginval's editor tests open a window: on a Linux machine without a
    # display, a virtual one.
    local gui=()
    if [ "$OS" = linux ] && [ -z "${DISPLAY:-}" ]; then
        command -v xvfb-run >/dev/null 2>&1 \
            || { echo "no display and no xvfb-run: install xvfb for pluginval's editor tests" >&2; exit 2; }
        gui=(xvfb-run -a -s "-screen 0 1920x1080x24")
    fi

    local bundles=() bundle
    for bundle in "$out"/*.vst3; do
        [ -d "$bundle" ] && bundles+=("$bundle")
    done
    [ ${#bundles[@]} -gt 0 ] || { echo "no .vst3 bundle in $out" >&2; exit 1; }

    for bundle in "${bundles[@]}"; do
        local name
        name=$(basename "$bundle" .vst3)

        echo "::group::pluginval $name.vst3"
        ni_time_stage "pluginval $name" -- ${gui[@]+"${gui[@]}"} "$pluginval" \
            --strictness-level 10 --timeout-ms 300000 --validate "$bundle" \
            || failed+=("pluginval $name.vst3")
        echo "::endgroup::"

        echo "::group::vst3-validator $name.vst3"
        ni_time_stage "vst3-validator $name" -- "$vst3val" "$bundle" \
            || failed+=("vst3-validator $name.vst3")
        echo "::endgroup::"

        if [ "$OS" = macos ]; then
            echo "::group::codesign --verify --deep --strict $name.vst3"
            ni_time_stage "codesign $name" -- codesign --verify --deep --strict --verbose=2 "$bundle" \
                || failed+=("codesign $name.vst3")
            echo "::endgroup::"
        fi
    done

    if [ ${#failed[@]} -gt 0 ]; then
        printf '::error::validation failed: %s\n' "${failed[@]}"
        exit 1
    fi
    echo "every validator passed on every bundle in $out"
}

case "${1:-}" in
    fetch) [ $# -eq 2 ] || { echo "usage: $0 fetch <tools-dir>" >&2; exit 2; }
           ni_time_stage fetch -- fetch "$2" ;;
    fetched) [ $# -eq 2 ] || { echo "usage: $0 fetched <tools-dir>" >&2; exit 2; }; fetched "$2" ;;
    run)   [ $# -ge 2 ] || { echo "usage: $0 run <tools-dir> [bundle-dir]" >&2; exit 2; }; run "$2" "${3:-}" ;;
    *)     echo "usage: $0 fetch <tools-dir> | fetched <tools-dir> | run <tools-dir> [bundle-dir]" >&2; exit 2 ;;
esac
