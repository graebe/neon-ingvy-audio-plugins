#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# The conference (two-column) edition of the paper: paper.md -> paper.tex ->
# paper.pdf, with the figures as print PDFs. Each stage is timed into the
# build-timing log (scripts/timing.sh).
#
#   papers/subtractive-synthesis/tex/build.sh
#
# Needs python3, rsvg-convert (librsvg) and a TeX engine: tectonic, or
# latexmk with pdflatex. Without one, it stops after writing paper.tex and
# says so -- it does not fall back to anything else.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
# shellcheck source=../../../scripts/timing.sh
source "$root/scripts/timing.sh"
cd "$here"

ni_time_stage paper-tex-convert -- python3 md2tex.py ../paper.md paper.tex
ni_time_stage paper-tex-figures -- python3 figures.py ../figures figures

if command -v tectonic >/dev/null; then
    ni_time_stage paper-tex-compile -- tectonic --keep-logs paper.tex
elif command -v latexmk >/dev/null; then
    ni_time_stage paper-tex-compile -- latexmk -pdf -interaction=nonstopmode -halt-on-error paper.tex
else
    echo "paper.tex is written; no TeX engine found to compile it (install tectonic, or TeX Live for latexmk)." >&2
    exit 2
fi
