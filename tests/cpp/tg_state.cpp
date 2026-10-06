// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's pattern survives a save and a reload.
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

#include <string>
#include <vector>

using namespace tg::patch;
using iplug::IByteChunk;

namespace {

constexpr int kParams = kNumParams;

/* The plugin's parameters at their defaults: what a chunk's parameter block
 * holds in the cases below that are about the pattern, not the parameters. */
double default_value(int i)
{
  static const test::ParamHost host = [] {
    test::ParamHost h(kNumParams);
    tg::params::Declare([&](int k) { return h.GetParam(k); });
    return h;
  }();
  return host.GetParam(i)->Value();
}

/* iPlug2's SerializeParams: one double per parameter, in order. */
bool put_params(IByteChunk& c)
{
  for (int i = 0; i < kParams; i++)
  {
    const double v = default_value(i);
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
    if (pos >= 0 && v != default_value(i)) return -1;
  }
  return pos;
}

int load_defaults(tg_shell_t* s, const IByteChunk& chunk)
{
  return Load(s, chunk, 0, get_params, get_params, default_value);
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

/* The system's clipboard, stood in for: what the plugin's Copy and Paste see
 * of NSPasteboard, without touching the clipboard of whoever runs the tests. */
struct FakeClipboard final : ni::Clipboard
{
  std::string text;
  bool holdsText = true;
  bool writable = true;
  explicit FakeClipboard(std::string t = {}) : text(std::move(t)) {}
  bool Read(std::string& out) override
  {
    if (!holdsText) return false;
    out = text;
    return true;
  }
  bool Write(const std::string& t) override
  {
    if (!writable) return false;
    text = t;
    holdsText = true;
    return true;
  }
};

} // namespace

/* A slot's whole sound, as the host would hold it: one value per parameter
 * but Slot, distinct from the defaults and from the other slot's. */
std::vector<std::pair<int, double>> sound(int k)
{
  return {{kLength, 5.0 + k},  {kRate, 3.0 + k},     {kLegato, 1.0},      {kTimeMode, 1.0},
          {kCurve, double((1 + k) % 3)},   {kAmount, 40.0 + k},  {kWidth, 60.0 + k},  {kAttack, 20.0 + k},
          {kDecay, 30.0 + k},  {kSustain, 50.0 + k}, {kRelease, 70.0 + k}, {kFade, 80.0 + k},
          {kFadeSoft, 1.0},    {kFadeDir, 1.0}};
}

TEST_CASE("an edited pattern survives save and reload")
{
  Gate a;
  const std::string fresh = read(a.s, "state");
  edit(a.s);
  block(a.s);
  const std::string edited = read(a.s, "state");
  REQUIRE(edited != fresh);

  IByteChunk chunk;
  REQUIRE(Save(a.s, chunk, put_params, default_value));

  Gate b;
  CHECK(load_defaults(b.s, chunk) == chunk.Size());
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
  REQUIRE(Save(a.s, chunk, put_params, default_value));

  Gate b;
  REQUIRE(load_defaults(b.s, chunk) == chunk.Size());
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
  FakeClipboard clip(read(a.s, "state"));
  std::string words;
  REQUIRE(Paste(b.s, 1, clip, words));
  IByteChunk chunk;
  REQUIRE(Save(b.s, chunk, put_params, default_value));
  Gate c;
  REQUIRE(load_defaults(c.s, chunk) == chunk.Size());
  CHECK(read(c.s, "state") == read(a.s, "state"));
}

TEST_CASE("the chunk starts with the versioned header")
{
  Gate a;
  IByteChunk chunk;
  REQUIRE(Save(a.s, chunk, put_params, default_value));
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
  CHECK(load_defaults(b.s, legacy) == legacy.Size());
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
  CHECK(load_defaults(b.s, legacy) == legacy.Size());
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
  CHECK(load_defaults(b.s, chunk) == chunk.Size() - int(sizeof bypass));
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
  CHECK(load_defaults(b.s, chunk) == -1);
}

TEST_CASE("a malformed edit is refused rather than sent")
{
  Gate a;
  CHECK_FALSE(Post(a.s, Edit::Step, "no colon"));
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
    return Save(
      gate.s, chunk, [this](IByteChunk& c) { return host.SerializeParams(c); },
      [this](int i) { return value(i); });
  }

