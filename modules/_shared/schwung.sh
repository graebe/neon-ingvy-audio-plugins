#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The Schwung the Move modules are tested against: the version
# modules/_shared/schwung.version pins, fetched once, verified on every use.
#
#   modules/_shared/schwung.sh checkout   print the verified checkout's path
#   modules/_shared/schwung.sh headers    the vendored host headers
#                                         (engines/trance-gate/include) are
#                                         that version's, byte for byte
#
# Exit 0, or:
#    1  A MISMATCH. The tag names another commit than the pin, a cached
#       checkout is not the pinned commit or has been edited, or a vendored
#       header differs. Said on stderr, with what to do about it.
#   77  UNAVAILABLE -- no git, or nothing cached and the fetch failed (no
#       network). ctest's skip: the test did not run, which is not a pass.
#
# THE CACHE IS OUTSIDE EVERY CHECKOUT, so the worktrees share one copy and
# removing one never costs a fetch: ~/Library/Caches/neon-ingvy/schwung on
# macOS, $XDG_CACHE_HOME/neon-ingvy/schwung (~/.cache by default) elsewhere;
# NI_SCHWUNG_CACHE names another directory. One checkout per pinned commit.
#
# OFFLINE ONCE CACHED. A cached checkout is verified locally -- its HEAD is the
# pinned commit and `git status` finds nothing changed -- and only a missing
# one is fetched. NI_SCHWUNG_REMOTE fetches from another URL or a local clone
# instead of GitHub; the commit check is the same, so it can supply the pinned
# Schwung without a network but never a different one.
#
# THIS REPLACES A SEARCH FOR ANY schwung/ CHECKOUT ABOVE THIS ONE, which ran
# the editor's test against whatever version that checkout happened to be on.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
PIN="$HERE/schwung.version"
VENDORED="$REPO_ROOT/engines/trance-gate/include"

say() { printf 'schwung.sh: %s\n' "$*" >&2; }

pin() {
    local v
    v="$(sed -n "s/^$1=//p" "$PIN")"
    [ -n "$v" ] || { say "$PIN sets no $1"; exit 1; }
    printf '%s\n' "$v"
}
REPO="$(pin SCHWUNG_REPO)"
TAG="$(pin SCHWUNG_TAG)"
COMMIT="$(pin SCHWUNG_COMMIT)"
case "$COMMIT" in
    *[!0-9a-f]*) say "SCHWUNG_COMMIT in $PIN is not a full commit SHA: $COMMIT"; exit 1 ;;
esac
[ "${#COMMIT}" -eq 40 ] || { say "SCHWUNG_COMMIT in $PIN is not a full commit SHA: $COMMIT"; exit 1; }

REMOTE="${NI_SCHWUNG_REMOTE:-https://github.com/$REPO.git}"
if [ -n "${NI_SCHWUNG_CACHE:-}" ]; then
    CACHE="$NI_SCHWUNG_CACHE"
elif [ "$(uname -s)" = Darwin ]; then
    CACHE="$HOME/Library/Caches/neon-ingvy/schwung"
else
    CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/neon-ingvy/schwung"
fi
DIR="$CACHE/$TAG-$COMMIT"

command -v git >/dev/null 2>&1 || { say "git is not installed, so Schwung $TAG cannot be fetched or verified"; exit 77; }

# The cached checkout is the pin, untouched. Its own .git, checked first: git
# would otherwise walk up to any repository the cache happens to sit in.
verify() {
    local head changed
    [ -d "$DIR/.git" ] || { say "$DIR is not a git checkout. Delete it and run again."; exit 1; }
    head="$(git -C "$DIR" rev-parse -q --verify 'HEAD^{commit}' || true)"
    if [ "$head" != "$COMMIT" ]; then
        say "the cached Schwung at $DIR is at ${head:-no commit}, not $TAG ($COMMIT). Delete it and run again."
        exit 1
    fi
    changed="$(git -C "$DIR" status --porcelain)"
    if [ -n "$changed" ]; then
        say "the cached Schwung $TAG at $DIR has been changed since it was fetched:"
        printf '%s\n' "$changed" | head -20 >&2
        say "the tests must run on the pinned version as released. Delete $DIR and run again."
        exit 1
    fi
}

# Fetch the tag, refuse it unless it is still the pinned commit, and move the
# checkout into place whole. One fetch at a time per commit: ctest runs the
# tests that need this in parallel, and the first one fetches for all of them.
LOCK="$DIR.lock"
TMP=""
fetch() {
    local waited=0 got
    mkdir -p "$CACHE"
    until mkdir "$LOCK" 2>/dev/null; do
        if [ "$waited" -ge 300 ]; then
            say "waited 300 s for $LOCK: another fetch is stuck or was killed. Remove $LOCK and run again."
            exit 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    trap 'if [ -n "$TMP" ]; then rm -rf "$TMP"; fi; rmdir "$LOCK" 2>/dev/null || true' EXIT
    [ -d "$DIR" ] && return 0 # fetched while we waited

    TMP="$(mktemp -d "$CACHE/.fetch.XXXXXX")"
    say "fetching Schwung $TAG from $REMOTE into $DIR"
    git init -q "$TMP"
    if ! git -C "$TMP" fetch -q --depth 1 --no-tags "$REMOTE" "refs/tags/$TAG"; then
        say "could not fetch $TAG from $REMOTE, and nothing is cached at $DIR"
        exit 77
    fi
    got="$(git -C "$TMP" rev-parse 'FETCH_HEAD^{commit}')"
    if [ "$got" != "$COMMIT" ]; then
        say "MISMATCH: tag $TAG at $REMOTE is commit $got, and $PIN pins $COMMIT."
        say "The tag was moved: it now names a different Schwung. Check what changed, then pin it deliberately."
        exit 1
    fi
    git -C "$TMP" -c advice.detachedHead=false checkout -q --detach "$COMMIT"
    mv "$TMP" "$DIR"
    TMP=""
}

checkout() {
    [ -d "$DIR" ] || fetch
    verify
}

case "${1:-}" in
    checkout)
        checkout
        printf '%s\n' "$DIR"
        ;;
    headers)
        checkout
        status=0
        for h in "$VENDORED"/*.h; do
            name="$(basename "$h")"
            theirs="$DIR/src/host/$name"
            if [ ! -f "$theirs" ]; then
                say "$name is vendored, and Schwung $TAG has no src/host/$name"
                status=1
            elif ! cmp -s "$h" "$theirs"; then
                say "$name differs from Schwung $TAG's -- vendor theirs (cp \"$theirs\" \"$h\"):"
                diff -u "$h" "$theirs" | head -40 >&2 || true
                status=1
            fi
        done
        [ "$status" -ne 0 ] || echo "  the vendored host headers are Schwung $TAG's ($COMMIT)        ok"
        exit "$status"
        ;;
    *)
        echo "usage: $0 checkout|headers" >&2
        exit 2
        ;;
esac
