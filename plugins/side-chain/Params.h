// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's fifteen host parameters, and the state chunk that holds them.
 *
 * Apart from the plugin class so a test can reach them: tests/cpp/sc_params.cpp
 * declares these on bare iplug::IParams -- the class the plugin uses -- and
 * checks that every value survives value -> text -> value, and that a saved
 * chunk reloads into a fresh instance exactly, as clap-validator's state tests
 * do.
 */
#pragma once

#include "IPlugParameter.h"
#include "IPlugStructs.h"

#include <cstdint>
#include <functional>

/*
 * THE FIFTEEN AUTOMATABLE VALUES, IN THE ENGINE'S OWN WIRE ORDER.
 *
 * Deliberately sc_param_t's order, so the host index IS the engine index and
 * there is no mapping table between them to get wrong.
 *
 * EVERYTHING PUMP HOLDS IS IN THIS LIST, which is why config.h sets
 * PLUG_DOES_STATE_CHUNKS 0. The shape editor's handles are these parameters,
 * so dragging one lands in the host's undo history and automation lane.
 */
enum EParams
{
  kSource = 0,
  kRate,
  kTimeMode,
  kDelay,
  kAttack,
  kHold,
  kRelease,
  kDepth,
  kCurve,
  kChannel,
  kNote,
  kMidiMode,
  kVelSens,
  kThreshold,
  kLockout,
  kNumParams
};

namespace sc {
namespace params {

/* Declares every parameter on the IParam `param(i)` returns for index i. */
void Declare(const std::function<iplug::IParam*(int)>& param);

/*
 * THE CHUNK: shell_state.h's header, then iPlug2's parameters, and nothing
 * else -- everything Side-Chain holds is a parameter. A chunk without the
 * header is an earlier build's: the parameters alone, which is what it holds.
 */
constexpr int32_t kChunkVersion = 1;

using PutParams = std::function<bool(iplug::IByteChunk&)>;
using GetParams = std::function<int(const iplug::IByteChunk&, int)>;

/* The plugin's SerializeState, with its SerializeParams passed in. */
bool Save(iplug::IByteChunk& chunk, const PutParams& params);

/*
 * The plugin's UnserializeState. `check` is shell::state::CheckParams and
 * `apply` the plugin's UnserializeParams, which runs only on a block the first
 * accepted. Returns the position past the chunk, or -1 -- and then nothing has
 * changed.
 */
int Load(const iplug::IByteChunk& chunk, int startPos,
         const GetParams& check, const GetParams& apply);

} // namespace params
} // namespace sc
