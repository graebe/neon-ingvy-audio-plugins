// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In -- a tap that other plugins can read.
 *
 * Audio passes through bit for bit and is published, stereo, on a shared-memory
 * bus a Spectrogram can listen to (engines/audio-bus). ni::WebPlugin is the
 * rest of the shell.
 */
#pragma once

#include "ni/WebPlugin.h"
#include "audio_bus.h"
#include "shell_handoff.h"
#include "State.h"

#include <atomic>
#include <string>
#include <vector>

const int kNumPresets = 1;

/* THIS PRODUCT'S MESSAGE TAGS, mirrored in ui/src/lib/msg.js. The shell's are
 * in ni/Editor.h. */
enum EMsgTags
{
  kMsgState = 64,  /* -> "<slot>:<status>:<peak>", every tick               */
  kMsgLabel = 96,  /* <-> the display name, both ways                       */
};

/*
 * The block staged for the bus. A host may hand over more than it announced,
 * so ProcessAudio chunks against this rather than resizing on the audio thread.
 */
const int kStageFrames = 4096;

class ListenIn final : public ni::WebPlugin
{
public:
  ListenIn(const iplug::InstanceInfo& info);
  ~ListenIn() override;

  /* The audio thread under VST3 and CLAP automation: records the slot only. */
  void OnParamChange(int paramIdx) override;

  /* Parameters, then the label: text cannot be a parameter. State.cpp. Any
   * thread but the audio thread: both go through mSession, never the bus. */
  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;

private:
  void ProcessAudio(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void ResetAudio() override;
  /* The bus is HOST-facing: its whole lifecycle runs editor or not. */
  void OnHostIdle() override { ServiceBus(); }
  void OnEditorIdle() override { SendState(); }
  void OnEditorReady() override;
  bool OnEditorMessage(int tag, const std::string& arg) override;

  /* Main thread only: claims, releases and retunes the bus to match what the
   * other threads asked for. */
  void ServiceBus();
  /* Main thread only: the label the session changed, to the writer -- and,
   * after a load, to an open editor. */
  void ServiceLabel();
  void SendState();

  /*
   * A CLAIM IS TWO HALVES, AND EACH THREAD HOLDS ONLY ITS OWN. Claiming maps
   * shared memory and releasing unlinks it, so neither happens on the audio
   * thread; every other thread only records what it wants, and OnIdle acts.
   * The main thread keeps the WRITER (label, rate); the audio thread's PUSHER
   * crosses to it through the handoff, which frees a replaced one only once no
   * block holds it. The slot goes with the last half.
   */
  abus_writer_t* mWriter = nullptr;     /* main thread only                   */
  shell_handoff_t* mBus = nullptr;      /* the pusher, lent to ProcessAudio   */
  std::atomic<int> mWantSlot{1};        /* the Slot parameter, from any thread */
  std::atomic<uint32_t> mRate{0};       /* the host's rate, from ResetAudio    */
  std::atomic<bool> mResetSeen{false};  /* a reset: retry, or retune           */
  /* The label, written by state loads and the editor; see State.h. */
  listenin::state::Session mSession;

  /* Main thread only. */
  int mStatus = 0;                 /* listenin::wire::Status                   */
  int mTriedSlot = 0;              /* the slot last asked for, won or not      */
  bool mWaiting = false;           /* a pusher is waiting on the audio thread  */
  bool mReclaim = false;           /* a state load: claim afresh               */
  std::string mLabel;              /* the label the writer was given           */

  std::vector<float> mStage;       /* interleaved, pre-sized, never resized    */
  /* The meter's peak: written on the audio thread, read on the main one. */
  std::atomic<float> mPeak{0.f};
};
