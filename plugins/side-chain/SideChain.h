/*
 * NI Side-Chain -- the Ableton Live plugin, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The DSP is the Rust engine in engines/side-chain, reached through its C ABI. That
 * is the SAME engine the Schwung module builds into its .so -- a second copy
 * would drift, and the symptom would be "it sounds different in Live", which is
 * the hardest kind of bug to chase. `sc_render_ab` is what keeps it honest.
 *
 * WHAT THIS CLASS IS ALLOWED TO DO: marshal. Convert buffers, read the host's
 * transport, push parameters, capture for the scope, and move messages to and
 * from the webview. It makes no decision about the sound; if a branch here is
 * not about a host or a wire, it is in the wrong file.
 */
#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "sc_core.h"
#include "ground_detect.h"  /* the ground's kick detector; editor builds only */
#include <atomic>
#include <cstdint>
#include <vector>

const int kNumPresets = 1;

/*
 * THE FIFTEEN AUTOMATABLE VALUES, IN THE ENGINE'S OWN WIRE ORDER.
 *
 * Deliberately sc_param_t's order, so the host index IS the engine index and
 * there is no mapping table between them to get wrong. The Trance Gate's JUCE
 * build carried exactly such a table because its declaration order had drifted
 * from the engine's; every plugin here since has started from this instead.
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

/*
 * THE MESSAGE TAGS, MIRRORED IN ui/src/lib/msg.js.
 *
 * TAGS 0..kNumParams-1 ARE RESERVED for a parameter's display string, tagged
 * with that parameter's own index. That is why the editor needs no routing
 * table and knows no unit, precision or enum label: the plugin owns all of
 * that, because iPlug2's IParam already does.
 *
 * 64+ is plugin -> UI, 96+ is UI -> plugin.
 */
enum EMsgTags
{
  kMsgUiState = 64,  /* -> UI: the engine's `ui` readout, once per frame     */
  kMsgParams,        /* -> UI: the `params` readout, all fifteen values      */
  kMsgScope,         /* -> UI: the capture -- dry, wet and the gain applied  */
  kMsgStageMs,       /* -> UI: the four stage lengths in ms, for the axis    */
  kMsgBuses,         /* -> UI: which input buses the host actually connected */
  /* -> UI: one kick, as a strength in 0..1. Sent only when the detector
   * fires, not every tick -- one message is one ring on the ground. */
  kMsgGround,

  kMsgSetText = 96,  /* <- UI: "<paramIdx>:<typed text>"                     */
  kMsgHeight,        /* <- UI: the height it needs, in viewport pixels       */
  kMsgReady,         /* <- UI: mounted -- send me everything                 */
};

class SideChain final : public iplug::Plugin
{
public:
  SideChain(const iplug::InstanceInfo& info);
  ~SideChain();

#if IPLUG_EDITOR
  /*
   * A PARAMETER'S DISPLAY STRING, pushed under that parameter's own tag.
   *
   * The editor draws "-18.0 dB" and "1/8" and "S-Curve" without knowing what a
   * dB is, which is the whole point: iPlug2's IParam already owns every unit,
   * precision and enum label, and a second copy of them in JavaScript is a
   * second copy that will disagree.
   */
  void SendDisplay(int paramIdx);
  /* Every value and every display string at once. Sent on open AND on
   * kMsgReady: the first is too early to be heard but costs nothing, and the
   * second is the one that actually arrives. */
  void SendFullState();
  void OnParamChangeUI(int paramIdx, iplug::EParamSource source) override;
  void OnUIOpen() override;
  /* Switches the ground's detector off; see the definition. */
  void CloseWindow() override;
  void OnIdle() override;
#ifdef WEBVIEW_EDITOR_DELEGATE
  /* One message per onset, from OnIdle. */
  void SendGround();
#endif
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
#endif

#if IPLUG_DSP
  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const iplug::IMidiMsg& msg) override;
  void OnReset() override;
