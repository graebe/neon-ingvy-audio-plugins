/*
 * Trance Gate -- the pattern's journeys. See Patch.h.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "Patch.h"
#include "Params.h"
#include "ni/Wire.h"
#include "shell_state.h"

#include <vector>

namespace tg {
namespace patch {

static bool post(tg_shell_t* gate, std::initializer_list<const char*> kv)
{
  const std::vector<const char*> pairs(kv);
  return tg_shell_post(gate, pairs.data(), int(pairs.size() / 2)) == 1;
}

bool Post(tg_shell_t* gate, Edit edit, const std::string& arg)
{
  if (!gate) return false;
  switch (edit)
  {
    case Edit::Cursor:
      return post(gate, {"cursor", arg.c_str()});

    /* The cursor moves first because `step`, `step_amount` and `step_order`
     * edit whatever it is on -- in ONE command, so nothing can move it in
     * between. The engine clamps an order out of range rather than breaking
     * the permutation. */
    case Edit::Step:
    case Edit::Depth:
    case Edit::Order:
    {
      std::string idx, val;
      if (!ni::wire::split_pair(arg, idx, val)) return false;
      const char* key = edit == Edit::Step ? "step"
                      : edit == Edit::Depth ? "step_amount" : "step_order";
      return post(gate, {"cursor", idx.c_str(), key, val.c_str()});
    }

    /* The engine does not touch the playhead, so this is safe mid-bar. */
    case Edit::Randomize:
      return post(gate, {"randomize", arg.c_str()});

    /* Not a plain edit: the pasted patch's current slot is what the host's
     * parameters must show afterwards, which the shell arranges. */
    case Edit::Paste:
      return !arg.empty() && tg_shell_paste(gate, arg.c_str()) == 1;
  }
  return false;
}

/*
 * THE BLOB IS THE ENGINE'S, READ FROM WHAT IT PUBLISHED -- including any edit
 * still on its way to it, and the host's parameters as the next block will
 * push them. This was a copy kept beside the engine and written only on load,
 * so every edit made since was missing from every save; and without the
 * parameters, a project saved before audio ran would reload its current slot
 * with values the host never showed.
 */
bool Save(tg_shell_t* gate, iplug::IByteChunk& chunk, const PutParams& params, const HostValue& value)
{
  const int at = shell::state::Begin(chunk, kChunkVersion);
  if (!params(chunk)) return false;
  std::vector<char> blob(TG_STATE_MAX, '\0');
  double values[TG_P_COUNT];
  Values(value, values);
  if (!gate || tg_shell_save(gate, values, TG_P_COUNT, blob.data(), int(blob.size())) < 0)
    blob[0] = '\0';
  chunk.PutStr(blob.data());
  return shell::state::End(chunk, at);
}

int Load(tg_shell_t* gate, const iplug::IByteChunk& chunk, int startPos,
         const GetParams& check, const GetParams& apply, const HostValue& value)
{
  const shell::state::Header h = shell::state::Read(chunk, startPos);
  if (h.body < 0) return -1;

  /*
   * EVERYTHING IS READ BEFORE ANYTHING IS APPLIED, so a chunk that is refused
   * leaves the instance exactly as it was -- not with its parameters from the
   * bad chunk and its pattern from before it.
   *
   * THE BLOB IS NOT OPTIONAL. Every build has written one after the
   * parameters, if only an empty one, so a chunk without it is not a chunk
   * this plugin wrote, and random bytes must not load "successfully".
   */
  int pos = check(chunk, h.body);
  if (pos < 0) return -1;
  WDL_String blob;
  const int after = shell::state::GetStr(chunk, blob, pos);
  if (after < pos) return -1;

  if (apply(chunk, h.body) != pos) return -1;
  /* Empty in a chunk from a build that lost the pattern on save: nothing to
   * restore, so the engine keeps what it has rather than being reset, and the
   * restored parameters reach it as host edits do. */
  if (gate && blob.Get() && *blob.Get())
  {
    double values[TG_P_COUNT];
    Values(value, values);
    tg_shell_load(gate, blob.Get(), values, TG_P_COUNT);
  }
  return shell::state::Finish(h, after);
}

void Values(const HostValue& value, double (&out)[TG_P_COUNT])
{
  for (int i = 0; i < TG_P_COUNT; i++)
    out[i] = params::ToEngine(i, value(i));
}

bool Follow(tg_shell_t* gate, const HostValue& value, const SetHost& set)
{
  double now[TG_P_COUNT];
  if (!gate || !tg_shell_take_params(gate, now, TG_P_COUNT)) return false;
  for (int i = 0; i < TG_P_COUNT; i++)
  {
    if (i == TG_P_SLOT || params::SameInEngine(i, value(i), now[i])) continue;
    set(i, params::FromEngine(i, now[i]));
  }
  return true;
}

} // namespace patch
} // namespace tg
