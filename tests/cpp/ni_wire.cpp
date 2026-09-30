/*
 * ni::wire and ni::Scope: what every shell does to a buffer or a message on its
 * way between host, engine and editor -- the places being wrong is SILENT.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * A dropped edit looks like not clicking, a wrong byte like a plausible
 * waveform, a comma for a point like a zero; none of them errors.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ni/Scope.h"
#include "ni/Wire.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ni::wire;

/* --------------------------------------------------------------- the split */

TEST_CASE("a pair splits at the first colon, and keeps the second")
{
  std::string a, b;
  REQUIRE(split_pair("7:2", a, b));
  CHECK(a == "7");
  CHECK(b == "2");
  /* A typed readout may hold another colon; splitting at the last would put
   * part of the text in the index. */
  REQUIRE(split_pair("13:-24.0 dB : loud", a, b));
  CHECK(a == "13");
  CHECK(b == "-24.0 dB : loud");
}

TEST_CASE("a payload with no colon is dropped, and touches neither half")
{
  std::string a = "keep", b = "these";
  CHECK_FALSE(split_pair("12", a, b));
  CHECK_FALSE(split_pair("", a, b));
  CHECK(a == "keep");
  CHECK(b == "these");
}

TEST_CASE("an empty half is still a split")
{
  std::string a, b;
  REQUIRE(split_pair("5:", a, b));
  CHECK(a == "5");
  CHECK(b.empty());
  REQUIRE(split_pair(":5", a, b));
  CHECK(a.empty());
  CHECK(b == "5");
}

/* ---------------------------------------------------------- the quantisers */

TEST_CASE("a waveform bound spans the byte, with silence at mid-scale")
{
  CHECK(int(encode_bipolar(-1.f)) == 0);
  CHECK(int(encode_bipolar(1.f)) == 255);
  /* 127.5 rounds to 128: one low and the whole trace sits a pixel down, which
   * reads as a DC offset in the audio. */
  CHECK(int(encode_bipolar(0.f)) == 128);
}

TEST_CASE("past full scale is pinned, never wrapped into an inversion")
{
  CHECK(int(encode_bipolar(2.f)) == 255);
  CHECK(int(encode_bipolar(-2.f)) == 0);
  CHECK(int(encode_bipolar(1e9f)) == 255);
  CHECK(int(encode_bipolar(-1e9f)) == 0);
}

TEST_CASE("a non-finite bound is silence and a non-finite gain is none")
{
  /* A NaN cast to int is undefined; fmin/fmax pass NaN through, so the
   * isfinite test is the guard. */
  CHECK(int(encode_bipolar(NAN)) == 128);
  CHECK(int(encode_bipolar(INFINITY)) == 128);
  CHECK(int(encode_bipolar(-INFINITY)) == 128);
  CHECK(int(encode_unipolar(NAN)) == 0);
  CHECK(int(encode_unipolar(INFINITY)) == 0);
}

TEST_CASE("a gain spends the whole byte on 0..1")
{
  CHECK(int(encode_unipolar(0.f)) == 0);
  CHECK(int(encode_unipolar(1.f)) == 255);
  CHECK(int(encode_unipolar(0.5f)) == 128);
  CHECK(int(encode_unipolar(-1.f)) == 0);
  CHECK(int(encode_unipolar(2.f)) == 255);
}

TEST_CASE("both quantisers are monotone")
{
  int lastB = -1, lastU = -1;
  for (int i = 0; i <= 2000; i++)
  {
    const float v = float(i) / 2000.f;
    const int b = encode_bipolar(2.f * v - 1.f), u = encode_unipolar(v);
    CHECK(b >= lastB);
    CHECK(u >= lastU);
    lastB = b;
    lastU = u;
  }
}

TEST_CASE("framed_size accounts for base64's extra third")
{
  CHECK(framed_size(0) == 32);
  CHECK(framed_size(3) == 36);
  CHECK(framed_size(3000) == 4032);
}