#endif

  /*
   * THE CAPTURE, written on the audio thread and read on the message thread.
   *
   * PHASE-LOCKED TO THE TRIGGER, NOT A ROLLING WINDOW OF WALL TIME -- and this
   * is the decision the whole editor is built on.
   *
   * The Trance Gate went the other way and its comments explain why: its
   * columns used to be pattern phase, a column was refreshed once per cycle,
   * and at sixteen steps that was seconds apart, so turning the input up made
   * the picture creep up after it. It moved to wall time and lost the alignment,
   * which cost it its gate overlay.
   *
   * Side-Chain needs the alignment more than it needs the refresh rate, because the
   * editor and the scope are ONE PICTURE: the shape you dragged is drawn
   * directly above the audio it shaped, on one axis. A rolling window would put
   * the dip somewhere else every frame and the two halves would stop being
   * comparable -- which is a worse picture that merely looks fresher.
   *
   * And the refresh is not the Trance Gate's problem here: one cycle at 1/4 and
   * 120 bpm is half a second, so a column is rewritten twice a second, which
   * reads as live. At 1/1 and 60 bpm it is once every four seconds and the
   * picture IS that old. That is inherent to phase-locking and is the honest
   * cost; the engine's `sweep01` comment records it too.
   *
   * The column index comes from the engine's per-sample sweep, so there is no
   * second copy of the phase arithmetic here to drift.
   *
   * MIN AND MAX PER COLUMN FOR THE AUDIO, never a mean: a transient is a
   * fraction of a column and averaging would quietly report a signal that is
   * not the one playing. MINIMUM for the gain, because the deepest point of the
   * duck is the thing being looked at.
   */
  /*
   * 512 COLUMNS, NOT 256.
   *
   * The plot is 696px wide inside its inset, so 256 columns is 2.7 pixels each
   * and the waveform came out visibly stepped -- the picture was quantised by
   * the WIRE rather than by the screen, which is the wrong place for it. At 512
   * a column is 1.36px, so `band`'s decimation has something to decimate and
   * the outline lands where the audio is.
   *
   * It costs about 5.7 KB of payload against a 65 536 transport, so this is not
   * where the budget goes. Amplitude stays at one byte per bound: 256 levels
   * across a well 220px tall is already finer than the plot can draw, so more
   * bits there would buy nothing.
   */
  static constexpr int kScopeCols = 512;
  /* The transport TRUNCATES rather than fails past this, so every push has to
   * fit under it with base64's extra third accounted for. Raised from iPlug2's
   * 8192 default and asserted against at the one call that can approach it. */
  static constexpr int kMaxJSString = 65536;

  struct Capture
  {
    std::atomic<float> dryLo[kScopeCols], dryHi[kScopeCols];
    std::atomic<float> wetLo[kScopeCols], wetHi[kScopeCols];
    /* The smallest gain seen in the column: the deepest the duck got. */
    std::atomic<float> gain[kScopeCols];
    /*
     * Whether this column has ever held real audio.
     *
     * With a phase-locked sweep there is no `head` to orient the window by, and
     * therefore nothing that distinguishes "silence" from "not yet reached" --
     * the first cycle would draw a flat line across the part it had not got to,
     * which reads as a signal that stopped rather than a picture still filling.
     */
    std::atomic<uint32_t> seen[kScopeCols];
  };

private:
#if IPLUG_DSP
  /* Runs at the top of every block, on the audio thread. */
  void PushParams();
  void CaptureBlock(const float* dry, const float* wet, const float* gain,
                    const float* sweep, int frames);
#endif

  sc_core_t* mCore = nullptr;

  Capture mCap;
  /* Audio thread only: the column being accumulated and its running bounds.
   * A column is closed when the sweep moves off it, which is why the index is
   * carried between blocks rather than recomputed. */
  int mCapCol = -1;
  float mCapDryLo = 0.f, mCapDryHi = 0.f, mCapWetLo = 0.f, mCapWetHi = 0.f;
  float mCapGain = 1.f;
  /* Bumped every time a column is written, so `seen` can hold a generation
   * rather than a bool and a reader can tell a stale column from a fresh one
   * after a sample-rate change wipes the buffers. */
  std::atomic<uint32_t> mCapGen{1};

  /*
   * iPlug2's `sample` is double and the engine's float path is the one the
   * render A/B pins, so the block is CONVERTED rather than the engine widened.
   * Sized in OnReset and never resized on the audio thread.
   *
   * mDry is the input before the engine overwrites it -- the grey trace. mGain
   * and mSweep are the engine's taps.
   */
  std::vector<float> mL, mR, mDry, mGain, mSweep;
  /* The sidechain bus, deinterleaved for the engine. */
  std::vector<float> mKeyL, mKeyR;

  /*
   * WHICH INPUT BUSES THE HOST ACTUALLY CONNECTED, published for the editor.
   *
   * The engine does not infer this and must not: an unconnected aux bus and a
   * silent one are the same block of zeroes. "No key" and "nothing is playing"
   * are different messages and only this side can tell them apart.
   */
  std::atomic<int> mKeyConnected{0};
  /* Set when the aux buffers are byte-for-byte the main input -- the Logic and
   * GarageBand bug. Reported, never worked around; see Wire.h. */
  std::atomic<int> mKeyIsMain{0};

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
