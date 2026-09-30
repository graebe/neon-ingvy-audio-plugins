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
#include "tg_shell.h"
#include "Params.h"
#include "ground_detect.h"  /* the ground's kick detector; editor builds only */
#include <atomic>
#include <string>
#include <vector>

const int kNumPresets = 1;

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
  kMsgParams,         /* -> UI: the `params` readout (15 values + width_ms) */
  kMsgScope,          /* -> UI: the signal capture, base64 floats           */
  kMsgPatch,          /* <-> :  the state blob, for copy and paste          */
  /* -> UI: one kick, as a strength in 0..1. Sent only when the detector
   * fires, not every tick -- one message is one ring on the ground. */
  kMsgGround,

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
  kMsgSetOrder,       /* <- UI: "<index>:<rank>" -- its place in the fade    */
  /*
   * REGENERATE THE CURRENT SLOT. An ACTION, which is why it is a message and
   * not a parameter: a host parameter that rerolled the pattern every time the
   * host rewrote it would be unusable, and the pattern is not automatable in
   * the first place. The payload is an optional seed; empty walks the engine's
   * own generator, so successive presses differ.
   */
  kMsgRandomize,
  /*
   * THE GATE ACROSS ONE CYCLE, AS THE ENGINE ACTUALLY APPLIES IT.
   *
   * Not a description the editor can draw from -- the samples themselves. See
   * RenderGate: a scratch engine runs the real patch with a DC input, so the
   * output IS the gain. The editor had been modelling this and got a release
   * that outlives its step wrong, which is most of them.
   */
  kMsgGate,
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
  /* Switches the ground's detector off; see the definition. */
  void CloseWindow() override;

  /* Once per frame while the editor is open: the pattern, the playhead, the
   * step duration and the capture. Everything the plots and the ring draw
   * that is not a host parameter. */
  void OnIdle() override;
#ifdef WEBVIEW_EDITOR_DELEGATE
  /* One message per onset, from OnIdle. */
  void SendGround();
#endif
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
#endif

  /*
   * THE SCOPE'S CAPTURE, written on the audio thread and read on the message
   * thread.
   *
   * A SWEEP ALIGNED TO THE PATTERN, NOT A ROLLING WINDOW OF WALL TIME.
   *
   * The x-axis IS one cycle of the gate: column k is pattern phase
   * k / kScopeCols, always, and the trace is written into the column the
   * playhead is in. So the axis stands still, the picture fills left to right,
   * and `head` is the write point rather than an origin the reader has to
   * rotate by.
   *
   * THIS REVERSES AN EARLIER DECISION AND THE REASONING BEHIND IT WAS REAL, so
   * it is worth saying which half did not hold. The phase mapping was removed
   * because a column was written ONCE PER CYCLE -- seconds apart at sixteen
   * steps -- so changing the input level only caught up as the sweep passed
   * each column, which read as a slow filtered rise. Wall time fixed that and
   * cost the alignment: a given step was no longer at a fixed x, which took the
   * step numbers and the gate overlay off the plot with it.
   *
   * What was actually wrong was never the mapping. It was that a column was
   * published only when it was COMPLETE. The partial column is published every
   * block now, so the write point is live to within one buffer and a level
   * change appears at the sweep immediately -- current audio, overwritten, at
   * no point averaged with the cycle before it. There is nothing here that
   * could filter: min and max of the samples that land in a column, reset when
   * the column changes.
   *
   * And with the axis being the pattern again, the ENVELOPE can be drawn over
   * it, which is the whole reason to want this arrangement.
   *
   * MIN AND MAX PER COLUMN, never a mean: a transient is a fraction of a column
   * and averaging would quietly report a signal that is not the one playing.
   */
  static constexpr int kScopeCols = 256;
  /*
   * The sweep's length when the pattern's own is not known yet -- before the
   * first idle tick has measured it. One second, which is what the wall-time
   * window used to be.
   */
  static constexpr double kScopeFallbackMs = 1000.0;
  /* The transport TRUNCATES rather than fails past this, so every push has to
   * fit under it with base64's extra third accounted for. Raised from
   * iPlug2's 8192 default and asserted against at the one call that can
   * approach it. */
  static constexpr int kMaxJSString = 65536;
  struct Capture
  {
    std::atomic<float> dryLo[kScopeCols], dryHi[kScopeCols];
    std::atomic<float> wetLo[kScopeCols], wetHi[kScopeCols];
    /* The column currently being written: the sweep's own position. Not an
     * origin -- the reader draws column 0 at the left edge and always has -- so
     * this is only what marks the fill point. */
    std::atomic<int> head{0};
  };

  /* The pattern is not a parameter and never will be -- 128 steps across 8
   * slots is 1024 of them. It travels as the engine's own state blob, which
   * is the same text the Move module writes. Patch.cpp holds both halves. */
  bool SerializeState(IByteChunk& chunk) const override;
  int  UnserializeState(const IByteChunk& chunk, int startPos) override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
