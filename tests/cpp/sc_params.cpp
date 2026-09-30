/*
 * NI Side-Chain's parameters as a host sees them: their text, and the chunk.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * clap-validator's param-conversions and state-reproducibility, on the
 * plugin's own declarations and state code (Params.cpp) and the CLAP wrapper's
 * own conversions (param_host.h).
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Params.h"
#include "param_host.h"
#include "shell_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using iplug::IByteChunk;

namespace {

struct Instance
{
  test::ParamHost host{kNumParams};
  Instance() { sc::params::Declare([this](int i) { return host.GetParam(i); }); }

  bool save(IByteChunk& chunk) const
  {
    return sc::params::Save(chunk, [this](IByteChunk& c) { return host.SerializeParams(c); });
  }

  int load(const IByteChunk& chunk)
  {
    return sc::params::Load(
      chunk, 0,
      [this](const IByteChunk& c, int pos) { return shell::state::CheckParams(c, pos, host); },
      [this](const IByteChunk& c, int pos) { return host.UnserializeParams(c, pos); });
  }
};

bool same_bytes(const IByteChunk& a, const IByteChunk& b)
{
  return a.Size() == b.Size() && std::equal(a.GetData(), a.GetData() + a.Size(), b.GetData());
}

} // namespace

/* ------------------------------------------------------------------ text */

TEST_CASE("every value a host can hold survives value -> text -> value")
{
  const Instance a;
  const int n = std::clamp((4000 + kNumParams - 1) / kNumParams, 5, 100);
  for (int i = 0; i < kNumParams; i++)
  {
    const iplug::IParam& p = *a.host.GetParam(i);
    for (int k = 0; k < n; k++)
    {
      double v = test::clap_min(p) + (test::clap_max(p) - test::clap_min(p)) * k / (n - 1);
      if (!test::is_double(p)) v = std::round(v);
      CHECK_MESSAGE(test::round_trip(p, v) == "", test::round_trip(p, v));
    }
  }
}

TEST_CASE("the bottom of the threshold reads as what it is, and parses back")
{
  /* It printed "off", which parsed as 0 dB -- the TOP of the range, the one
   * setting that almost never triggers. */
  const Instance a;
  const iplug::IParam& t = *a.host.GetParam(kThreshold);
  const std::string bottom = test::clap_to_text(t, 0.0);
  CHECK(test::clap_from_text(t, bottom) == 0.0);
  CHECK(t.StringToValue(bottom.c_str()) == -60.0);
}

TEST_CASE("a unit is shown once")
{
  const Instance a;
  for (int i = 0; i < kNumParams; i++)
  {
    const iplug::IParam& p = *a.host.GetParam(i);
    const std::string label = p.GetLabel();
    if (label.empty()) continue;
    WDL_String bare;
    p.GetDisplay(bare);
    const std::string text = bare.Get();
    CHECK_MESSAGE(!(text.size() >= label.size() &&
                    text.compare(text.size() - label.size(), label.size(), label) == 0),
                  p.GetName(), " says its unit twice: '", test::clap_to_text(p, p.Value()), "'");
  }
}

/* ----------------------------------------------------------------- state */

TEST_CASE("random parameters: save, a fresh instance, load, save again")
{
  std::mt19937 rng(20260930);
  for (int round = 0; round < 50; round++)
  {
    Instance a;
    a.host.Randomize(rng);
    IByteChunk first;
    REQUIRE(a.save(first));

    Instance b;
    REQUIRE(b.load(first) == first.Size());
    CHECK(b.host.Values() == a.host.Values());
    IByteChunk second;
    REQUIRE(b.save(second));
    CHECK(same_bytes(first, second));
  }
}

TEST_CASE("an earlier build's chunk -- the parameters alone -- still loads")
{
  Instance a;
  std::mt19937 rng(11);
  a.host.Randomize(rng);
  IByteChunk legacy;
  REQUIRE(a.host.SerializeParams(legacy));

  Instance b;
  CHECK(b.load(legacy) == legacy.Size());
  CHECK(b.host.Values() == a.host.Values());
}

TEST_CASE("a value an older build could hold and this one cannot is clamped, not refused")
{
  /* Curve had four options until Pump went. A project that chose it must
   * still open. */
  Instance a;
  IByteChunk legacy;
  REQUIRE(a.host.SerializeParams(legacy));
  const double pump = 3.0;
  std::memcpy(legacy.GetData() + kCurve * sizeof(double), &pump, sizeof pump);

  Instance b;
  CHECK(b.load(legacy) == legacy.Size());
  CHECK(b.host.GetParam(kCurve)->Value() == 2.0);
}

TEST_CASE("an empty chunk is refused")
{
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
    const std::vector<double> before = b.host.Values();
    CHECK(b.load(chunk) == -1);
    CHECK(b.host.Values() == before);
  }
}
