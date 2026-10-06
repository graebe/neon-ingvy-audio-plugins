// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::wire. See Wire.h.
 */
#include "ni/Wire.h"

#include <charconv>
#include <cmath>
#include <cstdint>

namespace ni {
namespace wire {

bool split_pair(std::string_view arg, std::string& a, std::string& b)
{
  const size_t colon = arg.find(':');
  if (colon == std::string_view::npos)
    return false;
  a.assign(arg.substr(0, colon));
  b.assign(arg.substr(colon + 1));
  return true;
}

/* isfinite FIRST: a NaN passes through fmin/fmax rather than being pinned. */
unsigned char encode_bipolar(float v)
{
  const float c = std::isfinite(v) ? std::fmin(1.f, std::fmax(-1.f, v)) : 0.f;
  return (unsigned char) int((c + 1.f) * 127.5f + 0.5f);
}

unsigned char encode_unipolar(float v)
{
  const float c = std::isfinite(v) ? std::fmin(1.f, std::fmax(0.f, v)) : 0.f;
  return (unsigned char) int(c * 255.f + 0.5f);
}

/* Exclusive at both ends: 100 is below any editor with a control in it, 4000
 * past the tallest display. Both mean "the editor miscalculated". */
int clamp_editor_height(int requested)
{
  return (requested > 100 && requested < 4000) ? requested : 0;
}

double advance_beats(double beats, int frames, double bpm, double sampleRate)
{
  if (frames <= 0 || !(bpm > 0.0) || !(sampleRate > 0.0))
    return beats;
  return beats + double(frames) * (bpm / 60.0) / sampleRate;
}

/* ------------------------------------------------------------- numbers */

static constexpr uint64_t kPow10[] = {
  1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull,
  100000000ull, 1000000000ull,
};

int format_fixed(char* buf, int cap, double v, int decimals)
{
  if (!buf || cap <= 0)
    return -1;
  decimals = decimals < 0 ? 0 : decimals > 9 ? 9 : decimals;

  char tmp[64];
  char* p = tmp;
  if (std::signbit(v) && !std::isnan(v))
    *p++ = '-';
  const double a = std::fabs(v);

  if (std::isnan(v) || std::isinf(v))
  {
    const char* word = std::isnan(v) ? "nan" : "inf";
    std::memcpy(p, word, 3);
    p += 3;
  }
  else
  {
    /* The fraction a - trunc(a) is exact, and so is the error of scaling it
     * (fma gives it), so the digits are rounded from the exact value: the
     * scaled product sits on a half only when the true value is within half
     * an ulp of it, and then the error's sign decides; an exact half goes to
     * even. That is printf's rounding of the binary value, digit for digit. */
    double ip = std::trunc(a);
    const double scale = double(kPow10[decimals]);
    const double f = a - ip;
    const double prod = f * scale;
    const double err = std::fma(f, scale, -prod);
    const double lo = std::floor(prod);
    const double half = prod - lo;
    double frac = lo;
    if (half > 0.5 || (half == 0.5 && (err > 0.0 || (err == 0.0 && std::fmod(lo, 2.0) != 0.0))))
      frac = lo + 1.0;
    if (frac >= scale)
    {
      ip += 1.0;
      frac -= scale;
    }
    const uint64_t whole = ip < 1.8e19 ? uint64_t(ip) : UINT64_MAX;
    p = std::to_chars(p, tmp + sizeof tmp, whole).ptr;
    if (decimals > 0)
    {
      *p++ = '.';
      char digits[16];
      char* end = std::to_chars(digits, digits + sizeof digits, uint64_t(frac)).ptr;
      const int n = int(end - digits);
      for (int i = n; i < decimals; i++)
        *p++ = '0';
      std::memcpy(p, digits, size_t(n));
      p += n;
    }
  }

  const int len = int(p - tmp);
  if (len >= cap)
    return -1;
  std::memcpy(buf, tmp, size_t(len));
  buf[len] = '\0';
  return len;
}

void append_fixed(std::string& out, double v, int decimals)
{
  char buf[64];
  const int n = format_fixed(buf, int(sizeof buf), v, decimals);
  if (n > 0)
    out.append(buf, size_t(n));
}

void append_decimal(std::string& out, double v, int maxDecimals)
{
  char buf[64];
  int n = format_fixed(buf, int(sizeof buf), v, maxDecimals);
  if (n <= 0)
    return;
  if (std::memchr(buf, '.', size_t(n)))
  {
    while (n > 0 && buf[n - 1] == '0')
      n--;
    if (n > 0 && buf[n - 1] == '.')
      n--;
  }
  /* "-0" is a zero that happened to be negative; say 0. */
  if (n == 2 && buf[0] == '-' && buf[1] == '0')
  {
    buf[0] = '0';
    n = 1;
  }
  out.append(buf, size_t(n));
}

void append_int(std::string& out, long long v)
{
  char buf[24];
  const char* end = std::to_chars(buf, buf + sizeof buf, v).ptr;
  out.append(buf, size_t(end - buf));
}

/*
 * The mantissa's first 19 significant digits in an integer, scaled by an
 * exact power of ten: correctly rounded while the digits fit 2^53 and the
 * power is within 10^22, which covers every number an editor or a saved
 * chunk sends; beyond that, within a unit in the last place.
 */
double parse_number(std::string_view s)
{
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r')))
    i++;
  bool neg = false;
  if (i < s.size() && (s[i] == '+' || s[i] == '-'))
    neg = s[i++] == '-';

  uint64_t mant = 0;
  int digits = 0, exp10 = 0;
  bool any = false;
  for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++, any = true)
  {
    if (digits < 19) { mant = mant * 10 + uint64_t(s[i] - '0'); if (mant) digits++; }
    else exp10++;
  }
  if (i < s.size() && s[i] == '.')
  {
    for (i++; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++, any = true)
    {
      if (digits < 19) { mant = mant * 10 + uint64_t(s[i] - '0'); if (mant) digits++; exp10--; }
    }
  }
  if (!any)
    return 0.0;
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
  {
    size_t j = i + 1;
    bool eneg = false;
    if (j < s.size() && (s[j] == '+' || s[j] == '-'))
      eneg = s[j++] == '-';
    if (j < s.size() && s[j] >= '0' && s[j] <= '9')
    {
      int e = 0;
      for (; j < s.size() && s[j] >= '0' && s[j] <= '9'; j++)
        e = e < 10000 ? e * 10 + (s[j] - '0') : e;
      exp10 += eneg ? -e : e;
    }
  }

  static constexpr double kExact[] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
  };
  double v = double(mant);
  if (exp10 >= -22 && exp10 <= 22)
    v = exp10 < 0 ? v / kExact[-exp10] : v * kExact[exp10];
  else
    v *= std::pow(10.0, double(exp10));
  return neg ? -v : v;
}

bool parse_int(std::string_view s, int& out)
{
  int v = 0;
  const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  if (r.ec != std::errc() || r.ptr != s.data() + s.size() || s.empty())
    return false;
  out = v;
  return true;
}

/* -------------------------------------------------------------- blocks */

Transport host_transport(bool running, double tempo, double ppq)
{
  Transport t;
  t.running = running ? 1 : 0;
  t.bpm = float(tempo > 0.0 ? tempo : 120.0);
  t.beats = running ? ppq : -1.0;
  if (t.running && t.beats < 0.0)
  {
    t.running = 0;
    t.beats = -1.0;
  }
  return t;
}

} // namespace wire
} // namespace ni
