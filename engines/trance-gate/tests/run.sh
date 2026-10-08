#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
#
# Headless tests for the gate engine. Runs natively -- no Move required.
#
# THE ENGINE IS RUST AND THE TESTS ARE STILL C. That is deliberate and is the
# whole verification strategy of the port: these suites link the engine
# through its C ABI and do not care what is behind it, so they were relinked
# rather than rewritten: the assertions written against the C engine are what
# say whether the port is correct.
set -e
# RESOLVED BEFORE THE cd, AND BOTH OF THEM. `$0` is relative when this is
# invoked by a relative path, so every later use of `dirname "$0"` resolved
# against the new directory and pointed into itself -- the script could only be
# run from one place, and said "no such file or directory" from anywhere else.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
cd "$HERE/.."

# CARGO_BUILD_TARGET NAMES THE PLATFORM, when it is not this machine's own.
# modules/_shared/test.sh sets it to aarch64-unknown-linux-gnu to run this on
# the Move's architecture in the module's container, against the same checkout
# a macOS build uses: cargo then builds into target/<triple>/ -- the directory
# package.sh ships from, with the Move's target-cpu -- and the binaries below
# go to build/<triple>/, so neither run overwrites what the other built.
TRIPLE="${CARGO_BUILD_TARGET:-}"
OUT="build${TRIPLE:+/$TRIPLE}"
# The binaries below are written there, and a fresh checkout has no build/ at
# all -- the linker then failed with errno=2 on the OUTPUT path, which reads
# like a missing input and sent the last person looking for the wrong thing.
mkdir -p "$OUT"

# cargo, wherever it is installed -- this file's own version of this searched
# only via rustup, so a toolchain installed any other way was not found.
. "$ROOT/scripts/rust-env.sh"
# trance_gate_core.h IS GENERATED: tg-capi's build.rs writes it from the Rust
# (engines/shared/cbindgen/capi_header.rs) into the directory this names, inside
# the target directory it was built with. include/ holds only Schwung's own
# headers now, vendored for the Move-side tests below.
CAPI="$ROOT/target/capi-include"
NI_CAPI_INCLUDE_DIR="$CAPI" cargo build --release -p tg-move
# ONE staticlib carries both surfaces -- the Schwung vtable and the tg_core_*
# ABI -- because two would each bundle a copy of the Rust runtime and collide.
#
# AT THE WORKSPACE ROOT, NOT BESIDE THE ENGINE. The engine was its own
# repository when this was written; as a subtree in the monorepo it shares one
# Cargo workspace, so the target directory is the root's.
ENGINE=$ROOT/target/${TRIPLE:+$TRIPLE/}release/libtg_move.a
# gnu11, as CMake compiles these same files (CMAKE_C_STANDARD 11 with its
# default extensions): strict c11 hides M_PI in glibc's math.h, which macOS's
# does not, so -std=c11 compiled here and nowhere on Linux.
CSTD=-std=gnu11
cc $CSTD -Wall -Wextra -Wno-unused-parameter -Iinclude -I"$CAPI" \
   tests/test_gate.c "$ENGINE" -o "$OUT/test_gate" -lm
"./$OUT/test_gate" || exit 1

# The portable engine's own tests: sample rate, the float paths and the
# transport struct -- three freedoms the Schwung shell cannot exercise,
# because it is always 44100, always int16 and always has a host.
cc $CSTD -Wall -Wextra -Iinclude -I"$CAPI" \
   tests/test_core.c "$ENGINE" -o "$OUT/test_core" -lm
"./$OUT/test_core" || exit 1

