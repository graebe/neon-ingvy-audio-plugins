# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# shellcheck shell=bash
# shellcheck disable=SC2034 # it sets variables its callers read
# What scripts/build-{macos,linux,windows}.sh share, and build-all.sh reads.
# Source it, do not run it. docs/tech/cross-build.md is the walk-through.
#
# EVERY build-<platform>.sh KEEPS ONE CONTRACT, so build-all.sh can line any of
# them up:
#
#   build-<platform>.sh [--juce <dir>] <cmake-project-dir>
#
#   - configures the project into <project>/build-<platform>/ (Release, Ninja)
#     with -DNI_JUCE_DIR=<dir>; <dir> defaults to external/JUCE, the
#     repository's JUCE submodule
#   - builds it, runs its ctest, and runs pluginval on every .vst3 bundle the
#     build produced
#   - writes <project>/build-<platform>/cross-result.tsv, one tab-separated
#     record per line:
#       stage   <name>  passed|FAILED  <seconds>
#       bundle  <path from the repository root>  <KiB>  <pluginval verdict>  <seconds>
#       note    <why a stage could not run>
#       time    total  <seconds>
#   - exits non-zero if any of it failed -- after running all of it, so one
#     failure does not hide the next
#   - in a container, names the image it built in, in
#     <project>/build-<platform>/cross-image, and empties a build directory
#     another image built first (see ni_claim_build_dir)
#
# The project must lie inside this repository: a container sees the repository
# through one bind mount, at /work.
#
# Runs under macOS's bash 3.2 as well as the containers' bash 5: no
# associative arrays, and no array expanded before it is known not to be empty
# (bash 3.2 calls an empty one unbound under set -u).

NI_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Every stage is timed into the timing log as well as the result file.
# shellcheck source=scripts/timing.sh
. "$NI_ROOT/scripts/timing.sh"

# Every platform validates at this strictness. The pluginval RELEASE is pinned
# in scripts/validate-plugins.sh for every platform (build-macos.sh reads the
# macOS pin from there) and again in the two Dockerfiles, by the same digests
# -- one release.
NI_PLUGINVAL_STRICTNESS=10

ni_die() { echo "error: $*" >&2; exit 2; }

