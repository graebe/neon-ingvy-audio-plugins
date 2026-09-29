/*
 * NI Side-Chain's wire arithmetic: the places where being wrong is SILENT.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THESE AND NOT THE REST OF SideChain.cpp. Everything else in that file is
 * a call into iPlug2 or into the engine, and a test that stubbed either would
 * be asserting on the stub. auval drives the real parameter API against a real
 * host and the render A/B hashes the real audio path; what was left uncovered
 * is the arithmetic BETWEEN those calls, which is what moved into Wire.cpp.
 *
 * Each function here fails without an error message, a crash or a visible
 * symptom. That is the selection rule, not line count.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Wire.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace sc::wire;

/* --------------------------------------------------------------- the split */

TEST_CASE("a pair splits at the first colon")
{
  std::string a, b;
  REQUIRE(split_pair("7:2", a, b));
  CHECK(a == "7");
  CHECK(b == "2");

  /* A typed readout may contain another colon, and the VALUE is what keeps it:
   * splitting at the last one would put part of the text in the index. */
  REQUIRE(split_pair("13:-24.0 dB : loud", a, b));
  CHECK(a == "13");
  CHECK(b == "-24.0 dB : loud");
}

TEST_CASE("a payload with no colon is dropped whole, not half-applied")
{
  /* Applying the index without the value would edit whatever the index landed
   * on -- an edit the user did not ask for, at a place they were not looking. */
  std::string a = "untouched", b = "untouched";
  CHECK_FALSE(split_pair("nocolon", a, b));
  CHECK(a == "untouched");
  CHECK(b == "untouched");
}

/* ------------------------------------------------------- the bipolar sample */

TEST_CASE("encode_sample maps -1..1 across the whole byte")
{
  CHECK(encode_sample(-1.f) == 0);
  CHECK(encode_sample(1.f) == 255);
  /* Silence sits at mid-scale, which is what makes a silent column read as a
   * flat line at the zero axis rather than at the bottom of the well. */
  CHECK(encode_sample(0.f) == 128);
}

TEST_CASE("encode_sample clamps rather than wrapping")
{
  CHECK(encode_sample(-5.f) == 0);
  CHECK(encode_sample(5.f) == 255);
}

TEST_CASE("a non-finite sample becomes mid-scale, not a random byte")
{
  /*
   * A NaN CAST TO INT IS UNDEFINED, and in practice it is a value that draws as
   * a full-scale spike and cannot be told from a real transient. An
   * uninitialised capture column can hold one.
   *
   * The ORDER of the guard is the guard: fmin/fmax PROPAGATE a NaN rather than
   * pinning it, so testing isfinite after clamping would not help.
   */
  CHECK(encode_sample(std::numeric_limits<float>::quiet_NaN()) == 128);
  CHECK(encode_sample(std::numeric_limits<float>::infinity()) == 128);
  CHECK(encode_sample(-std::numeric_limits<float>::infinity()) == 128);
}

/* ------------------------------------------------------ the unipolar sample */

TEST_CASE("encode_unipolar spends the whole byte on 0..1")
{
  /*
   * THE REASON THIS EXISTS SEPARATELY. The duck is unipolar, and pushed through
   * encode_sample every value would land in 128..255 -- seven bits, which draws
   * as a coarse meter rather than as a bug anyone would notice.
   */
  CHECK(encode_unipolar(0.f) == 0);
  CHECK(encode_unipolar(1.f) == 255);
  CHECK(encode_unipolar(0.5f) == 128);

  /* And it really is twice the resolution: two gains a hundredth apart must
   * differ by more than one step here, where the bipolar encoder rounds them
   * together. */
  CHECK(encode_unipolar(0.50f) != encode_unipolar(0.51f));
}

TEST_CASE("a non-finite gain reads as no reduction")
{
  /* Zero is the honest answer for "nothing happened" -- a NaN duck must not
   * draw as a full-scale dip. */
  CHECK(encode_unipolar(std::numeric_limits<float>::quiet_NaN()) == 0);
  CHECK(encode_unipolar(-1.f) == 0);
  CHECK(encode_unipolar(2.f) == 255);
}

/* --------------------------------------------------------- the editor's ask */

