// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain -- the Ableton Live plugin, on iPlug2.
 *
 * The DSP is the Rust engine in engines/side-chain, reached through its C ABI:
 * the same engine the Schwung module builds into its .so, which `sc_render_ab`
 * keeps honest. This class marshals -- buffers, the host's transport and buses,
 * parameters, a capture for the scope -- and decides nothing about the sound;
 * ni::WebPlugin is the rest of the shell.
 */
#pragma once

#include "ni/WebPlugin.h"
#include "ni/Scope.h"
#include "sc_shell.h"
#include "Params.h"

#include <atomic>
#include <vector>

const int kNumPresets = 1;

/* THIS PRODUCT'S MESSAGE TAGS, mirrored in ui/src/lib/msg.js. The shell's are
 * in ni/Editor.h. */
enum EMsgTags
{
  kMsgUiState = 64,  /* -> the engine's `ui` readout, once a frame          */
  kMsgParams = 65,   /* -> the `params` readout, all fifteen values         */
  kMsgScope = 66,    /* -> "<cols>:" + seen, dry, wet, gain: 6 bytes a column */
  kMsgStageMs = 67,  /* -> the four stage lengths in ms, for the axis       */
  kMsgBuses = 68,    /* -> "<keyConnected>:<keyIsMain>"                     */
};

class SideChain final : public ni::WebPlugin
{
public:
  SideChain(const iplug::InstanceInfo& info);
  ~SideChain() override;

  /* Everything this plugin holds is a parameter: Params.cpp's Save and Load
   * behind shell_state.h's header. */
  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;

  void ProcessMidiMsg(const iplug::IMidiMsg& msg) override;

  /*
   * The capture's width. The axis is ONE CYCLE, from the engine's sweep, so the
   * shape the user drags is drawn directly above the audio it shaped. 512
   * columns is about a pixel and a half each at the plot's width: the waveform
   * is quantised by the screen rather than the wire.
   */
  static constexpr int kScopeCols = 512;

private:
  void ProcessAudio(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void ResetAudio() override;
  void OnEditorIdle() override;
  void OnEditorReady() override { mScope.Retire(); }

  /* The fifteen parameters into the engine the shell lent this block. */
  void PushParams(sc_core_t* core);
  void SendScope();

  /* THE ENGINE, BEHIND ITS SHELL (sc_shell.h). */
  sc_shell_t* mShell = nullptr;

  ni::Scope<kScopeCols> mScope;

  /* The engine's float path is the one the render A/B pins, so the block is
   * converted. Sized in ResetAudio, never on the audio thread. mDry is the
   * input before the engine overwrites it; mGain and mSweep are its taps. */
  std::vector<float> mL, mR, mDry, mGain, mSweep, mKeyL, mKeyR;

  /*
   * WHAT ONLY THIS SIDE KNOWS, for the editor: whether the host connected a
   * key at all (an unpatched bus and a silent one are the same zeroes), and
   * whether the "key" is byte for byte the main input -- the Logic and
   * GarageBand bug, reported rather than worked around (Wire.h).
   */
  std::atomic<int> mKeyConnected{0};
  std::atomic<int> mKeyIsMain{0};
};
