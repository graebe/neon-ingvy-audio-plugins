/*
 * NI Listen-In's state chunk, saved and reloaded as a host does.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * clap-validator's state-reproducibility and state-invalid tests, on the
 * plugin's own State.cpp and a real IParam (param_host.h).
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "State.h"
#include "param_host.h"
#include "shell_state.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using iplug::IByteChunk;
namespace st = listenin::state;

namespace {

struct Instance
{
  test::ParamHost host{kNumParams};
  std::string label;
  Instance() { st::Declare([this](int i) { return host.GetParam(i); }); }

  bool save(IByteChunk& chunk) const
  {
    return st::Save(chunk, [this](IByteChunk& c) { return host.SerializeParams(c); }, label);
  }

  int load(const IByteChunk& chunk)
  {
    return st::Load(
      chunk, 0,
      [this](const IByteChunk& c, int pos) { return shell::state::CheckParams(c, pos, host); },
      [this](const IByteChunk& c, int pos) { return host.UnserializeParams(c, pos); },
      label);
  }
};

bool same_bytes(const IByteChunk& a, const IByteChunk& b)
{
  return a.Size() == b.Size() && std::equal(a.GetData(), a.GetData() + a.Size(), b.GetData());
}

} // namespace

TEST_CASE("a random bus and a label: save, a fresh instance, load, save again")
{
  std::mt19937 rng(20260930);
  for (int round = 0; round < 20; round++)
  {
    Instance a;
    a.host.Randomize(rng);
    a.label = "Kick bus " + std::to_string(round);
    IByteChunk first;
    REQUIRE(a.save(first));

    Instance b;
    REQUIRE(b.load(first) == first.Size());
    CHECK(b.host.Values() == a.host.Values());
    CHECK(b.label == a.label);
    IByteChunk second;
    REQUIRE(b.save(second));
    CHECK(same_bytes(first, second));
  }
}

TEST_CASE("an earlier build's chunk -- no header -- still loads")
{
  Instance a;
  a.host.GetParam(kSlot)->Set(3.0);
  IByteChunk legacy;
  REQUIRE(a.host.SerializeParams(legacy));
  legacy.PutStr("Kick bus");

  Instance b;
  CHECK(b.load(legacy) == legacy.Size());
  CHECK(b.host.GetParam(kSlot)->Value() == 3.0);
  CHECK(b.label == "Kick bus");
}

TEST_CASE("an empty chunk is refused")
{
  Instance b;
  IByteChunk empty;
  CHECK(b.load(empty) == -1);
}

TEST_CASE("a parameter with no label after it is refused, and not applied")
{
  Instance a;
  a.host.GetParam(kSlot)->Set(4.0);
  IByteChunk chunk;
  REQUIRE(a.host.SerializeParams(chunk));

  Instance b;
  CHECK(b.load(chunk) == -1);
  CHECK(b.host.GetParam(kSlot)->Value() == 1.0);
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
    b.label = "mine";
    CHECK(b.load(chunk) == -1);
    CHECK(b.host.GetParam(kSlot)->Value() == 1.0);
    CHECK(b.label == "mine");
  }
}
