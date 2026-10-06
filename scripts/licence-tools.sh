#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The two licence tools, at the versions this repository's checks are written
# against. This is the one place those versions are spelled.
#
#   scripts/licence-tools.sh install [cargo-deny|cargo-about]...
#       cargo install --locked, each at its pin; both when none is named. They
#       land in cargo's own bin directory (~/.cargo/bin unless CARGO_HOME says
#       otherwise), never anywhere under ~/Library.
#   scripts/licence-tools.sh verify [cargo-deny|cargo-about]...
#       Exits non-zero, naming the command that fixes it, when a tool is
#       missing or at another version.
#
#   cargo-deny   the allowlist, bans and sources check: ctest's cargo_deny
#   cargo-about  the Rust crates' notices: scripts/gen-rust-notices.sh
#
# WHY PINNED. deny.toml's schema has changed under cargo-deny before, and two
# versions of cargo-about can render the same graph differently. A notices
# file that changes with whoever ran the generator is a diff nobody can
# review.
set -euo pipefail

CARGO_DENY_VERSION=0.20.2
CARGO_ABOUT_VERSION=0.9.2

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=./rust-env.sh
. "$ROOT/scripts/rust-env.sh"

usage() {
    sed -n '/^#   scripts\/licence-tools.sh install/,/^#       missing or/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

pin() {
    case "$1" in
        cargo-deny)  echo "$CARGO_DENY_VERSION" ;;
        cargo-about) echo "$CARGO_ABOUT_VERSION" ;;
        *) echo "unknown tool: $1 (cargo-deny or cargo-about)" >&2; exit 2 ;;
    esac
}

# cargo-about 0.9 builds its binary only with the `cli` feature.
features() {
    case "$1" in
        cargo-about) echo cli ;;
        *)           echo "" ;;
    esac
}

install_cmd() {
    local f
    f="$(features "$1")"
    echo "cargo install --locked $1@$(pin "$1")${f:+ --features $f}"
}

cmd="${1:-}"
[ "$cmd" = install ] || [ "$cmd" = verify ] || usage
shift
tools=("$@")
[ ${#tools[@]} -gt 0 ] || tools=(cargo-deny cargo-about)

status=0
for tool in "${tools[@]}"; do
    want="$(pin "$tool")"
    if [ "$cmd" = install ]; then
        f="$(features "$tool")"
        cargo install --locked "$tool@$want" ${f:+--features "$f"}
        continue
    fi
    # `cargo deny --version` prints "cargo-deny 0.20.2"; cargo-about likewise.
    have="$(cargo "${tool#cargo-}" --version 2>/dev/null | awk '{print $2}')" || have=""
    if [ "$have" != "$want" ]; then
        echo "$tool is ${have:-not installed}, and this repository pins $want." >&2
        echo "  Install it:  scripts/licence-tools.sh install $tool" >&2
        echo "          or:  $(install_cmd "$tool")" >&2
        status=1
    fi
done
exit "$status"