TEST_CASE("hex is two upper-case digits a byte")
{
  char buf[4] = {};
  CHECK(put_hex(buf, 0xA9) == buf + 2);
  CHECK(std::string(buf) == "A9");
  put_hex(buf, 0x0F);
  CHECK(std::string(buf) == "0F");
}

/* ------------------------------------------------------------- the height */

TEST_CASE("an editor height is honoured only when a real editor could want it")
{
  CHECK(clamp_editor_height(700) == 700);
  CHECK(clamp_editor_height(101) == 101);
  CHECK(clamp_editor_height(3999) == 3999);
  CHECK(clamp_editor_height(100) == 0);
  CHECK(clamp_editor_height(4000) == 0);
  CHECK(clamp_editor_height(0) == 0);
  CHECK(clamp_editor_height(-700) == 0);
  CHECK(clamp_editor_height(40000) == 0);
}

/* ---------------------------------------------------------- the transport */

TEST_CASE("a chunk advances the transport by its own duration")
{
  CHECK(advance_beats(0.0, 24000, 120.0, 48000.0) == doctest::Approx(1.0));
  CHECK(advance_beats(3.0, 12000, 120.0, 48000.0) == doctest::Approx(3.5));
  const double whole = advance_beats(0.0, 2048, 174.0, 44100.0);
  double chunked = 0.0;
  for (int i = 0; i < 4; i++)
    chunked = advance_beats(chunked, 512, 174.0, 44100.0);
  CHECK(chunked == doctest::Approx(whole));
}

TEST_CASE("a degenerate advance leaves the position alone")
{
  CHECK(advance_beats(5.0, 512, 120.0, 0.0) == 5.0);
  CHECK(advance_beats(5.0, 512, 120.0, -48000.0) == 5.0);
  CHECK(advance_beats(5.0, 512, 0.0, 48000.0) == 5.0);
  CHECK(advance_beats(5.0, 512, NAN, 48000.0) == 5.0);
  CHECK(advance_beats(5.0, 0, 120.0, 48000.0) == 5.0);
  CHECK(advance_beats(5.0, -1, 120.0, 48000.0) == 5.0);
}

TEST_CASE("the host's clock: stopped is no position, and a lie is stopped")
{
  Transport t = host_transport(true, 128.0, 16.5);
  CHECK(t.running == 1);
  CHECK(t.beats == 16.5);
  CHECK(t.bpm == 128.0f);
  t = host_transport(false, 128.0, 16.5);
  CHECK(t.running == 0);
  CHECK(t.beats == -1.0);
  /* Running at -1 is a host that has not filled the position in. */
  t = host_transport(true, 0.0, -1.0);
  CHECK(t.running == 0);
  CHECK(t.beats == -1.0);
  CHECK(t.bpm == 120.0f);
}

/* -------------------------------------------------------------- the blocks */

TEST_CASE("a long block is processed in chunks of what was reserved")
{
  std::vector<std::pair<int, int>> got;
  for_each_chunk(1100, 512, [&](int off, int n) { got.emplace_back(off, n); });
  REQUIRE(got.size() == 3);
  CHECK(got[0] == std::make_pair(0, 512));
  CHECK(got[1] == std::make_pair(512, 512));
  CHECK(got[2] == std::make_pair(1024, 76));
  got.clear();
  for_each_chunk(100, 0, [&](int off, int n) { got.emplace_back(off, n); });
  CHECK(got.empty());
}

TEST_CASE("float and back is the block, sample for sample")
{
  const double in[3] = {0.25, -1.0, 0.125};
  float f[3];
  double out[3];
  to_float(in, f, 3);
  from_float(f, out, 3);
  CHECK(out[0] == 0.25);
  CHECK(out[1] == -1.0);
  CHECK(out[2] == 0.125);
}

