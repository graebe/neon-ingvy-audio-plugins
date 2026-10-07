#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Run the hosts' own validators over the built plugins.
#
#   scripts/validate-plugins.sh fetch <tools-dir>
#       Download pluginval and clap-validator at pinned releases, checked
#       against pinned SHA-256 digests.
#
#   scripts/validate-plugins.sh run <tools-dir> [bundle-dir]
#       Validate every plugin: auval on each INSTALLED Audio Unit, pluginval
#       (strictness 10) on each VST3 and AU -- and on each bundle on the JUCE
#       shell, editor tests included -- clap-validator on each CLAP --
#       the last held to tests/validators.known.json by
#       scripts/validator-verdict.mjs. bundle-dir defaults to build/out. Exits
#       non-zero if any validator fails, after running all of them.
#
# WHAT EACH ONE IS FOR. The suite checks what these plugins DO -- the render
# A/B, the oracles. It does not check that they follow each format's rules:
# that the host can instantiate them, save and restore their state, drive
# every parameter from any thread, change the block size. Those are the
# validators' job, and they are what a host vendor runs before blaming the
# plugin.
#
# THE STEINBERG VST3 VALIDATOR IS NOT RUN SEPARATELY. iPlug2's
# download-vst3-sdk.sh can build it, but only through an Xcode-generator build
# of the SDK's hosting samples that pins a deployment target current Xcode
# refuses (10.13); making it work would mean patching the submodule's script.
# pluginval's VST3 checks cover the same ground from a host's side.
#
# The AUs are identified by the (type, subtype, manufacturer) triple in each
# plugin's own AU Info.plist, so a new plugin needs no edit here.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

PLUGINVAL_VERSION=v1.0.4
PLUGINVAL_SHA256=3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f
CLAP_VALIDATOR_VERSION=0.4.1
CLAP_VALIDATOR_ASSET=clap-validator-0.4.1-127-g152b982-macos-universal.zip
CLAP_VALIDATOR_SHA256=bbec8cd7d18274e549d5d8c12ece3cec54be966129388dd2e742b9957f2ba9f1

fetch() {
    local dir=$1
    mkdir -p "$dir"
    cd "$dir"
    curl -sSfL -o pluginval.zip \
        "https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/pluginval_macOS.zip"
    echo "${PLUGINVAL_SHA256}  pluginval.zip" | shasum -a 256 -c -
    unzip -q -o pluginval.zip

    curl -sSfL -o clap-validator.zip \
        "https://github.com/free-audio/clap-validator/releases/download/${CLAP_VALIDATOR_VERSION}/${CLAP_VALIDATOR_ASSET}"
    echo "${CLAP_VALIDATOR_SHA256}  clap-validator.zip" | shasum -a 256 -c -
    # The release zip holds a tarball, which holds binaries/clap-validator.
    unzip -q -o clap-validator.zip
    tar -xzf clap-validator-*.tar.gz
    ./pluginval.app/Contents/MacOS/pluginval --version
    ./binaries/clap-validator --version
}

run() {
    local tools=$1 out=${2:-$ROOT/build/out}
    local pluginval="$tools/pluginval.app/Contents/MacOS/pluginval"
    local clapval="$tools/binaries/clap-validator"
    local failed=()

    for plist in "$ROOT"/plugins/*/resources/*-AU-Info.plist; do
        local bundle type sub mfr
        bundle=$(basename "$plist" -AU-Info.plist)
        type=$(plutil -extract AudioComponents.0.type raw "$plist")
        sub=$(plutil -extract AudioComponents.0.subtype raw "$plist")
        mfr=$(plutil -extract AudioComponents.0.manufacturer raw "$plist")

        echo "::group::auval -v $type $sub $mfr ($bundle)"
        auval -v "$type" "$sub" "$mfr" || failed+=("auval $bundle")
        echo "::endgroup::"

        echo "::group::pluginval $bundle.vst3"
        "$pluginval" --strictness-level 10 --skip-gui-tests --timeout-ms 300000 \
            --validate "$out/$bundle.vst3" || failed+=("pluginval $bundle.vst3")
        echo "::endgroup::"

        echo "::group::pluginval $bundle.component"
        "$pluginval" --strictness-level 10 --skip-gui-tests --timeout-ms 300000 \
            --validate "$HOME/Library/Audio/Plug-Ins/Components/$bundle.component" \
            || failed+=("pluginval $bundle.component")
        echo "::endgroup::"

        # HELD TO A MANIFEST, NOT TO ZERO. Some failures are iPlug2's, fixed by
        # patches not yet applied (docs/iplug2-patches), and one is the
        # validator's own; tests/validators.known.json lists each. Any failure
        # it does not list fails, and so does any listed one that now passes.
        # clap-validator's own exit code is therefore not the verdict: its
        # JSON is, and an empty or broken report fails the verdict too.
        echo "::group::clap-validator $bundle.clap"
        "$clapval" validate --json --hide-output "$out/$bundle.clap" > "$tools/$bundle.clap.json" || true
        node "$ROOT/scripts/validator-verdict.mjs" "$bundle" < "$tools/$bundle.clap.json" \
            || failed+=("clap-validator $bundle.clap")
        echo "::endgroup::"
    done

    # THE BUNDLES ON THE JUCE SHELL (cmake/NiJucePlugin.cmake): VST3 only, and
    # known by the moduleinfo.json JUCE writes into each. pluginval runs WITH
    # its editor tests -- native editors have no WebView to keep them from it.
    for bundle in "$out"/*.vst3; do
        [ -f "$bundle/Contents/Resources/moduleinfo.json" ] || continue
        local name
        name=$(basename "$bundle")
        echo "::group::pluginval $name"
        "$pluginval" --strictness-level 10 --timeout-ms 300000 \
            --validate "$bundle" || failed+=("pluginval $name")
        echo "::endgroup::"
    done

    if [ ${#failed[@]} -gt 0 ]; then
        printf '::error::validation failed: %s\n' "${failed[@]}"
        exit 1
    fi
    echo "every validator passed on every plugin"
}

case "${1:-}" in
    fetch) [ $# -eq 2 ] || { echo "usage: $0 fetch <tools-dir>" >&2; exit 2; }; fetch "$2" ;;
    run)   [ $# -ge 2 ] || { echo "usage: $0 run <tools-dir> [bundle-dir]" >&2; exit 2; }; run "$2" "${3:-}" ;;
    *)     echo "usage: $0 fetch <tools-dir> | run <tools-dir> [bundle-dir]" >&2; exit 2 ;;
esac