# THE GOLDEN RENDER. Four seconds of audio through the whole engine, compared
# by hash against a render captured before the engine was ever split out of
# the Schwung module. (It said 20 seconds here for a long time; render_ref's
# default is 4, and 705,600 bytes is what it writes.)
#
# This is the one check that the SOUND has not changed, and it is the reason
# the 32-bit-mask widening to 128 steps could be done at all: a step shifted
# by one position, an envelope restarted a sample early, a rate table entry
# INSERTED rather than appended -- none of those fail a unit test, and all of
# them fail here. It was being run by hand, which is the same as not being
# run; a check nobody is obliged to remember is not a check.
#
# If this fires and the change to the audio was DELIBERATE, re-record the
# hash from `build/render_ref | md5` and say so in the commit.
# Re-recorded 2026-09-23 when the per-step level became latched at gate-open.
# The diff was confined to steps 2 and 6 -- the steps FOLLOWING the golden
# patch's ties at 1 and 5, where the level used to jump mid-gate to the new
# step's amount. That jump was the bug; everything else is byte-identical.
# Previous: b208becc62657c9748247b9daa7b0362
# Re-recorded 2026-09-23 when the envelope's stages became a percentage of the
# gate's WIDTH rather than milliseconds. The patch is the same patch in the
# new units (3.5 ms at a 91.46 ms width is 3.8267%), and the render was
# compared NUMERICALLY before this was accepted: 554 of 176,400 frames differ
# and never by more than 4 LSB of 32768, which is the rounding in four decimal
# places of a percentage. A units change cannot keep a hash; it can and must
# keep the sound.
# Previous: 8e4892aa8e3947594e91cf966f7ddc98
# Re-recorded 2026-09-24 for the Rust port -- and the OLD HASH WAS PINNING A
# COMPILER, not the algorithm.
#
# The C engine compiled with clang's default fp-contract fuses `a - b*c` into
# a single FMA, one rounding instead of two. Rust does not contract, so the
# two differed by one f32 ulp wherever Amount or a per-step level was not 1 --
# 140 of 352,800 samples, never by more than 1 LSB of 32768.
#
# Built with -ffp-contract=off the C produces THIS hash exactly, which is what
# identified the cause and what makes the new number the algorithm's rather
# than a build flag's. Worth knowing: the shipped Move .so is built -Ofast,
# which contracts harder still, so the module and its tests never agreed
# bit-for-bit until now.
# Previous: 4264807b9e7da87844309fa48d0cc8a3 (C, with contraction)
# Re-recorded 2026-09-30 for two INTENDED sound changes, compared numerically
# against the previous render before this was accepted:
#   - The transport start seeds the envelope at the open gate instead of at
#     zero, removing a one-sample drop (1.0 -> 0.1 here) at play. All 306
#     samples that moved by more than 1 LSB are in frames 1..153, the first
#     step's attack.
#   - process_i16 ROUNDS instead of truncating, as sc-core's does. Every other
#     difference is exactly 1 LSB, where truncation had lost it.
# The phase-loop and parameter-glide changes of the same series move nothing
# here. tests/render_plugin.c pins the same bytes through the plugin path.
# Previous: 3992810c52d7962b4d25b3a30494ee2e
GOLDEN=d8389d25abb3c44b34461f3029f6ab48
cc $CSTD -Wall -Wextra -Wno-unused-parameter -Iinclude -I"$CAPI" \
   tests/render_ref.c "$ENGINE" \
   -o "$OUT/render_ref" -lm
GOT=$("./$OUT/render_ref" | md5 -q 2>/dev/null || "./$OUT/render_ref" | md5sum | cut -d" " -f1)
echo
echo "golden render:"
if [ "$GOT" = "$GOLDEN" ]; then
  echo "  4s reference render is bit-identical                       ok"
else
  echo "  RENDER CHANGED: got $GOT want $GOLDEN"
  exit 1
fi

# The UI smoke test needs the host's shared modules. SEARCH FOR THEM RATHER
# THAN NAMING ONE PATH. A single "../schwung/src/shared" is right in a plain
# checkout and wrong in a worktree, where .. is the worktree group directory --
# so the 140 UI checks quietly skipped themselves for anyone working on a
# branch, which is precisely the silent-no-fixture case the comment below
# objects to. Set SCHWUNG_SHARED to override.
if [ -n "$SCHWUNG_SHARED" ]; then
  SHARED="$SCHWUNG_SHARED"
else
  SHARED=""
  for d in ../schwung ../../schwung ../../../schwung/schwung ../../schwung/schwung; do
    if [ -d "$d/src/shared/param_pages" ]; then
      SHARED="$(cd "$d/src/shared" && pwd)"
      break
    fi
  done
fi
if [ -d "$SHARED/param_pages" ]; then
  echo
  # ALWAYS REBUILD. This used to try the existing binary first and only
  # compile if running it failed -- so an edit to chain_params was tested
  # against the PREVIOUS build's JSON, silently, for as long as the old
  # binary kept working. A stale fixture reports the old contract as the
  # current one, which is worse than no fixture at all.
  cc $CSTD -Iinclude -I"$CAPI" tests/dump_params.c "$ENGINE" \
     -o "$OUT/dump_params" -lm
  "./$OUT/dump_params" > "$OUT/chain_params.json"
  TG_PARAMS="$OUT/chain_params.json" node tests/smoke_ui.mjs "$SHARED" "$OUT/.smoke"
else
  echo
  echo "(ui smoke test skipped: $SHARED not found)"
fi
