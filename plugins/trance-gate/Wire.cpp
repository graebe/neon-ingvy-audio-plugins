/*
 * The Trance Gate's wire arithmetic. See Wire.h for why it lives apart.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * NO iPlug2 AND NO ENGINE, so tests/cpp/tg_wire.cpp links it alone.
 */
#include "Wire.h"

#include <algorithm>
#include <cmath>

namespace tg {
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
  /* isfinite FIRST. The clamp below is written with fmin/fmax, and a NaN
   * passed through those propagates rather than being pinned -- the order is
   * the guard, not the presence of both. */
  const float c = std::isfinite(v) ? std::fmin(1.f, std::fmax(-1.f, v)) : 0.f;
  return (unsigned char) int((c + 1.f) * 127.5f + 0.5f);
}

unsigned char encode_gain(float v)
{
  /* isfinite FIRST, for the reason encode_sample gives: a NaN propagates
   * through fmin/fmax rather than being pinned by them. Zero and not mid-scale,
   * because this is a gain -- "no value" is a shut gate. */
  const float c = std::isfinite(v) ? std::fmin(1.f, std::fmax(0.f, v)) : 0.f;
  return (unsigned char) int(c * 255.f + 0.5f);
}

int gate_per_step(int length)
{
  if (length < 1) length = 1;
  int n = (1024 + length / 2) / length;         /* round(1024 / length) */
  if (n < 8) n = 8;
  if (n > 64) n = 64;
  return n;
}

int clamp_editor_height(int requested)
{
  /* Exclusive at both ends, as it has always been: 100 is below any editor
   * that has a control in it, and 4000 is past the tallest display this runs
   * on. Neither bound is a design limit, they are both "the editor has
   * miscalculated". */
  return (requested > 100 && requested < 4000) ? requested : 0;
}

double advance_beats(double beats, int frames, double bpm, double sampleRate)
{
  if (frames <= 0 || sampleRate <= 0.0)
    return beats;

  return beats + double(frames) * (bpm / 60.0) / sampleRate;
}

bool slot_moved(int& pushed, int slot, bool rebase)
{
  /* No slot left behind -- the first block, or parameters that arrived
   * together -- is a new starting point, not a switch. */
  const bool moved = pushed >= 0 && !rebase && slot != pushed;
  pushed = slot;
  return moved;
}

} // namespace wire
} // namespace tg
