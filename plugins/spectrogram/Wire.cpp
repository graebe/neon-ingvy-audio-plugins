/*
 * The Spectrogram's wire format. See Wire.h.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * No iPlug2 and no engine, so tests/cpp/spectro_wire.cpp links it alone.
 */
#include "Wire.h"
#include "ni/Wire.h"

#include <cstdio>
#include <cstdlib>

namespace spectro {
namespace wire {

std::string encode_columns(const unsigned char* cols, int nCols, int bands, int ch)
{
  if (!cols || nCols <= 0 || bands <= 0 || ch < 0)
    return std::string();

  std::string out;
  char head[32];
  snprintf(head, sizeof head, "%d:%d:%d:", ch, nCols, bands);
  out += head;

  /* Reserved rather than grown: this runs once per editor frame, and the size
   * is known exactly before the first character is written. */
  const int bytes = nCols * bands;
  out.reserve(out.size() + size_t(bytes));
  out.append(reinterpret_cast<const char*>(cols), size_t(bytes));

  return out;
}

std::string encode_axis(const float* hz, int n)
{
  if (!hz || n <= 0)
    return std::string();

  std::string out;
  out.reserve(size_t(n) * 8);
  for (int i = 0; i < n; i++)
  {
    if (i)
      out += ',';
    ni::wire::append_fixed(out, double(hz[i]), 1);
  }
  return out;
}

std::string encode_sync(double ppq, double bpm, int num, int denom, bool running,
                        double ppqPerCol, int sampleRate)
{
  std::string out;
  out.reserve(96);
  ni::wire::append_fixed(out, ppq, 6);
  out += ':';
  ni::wire::append_fixed(out, bpm, 4);
  out += ':';
  ni::wire::append_int(out, num);
  out += ':';
  ni::wire::append_int(out, denom);
  out += running ? ":1:" : ":0:";
  ni::wire::append_fixed(out, ppqPerCol, 8);
  out += ':';
  ni::wire::append_int(out, sampleRate);
  return out;
}

std::string encode_state(float fMin, float fMax, const std::vector<int>& view,
                         int cmpA, int cmpB, bool clashOn,
                         float floorDb, float balanceDb)
{
  std::string out;
  out.reserve(64);
  ni::wire::append_fixed(out, fMin, 2);
  out += ':';
  ni::wire::append_fixed(out, fMax, 2);
  out += ':';
  if (view.empty())
    out += '0';
  for (size_t i = 0; i < view.size(); i++)
  {
    if (i)
      out += ',';
    ni::wire::append_int(out, view[i]);
  }
  out += ':';
  ni::wire::append_int(out, cmpA);
  out += ':';
  ni::wire::append_int(out, cmpB);
  out += clashOn ? ":1:" : ":0:";
  ni::wire::append_fixed(out, floorDb, 2);
  out += ':';
  ni::wire::append_fixed(out, balanceDb, 2);
  return out;
}

bool parse_range(const std::string& arg, float& lo, float& hi)
{
  /* THE COLON IS THE WHOLE VALIDATION. An empty half reads as 0, and the
   * engine refuses anything undrawable -- so "0 Hz to 0 Hz" leaves the range
   * alone, which the editor sees when the axis comes back unchanged. */
  const size_t sep = arg.find(':');
  if (sep == std::string::npos)
    return false;

  lo = float(ni::wire::parse_number(std::string_view(arg).substr(0, sep)));
  hi = float(ni::wire::parse_number(std::string_view(arg).substr(sep + 1)));
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
