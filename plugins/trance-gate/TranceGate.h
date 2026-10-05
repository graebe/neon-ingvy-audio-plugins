/*
 * Trance Gate -- the Ableton Live plugin, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The DSP is the Rust engine in engines/trance-gate, reached through its C ABI:
 * the same engine the Schwung module builds into the Move's .so. This class
 * marshals -- parameters in, the pattern edits in, readouts and a capture out;
 * ni::WebPlugin is the rest of the shell.
 */
#pragma once

#include "ni/WebPlugin.h"
#include "ni/FileDialog.h"
#include "ni/Scope.h"
#include "tg_shell.h"
#include "Params.h"

#include <string>
#include <vector>

const int kNumPresets = 1;

/*
 * THIS PRODUCT'S MESSAGE TAGS, mirrored in ui/src/lib/msg.js. The shell's
 * (display strings, ready, typed text, height, ground, defaults) are in
 * ni/Editor.h.
 */
enum EMsgTags
{
  kMsgUiState = 64,       /* -> the engine's `ui` readout, once a frame          */
  kMsgParams = 65,        /* -> the `params` readout (fifteen values + width_ms) */
  kMsgScope = 66,         /* -> "<cols>:<cycleMs>:<head>:" + 4 bytes a column    */
  kMsgPatch = 67,         /* <-> the state blob, for copy and paste              */
  kMsgFileStatus = 68,    /* -> "ok:<words>" | "error:<words>", a file's outcome */
  kMsgSetStep = 96,       /* <- "<index>:<0 off|1 on|2 tie>"                     */
  kMsgSetDepth = 97,      /* <- "<index>:<0..1>"                                 */
  kMsgSetCursor = 98,     /* <- "<index>"                                        */
  kMsgRequestPatch = 99,  /* <- send me the blob (Copy gate config)              */
  kMsgSetOrder = 103,     /* <- "<index>:<rank>", its place in the fade          */
  /* <- regenerate the current slot. An action, not a parameter: a host
   * rewriting it would reroll the pattern. Payload: an optional seed. */
  kMsgRandomize = 104,
  /* -> the gate across one cycle as the engine applies it (tg_core_render_gate) */
  kMsgGate = 105,
  /* -> the envelope plot's gated and dialled curves (tg_core_render_envelope) */
  kMsgEnvelope = 106,
  /* <- "slot" | "bank": save the current slot, or all eight, to a file the
   * user picks. Answered with kMsgFileStatus once the panel closes. */
  kMsgExportFile = 107,
  /* <- open a slot or bank file and import it. Answered the same way. */
  kMsgImportFile = 108,
};

class TranceGate final : public ni::WebPlugin
{
public:
  TranceGate(const iplug::InstanceInfo& info);
  ~TranceGate() override;

  /* The pattern is not a parameter -- 128 steps across 8 slots. It travels as
   * the engine's own state blob, the text the Move module writes. */
  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;

  /* The capture's width: column k is pattern phase k / kScopeCols. */
  static constexpr int kScopeCols = 256;

private:
  void ProcessAudio(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void ResetAudio() override;
  void OnHostIdle() override;
  void OnEditorIdle() override;
  void OnEditorReady() override;
  bool OnEditorMessage(int tag, const std::string& arg) override;
  /* The stages read in the unit Env Time asks for (Params.cpp). */
  void FormatDisplay(int paramIdx, WDL_String& str) const override;
  double ParseDisplay(int paramIdx, const char* text) const override;
  /* The gate's open time in ms, as the engine last published it; 0 unknown. */
  double WidthMs() const;

  /* The fifteen parameters into the engine the shell lent this block; the
   * shell writes what the host moved into the current slot. */
  void PushParams(tg_core_t* core);
  /* The pattern plot's curve, when the patch has moved (or `force`). */
  void SendGate(bool force);
  void SendScope();
  /* Slot files: a panel, then the file, then the outcome to the editor. */
  void ExportFile(bool all);
  void ImportFile();
  void SendFileStatus(bool ok, const std::string& words);

  /* THE ENGINE, BEHIND ITS SHELL: the audio thread takes it for a block, every
   * other thread posts edits and reads what it published (tg_shell.h). */
  tg_shell_t* mShell = nullptr;

  ni::Scope<kScopeCols> mScope;
  /* The save and open panels, remembering the last folder. Main thread. */
  ni::FileDialog mFiles;
  /* The patch the last curve was rendered from. Main thread. */
  std::string mGateState;
  /* The stage readouts' unit and scale as the editor was last told them, so a
   * tempo, Width or Env Time change re-sends them. Main thread. */
  bool mStageMs = false;
  double mStageWidthMs = -1.0;

  /* iPlug2's `sample` is double and the engine's float path is the one the
   * golden render pins. Sized in ResetAudio, never on the audio thread. */
  std::vector<float> mL, mR, mDry, mSweep;
};
