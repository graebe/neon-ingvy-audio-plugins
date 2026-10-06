// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's input arithmetic, on its own so it can be tested.
 *
 * No iPlug2 type and no engine: tests/cpp/sc_wire.cpp links it alone. The
 * shared pieces are in ni/Wire.h. These two are where being wrong is silent:
 *
 *   key_is_duplicate     a sidechain that ducks on its own input
 *   map_inputs           a key read as the main's right channel
 */
#pragma once

namespace sc {
namespace wire {

/*
 * Whether a "sidechain" buffer is really just the main input again.
 *
 * LOGIC AND GARAGEBAND COPY BUS 1 INTO THE SIDECHAIN BUS when nothing is
 * patched, a bug old enough to have its own forum threads. iPlug2's own example
 * works around it with a memcmp and a comment calling the workaround imperfect,
 * and saying the better answer is an explicit enable.
 *
 * Side-Chain takes the better answer -- the Source parameter IS the explicit enable --
 * so this is not load-bearing for correctness. It is used for one thing: to stop
 * the UI reporting a key signal that is actually the track's own audio, which
 * would read as "the sidechain is working" while nothing is routed.
 *
 * Compares exactly, because that is what the bug does: it hands over the same
 * samples, not similar ones. Two genuinely identical buffers are possible --
 * both silent, most obviously -- so a true answer here means "indistinguishable
 * from the main input", never "definitely unpatched", and the caller must treat
 * it as the weaker claim.
 */
bool key_is_duplicate(const float* main, const float* key, int frames);

/*
 * WHICH INPUT CHANNEL CARRIES WHAT, from which of the first four the host says
 * are connected. -1 is "none".
 *
 * The key always starts at channel 2: VST3 and AU place the sidechain bus after
 * the main bus's widest layout, and iPlug2's CLAP wrapper packs buses back to
 * back -- the same place only because config.h offers CLAP a key with a stereo
 * main and nowhere else (docs/iplug2-patches/0002-clap-bus-offsets.patch). A mono main duplicates into both sides; a mono key into both of
 * its own.
 */
struct InputMap
{
  int mainL, mainR, keyL, keyR;
};
InputMap map_inputs(bool c0, bool c1, bool c2, bool c3);

} // namespace wire
} // namespace sc
