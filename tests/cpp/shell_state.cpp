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
#include "param_host.h"

#include <cmath>
#include <cstring>
#include <string>

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

}

TEST_CASE("an empty chunk is refused, not read as the Spectrogram's first")
{
  /* The very first Spectrogram wrote no bytes at all. That chunk restores
   * nothing, so refusing it leaves an instance exactly as loading it did -- and
   * a CLAP host hands an empty stream to every plugin to see it refused. */
  IByteChunk empty;
  CHECK(st::Read(empty, 0).body == -1);

  IByteChunk c;
  put_doubles(c, {1.0});
  CHECK(st::Read(c, c.Size()).body == -1);
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

/* ------------------------------------------------------- reading carefully */

TEST_CASE("a string whose length runs past the end is refused")
{
  /* IByteChunk::GetStr returns a position past the end for it, and leaves the
   * string alone; that is how random bytes read as a label. */
  IByteChunk c;
  const int32_t len = 1000;
  c.Put(&len);
  c.PutStr("short");
  WDL_String s;
  CHECK(c.GetStr(s, 0) > c.Size());
  CHECK(st::GetStr(c, s, 0) == -1);
}

TEST_CASE("a string with a negative length is refused")
{
  IByteChunk c;
  const int32_t len = -8;
  c.Put(&len);
  WDL_String s;
  CHECK(st::GetStr(c, s, 0) == -1);
}

TEST_CASE("a string that is there is read, empty or not")
{
  IByteChunk c;
  c.PutStr("");
  c.PutStr("Kick bus");
  WDL_String s;
  const int first = st::GetStr(c, s, 0);
  CHECK(first == 4);
  CHECK(st::GetStr(c, s, first) == c.Size());
  CHECK(std::string(s.Get()) == "Kick bus");
  CHECK(st::GetStr(c, s, c.Size()) == -1);
}

TEST_CASE("a parameter block is numbers of the right kind, not numbers in range")
{
  test::ParamHost host(3);
  host.GetParam(0)->InitEnum("e", 0, {"a", "b", "c"});
  host.GetParam(1)->InitDouble("d", 0.0, 0.0, 1.0, 0.01);
  host.GetParam(2)->InitBool("b", false);

  const auto block = [](double a, double b, double c) {
    IByteChunk chunk;
    put_doubles(chunk, {a, b, c});
    return chunk;
  };
  CHECK(st::CheckParams(block(2.0, 0.5, 1.0), 0, host) == 24);
  /* Out of range is a value an older build could hold -- clamped on load. */
  CHECK(st::CheckParams(block(3.0, 7.5, 0.0), 0, host) == 24);
  /* Not a whole number where Constrain would have made one. */
  CHECK(st::CheckParams(block(1.5, 0.5, 1.0), 0, host) == -1);
  CHECK(st::CheckParams(block(1.0, 0.5, 1e300), 0, host) == -1);
  /* Not a number at all. */
  CHECK(st::CheckParams(block(1.0, std::nan(""), 1.0), 0, host) == -1);
  CHECK(st::CheckParams(block(1.0, INFINITY, 1.0), 0, host) == -1);
  /* Short. */
  IByteChunk shortBlock;
  put_doubles(shortBlock, {1.0, 0.5});
  CHECK(st::CheckParams(shortBlock, 0, host) == -1);
}

TEST_CASE("a refused block is not applied")
{
  test::ParamHost host(2);
  host.GetParam(0)->InitInt("i", 1, 1, 8);
  host.GetParam(1)->InitDouble("d", 0.25, 0.0, 1.0, 0.01);
  IByteChunk chunk;
  put_doubles(chunk, {4.0, std::nan("")});
  CHECK(st::LoadParams(host, chunk, 0) == -1);
  CHECK(host.GetParam(0)->Value() == 1.0);

  IByteChunk good;
  put_doubles(good, {4.0, 0.75});
  CHECK(st::LoadParams(host, good, 0) == good.Size());
  CHECK(host.GetParam(0)->Value() == 4.0);
  CHECK(host.GetParam(1)->Value() == 0.75);
}
