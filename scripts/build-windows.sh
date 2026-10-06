#!/usr/bin/env bash
# Cross-compile a CMake plugin project for Windows x64 in tools/docker/windows,
# run its ctest under Wine, and smoke-test every VST3 it built with
# pluginval.exe under Wine.
#
#   XWIN_ACCEPT_LICENSE=yes scripts/build-windows.sh [--juce <dir>] <cmake-project-dir>
#
# THE LICENCE. The image carries Microsoft's C runtime and Windows SDK, which
# xwin downloads only once their licence is accepted -- by the owner, with
# XWIN_ACCEPT_LICENSE=yes. tools/docker/windows/README.md says what that means.
# It is needed only while the image is built (once per change to either
# Dockerfile); without it, and without a built image, this stops and says so.
#
# WINE IS A SMOKE TEST, NOT WINDOWS. pluginval.exe really loads the DLL and
# drives it -- instantiation, audio at every sample rate and block size,
# state, parameters, buses, the editor -- but through Wine's implementation of
# Windows, under x86_64 emulation on Apple silicon. A pass says the binary is
# sound enough to be worth the real run, which is the native one in GitHub
# Actions at publishing time. docs/tech/cross-build.md lists what the Wine run
# does and does not cover.
#
# Build directory: <project>/build-windows-x64, with a Wine prefix of its own
# in it. scripts/cross-common.sh has the contract every build-<platform>.sh
# keeps.
set -euo pipefail

# shellcheck source=scripts/cross-common.sh
. "$(dirname "$0")/cross-common.sh"
ni_parse_args "$@"
PLATFORM=linux/amd64
BUILD="$NI_PROJECT/build-windows-x64"

# ---------------------------------------------------------------- on the host
if [ "${NI_INSIDE:-}" != 1 ]; then
    start=$SECONDS
    ni_result_begin "$BUILD"
    ni_stage linux-image ni_image ni-cross-linux "$PLATFORM" "$NI_ROOT/tools/docker/linux" || exit 1
    base=$NI_IMAGE
    windows_dir="$NI_ROOT/tools/docker/windows"
    ni_image_tag ni-cross-windows "$PLATFORM" "$windows_dir" --build-arg "BASE_IMAGE=$base"
    if ! ni_image_exists "$NI_IMAGE"; then
        if [ "${XWIN_ACCEPT_LICENSE:-}" != yes ]; then
            ni_record stage licence FAILED 0
            ni_record note "Microsoft's CRT/SDK licence not accepted: XWIN_ACCEPT_LICENSE=yes (tools/docker/windows/README.md)"
            {
                echo "The Windows image is not built yet, and building it downloads Microsoft's"
                echo "C runtime and Windows SDK, whose licence only the owner can accept."
                echo "Read tools/docker/windows/README.md, then run this again with"
                echo "XWIN_ACCEPT_LICENSE=yes in the environment."
            } >&2
            exit 1
        fi
        ni_stage windows-image docker build --platform "$PLATFORM" --progress=plain --target sdk \
            -t "$NI_IMAGE" --build-arg "BASE_IMAGE=$base" --build-arg XWIN_ACCEPT_LICENSE=yes \
            -f "$windows_dir/Dockerfile" "$windows_dir" || exit 1
    fi
    status=0
    ni_docker_run "$NI_IMAGE" "$PLATFORM" "$BUILD" \
        scripts/build-windows.sh --juce /juce "/work/$NI_PROJECT_REL" || status=$?
    ni_record time total "$((SECONDS - start))"
    exit "$status"
fi

# ----------------------------------------------------- inside the container
NI_RESULT="$BUILD/cross-result.tsv"

# A Wine prefix of the build's own, made once: every later Wine start --
# ctest's, pluginval's -- then takes seconds rather than half a minute.
export WINEPREFIX="$BUILD/wine"
if [ ! -f "$WINEPREFIX/system.reg" ]; then
    ni_stage wine-prefix wineboot --init || { ni_finish; exit 1; }
fi

# pluginval.exe takes a Windows path: the bundle as Wine's Z: drive sees it.
windows_path() { winepath -w "$1"; }

if ni_build_and_test "$BUILD" \
        -DCMAKE_TOOLCHAIN_FILE=/work/tools/cross/windows-clang-cl.cmake \
        -DCMAKE_CROSSCOMPILING_EMULATOR=wine; then
    NI_PLUGINVAL_PATH=windows_path
    ni_validate "$BUILD" ", editor tests included, under Wine + Xvfb" \
        xvfb-run -a -s "-screen 0 1920x1080x24" wine /opt/pluginval/pluginval.exe || true
fi
ni_finish
