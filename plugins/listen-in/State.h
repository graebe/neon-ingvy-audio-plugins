/*
 * NI Listen-In's host parameter and state chunk, on their own so they can be
 * tested.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Apart from the plugin class so a test can reach them: tests/cpp/listenin_state.cpp
 * declares the parameter on a bare iplug::IParam and saves and reloads the
 * chunk the way clap-validator's state tests do.
 */
#pragma once

#include "IPlugParameter.h"
#include "IPlugStructs.h"

#include <cstdint>
#include <functional>
#include <string>

/*
 * ONE PARAMETER, AND IT EARNS ITS PLACE.
 *
 * The Spectrogram has none, and says so as a statement: nothing about it
 * changes what comes out. That is true here too -- the audio is passed through
 * bit for bit whatever the slot says. But a bus NUMBER is a piece of session
 * structure the host should own: it belongs in the saved set, it should survive
 * a reopen, and somebody will want to automate a switch between two sources.
 * The host does all of that for a parameter and none of it for a message.
 */
enum EParams
{
  kSlot = 0,
  kNumParams
};

namespace listenin {
namespace state {

/* Declares the parameter on the IParam `param(i)` returns for index i. */
void Declare(const std::function<iplug::IParam*(int)>& param);

/* The chunk's layout, after shell_state.h's header: parameters, then the
 * label. A chunk with no header is this layout as every earlier build wrote it. */
constexpr int32_t kChunkVersion = 1;

using PutParams = std::function<bool(iplug::IByteChunk&)>;
using GetParams = std::function<int(const iplug::IByteChunk&, int)>;

/* The plugin's SerializeState, with its SerializeParams passed in. */
bool Save(iplug::IByteChunk& chunk, const PutParams& params, const std::string& label);

/*
 * The plugin's UnserializeState. `check` is shell::state::CheckParams and
 * `apply` the plugin's UnserializeParams, which runs only once the whole chunk
 * has been found good; `label` is set then too, sanitised. Returns the position
 * past the chunk, or -1 -- and then nothing has changed.
 */
int Load(const iplug::IByteChunk& chunk, int startPos, const GetParams& check,
         const GetParams& apply, std::string& label);

} // namespace state
} // namespace listenin
