/*
 * Trance Gate -- the pattern's journeys. See Patch.h.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "Patch.h"
#include "Params.h"
#include "ni/Wire.h"
#include "shell_state.h"

#include <algorithm>
#include <fstream>
#include <iterator>
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

const char* Extension(FileKind kind)
{
  return kind == FileKind::Slot ? "nitgslot" : "nitgbank";
}

std::string FileName(FileKind kind, int slot)
{
  std::string name = kind == FileKind::Slot ? "NI Trance Gate Slot " + std::to_string(slot)
                                            : std::string("NI Trance Gate Bank");
  return name + "." + Extension(kind);
}

/* The file's name, for a status line: what the panel showed, not the path. */
static std::string BaseName(const std::string& path)
{
  const size_t at = path.find_last_of('/');
  return at == std::string::npos ? path : path.substr(at + 1);
}

bool ExportFile(tg_shell_t* gate, const HostValue& value, FileKind kind, int slot,
                const std::string& path, std::string& status)
{
  std::vector<char> text(TG_SLOTFILE_MAX, '\0');
  double values[TG_P_COUNT];
  Values(value, values);
  const int n = gate ? tg_shell_export(gate, values, TG_P_COUNT, kind == FileKind::Bank,
                                       text.data(), int(text.size()))
                     : -1;
  if (n <= 0)
  {
    status = "Failed to export: the plugin is not ready.";
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out || !out.write(text.data(), n) || !out.flush())
  {
    status = "Failed to write " + BaseName(path) + ".";
    return false;
  }
  status = (kind == FileKind::Slot ? "Exported slot " + std::to_string(slot)
                                   : std::string("Exported all 8 slots")) +
           " to " + BaseName(path) + ".";
  return true;
}

bool ImportFile(tg_shell_t* gate, int slot, const std::string& path, std::string& status)
{
  const std::string name = BaseName(path);
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    status = "Failed to open " + name + ".";
    return false;
  }
  /* One byte past the largest file the engine reads, so an oversized one is
   * refused by the engine with its own words rather than read whole. */
  std::string text;
  text.resize(TG_SLOTFILE_MAX + 1);
  in.read(text.data(), std::streamsize(text.size()));
  text.resize(size_t(in.gcount()));
  if (text.find('\0') != std::string::npos)
  {
    status = "Failed to import " + name + ": this is not a Trance Gate slot or bank file.";
    return false;
  }
  char err[256] = {};
  const int kind = gate ? tg_shell_import(gate, text.c_str(), err, int(sizeof err)) : 0;
  if (kind == 0)
  {
    status = "Failed to import " + name + ": " + (err[0] ? err : "the plugin is not ready.");
    return false;
  }
  status = kind == 1 ? "Imported " + name + " into slot " + std::to_string(slot) + "."
                     : "Imported all 8 slots from " + name + ".";
  return true;
}

bool CopySlot(tg_shell_t* gate, const HostValue& value, int slot, ni::Clipboard& clipboard,
              std::string& status)
{
  std::vector<char> text(TG_SLOTFILE_MAX, '\0');
  double values[TG_P_COUNT];
  Values(value, values);
  const int n = gate ? tg_shell_export(gate, values, TG_P_COUNT, 0, text.data(), int(text.size())) : -1;
  if (n <= 0)
  {
    status = "Failed to copy: the plugin is not ready.";
    return false;
  }
  if (!clipboard.Write(std::string(text.data(), size_t(n))))
  {
    status = "Failed to copy: the clipboard could not be written.";
    return false;
  }
  status = "Copied slot " + std::to_string(slot) + ".";
  return true;
}

bool Paste(tg_shell_t* gate, int slot, ni::Clipboard& clipboard, std::string& status)
{
  /* No text on the clipboard is an empty text, which the engine words. */
  std::string text;
  if (!clipboard.Read(text))
    text.clear();
  /* Past the largest text the engine reads, the length only has to stay past
   * it: the engine refuses it whole, with its own words. */
  const int len = int(std::min<size_t>(text.size(), TG_SLOTFILE_MAX + 1));
  char err[256] = {};
  const int holds = gate ? tg_shell_paste(gate, slot - 1, text.data(), len, err, int(sizeof err)) : 0;
  switch (holds)
  {
    case TG_PASTE_SLOT: status = "Pasted into slot " + std::to_string(slot) + "."; return true;
    case TG_PASTE_BANK: status = "Pasted all 8 slots."; return true;
    case TG_PASTE_PATCH: status = "Pasted a whole patch into all 8 slots."; return true;
    default:
      status = std::string("Failed to paste: ") + (err[0] ? err : "the plugin is not ready.");
      return false;
  }
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
