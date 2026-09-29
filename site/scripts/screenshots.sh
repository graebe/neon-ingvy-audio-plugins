#!/bin/sh
# Screenshots of the editors, generated rather than taken by hand.
#
# THE HARNESS IS THE SUBJECT, not a mock-up of it: test/harness/build.sh lays
# the SHIPPING bundle beside its page and mock.js drives it, so what is captured
# here is the editor the plugin carries. Regenerate after a UI change and the
# picture on the site is current; a screenshot cropped out of a DAW is stale the
# day the editor moves and nobody can tell.
#
# Headless Chrome rather than a browser-automation dependency -- this repository
# has none, and one 12-line shell script is not the place to start. A static
# server is required because a module script will not load over file://.
#
#   sh site/scripts/screenshots.sh        # -> docs/media/<product>/live.png
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
[ -x "$CHROME" ] || { echo "screenshots: no Chrome at $CHROME"; exit 1; }

PORT=8919
python3 -m http.server "$PORT" --directory "$ROOT" >/dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT
sleep 1

shoot() {
  product=$1; w=$2; h=$3; budget=$4; query=$5
  echo "screenshots: $product (${w}x${h})"
  sh "plugins/$product/ui/test/harness/build.sh" >/dev/null
  mkdir -p "docs/media/$product"
  # --virtual-time-budget lets the editor mount, take the mock's pushes and
  # settle before the frame is grabbed; without it the capture is a blank ground.
  # The Spectrogram needs the whole thirteen-second history to have scrolled past
  # or the picture is mostly empty ground, so its budget is the history length.
  "$CHROME" --headless --disable-gpu --hide-scrollbars \
    --window-size="$w,$h" \
    --virtual-time-budget="$budget" \
    --screenshot="$ROOT/docs/media/$product/live.png" \
    "http://localhost:$PORT/plugins/$product/ui/test/harness/index.html$query" \
    >/dev/null 2>&1
  [ -s "docs/media/$product/live.png" ] || { echo "  FAILED: no image"; exit 1; }
  echo "  -> docs/media/$product/live.png"
}

shoot trance-gate 856 660 4000 ""
shoot spectrogram 720 402 16000 ""
