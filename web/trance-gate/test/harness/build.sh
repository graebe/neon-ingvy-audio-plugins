#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Build the editor and lay it out beside the harness page, so a review looks at
# the SAME bundle the plugin ships rather than at a dev server.
#
# vite writes to ../resources/web (that is what CMake globs into the bundle);
# this copies assets and fonts in next to index.html and mock.js.
set -e
cd "$(dirname "$0")"
UI=../..
# NI_HARNESS_SOURCEMAP=<dir> builds into <dir> WITH a source map instead: the
# e2e coverage run maps Chrome's coverage of the bundle back to the sources
# through it (scripts/e2e-coverage.mjs). Never into resources/web, which is
# what the plugin bundle is built from -- a map must not ship.
if [ -n "${NI_HARNESS_SOURCEMAP:-}" ]; then
  (cd "$UI" && npx vite build --sourcemap --outDir "$NI_HARNESS_SOURCEMAP" --emptyOutDir >/dev/null 2>&1)
  WEB="$NI_HARNESS_SOURCEMAP"
else
  (cd "$UI" && npx vite build >/dev/null 2>&1)
  WEB="$UI/../resources/web"
fi
rm -rf assets fonts
cp -R "$WEB/assets" assets
cp -R "$WEB/fonts"  fonts
echo "harness ready: $(pwd)/index.html"
