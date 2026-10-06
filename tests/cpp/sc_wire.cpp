// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's input arithmetic: the places where being wrong is SILENT.
 *
 * The shared wire pieces are tests/cpp/ni_wire.cpp's; these two are this
 * product's -- a sidechain ducking on its own input, a key read as the main.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Wire.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace sc::wire;

/* -------------------------------------------------- the duplicated sidechain */

TEST_CASE("an aux buffer identical to the main input is reported as such")
{
  /*
   * Logic and GarageBand copy bus 1 into the sidechain bus when nothing is
   * patched. This is NOT used to work around that -- the Source parameter is
   * the explicit enable iPlug2's own example recommends -- it exists so the
   * editor does not show a key meter moving in time with the track's own audio,
   * which would read as a working sidechain.
   */
  const std::vector<float> a{0.1f, -0.2f, 0.3f, 0.0f};
  std::vector<float> b = a;
  CHECK(key_is_duplicate(a.data(), b.data(), int(a.size())));

  b[2] = 0.30001f;
  CHECK_FALSE(key_is_duplicate(a.data(), b.data(), int(a.size())));
}

TEST_CASE("the duplicate check compares numbers, not bit patterns")
{
  /*
   * memcmp would call +0.0 and -0.0 unequal, and a host that inverts phase
   * somewhere in its no-op path would then look like a patched sidechain.
   */
  const std::vector<float> a{0.0f, 1.0f};
  const std::vector<float> b{-0.0f, 1.0f};
  CHECK(key_is_duplicate(a.data(), b.data(), 2));
}

TEST_CASE("a missing bus is not a duplicate")
{
  /* A null pointer means the bus is not there, which the caller already knows
   * from IsChannelConnected -- answering "duplicate" would conflate "unpatched"
   * with "patched to ourselves". */
  const std::vector<float> a{1.0f, 2.0f};
  CHECK_FALSE(key_is_duplicate(nullptr, a.data(), 2));
  CHECK_FALSE(key_is_duplicate(a.data(), nullptr, 2));
  CHECK_FALSE(key_is_duplicate(a.data(), a.data(), 0));
}

/* ------------------------------------------------------- the input channels */

TEST_CASE("stereo main, stereo key")
{
  const InputMap m = map_inputs(true, true, true, true);
  CHECK(m.mainL == 0);
  CHECK(m.mainR == 1);
  CHECK(m.keyL == 2);
  CHECK(m.keyR == 3);
}

TEST_CASE("stereo main, mono key: the key feeds both of its sides")
{
  const InputMap m = map_inputs(true, true, true, false);
  CHECK(m.mainR == 1);
  CHECK(m.keyL == 2);
  CHECK(m.keyR == 2);
}

TEST_CASE("no key at all")
{
  const InputMap m = map_inputs(true, true, false, false);
  CHECK(m.keyL == -1);
  CHECK(m.keyR == -1);
}

TEST_CASE("a mono main with a key is mono: its right side is not a key channel")
{
  /* A mono track with a sidechain patched in. Counting connected channels called
   * that stereo and read channel 1 -- unconnected in VST3, and in CLAP, where
   * buses are packed, the key's left side -- as the main's right. */
  const InputMap m = map_inputs(true, false, true, true);
  CHECK(m.mainL == 0);
  CHECK(m.mainR == 0);
  CHECK(m.keyL == 2);
  CHECK(m.keyR == 3);
}

TEST_CASE("a mono main alone")
{
  const InputMap m = map_inputs(true, false, false, false);
  CHECK(m.mainR == 0);
  CHECK(m.keyL == -1);
}
