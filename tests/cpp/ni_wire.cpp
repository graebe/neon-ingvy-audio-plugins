// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::wire and ni::Scope: what every processor does to a block, a clock or a
 * number on its way between host and engine -- the places being wrong is
 * SILENT.
 *
 * A wrong byte looks like a plausible waveform, a comma for a point like a
 * zero, a stalled clock like a pause; none of them errors.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ni/Scope.h"
#include "ni/Wire.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace ni::wire;

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
  /* No musical position -- NaN from the JUCE shell's readClock -- and an
   * infinity are no position either, never one the engine chases. */
  for (double ppq : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
  {
    t = host_transport(true, 128.0, ppq);
    CHECK(t.running == 0);
    CHECK(t.beats == -1.0);
  }
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

/* ---------------------------------------------------------------- the loop */

TEST_CASE("a loop is the host's only when it can be played")
{
  CHECK(host_loop(true, 4.0, 8.0).on);
  CHECK(host_loop(true, 0.0, 0.25).on);
  CHECK_FALSE(host_loop(false, 4.0, 8.0).on);
  /* Looping with no points, backwards, empty, or nowhere. */
  CHECK_FALSE(host_loop(true, 0.0, 0.0).on);
  CHECK_FALSE(host_loop(true, 8.0, 4.0).on);
  CHECK_FALSE(host_loop(true, 4.0, 4.0 + loop_edge / 2).on);
  CHECK_FALSE(host_loop(true, -1.0, 4.0).on);
  CHECK_FALSE(host_loop(true, std::numeric_limits<double>::quiet_NaN(), 4.0).on);
  CHECK_FALSE(host_loop(true, 4.0, std::numeric_limits<double>::infinity()).on);
}

/* The spans of one block, and where each starts. */
struct Span
{
  int off, n;
  double beats;
};

static std::vector<Span> spans(int frames, int cap, Transport& t, Loop loop, double rate)
{
  std::vector<Span> out;
  for_each_span(frames, cap, t, loop, rate, [&](int off, int n) { out.push_back({off, n, t.beats}); });
  return out;
}

TEST_CASE("a block is split where the loop's end falls exactly on a frame")
{
  /* 120 BPM at 32768 Hz: a frame is 2^-14 beats exactly, and the end is
   * frame 100 of the block, to the bit. */
  const double rate = 32768.0, frame = 1.0 / 16384.0;
  Transport t{1, 8.0 - 100 * frame, 120.0f};
  const auto got = spans(512, 512, t, host_loop(true, 4.0, 8.0), rate);
  REQUIRE(got.size() == 2);
  CHECK(got[0].off == 0);
  CHECK(got[0].n == 100);
  CHECK(got[0].beats == 8.0 - 100 * frame);
  CHECK(got[1].off == 100);
  CHECK(got[1].n == 412);
  CHECK(got[1].beats == 4.0);
  CHECK(t.beats == 4.0 + 412 * frame);

  /* An end a hair either side of the frame, as a host's rounding leaves it,
   * is the same frame. */
  for (const double hair : {-1e-12, 1e-12})
  {
    Transport h{1, 8.0 - 100 * frame + hair, 120.0f};
    const auto split = spans(512, 512, h, host_loop(true, 4.0, 8.0), rate);
    REQUIRE(split.size() == 2);
    CHECK(split[0].n == 100);
    CHECK(split[1].beats == doctest::Approx(4.0).epsilon(1e-12));
  }
}

TEST_CASE("a loop's end inside a chunk splits it; no loop, a stopped clock or a position past the end does not")
{
  /* 120 BPM at 48 kHz, from 7.99: the end is 240 frames in. */
  Transport t{1, 7.99, 120.0f};
  auto got = spans(1100, 512, t, host_loop(true, 4.0, 8.0), 48000.0);
  REQUIRE(got.size() == 4);
  CHECK((got[0].off == 0 && got[0].n == 240));
  CHECK((got[1].off == 240 && got[1].n == 272));
  CHECK(got[1].beats == doctest::Approx(4.0));
  CHECK((got[2].off == 512 && got[2].n == 512));
  CHECK((got[3].off == 1024 && got[3].n == 76));
  CHECK(t.beats == doctest::Approx(4.0 + 860 / 24000.0));

  for (const Loop loop : {Loop{}, host_loop(true, 1.0, 2.0)})
  {
    Transport past{1, 7.99, 120.0f};
    CHECK(spans(1100, 512, past, loop, 48000.0).size() == 3);
    CHECK(past.beats == doctest::Approx(7.99 + 1100 / 24000.0));
  }
  Transport stopped{0, -1.0, 120.0f};
  CHECK(spans(1100, 512, stopped, host_loop(true, 0.0, 0.001), 48000.0).size() == 3);
  CHECK(stopped.beats == -1.0);
}

TEST_CASE("a loop shorter than a frame wraps every frame, and the block still ends")
{
  /* A frame is 1/24000 beat; the loop a third of one. */
  const Loop loop = host_loop(true, 2.0, 2.0 + 1.0 / 72000.0);
  Transport t{1, 2.0, 120.0f};
  const auto got = spans(64, 512, t, loop, 48000.0);
  REQUIRE(got.size() == 64);
  for (const auto& s : got)
  {
    CHECK(s.n == 1);
    CHECK(s.beats >= loop.start);
    CHECK(s.beats < loop.end);
  }
}

TEST_CASE("at every tempo a host plays, each span starts where the host would say the position is")
{
  /* The host's own position for frame k of a block that starts at p0: on
   * past p0 until the end, then round the loop. */
  const auto host = [](double p0, long k, double perFrame, Loop loop) {
    const double u = p0 + double(k) * perFrame;
    return u < loop.end - loop_edge ? u : loop.start + std::fmod(u - loop.end, loop.end - loop.start);
  };
  const Loop loop = host_loop(true, 3.0, 4.5);
  for (int bpm = 1; bpm <= 999; ++bpm)
    for (const double rate : {44100.0, 96000.0})
    {
      CAPTURE(bpm);
      CAPTURE(rate);
      const double perFrame = bpm / 60.0 / rate;
      const double p0 = 4.5 - 300.25 * perFrame;
      Transport t{1, p0, float(bpm)};
      const auto got = spans(8192, 1024, t, loop, rate);
      int frames = 0;
      bool wrapped = false;
      for (const auto& s : got)
      {
        CHECK(s.off == frames);
        CHECK(s.beats == doctest::Approx(host(p0, s.off, perFrame, loop)).epsilon(1e-9));
        wrapped = wrapped || s.off == 301;
        frames += s.n;
      }
      CHECK(frames == 8192);
      CHECK(wrapped);
      CHECK(t.beats == doctest::Approx(host(p0, 8192, perFrame, loop)).epsilon(1e-9));
    }
}

TEST_CASE("a span's timeline sample is the block's plus its offset, and after a wrap the loop start's")
{
  /* 120 BPM at 48 kHz, a beat 24000 samples: from 7.99 beats at timeline
   * sample 191760, the loop's end is 240 frames in, and its start, 4 beats,
   * is sample 96000. */
  const Loop loop = host_loop(true, 4.0, 8.0);
  const Transport block{1, 7.99, 120.0f};
  Transport t = block;
  const std::int64_t blockSample = 191760;
  std::vector<std::int64_t> stamps;
  for_each_span(1100, 512, t, loop, 48000.0,
                [&](int off, int) { stamps.push_back(span_timeline(blockSample, block, off, t.beats, 48000.0)); });
  REQUIRE(stamps.size() == 4);
  CHECK(stamps[0] == blockSample);
  CHECK(stamps[1] == 96000);           // the loop's start, on the frame after its end
  CHECK(stamps[2] == 96000 + 272);     // then on from there
  CHECK(stamps[3] == 96000 + 784);

  /* No running transport, tempo or rate: the block's sample plus the offset. */
  for (const Transport none : {Transport{0, 7.99, 120.0f}, Transport{1, 7.99, 0.0f}})
    CHECK(span_timeline(blockSample, none, 300, 4.0, 48000.0) == blockSample + 300);
  CHECK(span_timeline(blockSample, block, 300, 4.0, 0.0) == blockSample + 300);

  /* Before any wrap, exactly the offset at every tempo a host plays. */
  for (int bpm = 1; bpm <= 999; bpm += 7)
  {
    Transport b{1, 1234.5678, float(bpm)};
    Transport run = b;
    for_each_span(4096, 1024, run, Loop{}, 44100.0, [&](int off, int) {
      CHECK(span_timeline(98765, b, off, run.beats, 44100.0) == 98765 + off);
    });
  }
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

TEST_CASE("text starts with a number exactly when parse_number reads one")
{
  for (const char* yes : { "20", "  -60", "\t+1.5", "\n.5", "7.", "0x" })
  {
    CAPTURE(yes);
    CHECK(starts_with_number(yes));
  }
  for (const char* no : { "", "abc", "-", "+.", ". 5", "loud" })
  {
    CAPTURE(no);
    CHECK_FALSE(starts_with_number(no));
    CHECK(parse_number(no) == 0.0);
  }
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
  /* Five RAW bytes a column with the gain, four without -- no hex. */
  CHECK(scope.PutColumn(col, 0, true) == col + 5);
  CHECK(scope.PutColumn(col + 8, 0, false) == col + 12);
  /* Column 0: dry -0.5..0.3, wet -0.2..0.1, the gain's minimum 0.5. */
  const unsigned char want[5] = {encode_bipolar(-0.5f), encode_bipolar(0.3f),
                                 encode_bipolar(-0.2f), encode_bipolar(0.1f),
                                 encode_unipolar(0.5f)};
  for (int i = 0; i < 5; i++)
    CHECK((unsigned char) col[i] == want[i]);
  for (int i = 0; i < 4; i++)
    CHECK((unsigned char) col[8 + i] == want[i]);

  /* And as the floats themselves, for an editor that draws them. */
  float floats[5] = {};
  scope.ReadColumn(0, floats);
  CHECK(floats[0] == -0.5f);
  CHECK(floats[1] == 0.3f);
  CHECK(floats[2] == -0.2f);
  CHECK(floats[3] == 0.1f);
  CHECK(floats[4] == 0.5f);

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
