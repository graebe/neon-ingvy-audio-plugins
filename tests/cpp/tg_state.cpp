/*
 * The Trance Gate's pattern survives a save and a reload.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Driven through Patch.cpp, which is the code the plugin runs: an edit goes in
 * the way the editor's message does, the chunk comes out the way the host asks
 * for it, and a second engine loads it the way a reopened project would. Only
 * iPlug2's parameter block is stood in for, by fifteen doubles -- the layout
 * SerializeParams writes.
 *
 * The pattern was lost on every save before this: the chunk carried a copy of
 * the blob taken on LOAD, so no edit made afterwards was ever written.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Params.h"
#include "Patch.h"
#include "param_host.h"
#include "shell_state.h"

#include <algorithm>
#include <cstring>
#include <random>

#include <string>
#include <vector>

using namespace tg::patch;
using iplug::IByteChunk;

namespace {

constexpr int kParams = 15;

/* iPlug2's SerializeParams: one double per parameter, in order. */
bool put_params(IByteChunk& c)
{
  for (int i = 0; i < kParams; i++)
  {
    const double v = 1.0 + i;
    c.Put(&v);
  }
  return true;
}

int get_params(const IByteChunk& c, int pos)
{
  for (int i = 0; i < kParams && pos >= 0; i++)
  {
    double v = 0.0;
    pos = c.Get(&v, pos);
    if (pos >= 0 && v != 1.0 + i) return -1;
  }
  return pos;
}

std::string read(tg_shell_t* s, const char* key)
{
  std::vector<char> buf(TG_STATE_MAX, '\0');
  const int n = tg_shell_read(s, key, buf.data(), int(buf.size()));
  REQUIRE(n >= 0);
  return std::string(buf.data(), size_t(n));
}

/* One audio block: the edits land, the readouts are republished. */
void block(tg_shell_t* s)
{
  tg_core_t* c = tg_shell_begin(s);
  float l[64] = {}, r[64] = {};
  tg_core_process_f32_split(c, l, r, 64, nullptr);
  tg_shell_end(s, 64);
}

/* Edits of every kind the editor sends, none of them a host parameter. */
void edit(tg_shell_t* s)
{
  REQUIRE(Post(s, Edit::Step, "1:1"));
  REQUIRE(Post(s, Edit::Step, "2:2"));
  REQUIRE(Post(s, Edit::Step, "4:0"));
  REQUIRE(Post(s, Edit::Depth, "2:0.5"));
  REQUIRE(Post(s, Edit::Order, "1:3"));
  REQUIRE(Post(s, Edit::Cursor, "7"));
}

struct Gate
{
  tg_shell_t* s = tg_shell_create(48000.0);
  ~Gate() { tg_shell_destroy(s); }
};

} // namespace

TEST_CASE("an edited pattern survives save and reload")
{
  Gate a;
  const std::string fresh = read(a.s, "state");
  edit(a.s);
  block(a.s);
  const std::string edited = read(a.s, "state");
  REQUIRE(edited != fresh);

  IByteChunk chunk;
  REQUIRE(Save(a.s, chunk, put_params));

  Gate b;
  CHECK(Load(b.s, chunk, 0, get_params, get_params) == chunk.Size());
  CHECK(read(b.s, "state") == edited);
  block(b.s);
  CHECK(read(b.s, "state") == edited);
  CHECK(read(b.s, "length") == read(a.s, "length"));
}

TEST_CASE("a save made before the audio thread has run still has the edit")
{
  /* A host with its audio engine off still lets the user edit and save. */
  Gate a;
  edit(a.s);
  REQUIRE(Post(a.s, Edit::Randomize, ""));
  IByteChunk chunk;
  REQUIRE(Save(a.s, chunk, put_params));

  Gate b;
  REQUIRE(Load(b.s, chunk, 0, get_params, get_params) == chunk.Size());
  const std::string loaded = read(b.s, "state");
  CHECK(loaded == read(a.s, "state"));

  /* And what plays once the audio starts is what was saved. */
  block(a.s);
  CHECK(read(a.s, "state") == loaded);
}

TEST_CASE("a paste is saved like any other edit")
{
  Gate a, b;
  edit(a.s);
  REQUIRE(Post(b.s, Edit::Paste, read(a.s, "state")));
  IByteChunk chunk;
  REQUIRE(Save(b.s, chunk, put_params));
  Gate c;
  REQUIRE(Load(c.s, chunk, 0, get_params, get_params) == chunk.Size());
  CHECK(read(c.s, "state") == read(a.s, "state"));
}

TEST_CASE("the chunk starts with the versioned header")
{
  Gate a;
  IByteChunk chunk;
  REQUIRE(Save(a.s, chunk, put_params));
  const shell::state::Header h = shell::state::Read(chunk, 0);
  CHECK_FALSE(h.legacy);
  CHECK(h.version == kChunkVersion);
  CHECK(h.body == shell::state::kHeaderBytes);
  CHECK(h.end == chunk.Size());
}

TEST_CASE("a chunk from an earlier build -- no header -- still loads")
{
  /* What every build before the header wrote: parameters, then the blob. */
  Gate a;
  edit(a.s);
  const std::string blob = read(a.s, "state");
  IByteChunk legacy;
  put_params(legacy);
  legacy.PutStr(blob.c_str());

  Gate b;
  CHECK(Load(b.s, legacy, 0, get_params, get_params) == legacy.Size());
  CHECK(read(b.s, "state") == blob);
}

