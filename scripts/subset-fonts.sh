#!/bin/sh
# Subset JetBrains Mono (SIL OFL 1.1) to what the editors draw, as WOFF, into
# ui-kit/src/fonts. Copyright (c) 2026 Torben Gräber. MIT.
#
#   scripts/subset-fonts.sh <dir holding JetBrainsMono-Regular.ttf and -Medium.ttf>
#
# The OFL allows subsetting and conversion; OFL.txt travels with the result.
# Needs pyftsubset (fonttools). WOFF, not WOFF2: WOFF2 needs fonttools' Brotli
# extension, and with it this becomes --flavor=woff2 and a rename.
#
# The ranges: Basic Latin, Latin-1 and Latin Extended-A (a bus name typed as
# "Bässe"), the dashes, quotes and ellipsis, arrows, the minus sign, the euro
# and the command key. All layout features are kept -- tabular figures are why
# this face was chosen.
set -e
SRC=${1:?usage: subset-fonts.sh <dir with the JetBrains Mono TTFs>}
OUT="$(dirname "$0")/../ui-kit/src/fonts"
for w in Regular Medium; do
  pyftsubset "$SRC/JetBrainsMono-$w.ttf" \
    --unicodes="U+0020-007E,U+00A0-017F,U+2010-2027,U+2030-203A,U+2190-2193,U+2212,U+2318,U+20AC" \
    --layout-features='*' --flavor=woff \
    --output-file="$OUT/JetBrainsMono-$w.woff"
done
ls -l "$OUT"