  int load(const IByteChunk& chunk)
  {
    return Load(
      gate.s, chunk, 0,
      [this](const IByteChunk& c, int pos) { return shell::state::CheckParams(c, pos, host); },
      [this](const IByteChunk& c, int pos) { return host.UnserializeParams(c, pos); },
      [this](int i) { return host.GetParam(i)->Value(); });
  }

  double value(int i) const { return host.GetParam(i)->Value(); }

  /* One audio block as the plugin runs it: the edits land, every host
   * parameter is pushed, the readouts are published. */
  void block()
  {
    tg_core_t* c = tg_shell_begin(gate.s);
    double v[TG_P_COUNT];
    Values([this](int i) { return value(i); }, v);
    tg_shell_push(gate.s, c, v, TG_P_COUNT);
    float l[64] = {}, r[64] = {};
    tg_core_process_f32_split(c, l, r, 64, nullptr);
    tg_shell_end(gate.s, 64);
  }

  /* The idle tick: the host follows a switch, as SetParamFromPlugin does. */
  bool follow()
  {
    return Follow(
      gate.s, [this](int i) { return value(i); },
      [this](int i, double v) { host.GetParam(i)->Set(v); });
  }

  /* Host automation of one parameter, and the block that pushes it. */
  void automate(int i, double v)
  {
    host.GetParam(i)->Set(v);
    block();
    follow();
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

/* --------------------------------------------- the host mirrors the current slot */

TEST_CASE("a slot switch moves every host parameter to the new slot's values")
{
  Instance a;
  a.block();
  const std::vector<double> fresh = a.host.Values();
  for (auto [i, v] : sound(0)) a.automate(i, v);
  const std::vector<double> slot1 = a.host.Values();

  a.automate(kSlot, 2.0);
  std::vector<double> want = fresh;
  want[kSlot] = 2.0;
  for (int i = 0; i < kNumParams; i++) /* slot 2 was never edited: every default */
    CHECK_MESSAGE(tg::params::SameInEngine(i, a.value(i), tg::params::ToEngine(i, want[size_t(i)])), i);

  for (auto [i, v] : sound(1)) a.automate(i, v);
  const std::vector<double> slot2 = a.host.Values();
  a.automate(kSlot, 1.0);
  for (int i = 0; i < kNumParams; i++)
    CHECK_MESSAGE(tg::params::SameInEngine(i, a.value(i), tg::params::ToEngine(i, slot1[size_t(i)])), i);
  a.automate(kSlot, 2.0);
  for (int i = 0; i < kNumParams; i++)
    CHECK_MESSAGE(tg::params::SameInEngine(i, a.value(i), tg::params::ToEngine(i, slot2[size_t(i)])), i);
}

TEST_CASE("host automation writes the current slot and no other")
{
  Instance a;
  a.block();
  a.automate(kSlot, 3.0);
  a.automate(kAmount, 25.0);
  a.automate(kRate, 2.0);
  a.automate(kSlot, 4.0);
  CHECK(a.value(kAmount) == 100.0);
  CHECK(a.value(kRate) == double(tg_core_rate_default()));
  a.automate(kSlot, 3.0);
  CHECK(a.value(kAmount) == 25.0);
  CHECK(a.value(kRate) == 2.0);
}

TEST_CASE("nothing to follow is not a switch")
{
  Instance a;
  a.block();
  a.automate(kAmount, 33.0);
  CHECK_FALSE(a.follow());
  CHECK_FALSE(Follow(nullptr, [](int) { return 0.0; }, [](int, double) {}));
}

TEST_CASE("every slot's sound survives a save and a reload")
{
  Instance a;
  a.block();
  for (int k = 0; k < 3; k++)
  {
    a.automate(kSlot, 1.0 + k);
    for (auto [i, v] : sound(k)) a.automate(i, v);
  }
  a.automate(kSlot, 2.0);
  IByteChunk first;
  REQUIRE(a.save(first));
  const std::string blob = read(a.gate.s, "state");
  CHECK(blob.find("\"sv\":7") != std::string::npos);

  Instance b;
  REQUIRE(b.load(first) == first.Size());
  CHECK(b.host.Values() == a.host.Values());
  CHECK(read(b.gate.s, "state") == blob);
  b.block();
  CHECK_FALSE(b.follow()); /* nothing the host holds is stale */
  IByteChunk second;
  REQUIRE(b.save(second));
  CHECK(same_bytes(first, second));
  for (int k : {0, 2, 1})
  {
    b.automate(kSlot, 1.0 + k);
    for (const auto& [param, v] : sound(k))
    {
      const int i = param;
      CHECK_MESSAGE(tg::params::SameInEngine(i, b.value(i), tg::params::ToEngine(i, v)), "slot ", k, " param ", i);
    }
  }
}

TEST_CASE("an older project loads one sound into all eight slots")
{
  /* A v6 chunk: the parameters, and a blob with one instance-wide sound. */
  Instance a;
  std::mt19937 rng(11);
  a.host.Randomize(rng);
  a.host.GetParam(kSlot)->Set(3.0);
  IByteChunk legacy;
  const int at = shell::state::Begin(legacy, kChunkVersion);
  REQUIRE(a.host.SerializeParams(legacy));
  legacy.PutStr("{\"sv\":6,\"slot\":2,\"rate\":\"1/8\",\"attack\":12.50,\"amount\":0.700,"
                "\"p0\":\"5555:0:16\",\"p2\":\"FFFF:0:8\"}");
  REQUIRE(shell::state::End(legacy, at));

  Instance b;
  /* A different sound in another slot beforehand, which the load replaces. */
  b.block();
  b.automate(kSlot, 6.0);
  b.automate(kAmount, 12.0);
  REQUIRE(b.load(legacy) == legacy.Size());
  CHECK(b.host.Values() == a.host.Values());
  const std::string state = read(b.gate.s, "state");
  CHECK(state.find("\"sv\":7") == 1);
  CHECK(state.find("\"s0\"") == std::string::npos); /* no slot differs from the current */
  CHECK(state.find("\"s5\"") == std::string::npos);
  b.block();
  const std::vector<double> loaded = b.host.Values();
  b.follow(); /* the load moved the host's Slot: told what it already holds */
  CHECK(b.host.Values() == loaded);
  for (int k = 0; k < 8; k++)
  {
    b.automate(kSlot, 1.0 + k);
    for (int i = 1; i < kNumParams; i++)
    {
      if (i == kLength) continue; /* the pattern's own */
      CHECK_MESSAGE(tg::params::SameInEngine(i, b.value(i), tg::params::ToEngine(i, a.value(i))), "slot ", k, " param ", i);
    }
  }
}

TEST_CASE("a paste moves the host to the pasted slot's values")
{
  Instance a, b;
  a.block();
  for (auto [i, v] : sound(0)) a.automate(i, v);
  b.block();
  /* A whole patch on the clipboard: what Copy wrote before slots, and the
   * Move's patch. It still replaces everything. */
  FakeClipboard clip(read(a.gate.s, "state"));
  std::string words;
  REQUIRE(Paste(b.gate.s, 1, clip, words));
  CHECK(words == "Pasted a whole patch into all 8 slots.");
  b.block();
  CHECK(b.follow());
  for (int i = 0; i < kNumParams; i++)
    CHECK_MESSAGE(tg::params::SameInEngine(i, b.value(i), tg::params::ToEngine(i, a.value(i))), i);
}

/* ------------------------------------------------------------------ slot files */

namespace {

/* A file in the build's temporary directory, removed afterwards. */
struct TempFile
{
  std::string path;
  explicit TempFile(const char* name)
  : path(std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/tg_state_" + name)
  {
    std::remove(path.c_str());
  }
  ~TempFile() { std::remove(path.c_str()); }
  std::string text() const
  {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
  }
  void write(const std::string& t) const { std::ofstream(path, std::ios::binary) << t; }
};

} // namespace

TEST_CASE("a slot file's name says what it holds")
{
  CHECK(FileName(FileKind::Slot, 3) == "NI Trance Gate Slot 3.nitgslot");
  CHECK(FileName(FileKind::Bank, 3) == "NI Trance Gate Bank.nitgbank");
}

TEST_CASE("an exported slot imports into another instance's current slot, and its host follows")
{
  Instance a;
  a.block();
  for (auto [i, v] : sound(0)) a.automate(i, v);
  TempFile file("slot.nitgslot");
  std::string words;
  REQUIRE(ExportFile(a.gate.s, [&](int i) { return a.value(i); }, FileKind::Slot, 1, file.path, words));
  CHECK(words == "Exported slot 1 to tg_state_slot.nitgslot.");
  CHECK(file.text().find("\"format\": \"ni-trance-gate-slot\"") != std::string::npos);

  Instance b;
  b.block();
  b.automate(kSlot, 5.0);
  REQUIRE(ImportFile(b.gate.s, 5, file.path, words));
  CHECK(words == "Imported tg_state_slot.nitgslot into slot 5.");
  b.block();
  CHECK(b.follow());
  for (int i = 1; i < kNumParams; i++)
    CHECK_MESSAGE(tg::params::SameInEngine(i, b.value(i), tg::params::ToEngine(i, a.value(i))), i);
  /* Slot 1 of b is untouched. */
  b.automate(kSlot, 1.0);
  CHECK(b.value(kAmount) == 100.0);

  /* And it is saved with the project. */
  IByteChunk chunk;
  REQUIRE(b.save(chunk));
  Instance c;
  REQUIRE(c.load(chunk) == chunk.Size());
  c.block();
  c.automate(kSlot, 5.0);
  for (int i = 1; i < kNumParams; i++)
    CHECK_MESSAGE(tg::params::SameInEngine(i, c.value(i), tg::params::ToEngine(i, a.value(i))), i);
}

TEST_CASE("an exported bank imports all eight slots")
{
  Instance a;
  a.block();
  for (int k = 0; k < 3; k++)
  {
    a.automate(kSlot, 1.0 + k);
    for (auto [i, v] : sound(k)) a.automate(i, v);
  }
  TempFile file("bank.nitgbank");
  std::string words;
  REQUIRE(ExportFile(a.gate.s, [&](int i) { return a.value(i); }, FileKind::Bank, 3, file.path, words));
  CHECK(words == "Exported all 8 slots to tg_state_bank.nitgbank.");
  Instance b;
  b.block();
  REQUIRE(ImportFile(b.gate.s, 1, file.path, words));
  CHECK(words == "Imported all 8 slots from tg_state_bank.nitgbank.");
  b.block();
  b.follow();
  TempFile again("again.nitgbank");
  REQUIRE(ExportFile(b.gate.s, [&](int i) { return b.value(i); }, FileKind::Bank, 1, again.path, words));
  CHECK(again.text() == file.text());
}

TEST_CASE("a file that is not a slot file is refused with a reason and changes nothing")
{
  Instance b;
  b.block();
  const std::string before = read(b.gate.s, "state");
  std::string words;
  TempFile file("bad.nitgslot");
  file.write("{\"format\": \"ni-trance-gate-slot\", \"version\": 7}");
  CHECK_FALSE(ImportFile(b.gate.s, 1, file.path, words));
  CHECK(words.find("Failed to import tg_state_bad.nitgslot: The file is version 7") == 0);
  file.write(std::string("{\0}", 3));
  CHECK_FALSE(ImportFile(b.gate.s, 1, file.path, words));
  TempFile missing("missing.nitgslot");
  CHECK_FALSE(ImportFile(b.gate.s, 1, missing.path, words));
  CHECK(words == "Failed to open tg_state_missing.nitgslot.");
  b.block();
  CHECK_FALSE(b.follow());
  CHECK(read(b.gate.s, "state") == before);
  CHECK_FALSE(ImportFile(nullptr, 1, file.path, words));
  CHECK_FALSE(ExportFile(nullptr, [](int) { return 0.0; }, FileKind::Slot, 1, file.path, words));
  CHECK_FALSE(ExportFile(b.gate.s, [&](int i) { return b.value(i); }, FileKind::Slot, 1,
                         "/nonexistent-dir/x.nitgslot", words));
  CHECK(words == "Failed to write x.nitgslot.");
}

/* ------------------------------------------------------------ copy and paste */

TEST_CASE("copy slot 1, switch to slot 2, paste: slot 2 sounds like slot 1, and slot 1 is as it was")
{
  Instance a;
  a.block();
  for (auto [i, v] : sound(0)) a.automate(i, v);
  FakeClipboard clip;
  std::string words;
  REQUIRE(CopySlot(a.gate.s, [&](int i) { return a.value(i); }, 1, clip, words));
  CHECK(words == "Copied slot 1.");
  CHECK(clip.text.find("\"format\": \"ni-trance-gate-slot\"") != std::string::npos);
  const std::string one = clip.text;

  a.automate(kSlot, 2.0);
  CHECK(a.value(kAmount) == 100.0); /* slot 2 is fresh */
  REQUIRE(Paste(a.gate.s, 2, clip, words));
  CHECK(words == "Pasted into slot 2.");
  a.block();
  CHECK(a.follow());
  for (const auto& [i, v] : sound(0))
  {
    const int idx = i;
    CHECK_MESSAGE(tg::params::SameInEngine(i, a.value(i), tg::params::ToEngine(i, v)), idx);
  }

  /* Slot 2 copies back as slot 1 did, byte for byte. */
  FakeClipboard again;
  REQUIRE(CopySlot(a.gate.s, [&](int i) { return a.value(i); }, 2, again, words));
  CHECK(again.text == one);

  /* Slot 1 is untouched, and so is every other slot. */
  a.automate(kSlot, 1.0);
  for (const auto& [i, v] : sound(0))
  {
    const int idx = i;
    CHECK_MESSAGE(tg::params::SameInEngine(i, a.value(i), tg::params::ToEngine(i, v)), idx);
  }
  a.automate(kSlot, 3.0);
  CHECK(a.value(kAmount) == 100.0);
}

TEST_CASE("a bank on the clipboard replaces all eight slots")
{
  Instance a;
  a.block();
  a.automate(kSlot, 4.0);
  for (auto [i, v] : sound(1)) a.automate(i, v);
  TempFile file("clip.nitgbank");
  std::string words;
  REQUIRE(ExportFile(a.gate.s, [&](int i) { return a.value(i); }, FileKind::Bank, 4, file.path, words));
  Instance b;
  b.block();
  FakeClipboard clip(file.text());
  REQUIRE(Paste(b.gate.s, 1, clip, words));
  CHECK(words == "Pasted all 8 slots.");
  b.block();
  b.follow();
  b.automate(kSlot, 4.0);
  for (const auto& [i, v] : sound(1))
  {
    const int idx = i;
    CHECK_MESSAGE(tg::params::SameInEngine(i, b.value(i), tg::params::ToEngine(i, v)), idx);
  }
}

TEST_CASE("what is not a Trance Gate slot is refused with a reason and changes nothing")
{
  Instance b;
  b.block();
  const std::string before = read(b.gate.s, "state");
  std::string words;

  FakeClipboard garbage("https://example.com/not-a-slot");
  CHECK_FALSE(Paste(b.gate.s, 1, garbage, words));
  CHECK(words == "Failed to paste: The clipboard doesn't hold a Trance Gate slot.");

  /* A NUL inside: refused whole, not cut short into a good patch before it. */
  FakeClipboard nul(std::string("{\"sv\":7}") + '\0' + "trailing");
  CHECK_FALSE(Paste(b.gate.s, 1, nul, words));
  CHECK(words == "Failed to paste: The clipboard doesn't hold a Trance Gate slot.");

  FakeClipboard huge(std::string(1 << 20, ' ') + read(b.gate.s, "state"));
  CHECK_FALSE(Paste(b.gate.s, 1, huge, words));
  CHECK(words == "Failed to paste: The clipboard doesn't hold a Trance Gate slot.");

  FakeClipboard nothing;
  nothing.holdsText = false; /* an image, say */
  CHECK_FALSE(Paste(b.gate.s, 1, nothing, words));
  CHECK(words == "Failed to paste: The clipboard is empty.");

  CHECK_FALSE(Paste(nullptr, 1, garbage, words));
  CHECK(words == "Failed to paste: the plugin is not ready.");

  b.block();
  CHECK_FALSE(b.follow());
  CHECK(read(b.gate.s, "state") == before);

  FakeClipboard locked;
  locked.writable = false;
  CHECK_FALSE(CopySlot(b.gate.s, [&](int i) { return b.value(i); }, 1, locked, words));
  CHECK(words == "Failed to copy: the clipboard could not be written.");
  CHECK_FALSE(CopySlot(nullptr, [](int) { return 0.0; }, 1, locked, words));
  CHECK(words == "Failed to copy: the plugin is not ready.");
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
