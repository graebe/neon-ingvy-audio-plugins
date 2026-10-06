// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's input arithmetic. See Wire.h.
 */
#include "Wire.h"

namespace sc {
namespace wire {

bool key_is_duplicate(const float* main, const float* key, int frames)
{
  /* A null pointer is not a duplicate -- it is a bus that is not there, which
   * the caller already knows about from IsChannelConnected. */
  if (!main || !key || frames <= 0)
    return false;

  /* No memcmp: the buffers are float and memcmp would also call two bit
   * patterns of the same value unequal, +0.0 and -0.0 most obviously. Reading
   * them as numbers is the comparison that was meant. */
  for (int i = 0; i < frames; i++)
  {
    if (main[i] != key[i])
      return false;
  }
  return true;
}

InputMap map_inputs(bool c0, bool c1, bool c2, bool c3)
{
  /* Stereo is channel 1 being there, not "more than one channel in": with a
   * key patched, a mono main is one channel of three. */
  (void) c0;
  InputMap m;
  m.mainL = 0;
  m.mainR = c1 ? 1 : 0;
  m.keyL = c2 ? 2 : -1;
  m.keyR = c3 ? 3 : m.keyL;
  return m;
}

} // namespace wire
} // namespace sc