#endif

private:
#if IPLUG_DSP
  /* At the top of every block, on the audio thread, into the engine
   * tg_shell_begin lent it. True when the slot moved this block. */
  bool PushParams(tg_core_t* core);
#endif

  /*
   * THE ENGINE, BEHIND ITS SHELL. The audio thread takes it for one block at a
   * time; every other thread posts edits to it and reads what it published.
   * tg_shell.h states the rule, and there is no other door.
   */
  tg_shell_t* mShell = nullptr;

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
   * When it moves, the Length push is SUPPRESSED -- the engine's length is the
   * authority until the host has caught up -- and once the block's readout is
   * published, mSlotSync goes up. OnIdle sees the flag, reads the published
   * length into the parameter, and clears it, which is one tick at 50Hz. OnIdle runs off a timer created in the
   * API wrapper's constructor, not with the editor, so this completes whether
   * or not a window is open.
   */
  int mSlotPushed = -1;                 /* audio thread only */
  double mLengthPushed = -1.0;          /* audio thread only */
  std::atomic<int> mSlotSync{0};
  /* Set by a state load: the parameters arrived together, so the next block
   * is a new starting point for the handshake, not a switch. */
  std::atomic<int> mSlotRebase{0};
  void SyncSlotParams();                /* main thread only */

  Capture mCap;
  /* The column being accumulated, its running bounds, and how much of it has
   * been filled. Audio thread only. */
  int mCapCol = -1;                 /* -1 = nothing accumulated yet */
  int mCapCount = 0;                /* samples into the current column */
  float mCapDryLo = 0.f, mCapDryHi = 0.f, mCapWetLo = 0.f, mCapWetHi = 0.f;
  /*
   * THE FREE-RUN SWEEP, for a transport that is not moving.
   *
   * A stopped gate has no phase -- the engine parks step_pos at 0 -- so every
   * sample would land in column 0 and the plot would show one pixel of a signal
   * that is passing through perfectly audibly. This sweeps the same axis at the
   * same length instead, so the picture keeps its meaning and keeps moving.
   */
  double mSweep = 0.0;
  /* One cycle in milliseconds, for the axis the editor draws. Message thread
   * only -- it is written where it is measured and read where it is sent. */
  double mScopeCycleMs = kScopeFallbackMs;

  /*
   * THE PATTERN PLOT'S CURVE, RENDERED RATHER THAN DESCRIBED.
   *
   * `mGateState` is the patch the cached curve was rendered from; when the
   * engine's state string stops matching it, the curve is stale. Amount is in
   * that string, so dragging Amount re-renders -- harmless, because a render is
   * ~1024 samples through the engine, and cheaper than parsing the blob to
   * exclude it.
   */
  std::string mGateState;
  std::string mGatePayload;
  void RenderGate(const char* state);
  /*
   * How many samples one cycle of the pattern is. Measured on the MESSAGE
   * thread, where the step duration and the length can be read as strings
   * without formatting on the audio callback, and handed over as one double.
   */
  std::atomic<double> mCycleSamples{0.0};
  std::vector<float> mDry;          /* the input, before the engine overwrites it */
  void CaptureBlock(const float* dry, const float* wet, int frames,
                    double ph0, double ph1, bool advancing);
  /* iPlug2's `sample` is double and the engine's float path is the one the
   * golden render pins, so the block is converted rather than the engine
   * widened. Sized in OnReset; never resized on the audio thread. */
  std::vector<float> mL, mR;

#ifdef WEBVIEW_EDITOR_DELEGATE
  /*
   * THE ANIMATED GROUND'S KICK DETECTOR. The design system gives every window a
   * ground that rings when a kick lands, and the editor is a WebView with no
   * access to the host's audio -- so the detection happens here and one message
   * per onset crosses over. engines/ground/include/ground_detect.h says why, and
   * shows the whole idiom.
   *
   * mGroundFires is the last count the EDITOR was told about, so it belongs to
   * the message thread alone and needs no atomic. Inside the editor guard, so a
   * build with no WebView carries no detector on its audio thread.
   */
  gnd_t* mGround = nullptr;
  uint32_t mGroundFires = 0;
#endif

};
