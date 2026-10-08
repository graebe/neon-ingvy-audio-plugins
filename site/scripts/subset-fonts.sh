#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The site's JetBrains Mono (SIL OFL 1.1): the native kit's faces
# (plugins/_shared/ui/fonts), subset to what the pages draw and packed as WOFF
# into site/src/uv/fonts -- ~49 KB a weight where the TTFs are 274 KB.
#
#   sh site/scripts/subset-fonts.sh
#
# The OFL allows subsetting and conversion; OFL.txt travels with the result.
# Needs pyftsubset (fonttools). WOFF, not WOFF2: WOFF2 needs fonttools' Brotli
# extension, and with it this becomes --flavor=woff2 and a rename.
#
# The ranges: Basic Latin, Latin-1 and Latin Extended-A, the dashes, quotes
# and ellipsis, arrows, the minus sign, the euro and the command key. All
# layout features are kept -- tabular figures are why this face was chosen.
set -e
HERE="$(dirname "$0")"
SRC="$HERE/../../plugins/_shared/ui/fonts"
OUT="$HERE/../src/uv/fonts"
for w in Regular Medium; do
  pyftsubset "$SRC/JetBrainsMono-$w.ttf" \
    --unicodes="U+0020-007E,U+00A0-017F,U+2010-2027,U+2030-203A,U+2190-2193,U+2212,U+2318,U+20AC" \
    --layout-features='*' --flavor=woff \
    --output-file="$OUT/JetBrainsMono-$w.woff"
done
cp "$SRC/OFL.txt" "$OUT/OFL.txt"
ls -l "$OUT"
