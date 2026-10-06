// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's host parameter and state chunk, on their own so they can be
 * tested.
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
#include <mutex>
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

/*
 * THE LABEL, BETWEEN THREADS.
 *
 * A host calls SerializeState and UnserializeState on a thread of its choosing
 * (auval's stress test, a DAW's loader), while the editor's messages and the
 * idle timer run on the main thread, which alone owns the bus writer. So the
 * label is not a plain member any of them writes: a load records it here, the
 * editor edits it here, and the main thread takes what changed on its next
 * idle tick and hands it to the writer and the editor. A save reads it here,
 * so a save straight after a load writes the load.
 *
 *   any thread but audio     Label, Load, Edit
 *   main thread              Take
 *
 * The lock is held only to copy the label in or out -- never across a bus or
 * editor call, never on the audio thread. The slot is a parameter, which
 * iPlug2 keeps atomic, and reaches the main thread through OnParamChange.
 */
class Session
{
public:
  /* What a save writes: the label, a load not yet applied included. */
  std::string Label() const;
  /* state::Load into the session, as one step. Returns as state::Load does;
   * a refused chunk changes nothing and marks nothing. */
  int Load(const iplug::IByteChunk& chunk, int startPos, const GetParams& check,
           const GetParams& apply);
  /* A label typed in the editor, already sanitised. */
  void Edit(const std::string& label);

  /* Main thread. What changed since the last Take: false when nothing did.
   * `loaded` says a state load did it, which also means the bus is claimed
   * afresh and an open editor is told. */
  bool Take(std::string& label, bool& loaded);

private:
  mutable std::mutex mLock;
  std::string mLabel;    /* under mLock */
  bool mChanged = false; /* under mLock */
  bool mLoaded = false;  /* under mLock */
};

} // namespace state
} // namespace listenin
