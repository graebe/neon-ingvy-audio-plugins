/*
 * The Spectrogram's state chunk, on its own so it can be tested.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Lifted out of Spectrogram.cpp for the reason Wire.cpp was: a test cannot link
 * the plugin's own translation unit. tests/cpp/spectro_state.cpp saves and
 * reloads it the way clap-validator's state tests do.
 */
#pragma once

#include "IPlugStructs.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace spectro {
namespace state {

/* The chunk's layout, after shell_state.h's header: parameters (none yet),
 * then sources, clash, view and comparison as strings. A chunk with no header
 * is this layout as every earlier build wrote it. */
constexpr int32_t kChunkVersion = 1;

/* What a session remembers. Neither is a parameter -- see Spectrogram.h. */
struct Fields
{
  std::vector<unsigned int> sources;
  float clashFloorDb = -60.0f;
  float clashBalanceDb = 12.0f;
  std::vector<int> view;
  int cmpA = 0;
  int cmpB = 1;
  bool clashOn = false;
};

using PutParams = std::function<bool(iplug::IByteChunk&)>;
using GetParams = std::function<int(const iplug::IByteChunk&, int)>;

/* The plugin's SerializeState, with its SerializeParams passed in. */
bool Save(iplug::IByteChunk& chunk, const PutParams& params, const Fields& f);

/*
 * The plugin's UnserializeState. `check` is shell::state::CheckParams and
 * `apply` the plugin's UnserializeParams, run only once the chunk has been
 * found good. `f` comes in holding the current values: a field the chunk does
 * not carry keeps its own. Returns the position past the chunk, or -1 -- and
 * then `f` and the parameters are untouched.
 */
int Load(const iplug::IByteChunk& chunk, int startPos, const GetParams& check,
         const GetParams& apply, Fields& f);

} // namespace state
} // namespace spectro
