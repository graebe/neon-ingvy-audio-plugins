/*
 * The Spectrogram's state chunk, saved and reloaded as a host does.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * clap-validator's state-reproducibility and state-invalid tests, on the
 * plugin's own State.cpp. It has no parameters, so the parameter block is an
 * empty test::ParamHost -- which is also what the plugin's own is.
 *
 * And the Session: a host's state calls on a thread of their own, the main
 * thread servicing the receiver beside them -- what auval -stress does, and
 * what once left one of the two waiting forever. The plugin's UnserializeState
 * and SerializeState are Session::Load and Session::Get, and its receiver calls
 * are the Sink, so this is the plugin's path without the plugin class.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "State.h"
#include "param_host.h"
#include "shell_state.h"
#include "spectro_recv.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <thread>
#include <unistd.h>
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
         a.cmpB == b.cmpB && a.clashOn == b.clashOn && a.rangeLo == b.rangeLo &&
         a.rangeHi == b.rangeHi;
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
  a.f.rangeLo = 40.f;
  a.f.rangeHi = 800.f;
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

TEST_CASE("the zoom survives a save and a reload")
{
  Instance a;
  a.f.rangeLo = 2000.f;
  a.f.rangeHi = 20000.f;
  IByteChunk chunk;
  REQUIRE(a.save(chunk));

  Instance b;
  REQUIRE(b.f.rangeLo == 10.f);
  REQUIRE(b.load(chunk) == chunk.Size());
  CHECK(b.f.rangeLo == 2000.f);
  CHECK(b.f.rangeHi == 20000.f);
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

/* ------------------------------------------------------------- session -- */

namespace {

test::ParamHost g_noParams{0};

int load_into(st::Session& s, const IByteChunk& chunk)
{
  return s.Load(
    chunk, 0,
    [](const IByteChunk& c, int pos) { return shell::state::CheckParams(c, pos, g_noParams); },
    [](const IByteChunk& c, int pos) { return g_noParams.UnserializeParams(c, pos); });
}

IByteChunk chunk_of(const st::Fields& f)
{
  IByteChunk c;
  REQUIRE(st::Save(c, [](IByteChunk& ch) { return g_noParams.SerializeParams(ch); }, f));
  return c;
}

/* What the plugin opens with (Spectrogram.cpp's Opening). */
st::Fields opening()
{
  st::Fields f;
  f.view = {0};
  return f;
}

/* The i-th of a run of distinct sessions, every field moved. */
st::Fields variant(int i)
{
  st::Fields f;
  f.sources = {unsigned(1 + i % 16), unsigned(1 + (i * 7 + 3) % 16)};
  f.clashFloorDb = -90.f + float(i % 60);
  f.clashBalanceDb = 1.f + float(i % 20);
  f.view = {0, 1 + i % 3};
  f.cmpA = i % 2;
  f.cmpB = 2 + i % 2;
  f.clashOn = (i % 2) != 0;
  f.rangeLo = 20.f + float(i);
  f.rangeHi = 5000.f + float(i);
  return f;
}

/* The receiver calls, recorded -- and each one checked to be on the thread
 * that is allowed to make it. `forward` passes them to a real receiver. */
struct Recorder final : st::Session::Sink
{
  std::thread::id main = std::this_thread::get_id();
  srecv_t* forward = nullptr;
  std::atomic<int> offMain{0};
  int sources = 0, clash = 0, range = 0;
  st::Fields last;

