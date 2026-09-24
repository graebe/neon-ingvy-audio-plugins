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
#include <atomic>

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

/*
 * THE MESSAGE TAGS, both directions.
 *
 * 0..kNumParams-1 are reserved for a parameter's DISPLAY STRING, tagged with
 * the parameter's own index, so the UI needs no routing table for them.
 * Everything else starts past that.
 */
enum EMsgTags
{
  kMsgUiState = 64,   /* -> UI: the engine's `ui` readout, once per frame   */
  kMsgParams,         /* -> UI: the `params` readout (12 values + width_ms) */
  kMsgScope,          /* -> UI: the signal capture, base64 floats           */
  kMsgPatch,          /* <-> :  the state blob, for copy and paste          */

  kMsgSetStep = 96,   /* <- UI: "<index>:<0 off|1 on|2 tie>"                */
  kMsgSetDepth,       /* <- UI: "<index>:<0..1>"                            */
  kMsgSetCursor,      /* <- UI: "<index>"                                   */
  kMsgRequestPatch,   /* <- UI: send me the blob (Copy gate config)         */
};

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* The UI holds normalised values and nothing else, so it cannot format a
   * readout. This pushes the plugin's own display text for one parameter,
   * tagged with that parameter's index -- which is why the UI never has to
   * know a unit, a precision or an enum's labels. */
  void SendDisplay(int paramIdx);
  void OnParamChangeUI(int paramIdx, EParamSource source) override;
  void OnUIOpen() override;

  /* Once per frame while the editor is open: the pattern, the playhead, the
   * step duration and the capture. Everything the plots and the ring draw
   * that is not a host parameter. */
  void OnIdle() override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
#endif

  /*
   * THE SCOPE'S CAPTURE, written on the audio thread and read on the message
   * thread. One sweep is one pattern cycle, filling left to right and
   * restarting on the wrap, so its x-axis is the pattern plot's.
   *
   * MIN AND MAX PER COLUMN, never a mean: a narrow gate is a fraction of a
   * pixel at 128 steps and averaging would quietly report a signal that is
   * not the one playing.
   */
  static constexpr int kScopeCols = 256;
  struct Capture
  {
    std::atomic<float> dryLo[kScopeCols], dryHi[kScopeCols];
    std::atomic<float> wetLo[kScopeCols], wetHi[kScopeCols];
    std::atomic<int> filled{0};
  };

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
  Capture mCap;
  /* The column being accumulated and its running bounds. Audio thread only. */
  int mCapCol = -1;
  float mCapDryLo = 0.f, mCapDryHi = 0.f, mCapWetLo = 0.f, mCapWetHi = 0.f;
  std::vector<float> mDry;          /* the input, before the engine overwrites it */
  void CaptureBlock(const float* dry, const float* wet, int frames,
                    int totalFrames, double phase0, double phase1);
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
