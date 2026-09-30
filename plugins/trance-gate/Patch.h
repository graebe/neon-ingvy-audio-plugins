/*
 * The pattern's two journeys: from the editor into the engine, and from the
 * engine into the host's saved state and back.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Lifted out of TranceGate.cpp for the reason Wire.cpp was: a test cannot link
 * the plugin's own translation unit, and this is the code whose failure loses a
 * user's pattern. tests/cpp/tg_state.cpp drives it exactly as the plugin does.
 */
#pragma once

#include "IPlugStructs.h"
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
  Paste,      /* a whole state blob                                */
};

/* Posts one edit to the engine; false for a payload that is not one. */
bool Post(tg_shell_t* gate, Edit edit, const std::string& arg);

using PutParams = std::function<bool(iplug::IByteChunk&)>;
using GetParams = std::function<int(const iplug::IByteChunk&, int)>;

/* The plugin's SerializeState, with its SerializeParams passed in. */
bool Save(tg_shell_t* gate, iplug::IByteChunk& chunk, const PutParams& params);

/* The plugin's UnserializeState, with its UnserializeParams passed in. Returns
 * the position past what it read, or -1. */
int Load(tg_shell_t* gate, const iplug::IByteChunk& chunk, int startPos,
         const GetParams& params);

} // namespace patch
} // namespace tg
