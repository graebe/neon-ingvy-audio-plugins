/*
 * The state chunk's header, and every older chunk it must not be mistaken for.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Each plugin's UnserializeState is Read, then its own fields from h.body, then
 * Finish. The legacy cases below are each plugin's chunk as the builds before
 * the header wrote it, byte for byte: if Read called any of them headed, that
 * plugin's saved projects would stop opening. tg_state covers the Trance Gate
 * end to end; this covers the header against all four layouts.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "IPlugStructs.h"
#include "shell_state.h"

#include <cmath>
#include <cstring>

using iplug::IByteChunk;
namespace st = shell::state;

namespace {

void put_doubles(IByteChunk& c, std::initializer_list<double> vs)
{
  for (double v : vs) c.Put(&v);
}

void check_legacy(const IByteChunk& c, int startPos = 0)
{
  const st::Header h = st::Read(c, startPos);
  CHECK(h.legacy);
  CHECK(h.version == 0);
  CHECK(h.body == startPos);
  CHECK(st::Finish(h, 123) == 123);
}

} // namespace

TEST_CASE("a header round-trips, and the body is exactly what follows it")
{
  IByteChunk c;
  const int at = st::Begin(c, 7);
  put_doubles(c, {1.0, 2.0});
  c.PutStr("tail");
  REQUIRE(st::End(c, at));

  const st::Header h = st::Read(c, 0);
  CHECK_FALSE(h.legacy);
  CHECK(h.version == 7);
  CHECK(h.body == st::kHeaderBytes);
  CHECK(h.end == c.Size());
  CHECK(st::Finish(h, 5) == c.Size());
}

TEST_CASE("the header is found where the host's chunk starts, not at zero")
{
  /* iPlug2's fxp path puts its own version in front before SerializeState. */
  IByteChunk c;
  const int prefix[2] = {0x49506C75, 0x10000};
  c.Put(&prefix[0]);
  c.Put(&prefix[1]);
  const int at = st::Begin(c, 1);
  put_doubles(c, {3.0});
  REQUIRE(st::End(c, at));

  const st::Header h = st::Read(c, 8);
  CHECK_FALSE(h.legacy);
  CHECK(h.body == 8 + st::kHeaderBytes);
  CHECK(h.end == c.Size());
}

TEST_CASE("the magic is a NaN, which no parameter value can be")
{
  double d = 0.0;
  std::memcpy(&d, st::kMagic, sizeof d);
  CHECK(std::isnan(d));
}

TEST_CASE("older chunks are legacy: the Trance Gate's")
{
  /* Fifteen parameters, Slot first (1..8), then the blob. */
  IByteChunk c;
  put_doubles(c, {1.0, 15.0, 7.0, 0.0, 0.0, 0.0, 100.0, 100.0, 1.6, 16.0, 100.0, 16.0,
                  100.0, 0.0, 0.0});
  c.PutStr("{\"sv\":6}");
  check_legacy(c);
}

TEST_CASE("older chunks are legacy: Listen-In's")
{
  IByteChunk c;
  put_doubles(c, {3.0});
  c.PutStr("Kick bus");
  check_legacy(c);
}

TEST_CASE("older chunks are legacy: Side-Chain's")
{
  IByteChunk c;
  put_doubles(c, {0.0, 5.0, 0.0, 0.0, 10.0, 20.0, 40.0, 100.0, 0.0, 0.0, 36.0, 0.0,
                  100.0, -24.0, 20.0});
  check_legacy(c);
}

TEST_CASE("older chunks are legacy: the Spectrogram's, which has no parameters")
{
  /* Its first field is a string, so the first four bytes are a length. */
  IByteChunk c;
  c.PutStr("3,4");
  c.PutStr("-60.00:12.00");
  c.PutStr("0");
  c.PutStr("0:1:0");
  check_legacy(c);

  /* And the empty chunk every build before the selection was saved wrote. */
  IByteChunk empty;
  check_legacy(empty);
}

TEST_CASE("a chunk too short to hold a header is legacy")
{
  IByteChunk c;
  c.PutBytes(st::kMagic, 5);
  check_legacy(c);
}

TEST_CASE("a header whose size runs past the end is refused")
{
  IByteChunk c;
  const int at = st::Begin(c, 1);
  put_doubles(c, {1.0, 2.0});
  REQUIRE(st::End(c, at));
  c.Resize(c.Size() - 4);
  const st::Header h = st::Read(c, 0);
  CHECK_FALSE(h.legacy);
  CHECK(h.body == -1);
}
