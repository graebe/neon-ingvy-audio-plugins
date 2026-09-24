/*
 * Trance Gate -- the Ableton Live plugin, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The DSP is the Rust engine in external/schwung-trance-gate, reached through
 * its C ABI. That is the SAME engine, pinned by submodule commit, that the
 * Schwung module on the Move builds into its .so -- a second copy would
 * drift, and the symptom would be "it sounds different in Live", which is the
 * hardest kind of bug to chase.
 */
#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "trance_gate_core.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

const int kNumPresets = 1;

/*
 * THE TWELVE AUTOMATABLE VALUES, IN THE ENGINE'S OWN WIRE ORDER.
 *
 * This enum is deliberately tg_param_t's order, so the host index IS the
 * engine index and there is no mapping table between them to get wrong. The
 * JUCE build carried exactly such a table (a `wire[]` array) because its
 * parameter declaration order had drifted from the engine's; starting again
 * is a chance not to.
 */
enum EParams
{
  kSlot = 0,
  kLength,
  kRate,
  kLegato,
  kTimeMode,
  kCurve,
  kAmount,
  kWidth,
  kAttack,
  kDecay,
  kSustain,
  kRelease,
  kNumParams
};

using namespace iplug;

/* iplug::Plugin, spelled out. The CLAP target pulls in clap-helpers, which
 * has a `Plugin` template of its own, and `using namespace iplug` makes the
 * unqualified name resolve to the wrong one there -- VST3 and AU build
 * fine, CLAP fails on a base class that is not a base class. */
class TranceGate final : public iplug::Plugin
{
public:
  TranceGate(const InstanceInfo& info);
  ~TranceGate();

  /* The pattern is not a parameter and never will be -- 128 steps across 8
   * slots is 1024 of them. It travels as the engine's own state blob, which
   * is the same text the Move module writes. */
  bool SerializeState(IByteChunk& chunk) const override;
  int  UnserializeState(const IByteChunk& chunk, int startPos) override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
#endif

private:
#if IPLUG_DSP
  /* Both run at the top of every block, on the audio thread. */
  void ApplyPendingPatch();
  void PushParams();
#endif

  tg_core_t* mCore = nullptr;
  /* iPlug2's `sample` is double and the engine's float path is the one the
   * golden render pins, so the block is converted rather than the engine
   * widened. Sized in OnReset; never resized on the audio thread. */
  std::vector<float> mL, mR;

  /* The patch text, and the authority on it -- see SerializeState. Mutable
   * because SerializeState is const and still has to take the lock; the lock
   * is what the const-ness is hiding, not a state change. */
  mutable std::mutex mPatchMx;
  std::string mPatch;
  std::atomic<bool> mPatchDirty{false};
};
