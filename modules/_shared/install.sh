#!/usr/bin/env bash
# Install a packaged Schwung module onto a Move.
#
#   ./modules/_shared/install.sh trance-gate                  # ableton@move.local
#   MOVE_HOST=ableton@192.168.1.42 ./modules/_shared/install.sh side-chain
#
# Installs what ./modules/_shared/package.sh <module> left in dist/.
set -euo pipefail

SHARED_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SHARED_DIR/../.." && pwd)"

MODULE="${1:-}"
MODULE_DIR="$REPO_ROOT/modules/$MODULE"
if [ -z "$MODULE" ] || [ ! -f "$MODULE_DIR/module.env" ]; then
    echo "usage: $0 <module>   (a directory under modules/ with a module.env)" >&2
    exit 2
fi
. "$MODULE_DIR/module.env"

HOST="${MOVE_HOST:-ableton@move.local}"
BASE="/data/UserData/schwung"
DEST="$BASE/modules/audio_fx/${MODULE_ID}"
SRC="$REPO_ROOT/dist/$MODULE_ID"

if [ ! -d "$SRC" ]; then
    echo "Error: $SRC not found. Build it first:" >&2
    echo "  ./modules/_shared/package.sh $MODULE" >&2
    exit 1
fi

echo "=== Installing $MODULE_TITLE to $HOST ==="

# FAIL RATHER THAN CREATE THE PATH. mkdir -p on a wrong base happily invents
# /data/UserData/schwung on a device that keeps its modules somewhere else
# (an older install used .../move-anything), and the result is a successful
# scp, no error anywhere, and a module that never appears in the FX picker.
if ! ssh "$HOST" "[ -d '$BASE/modules' ]"; then
    echo "Error: $BASE/modules does not exist on $HOST." >&2
    echo "Is Schwung installed there? Check with:" >&2
    echo "  ssh $HOST 'ls -d /data/UserData/*/modules'" >&2
    exit 1
fi

ssh "$HOST" "mkdir -p '$DEST'"
scp -q -r "$SRC/"* "$HOST:$DEST/"

# Owner-writable, readable by everyone, never world-writable. schwung-manager
# runs as `ableton` -- the same user this scp logs in as by default -- so the
# owner bit is all it needs to replace the module later; the old `a+rw` let
# any process on the device swap the .so the audio thread loads.
ssh "$HOST" "chmod -R u+rwX,go+rX,go-w '$DEST' && chmod 755 '$DEST/${MODULE_ID}.so'"

echo ""
echo "Installed to: $DEST"
ssh "$HOST" "ls -la '$DEST'"
echo ""
echo "No restart needed -- the FX picker scans the modules dir each time it opens."
echo "If the module is ALREADY loaded in a slot, that slot keeps the old .so:"
echo "set the position to None and pick it again, or reboot."
