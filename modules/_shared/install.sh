#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
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

# STAGED, THEN SWAPPED -- NEVER WRITTEN INTO THE LIVE DIRECTORY. scp into
# $DEST rewrote each file in place: a module.json or a web page read half
# written by the manager or the FX picker, and -- worse -- the .so of a module
# loaded in a slot truncated and refilled under the process that has it
# mapped, which takes MoveOriginal down (the shim lives in it). So the module
# goes up beside the live directory, is made whole there, and replaces it with
# two renames: a process that has the old .so mapped keeps the old inode, and
# nothing ever sees a directory that is half one version and half another.
# A failure anywhere leaves the live module as it was.
TOKEN=$$
STAGE="$BASE/modules/audio_fx/.${MODULE_ID}.install.$TOKEN"
OLD="$BASE/modules/audio_fx/.${MODULE_ID}.old.$TOKEN"
ssh "$HOST" "rm -rf '$STAGE' && mkdir -p '$STAGE'"
scp -q -r "$SRC/"* "$HOST:$STAGE/"

# Owner-writable, readable by everyone, never world-writable. schwung-manager
# runs as `ableton` -- the same user this scp logs in as by default -- so the
# owner bit is all it needs to replace the module later; the old `a+rw` let
# any process on the device swap the .so the audio thread loads.
ssh "$HOST" sh -s -- "$DEST" "$STAGE" "$OLD" "$MODULE_ID" <<'REMOTE'
set -eu
dest=$1 stage=$2 old=$3 id=$4
rollback() {
  status=$?
  trap - 0 1 2 15
  if [ ! -d "$dest" ] && [ -d "$old" ]; then mv "$old" "$dest"; fi
  rm -rf "$stage" "$old"
  exit "$status"
}
trap rollback 0 1 2 15
test -f "$stage/$id.so"
chmod -R u+rwX,go+rX,go-w "$stage"
chmod 755 "$stage/$id.so"
if [ -d "$dest" ]; then mv "$dest" "$old"; fi
mv "$stage" "$dest"
rm -rf "$old"
trap - 0 1 2 15
REMOTE

echo ""
echo "Installed to: $DEST"
ssh "$HOST" "ls -la '$DEST'"
echo ""
echo "No restart needed -- the FX picker scans the modules dir each time it opens."
echo "A slot that already has the module keeps the version it loaded (the old"
echo "file stays mapped): set the position to None and pick it again, or reboot."
