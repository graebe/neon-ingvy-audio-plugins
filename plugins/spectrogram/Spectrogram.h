/*
 * Spectrogram -- a rolling analyzer for Ableton Live, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Audio passes through bit for bit; the plugin's whole output is a picture.
 *
 * THE ANALYSIS IS NOT HERE. It is the Rust receiver in engines/spectro
 * (spectro_recv.h): the FFT, the log bands, the dB scale, the sum of several
 * sources and their clash are the engine's; the wire format, the message tags
 * and the buffer lifetimes are this file's.
 *
 * WHAT CROSSES A THREAD. ProcessAudio copies the mono sum into the receiver's
 * ring and nothing else; the receiver's own worker thread runs the transforms
 * and leaves finished columns in lock-free rings; OnIdle takes one tick's
 * picture (srecv_frame) and hands it to the WebView as bytes. The host's state
 * calls, on whatever thread it makes them, only record what the session is
 * looking at (State.h's Session); the main thread applies it to the receiver.
 */
#pragma once

#include "ni/WebPlugin.h"
#include "State.h"
#include "spectro_recv.h"
#include "shell_handoff.h"

#include <atomic>
#include <string>
#include <vector>

const int kNumPresets = 1;

/*
 * NO PARAMETERS, and that is a statement: nothing here changes what comes out,
 * and which picture a user is looking at is not something anyone automates.
 */
enum EParams
{
  kNumParams = 0
};

/* THIS PRODUCT'S MESSAGE TAGS, mirrored in ui/src/lib/msg.js. The shell's are
 * in ni/Editor.h. */
enum EMsgTags
{
  kMsgCols = 64,       /* -> "<ch>:<cols>:<bands>:" + raw bytes, oldest first     */
  kMsgAxis = 65,       /* -> the band centre frequencies, comma separated         */
  /* -> the host's clock, every tick whether or not a column went with it:
   * "<ppq>:<bpm>:<num>:<denom>:<running>:<ppqPerCol>:<sampleRate>". How many
   * bars the picture spans is the editor's layout, not a message. */
  kMsgSync = 66,
  /* -> the source list, "<slot>:<live>:<rate>:<label>" a line, on a slow timer:
   * a Listen-In appears at human speed. */
  kMsgSources = 67,
  kMsgClashCols = 68,  /* -> the clash mask, shaped as a column batch             */
  /* -> on ready: "<f_min>:<f_max>:<view>:<a>:<b>:<on>:<floor_db>:<balance_db>",
   * what the session is looking at, applied by the editor before it pushes. */
  kMsgState = 69,
  kMsgRange = 96,      /* <- "<f_min>:<f_max>" -- the zoom                        */
  /* <- "<slot>,<slot>,..." -- which buses to open, in order; empty is the own
   * channel alone. Derived by the editor from the view and the comparison. */
  kMsgSelect = 97,
  kMsgClash = 98,      /* <- "<floor_db>:<balance_db>" -- what counts as a clash  */
  /* <- "<ch>,<ch>,..." -- which channels are ADDED into the picture: one stream
   * reaches the editor, which never routes by channel. */
  kMsgView = 99,
  kMsgCompare = 100,   /* <- "<a>:<b>:<on>" -- the clash's two channels           */
};

class Spectrogram final : public ni::WebPlugin, private spectro::state::Session::Sink
{
public:
  Spectrogram(const iplug::InstanceInfo& info);
  ~Spectrogram() override;

  /* The chosen sources, the view and the clash settings, so a session reopens
   * looking at what it was looking at. State.cpp. Any thread but the audio
   * thread: neither touches the receiver. */
  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;

  /*
   * The most columns one tick sends. At the analyzer's rate a tick has none or
   * one waiting, so this is not a throttle: it is the bound that makes the
   * payload provably fit -- columns x bands is what the cap constrains.
   */
  static constexpr int kMaxColsPerTick = 32;
  static_assert(ni::wire::framed_size(kMaxColsPerTick * SPECTRO_BANDS + 32)
                    < ni::editor::kMaxJSString,
                "a full tick no longer fits the WebView's string cap -- "
                "kMaxColsPerTick and SPECTRO_BANDS are what constrain it");

private:
  void ProcessAudio(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void ResetAudio() override;
  void OnHostIdle() override;
  void OnEditorIdle() override;
  void OnEditorReady() override;
  bool OnEditorMessage(int tag, const std::string& arg) override;

  /* Main thread: replaces the receiver when ResetAudio asked for one, and
   * hands it whatever the session changed since the last tick. */
  void ServiceReceiver();
  /* Session::Sink -- the receiver calls, main thread only (they allocate, and
   * choosing sources waits for the analysis thread). */
  void ApplySources(const std::vector<unsigned int>& slots) override;
  void ApplyClash(float floorDb, float balanceDb) override;
  void ApplyRange(float lo, float hi) override;
  void SendPicture();
  /* The frequency scale. The log mapping is the analyzer's, never the UI's. */
  void SendAxis();
  void SendSync();
  /* The buses that exist, for the editor's picker. Probing creates nothing. */
  void SendSources();
  /* What the session is looking at, for an editor that has just opened. */
  void SendState();

  /*
   * THE RECEIVER: the own channel and every bus listened to, fed in step so
   * their columns compare cell by cell. The main thread's -- built, configured,
   * drained and freed there, and freeing it joins its worker. ProcessAudio
   * reaches it only through mRecvLend, which frees a replaced one once the
   * audio thread has let go. ResetAudio (which some AU hosts call off the main
   * thread) records the rate and asks for a rebuild; until OnIdle has made it,
   * the audio thread feeds nothing rather than a receiver built for the old
   * rate.
   */
  srecv_t* mRecv = nullptr;
  shell_handoff_t* mRecvLend = nullptr;
  std::atomic<float> mRecvRate{0.f};
  std::atomic<bool> mRecvStale{false};

  /* The mono sum, sized in ResetAudio; one tick's picture and clash, sized once. */
  std::vector<float> mMono;
  std::vector<unsigned char> mSum, mClash;
  /* The column payload, reused so OnIdle does not allocate every tick. */
  std::string mPayload;

  /*
   * WHAT THE SESSION IS LOOKING AT: the sources, the view, the comparison, the
   * clash and the zoom -- saved state, not parameters. Channel 0 is this track.
   * Written by the host's state calls and the editor, applied to the receiver
   * by the main thread only; a rebuilt receiver, a reopened editor and a saved
   * session all get it back from here.
   */
  spectro::state::Session mSession;

  /* The source list's slow timer. */
  int mSourceTick = 0;

  /*
   * THE HOST'S CLOCK, written on the audio thread -- the only place a host's
   * transport is legible -- and read by OnIdle. Relaxed: four independent facts
   * for a picture, where a tick mixing two blocks is off by one frame of
   * drawing.
   */
  static_assert(std::atomic<double>::is_always_lock_free,
                "publishing a playhead must not take a lock on the audio thread");
  std::atomic<double> mPubPpq{0.0};
  std::atomic<double> mPubBpm{120.0};
  std::atomic<double> mPubPpqPerCol{0.0};
  std::atomic<int> mPubSig{(4 << 8) | 4}; /* numerator << 8 | denominator */
  std::atomic<bool> mPubRunning{false};

  /* The audio thread's position: the host's PPQ while it runs, and advancing
   * at the last tempo seen while it does not, so the picture keeps filling. */
  double mPos = 0.0;
  double mLastBpm = 120.0;
  /* The analyzer's hop, for ppqPerCol; set by ResetAudio. */
  int mHop = 0;
};
