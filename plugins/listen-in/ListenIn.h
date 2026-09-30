/*
 * NI Listen-In -- a tap that other plugins can read.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "audio_bus.h"
#include "shell_handoff.h"
#include "shell_state.h"
#include "State.h"
#include "ground_detect.h"  /* the ground's kick detector; editor builds only */

#include <atomic>
#include <string>
#include <vector>

const int kNumPresets = 1;

/* Plugin -> UI from 64, UI -> plugin from 96; 0..kNumParams-1 belong to the
 * host's parameter display strings. The convention is the house's, the numbers
 * are this plugin's. Mirrored by hand in ui/src/lib/msg.js. */
enum EMsgTags
{
  kMsgState = 64,
  /* One kick, as a strength in 0..1. Sent only when the detector fires, not on
   * every tick like kMsgState -- the ground draws one ring per message. */
  kMsgGround = 65,
  kMsgLabel = 96,
  kMsgReady = 102,
};

/* The editor is told the truth about the bus 60 times a second, and the string
 * is "16:3:1.0000" at its longest. */
const int kMaxStateChars = 64;

/*
 * The block staged for the bus. A host may hand us a longer block than the one
 * it announced, so this is a reservation and ProcessBlock chunks against it
 * rather than resizing -- resizing here would allocate on the audio thread.
 * The Spectrogram's mMono is the same arrangement for the same reason.
 */
const int kStageFrames = 4096;

using namespace iplug;

/* FULLY QUALIFIED ON PURPOSE in the Spectrogram, and the reason applies here:
 * the CLAP target pulls in clap-helpers, which has its own `Plugin` template,
 * so an unqualified base class resolves to the wrong one in that target only. */
class ListenIn final : public iplug::Plugin
{
public:
  ListenIn(const InstanceInfo& info);
  ~ListenIn();

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
#endif

  /* The label is text, so it cannot be a parameter and the host cannot save it
   * for us. Parameters first, then the name -- the Trance Gate's arrangement
   * for its pattern. */
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

  /* The bus's whole lifecycle happens here, on the main thread -- editor or
   * not, because iPlug2's idle timer belongs to the API wrapper. */
  void OnIdle() override;

#ifdef WEBVIEW_EDITOR_DELEGATE
  void OnUIOpen() override;
  /* Switches the ground's detector off; see the definition. */
  void CloseWindow() override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
#endif

private:
  /* Main thread only: claims, releases and retunes the bus to match what the
   * other threads asked for. */
  void ServiceBus();
  void SendState();
#ifdef WEBVIEW_EDITOR_DELEGATE
  /* One message per onset, from OnIdle. */
  void SendGround();
#endif

  /*
   * A CLAIM IS TWO HALVES, AND EACH THREAD HOLDS ONLY ITS OWN.
   *
   * Claiming maps shared memory and releasing unlinks it, so neither may happen
   * on the audio thread -- and OnParamChange IS the audio thread under VST3 and
   * CLAP automation. So every other thread only records what it wants, below,
   * and OnIdle does the work.
   *
   * The main thread keeps the WRITER (label, rate) outright. The audio thread
   * needs only the PUSHER, and it crosses to ProcessBlock through the handoff,
   * which frees a replaced one only once no block holds it. The slot is
   * released with the last half, so a pusher still mid-block keeps it claimed.
   */
  abus_writer_t* mWriter = nullptr;     /* main thread only                  */
  shell_handoff_t* mBus = nullptr;      /* the pusher, lent to ProcessBlock  */
  std::atomic<int> mWantSlot{1};        /* the Slot parameter, from any thread */
  std::atomic<uint32_t> mRate{0};       /* the host's rate, from OnReset       */
  std::atomic<bool> mResetSeen{false};  /* OnReset ran: retry, or retune       */
  std::atomic<bool> mReclaim{false};    /* a state load: claim afresh          */

  /* Main thread only. */
  int mStatus = 0;                 /* listenin::wire::Status */
  int mTriedSlot = 0;              /* the slot last asked for, won or not     */
  bool mWaiting = false;           /* a pusher is waiting on the audio thread */

  std::vector<float> mStage;       /* interleaved, pre-sized, never resized   */
  std::string mLabel;

  /*
   * THE METER'S PEAK IS WRITTEN ON THE AUDIO THREAD AND READ ON THE MESSAGE
   * THREAD, and it is a float, so the race is real but benign: the worst
   * outcome is a meter frame drawn from a value half-updated, at 60 Hz, once.
   * An atomic would be free here and honest, so it is one rather than a
   * comment promising it does not matter.
   */
  std::atomic<float> mPeak{0.f};

#ifdef WEBVIEW_EDITOR_DELEGATE
  /*
   * THE ANIMATED GROUND'S KICK DETECTOR. Not part of what this plugin does --
   * it publishes audio and nothing else -- but the design system gives every
   * window a ground, and a ground needs to know when a kick lands. The editor
   * is a WebView with no access to the host's audio, so the detection happens
   * here and one message per onset crosses over. engines/ground says why.
   *
   * mGroundFires is the last count the EDITOR was told about, so it is the
   * message thread's alone and needs no atomic.
   *
   * INSIDE THE EDITOR GUARD, so a build with no WebView -- the Move module's
   * shell, a test harness -- does not carry a detector on its audio thread for
   * a window that does not exist.
   */
  gnd_t* mGround = nullptr;
  uint32_t mGroundFires = 0;
#endif
};
