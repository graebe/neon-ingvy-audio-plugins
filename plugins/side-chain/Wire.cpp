/*
 * NI Side-Chain's wire arithmetic. See Wire.h for why it lives apart.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * NO iPlug2 AND NO ENGINE, so tests/cpp/sc_wire.cpp links it alone.
 */
#include "Wire.h"

#include <algorithm>
#include <cmath>

namespace sc {
namespace wire {

bool split_pair(const std::string& arg, std::string& a, std::string& b)
{
  const size_t colon = arg.find(':');
  if (colon == std::string::npos)
    return false;

  a = arg.substr(0, colon);
  b = arg.substr(colon + 1);
  return true;
}

unsigned char encode_sample(float v)
{
  /* isfinite FIRST. The clamp is written with fmin/fmax, and a NaN passed
   * through those PROPAGATES rather than being pinned -- the order is the
   * guard, not the presence of both. */
  const float c = std::isfinite(v) ? std::fmin(1.f, std::fmax(-1.f, v)) : 0.f;
  return (unsigned char) int((c + 1.f) * 127.5f + 0.5f);
}

unsigned char encode_unipolar(float v)
{
  /* Same order, same reason. Zero is the honest answer for "no reduction",
   * which is also where a non-finite value belongs: a NaN duck must read as
   * "nothing happened" rather than as a full-scale dip. */
  const float c = std::isfinite(v) ? std::fmin(1.f, std::fmax(0.f, v)) : 0.f;
  return (unsigned char) int(c * 255.f + 0.5f);
}

int clamp_editor_height(int requested)
{
  /* Exclusive at both ends: 100 is below any editor that has a control in it,
   * and 4000 is past the tallest display this runs on. Neither bound is a
   * design limit -- they both mean "the editor has miscalculated". */
  return (requested > 100 && requested < 4000) ? requested : 0;
}

double advance_beats(double beats, int frames, double bpm, double sampleRate)
{
  if (frames <= 0 || sampleRate <= 0.0)
    return beats;

  return beats + double(frames) * (bpm / 60.0) / sampleRate;
}

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

} // namespace wire
} // namespace sc
