/*
 * Trance Gate -- the pattern's journeys. See Patch.h.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "Patch.h"
#include "Wire.h"
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
      if (!wire::split_pair(arg, idx, val)) return false;
      const char* key = edit == Edit::Step ? "step"
                      : edit == Edit::Depth ? "step_amount" : "step_order";
      return post(gate, {"cursor", idx.c_str(), key, val.c_str()});
    }

    /* The engine does not touch the playhead, so this is safe mid-bar. */
    case Edit::Randomize:
      return post(gate, {"randomize", arg.c_str()});

    case Edit::Paste:
      return !arg.empty() && post(gate, {"state", arg.c_str()});
  }
  return false;
}

/*
 * THE BLOB IS THE ENGINE'S, READ FROM WHAT IT PUBLISHED -- including any edit
 * still on its way to it. This was a copy kept beside the engine and written
 * only on load, so every edit made since was missing from every save.
 */
bool Save(tg_shell_t* gate, iplug::IByteChunk& chunk, const PutParams& params)
{
  const int at = shell::state::Begin(chunk, kChunkVersion);
  if (!params(chunk)) return false;
  std::vector<char> blob(TG_STATE_MAX, '\0');
  if (!gate || tg_shell_read(gate, "state", blob.data(), int(blob.size())) < 0)
    blob[0] = '\0';
  chunk.PutStr(blob.data());
  return shell::state::End(chunk, at);
}

int Load(tg_shell_t* gate, const iplug::IByteChunk& chunk, int startPos,
         const GetParams& check, const GetParams& apply)
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
   * this plugin wrote -- and accepting it anyway is how a megabyte of random
   * bytes used to load "successfully".
   */
  int pos = check(chunk, h.body);
  if (pos < 0) return -1;
  WDL_String blob;
  const int after = shell::state::GetStr(chunk, blob, pos);
  if (after < pos) return -1;

  if (apply(chunk, h.body) != pos) return -1;
  /* Empty in a chunk from a build that lost the pattern on save: nothing to
   * restore, so the engine keeps what it has rather than being reset. */
  if (blob.Get() && *blob.Get())
    Post(gate, Edit::Paste, blob.Get());
  return shell::state::Finish(h, after);
}

} // namespace patch
} // namespace tg