TEST_CASE("the passthrough is bit for bit, and a mono input feeds both sides")
{
  double l[4] = {1, -0.0, 3e-310, 4}, r[4] = {5, 6, 7, 8};
  double o0[4] = {}, o1[4] = {};
  double* in[2] = {l, r};
  double* out[2] = {o0, o1};
  passthrough(in, 2, out, 2, 4);
  CHECK(std::memcmp(o0, l, sizeof l) == 0);
  CHECK(std::memcmp(o1, r, sizeof r) == 0);
  double m0[4] = {}, m1[4] = {};
  double* mono[2] = {m0, m1};
  passthrough(in, 1, mono, 2, 4);
  CHECK(std::memcmp(m1, l, sizeof l) == 0);
  /* In place is left alone. */
  double* same[2] = {l, r};
  passthrough(in, 2, same, 2, 4);
  CHECK(l[3] == 4);
}

/* ------------------------------------------------------------- the numbers */

static std::string fixed(double v, int d)
{
  std::string s;
  append_fixed(s, v, d);
  return s;
}

TEST_CASE("fixed decimals read as printf's in the C locale")
{
  const double values[] = {0.0, -0.0, 0.5, 1.0, -1.0, 0.125, 0.375, 2.675, 123.456789,
                           -60.0, 12.0, 0.0004999, 0.00005, 1e-9, 12345678.12345678,
                           0.9999996, -0.0000001, 20.6, 19999.95};
  for (double v : values)
    for (int d : {0, 1, 2, 3, 4, 6, 8})
    {
      char want[64];
      std::snprintf(want, sizeof want, "%.*f", d, v);
      CAPTURE(v);
      CAPTURE(d);
      CHECK(fixed(v, d) == want);
    }
  CHECK(fixed(NAN, 2) == "nan");
  CHECK(fixed(INFINITY, 2) == "inf");
  CHECK(fixed(-INFINITY, 2) == "-inf");
}

TEST_CASE("a buffer too small for the number is refused")
{
  char buf[4];
  CHECK(format_fixed(buf, 4, 1.5, 2) == -1);
  CHECK(format_fixed(buf, 5, 1.5, 2) == 4);
  CHECK(std::string(buf) == "1.50");
}

TEST_CASE("a decimal drops its trailing zeros")
{
  std::string s;
  append_decimal(s, 0.5, 9);
  CHECK(s == "0.5");
  s.clear();
  append_decimal(s, 1.0, 9);
  CHECK(s == "1");
  s.clear();
  append_decimal(s, 7.0 / 12.0, 9);
  CHECK(s == "0.583333333");
  s.clear();
  append_decimal(s, -1e-12, 9);
  CHECK(s == "0");
  s.clear();
  append_int(s, -42);
  CHECK(s == "-42");
}

TEST_CASE("a number reads as atof reads it")
{
  CHECK(parse_number("20") == 20.0);
  CHECK(parse_number("  -60.00:12") == -60.0);
  CHECK(parse_number("0.583333333") == 0.583333333);
  CHECK(parse_number("+1.5e3xyz") == 1500.0);
  CHECK(parse_number("2.5E-2") == 0.025);
  CHECK(parse_number(".5") == 0.5);
  CHECK(parse_number("7.") == 7.0);
  CHECK(parse_number("") == 0.0);
  CHECK(parse_number("abc") == 0.0);
  CHECK(parse_number("-") == 0.0);
  CHECK(parse_number("1e") == 1.0);
  CHECK(parse_number("0.1") == 0.1);
  CHECK(parse_number("123456789012345678901234") == doctest::Approx(1.2345678901234568e23));
}

TEST_CASE("every number written reads back as the same number")
{
  for (int i = -2000; i <= 2000; i++)
  {
    const double v = double(i) * 0.013;
    CHECK(parse_number(fixed(v, 3)) == std::atof(fixed(v, 3).c_str()));
  }
}

