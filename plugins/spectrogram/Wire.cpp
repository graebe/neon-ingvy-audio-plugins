/*
 * The Spectrogram's wire format. See Wire.h for why it lives apart.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * NO iPlug2 AND NO ENGINE. This file includes <string> and <cstdio> and
 * nothing else, which is what lets tests/cpp/spectro_wire.cpp link it in
 * isolation and run in milliseconds. Adding an include here that reaches
 * either direction would quietly undo that.
 */
#include "Wire.h"

#include <cstdio>
#include <cstdlib>

namespace spectro {
namespace wire {

std::string encode_columns(const unsigned char* cols, int nCols, int bands)
{
  if (!cols || nCols <= 0 || bands <= 0)
    return std::string();

  static const char* const kHex = "0123456789ABCDEF";

  std::string out;
  char head[32];
  snprintf(head, sizeof head, "%d:%d:", nCols, bands);
  out += head;

  /* Reserved rather than grown: this runs once per editor frame, and the size
   * is known exactly before the first character is written. */
  const int bytes = nCols * bands;
  out.reserve(out.size() + size_t(bytes) * 2);

  for (int i = 0; i < bytes; i++)
  {
    const unsigned char b = cols[i];
    out += kHex[(b >> 4) & 0xF];
    out += kHex[b & 0xF];
  }

  return out;
}

std::string encode_axis(const float* hz, int n)
{
  if (!hz || n <= 0)
    return std::string();

  std::string out;
  out.reserve(size_t(n) * 8);

  char num[24];
  for (int i = 0; i < n; i++)
  {
    snprintf(num, sizeof num, "%s%.1f", i ? "," : "", double(hz[i]));
    out += num;
  }

  return out;
}

std::string encode_sync(double ppq, double bpm, int num, int denom, bool running,
                        double ppqPerCol)
{
  char buf[128];
  snprintf(buf, sizeof buf, "%.6f:%.4f:%d:%d:%d:%.8f",
           ppq, bpm, num, denom, running ? 1 : 0, ppqPerCol);
  return std::string(buf);
}

double advance_beats(double beats, int frames, double bpm, double sampleRate)
{
  if (frames <= 0 || !(bpm > 0.0) || !(sampleRate > 0.0))
    return beats;

  return beats + double(frames) * (bpm / 60.0) / sampleRate;
}

bool parse_range(const std::string& arg, float& lo, float& hi)
{
  /* THE COLON IS THE WHOLE VALIDATION, and that is the pre-existing contract
   * rather than an oversight worth quietly fixing here. An empty half gives
   * atof 0, and spectro_set_range refuses anything undrawable -- so "0 Hz to
   * 0 Hz" reaches the engine and the range is left alone, which is what the
   * editor is told when the axis comes back unchanged.
   *
   * Moving that judgement forward into this function would be a behaviour
   * change wearing a refactor's clothes. If it should be stricter, that is a
   * separate commit with the engine's refusal tested first. */
  const size_t sep = arg.find(':');
  if (sep == std::string::npos)
    return false;

  lo = float(atof(arg.substr(0, sep).c_str()));
  hi = float(atof(arg.substr(sep + 1).c_str()));
  return true;
}

} // namespace wire
} // namespace spectro
