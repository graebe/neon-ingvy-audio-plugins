# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
"""
The paper's SVG figures as PDFs for print: ../figures/*.svg -> figures/*.pdf.

The SVGs carry both colour schemes as CSS custom properties and pick one with
prefers-color-scheme. Paper has one scheme, the light one, so each variable is
replaced by its light value and the dark block is dropped; the font is the
print sans. rsvg-convert (librsvg) draws the result.

  python3 figures.py ../figures figures
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path


def for_print(svg):
    light = re.search(r'svg\{(--[^}]*)\}', svg)[1]
    values = dict(re.findall(r'(--[\w-]+):([^;}]+)', light))
    svg = re.sub(r'@media \(prefers-color-scheme:dark\)\{svg\{[^}]*\}\}\n?', '', svg)
    svg = re.sub(r'svg\{--[^}]*\}\n?', '', svg, count=1)
    svg = re.sub(r'var\((--[\w-]+)\)', lambda m: values[m[1]], svg)
    return svg.replace('font-family:system-ui,-apple-system,"Segoe UI",sans-serif', 'font-family:Helvetica,Arial,sans-serif')


def main(src, dst):
    dst.mkdir(parents=True, exist_ok=True)
    for svg in sorted(src.glob('*.svg')):
        with tempfile.NamedTemporaryFile('w', suffix='.svg', delete=False) as tmp:
            tmp.write(for_print(svg.read_text()))
        pdf = dst / svg.with_suffix('.pdf').name
        subprocess.run(['rsvg-convert', '-f', 'pdf', '-o', str(pdf), tmp.name], check=True)
        Path(tmp.name).unlink()
        print(pdf)


if __name__ == '__main__':
    main(Path(sys.argv[1]), Path(sys.argv[2]))
