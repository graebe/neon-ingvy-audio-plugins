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

std::string encode_columns(const unsigned char* cols, int nCols, int bands, int ch)
{
  if (!cols || nCols <= 0 || bands <= 0 || ch < 0)
    return std::string();

  static const char* const kHex = "0123456789ABCDEF";

  std::string out;
  char head[32];
  snprintf(head, sizeof head, "%d:%d:%d:", ch, nCols, bands);
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
                        double ppqPerCol, int sampleRate)
{
  char buf[160];
  snprintf(buf, sizeof buf, "%.6f:%.4f:%d:%d:%d:%.8f:%d",
           ppq, bpm, num, denom, running ? 1 : 0, ppqPerCol, sampleRate);
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

void parse_slots(const std::string& arg, std::vector<unsigned int>& out)
{
  /*
   * "<slot>,<slot>,..." -- and anything unreadable is SKIPPED rather than
   * failing the whole list. A receiver asked for "2,x,5" should listen to 2 and
   * 5: the alternative is a picker that silently does nothing because one field
   * was mangled in transit, which is the harder fault to see.
   *
   * The engine refuses a slot outside 1..abus_max_slot() and caps the count, so
   * nothing here needs to know either bound.
   */
  size_t i = 0;
  while (i < arg.size())
  {
    size_t j = arg.find(',', i);
    if (j == std::string::npos)
      j = arg.size();

    const std::string field = arg.substr(i, j - i);
    if (!field.empty())
    {
      const long v = strtol(field.c_str(), nullptr, 10);
      if (v > 0 && v < 1000)
        out.push_back(static_cast<unsigned int>(v));
    }
    i = j + 1;
  }
}

bool parse_compare(const std::string& arg, int& a, int& b, bool& on)
{
  const size_t first = arg.find(':');
  if (first == std::string::npos)
    return false;
  const size_t second = arg.find(':', first + 1);
  if (second == std::string::npos)
    return false;

  a = int(strtol(arg.substr(0, first).c_str(), nullptr, 10));
  b = int(strtol(arg.substr(first + 1, second - first - 1).c_str(), nullptr, 10));
  on = arg.compare(second + 1, std::string::npos, "0") != 0;

  /* A channel index is never negative. Anything past what is actually open is
   * the RECEIVER's to refuse -- it is the only side that knows how many buses
   * opened, and a slot that failed to open shifts every index after it. */
  if (a < 0 || b < 0)
    return false;
  return true;
}

void parse_channels(const std::string& arg, std::vector<int>& out)
{
  size_t i = 0;
  while (i < arg.size())
  {
    size_t j = arg.find(',', i);
    if (j == std::string::npos)
      j = arg.size();

    const std::string field = arg.substr(i, j - i);
    if (!field.empty())
    {
      /* strtol reports "not a number" only through endptr, and an empty or
       * alphabetic field would otherwise arrive as a perfectly good channel 0.
       * That matters here in a way it does not for slots, where 0 is refused
       * anyway. */
      char* end = nullptr;
      const long v = strtol(field.c_str(), &end, 10);
      if (end != field.c_str() && v >= 0 && v < 64)
        out.push_back(int(v));
    }
    i = j + 1;
  }
}

} // namespace wire
} // namespace spectro
