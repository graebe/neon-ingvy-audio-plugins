#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Build a CMake plugin project for Linux in tools/docker/linux, run its ctest,
# and validate every VST3 it built with pluginval at strictness 10 under Xvfb,
# editor tests included.
#
#   scripts/build-linux.sh [--arch amd64|arm64] [--juce <dir>] <cmake-project-dir> [-- <ctest args>]
#
# amd64, the default, is the platform that is built and validated. arm64
# builds and runs ctest from the same Dockerfile, but pluginval publishes no
# arm64 Linux binary, so its bundles are reported as NOT VALIDATED -- never as
# passed.
#
# Build directory: <project>/build-linux-<arch>. The image is built on first
# use and again whenever tools/docker/linux/Dockerfile changes; the build runs
# as the invoking user, so nothing in the checkout ends up owned by root.
# scripts/cross-common.sh has the contract every build-<platform>.sh keeps;
# docs/tech/cross-build.md the walk-through.
set -euo pipefail

# shellcheck source=scripts/cross-common.sh
. "$(dirname "$0")/cross-common.sh"
NI_ARCHES="amd64 arm64"
NI_ARCH=amd64
ni_parse_args "$@"
PLATFORM="linux/$NI_ARCH"
BUILD="$NI_PROJECT/build-linux-$NI_ARCH"

# ---------------------------------------------------------------- on the host
if [ "${NI_INSIDE:-}" != 1 ]; then
    start=$SECONDS
    ni_result_begin "$BUILD"
    ni_stage image ni_image ni-cross-linux "$PLATFORM" "$NI_ROOT/tools/docker/linux" || exit 1
    ni_claim_build_dir "$BUILD"
    ni_ctest_forward
    status=0
    ni_docker_run "$NI_IMAGE" "$PLATFORM" "$BUILD" \
        scripts/build-linux.sh --arch "$NI_ARCH" --juce /juce "$NI_MOUNT/$NI_PROJECT_REL" \
        ${NI_CTEST_FORWARD[@]+"${NI_CTEST_FORWARD[@]}"} || status=$?
    ni_record time total "$((SECONDS - start))"
    exit "$status"
fi

# ----------------------------------------------------- inside the container
NI_RESULT="$BUILD/cross-result.tsv"
# One X server for the whole ctest run: a test that opens an editor (a hosted
# bundle's, a kit page's) needs a display, as pluginval's editor tests do.
NI_CTEST_RUNNER=(xvfb-run -a -s "-screen 0 1920x1080x24")
if ni_build_and_test "$BUILD"; then
    if [ -x /opt/pluginval/pluginval ]; then
        ni_validate "$BUILD" ", editor tests under Xvfb" \
            xvfb-run -a -s "-screen 0 1920x1080x24" /opt/pluginval/pluginval || true
    else
        ni_unvalidated "$BUILD" "not validated: pluginval has no $PLATFORM build"
    fi
fi
ni_finish
