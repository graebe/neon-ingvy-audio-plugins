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

} // namespace wire
} // namespace tg