TEST_CASE("an int is a whole int and nothing else")
{
  int v = 99;
  CHECK(parse_int("12", v));
  CHECK(v == 12);
  CHECK(parse_int("-3", v));
  CHECK(v == -3);
  v = 99;
  CHECK_FALSE(parse_int("", v));
  CHECK_FALSE(parse_int("12a", v));
  CHECK_FALSE(parse_int(" 12", v));
  CHECK_FALSE(parse_int("1.5", v));
  CHECK_FALSE(parse_int("99999999999", v));
  CHECK(v == 99);
}

/*
 * THE REASON THE NUMBERS ARE HAND-WRITTEN. A host may set a comma-decimal
 * LC_NUMERIC for its own UI, and printf and atof follow it: "%.3f" becomes
 * "0,734" and atof("0.5") becomes 0. Nothing on the wire may.
 */
TEST_CASE("a comma-decimal locale changes nothing on the wire")
{
  const char* before = std::setlocale(LC_NUMERIC, nullptr);
  const std::string saved = before ? before : "C";
  const char* got = std::setlocale(LC_NUMERIC, "de_DE.UTF-8");
  if (!got)
    got = std::setlocale(LC_NUMERIC, "de_DE");
  REQUIRE_MESSAGE(got != nullptr, "no German locale to test under");

  char probe[16];
  std::snprintf(probe, sizeof probe, "%.1f", 0.5);
  REQUIRE_MESSAGE(std::string(probe) == "0,5", "the locale did not take: " << probe);

  CHECK(fixed(0.734, 3) == "0.734");
  CHECK(fixed(-60.0, 2) == "-60.00");
  CHECK(parse_number("0.5") == 0.5);
  CHECK(parse_number("-60.00") == -60.0);
  std::string s;
  append_decimal(s, 0.25, 9);
  CHECK(s == "0.25");

  std::setlocale(LC_NUMERIC, saved.c_str());
}

/* --------------------------------------------------------------- the scope */

TEST_CASE("a column holds the bounds of the samples filed under it")
{
  static ni::Scope<4> scope;
  scope.Clear();
  const float dry[6] = {0.1f, -0.5f, 0.3f, 0.9f, -0.9f, 0.0f};
  const float wet[6] = {0.0f, -0.2f, 0.1f, 0.4f, -0.4f, 0.0f};
  const float gain[6] = {1.f, 0.5f, 0.8f, 0.2f, 0.9f, 1.f};
  const float sweep[6] = {0.0f, 0.1f, 0.2f, 0.6f, 0.7f, 1.0f};
  scope.Push(dry, wet, gain, sweep, 6);

  char col[16] = {};
  scope.PutColumn(col, 0, true);
  /* Column 0: dry -0.5..0.3, wet -0.2..0.1, the gain's minimum 0.5. */
  std::string want;
  for (float v : {-0.5f, 0.3f, -0.2f, 0.1f})
  {
    char h[3] = {};
    put_hex(h, encode_bipolar(v));
    want += h;
  }
  char g[3] = {};
  put_hex(g, encode_unipolar(0.5f));
  want += g;
  CHECK(std::string(col) == want);

  CHECK(scope.Seen(0));
  CHECK_FALSE(scope.Seen(1));
  CHECK(scope.Seen(2));
  /* 1.0 is the end of the sweep, not one past the last column. */
  CHECK(scope.Seen(3));
  CHECK(scope.Head() == 3);
}

TEST_CASE("retired columns are unseen until the sweep writes them again")
{
  static ni::Scope<2> scope;
  scope.Clear();
  const float x[2] = {0.5f, 0.5f}, sweep[2] = {0.0f, 0.9f};
  scope.Push(x, x, nullptr, sweep, 2);
  CHECK(scope.Seen(0));
  scope.Retire();
  CHECK_FALSE(scope.Seen(0));
  CHECK_FALSE(scope.Seen(1));
  scope.Push(x, x, nullptr, sweep, 1);
  /* The column that just ended is published at the end of the block. */
  CHECK(scope.Seen(0));
}