  void onMain()
  {
    if (std::this_thread::get_id() != main)
      offMain.fetch_add(1);
  }
  void ApplySources(const std::vector<unsigned int>& slots) override
  {
    onMain();
    sources++;
    last.sources = slots;
    if (forward)
      srecv_set_sources(forward, slots.empty() ? nullptr : slots.data(), int(slots.size()));
  }
  void ApplyClash(float floorDb, float balanceDb) override
  {
    onMain();
    clash++;
    last.clashFloorDb = floorDb;
    last.clashBalanceDb = balanceDb;
    if (forward)
      srecv_set_clash(forward, floorDb, balanceDb);
  }
  void ApplyRange(float lo, float hi) override
  {
    onMain();
    range++;
    last.rangeLo = lo;
    last.rangeHi = hi;
    if (forward)
      srecv_set_range(forward, lo, hi);
  }
  int calls() const { return sources + clash + range; }
};

/* A deadlock is the failure these guard against, so it must fail rather than
 * hang: past the bound the process says so and exits. */
struct Watchdog
{
  std::atomic<bool> done{false};
  std::thread t;
  explicit Watchdog(const char* what)
  : t([this, what] {
      const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
      while (!done.load())
      {
        if (std::chrono::steady_clock::now() > until)
        {
          std::fprintf(stderr, "HUNG: %s\n", what);
          std::_Exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    })
  {}
  ~Watchdog()
  {
    done.store(true);
    t.join();
  }
};

} // namespace

TEST_CASE("a load on the host's thread waits for the main thread's next service")
{
  st::Session s(opening());
  Recorder sink;
  const st::Fields loaded = variant(5);
  const IByteChunk chunk = chunk_of(loaded);

  int pos = 0;
  std::thread host([&] { pos = load_into(s, chunk); });
  host.join();
  REQUIRE(pos == chunk.Size());

  CHECK(sink.calls() == 0);
  CHECK(same(s.Applied(), opening()));

  /* SAVED STATE NEVER REGRESSES: a save before the main thread has applied the
   * load writes the load, byte for byte. */
  IByteChunk saved;
  REQUIRE(st::Save(saved, [](IByteChunk& c) { return g_noParams.SerializeParams(c); }, s.Get()));
  CHECK(same_bytes(saved, chunk));

  /* The next idle tick applies all of it, on the main thread, and says it was
   * a load so an open editor can be told. */
  CHECK(s.Service(sink));
  CHECK(sink.offMain.load() == 0);
  CHECK(sink.sources == 1);
  CHECK(sink.clash == 1);
  CHECK(sink.range == 1);
  CHECK(sink.last.sources == loaded.sources);
  CHECK(sink.last.clashFloorDb == loaded.clashFloorDb);
  CHECK(sink.last.rangeHi == loaded.rangeHi);
  CHECK(same(s.Applied(), loaded));

  /* And nothing twice. */
  CHECK_FALSE(s.Service(sink));
  CHECK(sink.calls() == 3);
}

TEST_CASE("a refused chunk marks nothing")
{
  st::Session s(opening());
  Recorder sink;
  IByteChunk empty;
  std::thread host([&] { CHECK(load_into(s, empty) == -1); });
  host.join();
  CHECK_FALSE(s.Service(sink));
  CHECK(sink.calls() == 0);
  CHECK(same(s.Get(), opening()));
}

TEST_CASE("an edit reaches the receiver only in the part that moved")
{
  st::Session s(opening());
  Recorder sink;
  s.Edit([](st::Fields& f) { f.view = {0, 1}; });
  CHECK_FALSE(s.Service(sink)); /* an edit is not a load */
  CHECK(sink.calls() == 0);     /* and the view is not the receiver's */
  CHECK(s.Applied().view == std::vector<int>{0, 1});

  s.Edit([](st::Fields& f) { f.sources = {2}; });
  s.Service(sink);
  CHECK(sink.sources == 1);
  CHECK(sink.clash == 0);
  CHECK(sink.range == 0);

  /* A rebuilt receiver is handed everything. */
  s.Service(sink, true);
  CHECK(sink.sources == 2);
  CHECK(sink.clash == 1);
  CHECK(sink.range == 1);
}

TEST_CASE("loads on the host's thread beside the main thread's service: the receiver is only "
          "ever called from the main thread, and the last load is what it ends up with")
{
  Watchdog dog("state loads beside the main thread's service");
  st::Session s(opening());
  Recorder sink;
  constexpr int kLoads = 300;
  std::vector<IByteChunk> chunks;
  for (int i = 0; i < kLoads; i++)
    chunks.push_back(chunk_of(variant(i)));

  std::atomic<bool> loading{true};
  std::atomic<int> refused{0};
  std::thread host([&] {
    for (const auto& c : chunks)
    {
      if (load_into(s, c) != c.Size())
        refused.fetch_add(1);
      /* A save between loads, as auval's stress test does. */
      IByteChunk out;
      if (!st::Save(out, [](IByteChunk& ch) { return g_noParams.SerializeParams(ch); }, s.Get()))
        refused.fetch_add(1);
    }
    loading.store(false);
  });
  while (loading.load())
    s.Service(sink);
  host.join();
  s.Service(sink);

  CHECK(refused.load() == 0);
  CHECK(sink.offMain.load() == 0);
  CHECK(sink.sources > 0);
  CHECK(same(s.Applied(), variant(kLoads - 1)));
  CHECK(sink.last.sources == variant(kLoads - 1).sources);
}

TEST_CASE("the same, against a running receiver: nothing waits forever")
{
  Watchdog dog("state loads beside a running receiver");
  /* A private set of buses, as tests/srecv_api.c takes: the slots probed here
   * must never be a Live session's. Set before the process's first bus call. */
  char ns[64];
  std::snprintf(ns, sizeof ns, "spectro_state.%d", int(getpid()));
  setenv("NIA_BUS_NS", ns, 1);
  srecv_t* r = srecv_new(48000.f, 8192, 1024, 256, 10.f, 20000.f, -96.f, 0.f);
  REQUIRE(r);
  REQUIRE(srecv_start(r) == 1);

  st::Session s(opening());
  Recorder sink;
  sink.forward = r;
  constexpr int kLoads = 100;
  std::vector<IByteChunk> chunks;
  for (int i = 0; i < kLoads; i++)
    chunks.push_back(chunk_of(variant(i)));

  std::atomic<bool> loading{true};
  std::thread host([&] {
    for (const auto& c : chunks)
      load_into(s, c);
    loading.store(false);
  });
  while (loading.load())
    s.Service(sink);
  host.join();
  s.Service(sink);

  CHECK(sink.offMain.load() == 0);
  CHECK(same(s.Applied(), variant(kLoads - 1)));
  /* No slot has a sender here, so the receiver draws the own channel alone. */
  CHECK(srecv_channels(r) == 1);
  srecv_free(r);
}
