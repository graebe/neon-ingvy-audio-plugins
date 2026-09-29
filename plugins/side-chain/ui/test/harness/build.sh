#!/bin/sh
# Build the editor and lay it out beside the harness page, so a review looks at
# the SAME bundle the plugin ships rather than at a dev server.
#
# vite writes to ../../resources/web (that is what CMake globs into the bundle);
# this copies assets and fonts in next to index.html and mock.js.
set -e
cd "$(dirname "$0")"
UI=../..
(cd "$UI" && npx vite build >/dev/null 2>&1)
rm -rf assets fonts
cp -R "$UI/../resources/web/assets" assets
cp -R "$UI/../resources/web/fonts"  fonts
echo "harness ready: $(pwd)/index.html"
