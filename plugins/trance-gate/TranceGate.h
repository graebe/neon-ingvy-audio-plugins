/*
 * Trance Gate -- the Ableton Live plugin, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The DSP is the Rust engine in engine, reached through
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
  kMsgSetText,        /* <- UI: "<paramIdx>:<typed text>"                   */
  kMsgRows,           /* <- UI: the grid's row count, for the window height */
  /*
   * "I AM LISTENING." The reply to this is the whole of the UI's initial
   * state, and it exists because the push on open CANNOT be heard.
   *
   * OnUIOpen fires from didFinishNavigation, but the editor is a
   * <script type="module"> and module scripts are DEFERRED -- they evaluate
   * after the document is done. So every SPVFD() from OnUIOpen lands before
   * globalThis.SPVFD exists and is dropped on the floor, and the UI sits on
   * twelve zeroes until the user touches something. That is one bug wearing
   * four hats: a knob whose first drag jumps to zero, a switch drawn off
   * whatever the engine holds, and two dropdowns stuck on their first entry.
   *
   * A push that races page load, replaced by a request that cannot.
   */
  kMsgReady,          /* <- UI: mounted -- send me everything                */
};

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* The UI holds normalised values and nothing else, so it cannot format a
   * readout. This pushes the plugin's own display text for one parameter,
   * tagged with that parameter's index -- which is why the UI never has to
   * know a unit, a precision or an enum's labels. */
  void SendDisplay(int paramIdx);
  /* Every value and every display string, in one go. Sent on open AND on
   * kMsgReady: the first is too early to be heard but costs nothing, and the
   * second is the one that actually arrives. */
  void SendFullState();
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
   * thread.
   *
   * A ROLLING WINDOW OF WALL TIME, NOT A SWEEP OF THE PATTERN.
   *
   * It used to map each sample to a column by PATTERN PHASE, so a column was
   * written once per cycle -- seconds apart at sixteen steps. Change the
   * input level and the picture only caught up as the sweep passed each
   * column, which read as a slow, filtered rise and fall. Nothing was being
   * smoothed; the display was simply that old.
   *
   * Now the columns advance with TIME: one window's worth of audio, always
   * completely full, every column refreshed within a window. `head` is the
   * column being written -- which is also the OLDEST data, since it is about
   * to be overwritten -- so the reader walks head, head+1, ... head+255 to get
   * oldest to newest, left to right.
   *
   * The cost is the pattern alignment: a given step is no longer always at the
   * same x, so the step numbers and the gate overlay came off the plot. The
   * wet trace shows the gating, which is what they were explaining.
   *
   * MIN AND MAX PER COLUMN, never a mean: a transient is a fraction of a
   * column and averaging would quietly report a signal that is not the one
   * playing.
   */
  static constexpr int kScopeCols = 256;
  /* One second of audio across the well. Long enough to see a bar of a slow
   * pattern, short enough that a level change is visible at once. */
  static constexpr double kScopeWindowMs = 1000.0;
  /* The transport TRUNCATES rather than fails past this, so every push has to
   * fit under it with base64's extra third accounted for. Raised from
   * iPlug2's 8192 default and asserted against at the one call that can
   * approach it. */
  static constexpr int kMaxJSString = 65536;
  struct Capture
  {
    std::atomic<float> dryLo[kScopeCols], dryHi[kScopeCols];
    std::atomic<float> wetLo[kScopeCols], wetHi[kScopeCols];
    /* The column currently being accumulated: the write cursor, and the
     * oldest data in the ring. */
    std::atomic<int> head{0};
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

  /*
   * THE SLOT SWITCH, AND WHY IT NEEDS A HANDSHAKE.
   *
   * `length` is PER SLOT in the engine -- pat[slot].length -- but it is also
   * a host parameter, and PushParams writes all twelve every block. So the
   * order slot-then-length meant that switching slots pushed the OLD slot's
   * length straight over the new slot's: not a stale readout, the pattern's
   * length actually destroyed, and silently.
   *
   * mSlotPushed is the audio thread's own record of the slot it last set.
   * When it moves, mSlotSync goes up and the Length push is SUPPRESSED -- the
   * engine's length is the authority until the host has caught up. OnIdle
   * sees the flag, reads the engine's length into the parameter, and clears
   * it, which is one tick at 50Hz. OnIdle runs off a timer created in the
   * API wrapper's constructor, not with the editor, so this completes whether
   * or not a window is open.
   */
  int mSlotPushed = -1;                 /* audio thread only */
  std::atomic<int> mSlotSync{0};
  void SyncSlotParams();                /* main thread only */

  Capture mCap;
  /* The column being accumulated, its running bounds, and how much of it has
   * been filled. Audio thread only. */
  int mCapCol = 0;
  int mCapCount = 0;                /* samples into the current column */
  double mCapPerCol = 1.0;          /* samples a column spans, from the rate */
  float mCapDryLo = 0.f, mCapDryHi = 0.f, mCapWetLo = 0.f, mCapWetHi = 0.f;
  std::vector<float> mDry;          /* the input, before the engine overwrites it */
  void CaptureBlock(const float* dry, const float* wet, int frames);
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