TEST_CASE("an earlier build's empty blob leaves the engine as it was")
{
  /* The lost-pattern bug wrote "" for a pattern nobody had reloaded. Loading
   * one must not reset anything -- there is nothing in it to restore. */
  IByteChunk legacy;
  put_params(legacy);
  legacy.PutStr("");

  Gate b;
  const std::string before = read(b.s, "state");
  CHECK(Load(b.s, legacy, 0, get_params, get_params) == legacy.Size());
  CHECK(read(b.s, "state") == before);
}

TEST_CASE("a later version's extra fields are skipped, and the position is past them")
{
  Gate a;
  edit(a.s);
  IByteChunk chunk;
  const int at = shell::state::Begin(chunk, kChunkVersion + 1);
  put_params(chunk);
  const std::string blob = read(a.s, "state");
  chunk.PutStr(blob.c_str());
  chunk.PutStr("a field from the future");
  REQUIRE(shell::state::End(chunk, at));
  const int32_t bypass = 1; /* what a VST3 host finds after the chunk */
  chunk.Put(&bypass);

  Gate b;
  CHECK(Load(b.s, chunk, 0, get_params, get_params) == chunk.Size() - int(sizeof bypass));
  CHECK(read(b.s, "state") == blob);
}

TEST_CASE("a header whose size runs past the end is refused, not misread")
{
  IByteChunk chunk;
  const int at = shell::state::Begin(chunk, kChunkVersion);
  put_params(chunk);
  REQUIRE(shell::state::End(chunk, at));
  chunk.Resize(chunk.Size() - 8);

  Gate b;
  CHECK(Load(b.s, chunk, 0, get_params, get_params) == -1);
}

TEST_CASE("a malformed edit is refused rather than sent")
{
  Gate a;
  CHECK_FALSE(Post(a.s, Edit::Step, "no colon"));
  CHECK_FALSE(Post(a.s, Edit::Paste, ""));
  CHECK_FALSE(Post(nullptr, Edit::Cursor, "1"));
}

/* ----------------------------------------- as clap-validator checks a state */

namespace {

struct Instance
{
  test::ParamHost host{kNumParams};
  Gate gate;
  Instance() { tg::params::Declare([this](int i) { return host.GetParam(i); }); }

  bool save(IByteChunk& chunk) const
  {
    return Save(gate.s, chunk, [this](IByteChunk& c) { return host.SerializeParams(c); });
  }

  int load(const IByteChunk& chunk)
  {
    return Load(
      gate.s, chunk, 0,
      [this](const IByteChunk& c, int pos) { return shell::state::CheckParams(c, pos, host); },
      [this](const IByteChunk& c, int pos) { return host.UnserializeParams(c, pos); });
  }
};

bool same_bytes(const IByteChunk& a, const IByteChunk& b)
{
  return a.Size() == b.Size() && std::equal(a.GetData(), a.GetData() + a.Size(), b.GetData());
}

} // namespace

TEST_CASE("random parameters and a pattern: save, a fresh instance, load, save again")
{
  /* state-reproducibility-binary: the parameters come back exactly, and the
   * second save is the first byte for byte. */
  std::mt19937 rng(20260930);
  for (int round = 0; round < 20; round++)
  {
    Instance a;
    a.host.Randomize(rng);
    edit(a.gate.s);
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

TEST_CASE("an empty chunk is refused")
{
  Instance b;
  IByteChunk empty;
  CHECK(b.load(empty) == -1);
}

TEST_CASE("random bytes are refused, and change nothing")
{
  /* state-invalid-random: three megabytes of noise. Loading them used to
   * succeed -- fifteen doubles, clamped, and a pattern if the dice allowed. */
  std::mt19937 rng(7);
  for (int round = 0; round < 3; round++)
  {
    std::vector<uint8_t> noise(1024 * 1024);
    for (auto& b : noise) b = uint8_t(rng());
    IByteChunk chunk;
    chunk.PutBytes(noise.data(), int(noise.size()));

    Instance b;
    const std::vector<double> before = b.host.Values();
    const std::string state = read(b.gate.s, "state");
    CHECK(b.load(chunk) == -1);
    CHECK(b.host.Values() == before);
    CHECK(read(b.gate.s, "state") == state);
  }
}

TEST_CASE("parameters with no pattern after them are refused, and none is applied")
{
  Instance a;
  std::mt19937 rng(3);
  a.host.Randomize(rng);
  IByteChunk legacy;
  REQUIRE(a.host.SerializeParams(legacy));

  Instance b;
  const std::vector<double> before = b.host.Values();
  CHECK(b.load(legacy) == -1);
  CHECK(b.host.Values() == before);
}

TEST_CASE("a stepped parameter that is not a whole number was not written by iPlug2")
{
  Instance a;
  IByteChunk chunk;
  REQUIRE(a.host.SerializeParams(chunk));
  chunk.PutStr("");
  /* Rate, the third double, nudged off its step. */
  double rate = 7.25;
  std::memcpy(chunk.GetData() + 2 * sizeof(double), &rate, sizeof rate);

  Instance b;
  CHECK(b.load(chunk) == -1);
}
