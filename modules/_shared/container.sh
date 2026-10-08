# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# How a module script reaches the module image. Sourced, not run, by
# package.sh and test.sh, which each re-enter themselves through it:
#
#   if ! ni_in_module_container; then
#       ni_module_container <stage> modules/_shared/<script> <args...>; exit
#   fi
#
# ONE IMAGE, ONE WAY IN. The image is the pinned toolchain the modules ship
# from (Dockerfile), and the package and its tests must meet the same one --
# a test that ran under another compiler would be testing another .so.
#
# Expects SHARED_DIR and REPO_ROOT, and scripts/timing.sh sourced.

# Its own name, not the "schwung-module-builder" the per-module scripts used:
# an image tag is machine-wide, and a checkout still on the old scripts would
# otherwise run this image with their older expectations of it.
NI_MODULE_IMAGE="ni-schwung-module-builder"

# Inside the image, or on a machine set up as it is: the image sets
# CROSS_PREFIX, and a cross toolchain set up by hand sets it too.
ni_in_module_container() {
    [ -n "${CROSS_PREFIX:-}" ] || [ -f /.dockerenv ]
}

# ni_module_container <stage> <script> <args...>: run <script> (its path from
# the repository root) with <args> in the image, timed as <stage>.
ni_module_container() {
    local stage="$1" script="$2"
    shift 2
    # `docker build` every time rather than only when the image is missing: it
    # is a no-op when the Dockerfile has not changed, and the old "first time
    # only" check kept building with a stale toolchain after the pin moved.
    ni_time_stage "image" -- \
        docker build -q -t "$NI_MODULE_IMAGE" -f "$SHARED_DIR/Dockerfile" "$SHARED_DIR" >/dev/null
    # As the invoking user, so dist/ and target/ are not left owned by root.
    # Group 0 is what the image made its toolchain writable to.
    ni_time_stage "$stage" -- docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" --group-add 0 \
        -w /build \
        "$NI_MODULE_IMAGE" \
        "./$script" "$@"
}
