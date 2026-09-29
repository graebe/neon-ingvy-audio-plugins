/*
 * The Trance Gate's wire arithmetic: the four places being wrong is silent.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THESE FOUR AND NOT THE REST OF TranceGate.cpp. Everything else in that
 * file is a call into iPlug2 or into the engine, and a test that stubbed
 * either would be asserting on the stub. tg_au drives the real parameter API
 * against a real host and tg_render_ab hashes the real audio path; what was
 * left uncovered was the arithmetic BETWEEN those calls, which is what moved
 * into Wire.cpp and what is checked here.
 *
 * Each of the four fails without an error message, a crash or a visible
 * symptom. That is the selection rule, not line count.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Wire.h"

#include <cmath>
#include <string>

using namespace tg::wire;

/* --------------------------------------------------------------- the split */

TEST_CASE("a pair splits at the first colon")
{
  std::string a, b;
  REQUIRE(split_pair("7:2", a, b));
  CHECK(a == "7");
  CHECK(b == "2");
}

TEST_CASE("a typed readout may contain a second colon, and keeps it")
{
  /* "8:12:30" is parameter 8 set to the text "12:30". Splitting at the last
   * colon instead would set parameter 8 from "30" and silently discard the
   * rest -- a value the user typed, replaced by a different valid one. */
  std::string a, b;
  REQUIRE(split_pair("8:12:30", a, b));
  CHECK(a == "8");
  CHECK(b == "12:30");
}

TEST_CASE("a payload with no colon is dropped rather than half-applied")
{
  /* Half-applying is the dangerous outcome: the index alone moves the cursor,
   * and the next edit lands on a step the user was not looking at. */
  std::string a = "untouched", b = "untouched";
  CHECK(split_pair("garbage", a, b) == false);
  CHECK(a == "untouched");
  CHECK(b == "untouched");
}

TEST_CASE("an empty half is still a split")
{
  /* The engine is what refuses an empty value; this side's job is only to say
   * whether the message had the shape it claimed. */
  std::string a, b;
  REQUIRE(split_pair("5:", a, b));
  CHECK(a == "5");
  CHECK(b.empty());

  REQUIRE(split_pair(":5", a, b));
  CHECK(a.empty());
  CHECK(b == "5");
}

/* ----------------------------------------------------------- the quantiser */

TEST_CASE("the scope's ends map to the ends of the byte")
{
  CHECK(int(encode_sample(-1.f)) == 0);
  CHECK(int(encode_sample(1.f)) == 255);
}

TEST_CASE("silence sits at mid-scale")
{
  /* 127.5 rounds to 128. The plot draws this as the centre line, so an
   * off-by-one here is a waveform sitting a pixel low for its whole length --
   * which looks like a DC offset in the audio rather than a bug in the plot. */
  CHECK(int(encode_sample(0.f)) == 128);
}

TEST_CASE("anything past the well edge is pinned to it, not wrapped")
{
  /* Wrapping is what an unclamped cast does, and a sample just over full
   * scale would draw at the OPPOSITE edge -- a clipped signal rendered as a
   * full-amplitude inversion, which reads as a real and very alarming fault. */
  CHECK(int(encode_sample(2.f)) == 255);
  CHECK(int(encode_sample(-2.f)) == 0);
  CHECK(int(encode_sample(1e9f)) == 255);
  CHECK(int(encode_sample(-1e9f)) == 0);
}

TEST_CASE("a non-finite sample becomes silence rather than a random byte")
{
  /* An uninitialised capture column can hold a NaN, and a NaN cast to int is
   * undefined -- in practice a full-scale spike indistinguishable from a real
   * transient. Note fmin/fmax PROPAGATE NaN, so the isfinite test is the
   * guard and its position is load-bearing. */
  CHECK(int(encode_sample(NAN)) == 128);
  CHECK(int(encode_sample(INFINITY)) == 128);
  CHECK(int(encode_sample(-INFINITY)) == 128);
}

TEST_CASE("the quantiser is monotone across its whole range")
{
  /* A dip anywhere is a waveform that folds back on itself at one amplitude. */
  int last = -1;
  for (int i = 0; i <= 2000; i++)
  {
    const float v = -1.f + 2.f * float(i) / 2000.f;
    const int b = encode_sample(v);
    CHECK(b >= last);
    last = b;
  }
  CHECK(last == 255);
}

/* --------------------------------------------------------------- the height */

TEST_CASE("a plausible editor height is passed through unchanged")
{
  CHECK(clamp_editor_height(700) == 700);
  CHECK(clamp_editor_height(101) == 101);
  CHECK(clamp_editor_height(3999) == 3999);
}

TEST_CASE("an implausible height is refused, and the bounds are exclusive")
{
  /* The host honours whatever it is told, so a bug in the editor's row
   * arithmetic becomes a window taller than the display with no error. */
  CHECK(clamp_editor_height(100) == 0);
  CHECK(clamp_editor_height(4000) == 0);
  CHECK(clamp_editor_height(0) == 0);
  CHECK(clamp_editor_height(-700) == 0);
  CHECK(clamp_editor_height(40000) == 0);
}

/* ------------------------------------------------------------ the transport */

TEST_CASE("a chunk advances the transport by its own duration")
{
  /* 120 bpm, 48 kHz: 2 beats a second, so 24000 samples is exactly one beat. */
  CHECK(advance_beats(0.0, 24000, 120.0, 48000.0) == doctest::Approx(1.0));
  CHECK(advance_beats(3.0, 12000, 120.0, 48000.0) == doctest::Approx(3.5));
}

TEST_CASE("chunking a block lands where processing it whole would")
{
  /* This is the rule the chunk loop exists to preserve. If it drifts, the
   * gate moves against the grid only on blocks longer than the reservation --
   * so it is correct at every buffer size the developer tried. */
  const double whole = advance_beats(0.0, 2048, 174.0, 44100.0);

  double chunked = 0.0;
  for (int i = 0; i < 4; i++)
    chunked = advance_beats(chunked, 512, 174.0, 44100.0);

  CHECK(chunked == doctest::Approx(whole));
}

TEST_CASE("a degenerate sample rate leaves the position alone")
{
  /* Dividing by it would put an infinity into the engine's phase, and the
   * engine has no way to tell that from a legitimate position. */
  CHECK(advance_beats(5.0, 512, 120.0, 0.0) == 5.0);
  CHECK(advance_beats(5.0, 512, 120.0, -48000.0) == 5.0);
  CHECK(advance_beats(5.0, 0, 120.0, 48000.0) == 5.0);
  CHECK(advance_beats(5.0, -1, 120.0, 48000.0) == 5.0);
}

TEST_CASE("the scope payload fits the transport's cap")
{
  /* kScopeCols is 256: four hex pairs a column, plus the header. */
  CHECK(framed_size(256 * 8 + 32) < 65536);
}
