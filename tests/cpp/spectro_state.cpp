/*
 * The Spectrogram's state chunk, saved and reloaded as a host does.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * clap-validator's state-reproducibility and state-invalid tests, on the
 * plugin's own State.cpp. It has no parameters, so the parameter block is an
 * empty test::ParamHost -- which is also what the plugin's own is.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "State.h"
#include "param_host.h"
#include "shell_state.h"

#include <algorithm>
#include <random>
#include <vector>

using iplug::IByteChunk;
namespace st = spectro::state;

namespace {

struct Instance
{
  test::ParamHost host{0};
  st::Fields f;

  bool save(IByteChunk& chunk) const
  {
    return st::Save(chunk, [this](IByteChunk& c) { return host.SerializeParams(c); }, f);
  }

  int load(const IByteChunk& chunk)
  {
    return st::Load(
      chunk, 0,
      [this](const IByteChunk& c, int pos) { return shell::state::CheckParams(c, pos, host); },
      [this](const IByteChunk& c, int pos) { return host.UnserializeParams(c, pos); },
      f);
  }
};

bool same(const st::Fields& a, const st::Fields& b)
{
  return a.sources == b.sources && a.clashFloorDb == b.clashFloorDb &&
         a.clashBalanceDb == b.clashBalanceDb && a.view == b.view && a.cmpA == b.cmpA &&
         a.cmpB == b.cmpB && a.clashOn == b.clashOn;
}

bool same_bytes(const IByteChunk& a, const IByteChunk& b)
{
  return a.Size() == b.Size() && std::equal(a.GetData(), a.GetData() + a.Size(), b.GetData());
}

} // namespace

TEST_CASE("a session: save, a fresh instance, load, save again")
{
  Instance a;
  a.f.sources = {3, 4, 9};
  a.f.clashFloorDb = -48.5f;
  a.f.clashBalanceDb = 6.25f;
  a.f.view = {0, 2};
  a.f.cmpA = 2;
  a.f.cmpB = 3;
  a.f.clashOn = true;
  IByteChunk first;
  REQUIRE(a.save(first));

  Instance b;
  REQUIRE(b.load(first) == first.Size());
  CHECK(same(b.f, a.f));
  IByteChunk second;
  REQUIRE(b.save(second));
  CHECK(same_bytes(first, second));
}

TEST_CASE("an earlier build's chunk -- the selection only, no header -- still loads")
{
  IByteChunk legacy;
  legacy.PutStr("3,4");

  Instance b;
  CHECK(b.load(legacy) == legacy.Size());
  CHECK(b.f.sources == std::vector<unsigned int>{3, 4});
  CHECK(same([] { st::Fields d; d.sources = {3, 4}; return d; }(), b.f));
}

TEST_CASE("an empty chunk is refused")
{
  /* The very first build's -- it wrote nothing, and nothing is what loading it
   * restored. The AU wrapper already refused it. */
  Instance b;
  IByteChunk empty;
  CHECK(b.load(empty) == -1);
}

TEST_CASE("random bytes are refused, and change nothing")
{
  std::mt19937 rng(7);
  for (int round = 0; round < 3; round++)
  {
    std::vector<uint8_t> noise(1024 * 1024);
    for (auto& byte : noise) byte = uint8_t(rng());
    IByteChunk chunk;
    chunk.PutBytes(noise.data(), int(noise.size()));

    Instance b;
    b.f.sources = {7};
    CHECK(b.load(chunk) == -1);
    CHECK(b.f.sources == std::vector<unsigned int>{7});
  }
}