# ni_parse_args "$@": sets NI_PROJECT and NI_JUCE (absolute paths) and
# NI_PROJECT_REL (from the repository root). --arch is accepted where the
# script lists its choices in NI_ARCHES.
ni_parse_args() {
    NI_JUCE="$NI_ROOT/external/JUCE"
    NI_PROJECT=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --juce)
                [ $# -ge 2 ] || ni_die "--juce needs a directory"
                NI_JUCE=$2; shift 2 ;;
            --arch)
                [ $# -ge 2 ] || ni_die "--arch needs a value"
                case " ${NI_ARCHES:-} " in
                    *" $2 "*) NI_ARCH=$2 ;;
                    *) ni_die "--arch $2: this script builds ${NI_ARCHES:-one architecture only}" ;;
                esac
                shift 2 ;;
            -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
            -*) ni_die "unknown option $1 (see --help)" ;;
            *)
                [ -z "$NI_PROJECT" ] || ni_die "one project at a time"
                NI_PROJECT=$1; shift ;;
        esac
    done
    [ -n "$NI_PROJECT" ] || ni_die "usage: $(basename "$0") [--juce <dir>] <cmake-project-dir>"
    [ -f "$NI_PROJECT/CMakeLists.txt" ] || ni_die "$NI_PROJECT has no CMakeLists.txt"
    [ -f "$NI_JUCE/CMakeLists.txt" ] || ni_die "no JUCE checkout at $NI_JUCE (pass --juce <dir>)"
    NI_PROJECT="$(cd "$NI_PROJECT" && pwd)"
    NI_JUCE="$(cd "$NI_JUCE" && pwd)"
    case "$NI_PROJECT/" in
        "$NI_ROOT"/*) NI_PROJECT_REL=${NI_PROJECT#"$NI_ROOT"/} ;;
        *) ni_die "$NI_PROJECT is outside the repository ($NI_ROOT)" ;;
    esac
}

# ni_result_begin <build-dir>: start a fresh result file there.
ni_result_begin() {
    mkdir -p "$1"
    NI_RESULT="$1/cross-result.tsv"
    : > "$NI_RESULT"
}

# ni_record <fields...>: one tab-separated record in the result file.
ni_record() {
    local IFS=$'\t'
    echo "$*" >> "$NI_RESULT"
}

# ni_stage <name> <command...>: run it, time it, record whether it passed --
# in the result file and in the timing log (scripts/timing.sh), in this shell,
# so a function it runs can set what its caller reads (ni_image's NI_IMAGE).
# NI_STAGE_BUILD names the build directory a stage builds in, for the log.
ni_stage() {
    local name=$1 start=$SECONDS status=passed
    shift
    echo "=== $name: $*"
    ni_time_stage "$name" ${NI_STAGE_BUILD:+--build "$NI_STAGE_BUILD"} --here -- "$@" || status=FAILED
    ni_record stage "$name" "$status" "$((SECONDS - start))"
    [ "$status" = passed ]
}

# ni_finish: the exit status the result file adds up to. A bundle with no
# validator is not a failure of the build, and is not called a pass either.
ni_finish() {
    if grep -q $'\tFAILED' "$NI_RESULT"; then
        echo "=== FAILED: $(grep $'\tFAILED' "$NI_RESULT" | cut -f2 | tr '\n' ' ')" >&2
        return 1
    fi
    if grep -q $'\tnot validated' "$NI_RESULT"; then
        echo "=== built and tested, NOT validated (${NI_RESULT#"$NI_ROOT"/})"
    else
        echo "=== all passed (${NI_RESULT#"$NI_ROOT"/})"
    fi
}

# How many compilers to run at once. JUCE's module translation units are big
# (one of them is all of juce_gui_basics), so where memory is the tighter limit
# -- a container on an 8 GB Docker VM -- it decides, at one job per GiB.
# CMAKE_BUILD_PARALLEL_LEVEL overrides.
ni_jobs() {
    if [ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]; then
        echo "$CMAKE_BUILD_PARALLEL_LEVEL"
    elif [ -r /proc/meminfo ]; then
        local cpus gib
        cpus=$(nproc)
        gib=$(awk '/^MemTotal:/ { print int($2 / 1048576) }' /proc/meminfo)
        [ "$gib" -ge 1 ] || gib=1
        echo $((gib < cpus ? gib : cpus))
    else
        getconf _NPROCESSORS_ONLN
    fi
}

# ni_build_and_test <build-dir> [cmake args...]: configure, build, ctest. A
# failed configure or build stops here (there is nothing to test); a failed
# ctest is recorded and validation still runs.
ni_build_and_test() {
    local build=$1
    shift
    NI_STAGE_BUILD=$build ni_stage configure cmake -S "$NI_PROJECT" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DNI_JUCE_DIR="$NI_JUCE" "$@" || return 1
    NI_STAGE_BUILD=$build ni_stage build cmake --build "$build" --parallel "$(ni_jobs)" || return 1
    ni_stage ctest ctest --test-dir "$build" --output-on-failure || true
}

# ni_bundles <build-dir>: set NI_BUNDLES to every .vst3 bundle the build
# produced (a bundle is a directory, on Windows too); none is a failure.
ni_bundles() {
    local bundle
    NI_BUNDLES=()
    while IFS= read -r -d '' bundle; do NI_BUNDLES+=("$bundle"); done \
        < <(find "$1" -name '*.vst3' -type d -prune -print0 | sort -z)
    if [ ${#NI_BUNDLES[@]} -eq 0 ]; then
        echo "no .vst3 bundle under $1" >&2
        ni_record stage bundles FAILED 0
        return 1
    fi
}

# ni_unvalidated <build-dir> <verdict>: record the bundles with no validator
# run -- the verdict says why, and never says passed.
ni_unvalidated() {
    local bundle
    ni_bundles "$1" || return 1
    for bundle in "${NI_BUNDLES[@]}"; do
        ni_record bundle "${bundle#"$NI_ROOT"/}" "$(du -sk "$bundle" | cut -f1)" "$2" 0
        echo "=== $(basename "$bundle"): $2"
    done
}

# ni_validate <build-dir> <verdict note> <pluginval command...>: pluginval on
# every .vst3 bundle the build produced, each with its own log beside it.
# NI_PLUGINVAL_PATH, if set, names a function that turns a bundle's path into
# the one pluginval is given (a Windows path, under Wine).
ni_validate() {
    local build=$1 note=$2 bundle failed=0
    shift 2
    ni_bundles "$build" || return 1
    for bundle in "${NI_BUNDLES[@]}"; do
        local name log arg start verdict
        name=$(basename "$bundle" .vst3)
        log="$build/pluginval-$name.log"
        arg=$bundle
        if [ -n "${NI_PLUGINVAL_PATH:-}" ]; then arg=$("$NI_PLUGINVAL_PATH" "$bundle"); fi
        echo "=== pluginval: $name.vst3, strictness $NI_PLUGINVAL_STRICTNESS$note (log: ${log#"$NI_ROOT"/})"
        start=$SECONDS
        # The verdict is pluginval's exit status AND its closing SUCCESS line:
        # a wrapper (xvfb-run, wine) must not turn a crash into a pass.
        if ni_time_stage "pluginval $name" --here -- "$@" --strictness-level "$NI_PLUGINVAL_STRICTNESS" \
                --timeout-ms 300000 --validate "$arg" > "$log" 2>&1 && grep -q '^SUCCESS' "$log"; then
            verdict="passed (strictness $NI_PLUGINVAL_STRICTNESS$note)"
        else
            verdict="FAILED (strictness $NI_PLUGINVAL_STRICTNESS$note)"
            failed=1
            tail -n 40 "$log" >&2
        fi
        ni_record bundle "${bundle#"$NI_ROOT"/}" "$(du -sk "$bundle" | cut -f1)" "$verdict" "$((SECONDS - start))"
        echo "    $verdict"
    done
    return "$failed"
}

# ni_image_tag <name> <platform> <dockerfile-dir> [docker build args...]: set
# NI_IMAGE to the tag the image built from that Dockerfile, with those
# arguments, for that platform has -- whether or not it exists yet.
#
# THE TAG IS THE DOCKERFILE'S CONTENT and the build arguments', so a missing
# tag means a changed Dockerfile: a moved pin builds a new image, and an
# unchanged one costs a `docker image inspect` rather than a `docker build`.
ni_image_tag() {
    local name=$1 platform=$2 dir=$3
    shift 3
    local hash
    hash=$({ cat "$dir/Dockerfile"; printf '%s\n' "$@"; } | shasum -a 256 | cut -c1-12)
    NI_IMAGE="$name:$hash-${platform#linux/}"
}

ni_image_exists() { docker image inspect "$1" >/dev/null 2>&1; }

# ni_claim_build_dir <build-dir>: make the build directory NI_IMAGE's.
#
# A BUILD DIRECTORY BELONGS TO ONE IMAGE. Ninja rebuilds what changed in the
# sources or in the commands, and a moved pin changes neither: the new image's
# clang sits at the old one's path, so what the old image compiled would be
# linked, tested and validated as the new one's work. A build directory that
# names another image -- or none, from before it was named -- is emptied
# first, all but this run's own logs.
ni_claim_build_dir() {
    local build=$1 stamp="$1/cross-image"
    [ "$(cat "$stamp" 2>/dev/null)" != "$NI_IMAGE" ] || return 0
    if [ -n "$(find "$build" -mindepth 1 -maxdepth 1 ! -name cross-result.tsv ! -name cross-build.log)" ]; then
        echo "=== ${build#"$NI_ROOT"/} holds a build from another image: emptying it for $NI_IMAGE"
        find "$build" -mindepth 1 -maxdepth 1 ! -name cross-result.tsv ! -name cross-build.log -exec rm -rf {} +
    fi
    echo "$NI_IMAGE" > "$stamp"
}

# ni_image <name> <platform> <dockerfile-dir> [docker build args...]: set
# NI_IMAGE (see ni_image_tag), building the image first if it does not exist.
ni_image() {
    local platform=$2 dir=$3
    ni_image_tag "$@"
    shift 3
    ni_image_exists "$NI_IMAGE" && return 0
    echo "=== building $NI_IMAGE (once per change to its Dockerfile)"
    docker build --platform "$platform" --progress=plain -t "$NI_IMAGE" "$@" \
        -f "$dir/Dockerfile" "$dir"
}

# ni_docker_run <image> <platform> <build-dir> <command...>: run a command in
# a build image as the invoking user, the repository at /work and JUCE at
# /juce (read-only). Everything a run writes -- objects, CARGO_HOME, HOME with
# its caches and Wine prefix -- stays in the build directory, never the image.
# The timing log's directory is mounted too, so the stages inside are logged
# with the host's checkout, commit and machine rather than the container's.
ni_docker_run() {
    local image=$1 platform=$2 build=$3
    shift 3
    local build_in=/work/${build#"$NI_ROOT"/}
    local timing=() log
    if log=$(ni_timing_log) && mkdir -p "$(dirname "$log")" 2>/dev/null; then
        [ -n "${NI_TIMING_RUN:-}" ] || NI_TIMING_RUN="$(date -u +%Y%m%dT%H%M%SZ)-$$"
        timing=(-v "$(dirname "$log"):/ni-timing"
                -e "NI_TIMING_LOG=/ni-timing/$(basename "$log")"
                -e "NI_TIMING_RUN=$NI_TIMING_RUN"
                -e "NI_TIMING_HOST=$(hostname -s 2>/dev/null || hostname)"
                -e "NI_TIMING_SHA=$(git -C "$NI_ROOT" rev-parse --short=12 HEAD 2>/dev/null || true)"
                -e "NI_TIMING_BRANCH=$(git -C "$NI_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
                -e "NI_TIMING_WORKTREE=$NI_ROOT")
    fi
    docker run --rm --platform "$platform" \
        ${timing[@]+"${timing[@]}"} \
        -u "$(id -u):$(id -g)" \
        -v "$NI_ROOT:/work" -v "$NI_JUCE:/juce:ro" -w /work \
        -e HOME="$build_in/home" -e CARGO_HOME="$build_in/cargo-home" \
        ${CMAKE_BUILD_PARALLEL_LEVEL:+-e CMAKE_BUILD_PARALLEL_LEVEL="$CMAKE_BUILD_PARALLEL_LEVEL"} \
        -e NI_INSIDE=1 \
        "$image" "$@"
}
