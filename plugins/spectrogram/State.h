/*
 * The Spectrogram's state chunk, on its own so it can be tested.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Apart from the plugin class so a test can reach it: tests/cpp/spectro_state.cpp
 * saves and reloads it the way clap-validator's state tests do.
 */
#pragma once

#include "IPlugStructs.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace spectro {
namespace state {

/* The chunk's layout, after shell_state.h's header: parameters (none yet),
 * then sources, clash, view, comparison and range as strings. A chunk with no header
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
  /* The zoom, in Hz. Appended last: a chunk from before it keeps the full range. */
  float rangeLo = 10.0f;
  float rangeHi = 20000.0f;
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

/*
 * WHAT THE SESSION IS LOOKING AT, BETWEEN THREADS.
 *
 * A host calls SerializeState and UnserializeState on a thread of its choosing
 * -- auval's stress test on one of its own, some DAWs on a loader thread --
 * while the editor's messages and the idle timer run on the main thread. The
 * receiver's source changes are the main thread's (spectro_recv.h): they open
 * readers and wait for the analysis thread. So a load does not touch the
 * receiver. It records what it read here, and the main thread's next Service
 * applies it; a save reads it here too, so a save straight after a load writes
 * the load even before the main thread has applied it.
 *
 *   any thread but audio     Get, Load, Edit
 *   main thread              Service, Applied
 *
 * The lock is held only to copy Fields in or out, never across a receiver call
 * and never on the audio thread, which does not see any of this.
 */
class Session
{
public:
  /* The receiver calls a change needs, made by Service on the main thread. The
   * plugin binds them to srecv_*; a test records them. */
  struct Sink
  {
    virtual ~Sink() = default;
    virtual void ApplySources(const std::vector<unsigned int>& slots) = 0;
    virtual void ApplyClash(float floorDb, float balanceDb) = 0;
    virtual void ApplyRange(float lo, float hi) = 0;
  };

  /* `initial` is what the receiver was built with: applied from the start. */
  explicit Session(const Fields& initial);

  /* What the session holds -- a change not yet applied included. */
  Fields Get() const;
  /* State::Load into the session, as one step. Returns as State::Load does;
   * a refused chunk changes nothing and marks nothing. */
  int Load(const iplug::IByteChunk& chunk, int startPos, const GetParams& check,
           const GetParams& apply);
  /* A change from the editor, for Service to apply. */
  void Edit(const std::function<void(Fields&)>& edit);

  /*
   * Main thread. Apply what changed since the last Service -- or, with `all`,
   * everything, for a receiver just built -- through `sink`, one call per part
   * that changed. Returns true when what it applied came from a Load, so the
   * plugin can tell an open editor.
   */
  bool Service(Sink& sink, bool all = false);
  /* Main thread. What the receiver has been given. */
  const Fields& Applied() const { return mApplied; }

private:
  mutable std::mutex mLock;
  Fields mWanted;       /* under mLock */
  bool mChanged = false; /* under mLock */
  bool mLoaded = false;  /* under mLock */
  Fields mApplied;      /* the main thread's */
};

} // namespace state
} // namespace spectro