TEST_CASE("a plausible editor height is honoured")
{
  CHECK(clamp_editor_height(624) == 624);
  CHECK(clamp_editor_height(101) == 101);
  CHECK(clamp_editor_height(3999) == 3999);
}

TEST_CASE("an implausible editor height is refused with 0")
{
  /*
   * The editor reports the height it needs because it is the side that knows
   * both its content and the scale it had to apply -- which makes the number
   * UNTRUSTED here. A slip in its arithmetic arrives as a request for a 40 000
   * pixel window, and the host will honour it.
   */
  CHECK(clamp_editor_height(40000) == 0);
  CHECK(clamp_editor_height(100) == 0);
  CHECK(clamp_editor_height(4000) == 0);
  CHECK(clamp_editor_height(0) == 0);
  CHECK(clamp_editor_height(-1) == 0);
}

/* -------------------------------------------------------------- the chunker */

TEST_CASE("beats advance with the frames, at the tempo")
{
  /* 120 bpm is 2 beats a second, so a second of frames is 2 beats. */
  CHECK(advance_beats(0.0, 48000, 120.0, 48000.0) == doctest::Approx(2.0));
  CHECK(advance_beats(4.0, 24000, 120.0, 48000.0) == doctest::Approx(5.0));
  /* And it is linear in the tempo. */
  CHECK(advance_beats(0.0, 48000, 60.0, 48000.0) == doctest::Approx(1.0));
}

TEST_CASE("a degenerate advance returns the position unchanged")
{
  /*
   * Not tidiness: dividing by a zero sample rate would put an infinity into the
   * engine's phase, and the phase-locked loop would then chase a target it can
   * never reach for the rest of the session.
   */
  CHECK(advance_beats(7.0, 512, 120.0, 0.0) == 7.0);
  CHECK(advance_beats(7.0, 0, 120.0, 48000.0) == 7.0);
  CHECK(advance_beats(7.0, -1, 120.0, 48000.0) == 7.0);
}

/* -------------------------------------------------- the duplicated sidechain */

TEST_CASE("an aux buffer identical to the main input is reported as such")
{
  /*
   * Logic and GarageBand copy bus 1 into the sidechain bus when nothing is
   * patched. This is NOT used to work around that -- the Source parameter is
   * the explicit enable iPlug2's own example recommends -- it exists so the
   * editor does not show a key meter moving in time with the track's own audio,
   * which would read as a working sidechain.
   */
  const std::vector<float> a{0.1f, -0.2f, 0.3f, 0.0f};
  std::vector<float> b = a;
  CHECK(key_is_duplicate(a.data(), b.data(), int(a.size())));

  b[2] = 0.30001f;
  CHECK_FALSE(key_is_duplicate(a.data(), b.data(), int(a.size())));
}

TEST_CASE("the duplicate check compares numbers, not bit patterns")
{
  /*
   * memcmp would call +0.0 and -0.0 unequal, and a host that inverts phase
   * somewhere in its no-op path would then look like a patched sidechain.
   */
  const std::vector<float> a{0.0f, 1.0f};
  const std::vector<float> b{-0.0f, 1.0f};
  CHECK(key_is_duplicate(a.data(), b.data(), 2));
}

TEST_CASE("a missing bus is not a duplicate")
{
  /* A null pointer means the bus is not there, which the caller already knows
   * from IsChannelConnected -- answering "duplicate" would conflate "unpatched"
   * with "patched to ourselves". */
  const std::vector<float> a{1.0f, 2.0f};
  CHECK_FALSE(key_is_duplicate(nullptr, a.data(), 2));
  CHECK_FALSE(key_is_duplicate(a.data(), nullptr, 2));
  CHECK_FALSE(key_is_duplicate(a.data(), a.data(), 0));
}

/* ------------------------------------------------------------ the size guard */

TEST_CASE("framed_size accounts for base64's extra third")
{
  /*
   * The transport TRUNCATES rather than fails, so every push has to fit under
   * SetMaxJSStringLength with the inflation counted. The scope payload is the
   * one message that can approach it.
   */
  CHECK(framed_size(0) == 32);
  CHECK(framed_size(3) == 36);
  CHECK(framed_size(3000) == 4032);
  /* The real payload: 256 columns of five hex pairs, plus the seen flags. */
  CHECK(framed_size(256 * 10 + 256 + 8) < 65536);
}
