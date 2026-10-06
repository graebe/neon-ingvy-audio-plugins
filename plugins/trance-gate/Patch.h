/*
 * The pattern's two journeys: from the editor into the engine, and from the
 * engine into the host's saved state and back.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Apart from the plugin class because this is the code whose failure loses a
 * user's pattern, and a test cannot link the class: tests/cpp/tg_state.cpp
 * drives it exactly as the plugin does.
 */
#pragma once

#include "IPlugStructs.h"
#include "ni/Clipboard.h"
#include "tg_shell.h"

#include <cstdint>
#include <functional>
#include <string>

namespace tg {
namespace patch {

/*
 * THE CHUNK, VERSION 1: shell_state.h's header, iPlug2's parameters, then the
 * engine's state blob. A chunk with no header is the layout every earlier build
 * wrote -- parameters, then the blob -- and still loads.
 */
constexpr int32_t kChunkVersion = 1;

/* The editor's pattern edits. None is a host parameter. */
enum class Edit
{
  Cursor,     /* "<index>"                                         */
  Step,       /* "<index>:<0 off|1 on|2 tie>"                      */
  Depth,      /* "<index>:<0..1>"                                  */
  Order,      /* "<index>:<rank>"                                  */
  Randomize,  /* "" or a seed                                      */
};

/* Posts one edit to the engine; false for a payload that is not one. */
bool Post(tg_shell_t* gate, Edit edit, const std::string& arg);

using PutParams = std::function<bool(iplug::IByteChunk&)>;
using GetParams = std::function<int(const iplug::IByteChunk&, int)>;
/* The host's parameter `i` as it stands, in the host's units. */
using HostValue = std::function<double(int)>;

/* The plugin's SerializeState, with its SerializeParams passed in, and its
 * parameters' values, which the blob's current slot is saved with. */
bool Save(tg_shell_t* gate, iplug::IByteChunk& chunk, const PutParams& params, const HostValue& value);

/*
 * The plugin's UnserializeState. `check` is shell::state::CheckParams and
 * `apply` the plugin's UnserializeParams: the first reads the parameter block
 * without touching anything, the second sets it, and it runs only once the
 * whole chunk has been found good. `value` reads a host parameter once they
 * are set. Returns the position past what it read, or -1 for a chunk this
 * plugin did not write -- nothing there, parameters that are not numbers of
 * their kind, or no blob after them -- in which case nothing has changed.
 *
 * THE ORDER IS FIXED: the blob -- every slot's pattern and sound -- and then
 * the restored parameters into the current slot, as one edit (tg_shell_load).
 * The parameters are the exact values the project saved, where the blob holds
 * them rounded, so a project reopens with its parameters bit for bit; and a
 * blob from before every slot had its own sound takes them in all eight.
 */
int Load(tg_shell_t* gate, const iplug::IByteChunk& chunk, int startPos,
         const GetParams& check, const GetParams& apply, const HostValue& value);

/* Every host parameter on the engine's numeric wire, in tg_param_t order: what
 * the audio thread pushes each block (tg_shell_push). */
void Values(const HostValue& value, double (&out)[TG_P_COUNT]);

/*
 * THE HOST FOLLOWS A SLOT SWITCH. Once per published switch or paste, every
 * host parameter whose value says something different from the new slot's is
 * handed to `set`, in host units -- the plugin's SetParamFromPlugin, so the
 * host's automation lanes and UI, and the editor, show the recalled values.
 * Slot is the host's own and is never set. Returns whether there was a switch
 * to follow. The main thread.
 */
using SetHost = std::function<void(int, double)>;
bool Follow(tg_shell_t* gate, const HostValue& value, const SetHost& set);

/*
 * SLOT FILES, between the engine and the disk. The text is the engine's; these
 * move it, and say what happened in words for the editor's hint bar.
 */
enum class FileKind
{
  Slot, /* the current slot, .nitgslot */
  Bank, /* all eight, .nitgbank        */
};
const char* Extension(FileKind kind);
/* What the save panel proposes: "NI Trance Gate Slot 3.nitgslot". */
std::string FileName(FileKind kind, int slot);

/* Writes the export to `path`. `slot` is the current slot, 1-based, for the
 * words. True when written; `status` says what happened either way. */
bool ExportFile(tg_shell_t* gate, const HostValue& value, FileKind kind, int slot,
                const std::string& path, std::string& status);
/* Reads `path` and imports it (tg_shell_import): a slot file into the current
 * slot, a bank into all eight. True when queued; a file refused changes
 * nothing, and `status` says why. */
bool ImportFile(tg_shell_t* gate, int slot, const std::string& path, std::string& status);

/*
 * COPY AND PASTE, between the engine and the clipboard. The main thread, as the
 * clipboard is (ni/Clipboard.h). `slot` is the host's current slot, 1-based:
 * the words name it, and a pasted slot goes into it. `status` says what
 * happened either way, for the hint bar.
 *
 * CopySlot puts the current slot on the clipboard as a slot file's text, as
 * the next block will hold it -- the host's values included, as for an export.
 *
 * Paste reads the clipboard and lets the engine decide what it holds
 * (tg_shell_paste): a slot replaces the current slot, a bank all eight, a whole
 * patch -- what Copy wrote before slots, and the Move's -- everything. True
 * when queued; the host then follows the current slot (Follow). Anything else
 * is refused with the engine's reason and changes nothing.
 */
bool CopySlot(tg_shell_t* gate, const HostValue& value, int slot, ni::Clipboard& clipboard,
              std::string& status);
bool Paste(tg_shell_t* gate, int slot, ni::Clipboard& clipboard, std::string& status);

} // namespace patch
} // namespace tg
