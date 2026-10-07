// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The spike's chunk reader against what the iPlug2 builds actually wrote.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 *
 * The fixtures are the iPlug2 build's own bytes (tests/fixtures/iplug2), so a
 * reader that passes here reads what Live stored. Writing them back must give
 * the same bytes: then a set this build saves is one the iPlug2 build reads
 * as its own. The legacy and refusal cases are built from those bytes, the
 * way FORMAT.md says earlier builds and garbage differ from them.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Chunk.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace ni::tg;
using Bytes = std::vector<uint8_t>;

namespace {

Bytes Fixture(const std::string& scenario)
{
  const std::string path = std::string(NI_FIXTURES) + "/NITranceGate/" + scenario + ".component.bin";
  std::ifstream f(path, std::ios::binary);
  REQUIRE_MESSAGE(bool(f), "missing fixture " << path);
  return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

/* The header and the bypass taken off: the body, as a chunk without the
 * header -- the layout every build before 2026-09-30 wrote. */
Bytes Body(const Bytes& headed)
{
  return Bytes(headed.begin() + 16, headed.end());
}

void PutF64(Bytes& out, double v)
{
  uint8_t b[8];
  std::memcpy(b, &v, 8);
  out.insert(out.end(), b, b + 8);
}

void PutI32(Bytes& out, int32_t v)
{
  uint8_t b[4];
  std::memcpy(b, &v, 4);
  out.insert(out.end(), b, b + 4);
}

/* A legacy chunk from a build with `count` parameters: those values, the
 * blob, and the wrapper's bypass. */
Bytes Legacy(const chunk::State& from, int count, bool withBypass = true)
{
  Bytes out;
  for (int i = 0; i < count; i++)
    PutF64(out, from.params[i]);
  PutI32(out, int32_t(from.blob.size()));
  out.insert(out.end(), from.blob.begin(), from.blob.end());
  if (withBypass)
    PutI32(out, from.bypass.value_or(false) ? 1 : 0);
  return out;
}

void SetF64(Bytes& b, size_t at, double v)
{
  std::memcpy(b.data() + at, &v, 8);
}

void SetI32(Bytes& b, size_t at, int32_t v)
{
  std::memcpy(b.data() + at, &v, 4);
}

} // namespace

TEST_CASE("every Trance Gate fixture reads, and writes back byte for byte")
{
  for (const char* scenario : {"default", "slots", "bypassed"})
  {
    CAPTURE(scenario);
    const Bytes saved = Fixture(scenario);
    const auto state = chunk::Read(saved.data(), saved.size());
    REQUIRE(state.has_value());
    CHECK_FALSE(state->legacy);
    CHECK(state->carried == kNumParams);
    CHECK(state->bypass.has_value());
    CHECK(state->blob.rfind("{\"sv\":7,", 0) == 0);
    CHECK(chunk::Write(*state) == saved);
  }
}

TEST_CASE("the slots fixture says what its scenario did")
{
  const Bytes saved = Fixture("slots");
  const auto s = chunk::Read(saved.data(), saved.size());
  REQUIRE(s.has_value());
  CHECK(s->params[kSlot] == 2.0);
  CHECK(s->params[kLength] == 32.0);
  CHECK(s->params[kWidth] == 50.0);
  CHECK(s->params[kLegato] == 1.0);
  CHECK(s->params[kFade] == 50.0);
  CHECK(s->params[kFadeSoft] == 1.0);
  CHECK(s->params[kFadeDir] == 1.0);
  CHECK(s->blob.find("\"slot\":1,") != std::string::npos);
  CHECK(s->bypass == false);
}

TEST_CASE("the bypass after the chunk is read")
{
  const Bytes saved = Fixture("bypassed");
  const auto s = chunk::Read(saved.data(), saved.size());
  REQUIRE(s.has_value());
  CHECK(s->bypass == true);
}

TEST_CASE("a chunk without the wrapper's bypass reads, and says it has none")
{
  Bytes saved = Fixture("slots");
  saved.resize(saved.size() - 4);
  const auto s = chunk::Read(saved.data(), saved.size());
  REQUIRE(s.has_value());
  CHECK_FALSE(s->bypass.has_value());
}

TEST_CASE("a legacy chunk with today's fifteen parameters reads as the headed one")
{
  const Bytes saved = Fixture("slots");
  const auto headed = chunk::Read(saved.data(), saved.size());
  const Bytes legacy = Body(saved);
  const auto s = chunk::Read(legacy.data(), legacy.size());
  REQUIRE(s.has_value());
  CHECK(s->legacy);
  CHECK(s->carried == 15);
  CHECK(std::memcmp(s->params, headed->params, sizeof s->params) == 0);
  CHECK(s->blob == headed->blob);
  CHECK(s->bypass == headed->bypass);
}

TEST_CASE("a legacy chunk from before the fade parameters reads, with their defaults")
{
  const Bytes saved = Fixture("slots");
  const auto headed = chunk::Read(saved.data(), saved.size());
  for (int count : {12, 14})
  {
    CAPTURE(count);
    for (bool withBypass : {true, false})
    {
      const Bytes legacy = Legacy(*headed, count, withBypass);
      const auto s = chunk::Read(legacy.data(), legacy.size());
      REQUIRE(s.has_value());
      CHECK(s->legacy);
      CHECK(s->carried == count);
      for (int i = 0; i < kNumParams; i++)
        CHECK(s->params[i] == (i < count ? headed->params[i] : Default(i)));
      CHECK(s->blob == headed->blob);
      CHECK(s->bypass.has_value() == withBypass);
    }
  }
}

TEST_CASE("an empty blob is a chunk, which restores no pattern")
{
  chunk::State empty;
  for (int i = 0; i < kNumParams; i++)
    empty.params[i] = Default(i);
  const Bytes legacy = Legacy(empty, kNumParams);
  const auto s = chunk::Read(legacy.data(), legacy.size());
  REQUIRE(s.has_value());
  CHECK(s->blob.empty());
}

TEST_CASE("a later version's extra fields are skipped, and the bypass found after them")
{
  Bytes saved = Fixture("bypassed");
  const auto before = chunk::Read(saved.data(), saved.size());
  /* Four more bytes inside the body, the size grown to cover them. */
  const size_t end = saved.size() - 4;
  saved.insert(saved.begin() + long(end), {0xAA, 0xBB, 0xCC, 0xDD});
  int32_t body;
  std::memcpy(&body, saved.data() + 12, 4);
  SetI32(saved, 12, body + 4);
  SetI32(saved, 8, 2);
  const auto s = chunk::Read(saved.data(), saved.size());
  REQUIRE(s.has_value());
  CHECK(s->blob == before->blob);
  CHECK(s->bypass == true);
}

TEST_CASE("what no build wrote is refused")
{
  const Bytes saved = Fixture("slots");

  SUBCASE("nothing") { CHECK_FALSE(chunk::Read(nullptr, 0).has_value()); CHECK_FALSE(chunk::Read(saved.data(), 0).has_value()); }

  SUBCASE("a header whose size runs past the stream")
  {
    Bytes b = saved;
    SetI32(b, 12, int32_t(b.size()));
    CHECK_FALSE(chunk::Read(b.data(), b.size()).has_value());
  }

  SUBCASE("a header with a negative size")
  {
    Bytes b = saved;
    SetI32(b, 12, -1);
    CHECK_FALSE(chunk::Read(b.data(), b.size()).has_value());
  }

  SUBCASE("a stepped parameter that is not a whole number")
  {
    Bytes b = saved;
    SetF64(b, 16 + 8 * kSlot, 1.5);
    CHECK_FALSE(chunk::Read(b.data(), b.size()).has_value());
  }

  SUBCASE("a parameter that is not a number")
  {
    Bytes b = saved;
    SetF64(b, 16 + 8 * kAmount, std::numeric_limits<double>::quiet_NaN());
    CHECK_FALSE(chunk::Read(b.data(), b.size()).has_value());
  }

  SUBCASE("a blob that runs past the body")
  {
    Bytes b = saved;
    SetI32(b, 16 + 8 * kNumParams, int32_t(b.size()));
    CHECK_FALSE(chunk::Read(b.data(), b.size()).has_value());
  }

  SUBCASE("the JUCE week's state: the bare engine blob, no parameters")
  {
    const auto s = chunk::Read(saved.data(), saved.size());
    const std::string& blob = s->blob;
    CHECK_FALSE(chunk::Read(blob.data(), blob.size()).has_value());
  }

  SUBCASE("random bytes, a thousand times")
  {
    std::mt19937 rng(20261006);
    int read = 0;
    for (int n = 0; n < 1000; n++)
    {
      Bytes b(size_t(rng() % 400) + 1);
      for (auto& x : b)
        x = uint8_t(rng());
      read += chunk::Read(b.data(), b.size()).has_value() ? 1 : 0;
    }
    CHECK(read == 0);
  }
}

TEST_CASE("a stepped value is written whole, as iPlug2 stored it")
{
  chunk::State s;
  for (int i = 0; i < kNumParams; i++)
    s.params[i] = Default(i);
  s.params[kSlot] = 3.0000001;
  const Bytes b = chunk::Write(s);
  const auto back = chunk::Read(b.data(), b.size());
  REQUIRE(back.has_value());
  CHECK(back->params[kSlot] == 3.0);
  CHECK(back->bypass == false);
}
