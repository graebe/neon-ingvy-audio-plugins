#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The Rust crates' section of THIRD_PARTY_LICENSES.md, written by cargo-about.
#
#   scripts/gen-rust-notices.sh
#
# Run it whenever Cargo.lock changes: a crate added, removed or updated.
# Until then scripts/check-licenses.mjs (ctest -R licenses) fails, because it
# holds the section's crates to the ones `cargo metadata` says ship.
#
# WHAT IT WRITES. cargo-about walks the dependency graph by about.toml's rules
# -- every platform's dependencies, but no build or dev ones, and none of this
# repository's own crates -- reads each crate's own licence files, and renders
# scripts/rust-notices.hbs. The result replaces what is between the two
# markers in THIRD_PARTY_LICENSES.md, and nothing outside them.
#
# --fail: a crate whose licence cargo-about cannot read, or which about.toml
# does not accept, stops the run rather than leaving a hole in the notices.
# --locked: the notices are for the Cargo.lock that is checked in, so a run
# that would have to change it fails instead.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NOTICES="$ROOT/THIRD_PARTY_LICENSES.md"
BEGIN='<!-- BEGIN Rust crates: written by scripts/gen-rust-notices.sh. Edit scripts/rust-notices.hbs, never this text. -->'
END='<!-- END Rust crates -->'

# shellcheck source=./rust-env.sh
. "$ROOT/scripts/rust-env.sh"
# The pinned cargo-about, or a message naming the command that installs it.
"$ROOT/scripts/licence-tools.sh" verify cargo-about

for marker in "$BEGIN" "$END"; do
    n="$(grep -cxF -- "$marker" "$NOTICES" || true)"
    if [ "$n" != 1 ]; then
        echo "THIRD_PARTY_LICENSES.md must hold this line exactly once, and holds it $n times:" >&2
        echo "  $marker" >&2
        exit 1
    fi
done

generated="$(mktemp)"
spliced="$(mktemp)"
trap 'rm -f "$generated" "$spliced"' EXIT

cargo about generate --locked --fail --workspace \
    --manifest-path "$ROOT/Cargo.toml" --config "$ROOT/about.toml" \
    --output-file "$generated" "$ROOT/scripts/rust-notices.hbs"

# The markers stay; what is between them is replaced, with one blank line on
# each side of the generated text and no trailing blank lines inside it.
#
# LINE ENDINGS ARE THE FILE'S, NOT THE CRATES'. cargo-about copies a licence
# file byte for byte, and some ship with CRLF (rustfft's and transpose's do):
# spliced as they come, their lines end in a carriage return the rest of this
# file does not have, and every run of this script is a diff against the last.
awk -v begin="$BEGIN" -v end="$END" -v gen="$generated" '
    $0 == begin {
        print; print ""
        n = 0
        while ((getline line < gen) > 0) {
            sub(/\r$/, "", line)
            text[++n] = line
        }
        while (n > 0 && text[n] ~ /^[[:space:]]*$/) n--
        for (i = 1; i <= n; i++) print text[i]
        print ""
        skip = 1
        next
    }
    $0 == end { skip = 0 }
    !skip { print }
' "$NOTICES" > "$spliced"
# Written through, not moved over: the file keeps its own mode.
cat "$spliced" > "$NOTICES"

echo "THIRD_PARTY_LICENSES.md: the Rust crates' section is rewritten from Cargo.lock"
