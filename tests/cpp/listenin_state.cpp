// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's state chunk, saved and reloaded as a host does.
 *
 * clap-validator's state-reproducibility and state-invalid tests, on the
 * plugin's own State.cpp and a real IParam (param_host.h).
 *
 * And the Session: state loads and saves on a host thread of their own beside
 * the main thread taking the label for the bus -- what auval -stress does. The
 * plugin's UnserializeState and SerializeState are Session::Load and
 * Session::Label, so this is the plugin's path. Under -DNI_SANITIZE=thread it is
 * also the race detector's.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "State.h"
#include "param_host.h"
#include "shell_state.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
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

/* ------------------------------------------------------------- session -- */

namespace {

/* A deadlock must fail rather than hang: past the bound the process says so. */
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

int load_into(st::Session& s, Instance& host, const IByteChunk& chunk)
{
  return s.Load(
    chunk, 0,
    [&](const IByteChunk& c, int pos) { return shell::state::CheckParams(c, pos, host.host); },
    [&](const IByteChunk& c, int pos) { return host.host.UnserializeParams(c, pos); });
}

IByteChunk chunk_labelled(const std::string& label)
{
  Instance a;
  a.label = label;
  IByteChunk c;
  REQUIRE(a.save(c));
  return c;
}

} // namespace

TEST_CASE("a load on the host's thread is taken by the main thread, and saved before it is")
{
  st::Session s;
  Instance host;
  const IByteChunk chunk = chunk_labelled("Pad bus");
  int pos = 0;
  std::thread t([&] { pos = load_into(s, host, chunk); });
  t.join();
  REQUIRE(pos == chunk.Size());

  /* SAVED STATE NEVER REGRESSES: the load, before the main thread took it. */
  CHECK(s.Label() == "Pad bus");

  std::string label;
  bool loaded = false;
  CHECK(s.Take(label, loaded));
  CHECK(loaded);
  CHECK(label == "Pad bus");
  CHECK_FALSE(s.Take(label, loaded));
  CHECK_FALSE(loaded);
}

TEST_CASE("an edit is taken, but is not a load")
{
  st::Session s;
  s.Edit("Kick");
  std::string label;
  bool loaded = true;
  CHECK(s.Take(label, loaded));
  CHECK_FALSE(loaded);
  CHECK(label == "Kick");
}

TEST_CASE("a refused chunk marks nothing")
{
  st::Session s;
  Instance host;
  IByteChunk empty;
  CHECK(load_into(s, host, empty) == -1);
  std::string label;
  bool loaded = false;
  CHECK_FALSE(s.Take(label, loaded));
}

TEST_CASE("loads and saves on the host's thread beside the main thread's edits and takes")
{
  Watchdog dog("listen-in state loads beside the main thread");
  st::Session s;
  Instance host;
  constexpr int kLoads = 2000;
  std::vector<IByteChunk> chunks;
  for (int i = 0; i < kLoads; i++)
    chunks.push_back(chunk_labelled("Source " + std::to_string(i)));

  std::atomic<bool> loading{true};
  std::atomic<int> refused{0}, torn{0};
  std::thread t([&] {
    for (const auto& c : chunks)
    {
      if (load_into(s, host, c) != c.Size())
        refused.fetch_add(1);
      /* A save between loads, as auval's stress test does: always a whole
       * label, one some thread wrote. */
      const std::string saved = s.Label();
      if (saved.rfind("Source ", 0) != 0 && saved != "Typed")
        torn.fetch_add(1);
    }
    loading.store(false);
  });

  /* The main thread: the editor typing, and the idle tick taking the label. */
  std::string applied;
  bool loaded = false;
  while (loading.load())
  {
    s.Edit("Typed");
    s.Take(applied, loaded);
  }
  t.join();

  CHECK(refused.load() == 0);
  CHECK(torn.load() == 0);
  /* What the main thread holds after its last take is what a save writes. */
  s.Take(applied, loaded);
  CHECK(applied == s.Label());

  /* And one more load from the host, with the editor quiet: the next take is
   * that load. */
  std::thread again([&] { CHECK(load_into(s, host, chunks.front()) == chunks.front().Size()); });
  again.join();
  CHECK(s.Take(applied, loaded));
  CHECK(loaded);
  CHECK(applied == "Source 0");
}
