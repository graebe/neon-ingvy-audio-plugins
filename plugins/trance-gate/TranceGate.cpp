/*
 * Trance Gate -- the Ableton Live plugin, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "TranceGate.h"
#include "Patch.h"
#include "IPlug_include_in_plug_src.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>

using namespace iplug;

TranceGate::TranceGate(const InstanceInfo& info)
: ni::WebPlugin(info, MakeConfig(kNumParams, kNumPresets), {"tgate", __FILE__})
, mFiles(GetBundleID())
{
  /* Params.cpp, where a test can reach them. */
  tg::params::Declare([this](int i) { return GetParam(i); });
  MakeDefaultPreset("Default", kNumPresets);
  mShell = tg_shell_create(GetSampleRate() > 0.0 ? GetSampleRate() : 44100.0);
}

TranceGate::~TranceGate()
{
  tg_shell_destroy(mShell);
  mShell = nullptr;
}

/*
 * THE PATTERN IS SAVED FROM WHAT THE ENGINE PUBLISHED -- an edit posted a
 * moment ago and not yet applied included, so a save straight after an edit
 * has it whether or not audio is running. Patch.cpp has both halves.
 */
bool TranceGate::SerializeState(IByteChunk& chunk) const
{
  return tg::patch::Save(
    mShell, chunk, [this](IByteChunk& c) { return PutParams(c); },
    [this](int i) { return GetParam(i)->Value(); });
}

int TranceGate::UnserializeState(const IByteChunk& chunk, int startPos)
{
  /* The blob and the restored parameters are posted as one edit, not
   * applied: the audio thread picks them up at the top of its next block.
   * Nothing here may call a main-thread API -- a host picks this thread. */
  return tg::patch::Load(
    mShell, chunk, startPos,
    [this](const IByteChunk& c, int p) { return CheckParams(c, p); },
    [this](const IByteChunk& c, int p) { return GetParams(c, p); },
    [this](int i) { return GetParam(i)->Value(); });
}

void TranceGate::ResetAudio()
{
  tg_shell_post_sample_rate(mShell, GetSampleRate());
  const size_t n = size_t(std::max(GetBlockSize(), 1));
  mL.assign(n, 0.0f);
  mR.assign(n, 0.0f);
  mDry.assign(n, 0.0f);
  mSweep.assign(n, 0.0f);
  /* Or the first picture after a rate change is the last session's. */
  mScope.Clear();
}

/*
 * Every value, every block, through the shell: it writes what the host moved
 * into the current slot, and on the block the Slot moves it writes nothing
 * else -- the new slot's values win and the host follows (OnHostIdle).
 */
void TranceGate::PushParams(tg_core_t* core)
{
  double values[TG_P_COUNT];
  tg::patch::Values([this](int i) { return GetParam(i)->Value(); }, values);
  tg_shell_push(mShell, core, values, TG_P_COUNT);
}

void TranceGate::ProcessAudio(sample** inputs, sample** outputs, int nFrames)
{
  const int nOut = NOutChansConnected();
  const int cap = int(std::min(mL.size(), mR.size()));
  if (!mShell || nFrames <= 0 || nOut <= 0 || cap <= 0)
    return;

  /* Every edit posted since the last block lands here, before the host's
   * parameters are pushed over it. */
  tg_core_t* core = tg_shell_begin(mShell);
  PushParams(core);

  const ni::wire::Transport host = HostTransport();
  tg_transport_t t = {host.running, host.beats, host.bpm};
  const bool stereo = nOut > 1 && outputs[1] != nullptr;
  const bool capture = EditorIsOpen();

  ni::wire::for_each_chunk(nFrames, cap, [&](int off, int n) {
    ni::wire::to_float(inputs[0] + off, mL.data(), n);
    ni::wire::to_float((stereo ? inputs[1] : inputs[0]) + off, mR.data(), n);
    if (capture)
      std::memcpy(mDry.data(), mL.data(), sizeof(float) * size_t(n));

    tg_core_process_f32_split_tap(core, mL.data(), mR.data(),
                                  capture ? mSweep.data() : nullptr, n, &t);
    if (capture)
      mScope.Push(mDry.data(), mL.data(), nullptr, mSweep.data(), n);

    ni::wire::from_float(mL.data(), outputs[0] + off, n);
    if (stereo)
      ni::wire::from_float(mR.data(), outputs[1] + off, n);
    /* A chunk continues the block, so the transport moves with it. */
    if (t.running)
      t.beats = ni::wire::advance_beats(t.beats, n, double(t.bpm), GetSampleRate());
  });

  tg_shell_end(mShell, nFrames);
}

/*
 * A slot switch (or a paste) recalled a whole sound in the engine; every host
 * parameter follows, through the host -- automation lanes, the host's own UI
 * and the editor all show the recalled values.
 */
void TranceGate::OnHostIdle()
{
  tg::patch::Follow(
    mShell, [this](int i) { return GetParam(i)->Value(); },
    [this](int i, double v) { SetParamFromPlugin(i, v); });
}

double TranceGate::WidthMs() const
{
  /* "...:release:width_ms:fade:..." -- field 12 of the `params` readout. */
  char buf[TG_STATE_MAX];
  if (tg_shell_read(mShell, "params", buf, int(sizeof buf)) <= 0)
    return 0.0;
  const char* p = buf;
  for (int field = 0; field < 12 && p; field++)
  {
    p = std::strchr(p, ':');
    if (p)
      p++;
  }
  return p ? ni::wire::parse_number(p) : 0.0;
}

void TranceGate::FormatDisplay(int paramIdx, WDL_String& str) const
{
  if (!tg::params::IsStage(paramIdx))
    return ni::WebPlugin::FormatDisplay(paramIdx, str);
  const bool ms = GetParam(kTimeMode)->Int() == 0;
  str.Set(tg::params::FormatStage(GetParam(paramIdx)->Value(), ms, WidthMs()).c_str());
}

double TranceGate::ParseDisplay(int paramIdx, const char* text) const
{
  if (!tg::params::IsStage(paramIdx))
    return ni::WebPlugin::ParseDisplay(paramIdx, text);
  return tg::params::ParseStage(text, GetParam(kTimeMode)->Int() == 0, WidthMs());
}

void TranceGate::OnEditorIdle()
{
  /* The stage readouts follow Env Time and the gate's length in ms, neither of
   * which is the stage parameter itself changing. */
  const bool ms = GetParam(kTimeMode)->Int() == 0;
  const double width = WidthMs();
  if (ms != mStageMs || (ms && std::fabs(width - mStageWidthMs) > 1e-6))
  {
    mStageMs = ms;
    mStageWidthMs = width;
    SendDisplay(kAttack);
    SendDisplay(kDecay);
    SendDisplay(kRelease);
  }

  char buf[TG_STATE_MAX];
  if (tg_shell_read(mShell, "ui", buf, int(sizeof buf)) > 0)
    SendFramed(kMsgUiState, buf, int(strlen(buf)));
  SendGate(false);
  if (tg_shell_read(mShell, "params", buf, int(sizeof buf)) > 0)
    SendFramed(kMsgParams, buf, int(strlen(buf)));
  SendScope();
}

/* An editor that opens on a patch nobody then touches still gets its curve. */
void TranceGate::OnEditorReady()
{
  SendGate(true);
}

/*
 * The curve is rendered only when the patch moves: the state string is the
 * whole patch, so an unchanged string is an unchanged curve.
 */
void TranceGate::SendGate(bool force)
{
  char state[TG_STATE_MAX];
  if (tg_shell_read(mShell, "state", state, int(sizeof state)) <= 0)
    return;
  if (!force && mGateState == state)
    return;
  mGateState = state;
  char gate[TG_GATE_MAX];
  const int n = tg_core_render_gate(state, gate, int(sizeof gate));
  if (n > 0)
    SendFramed(kMsgGate, gate, n);
  /* The envelope plot's curves, from the same patch and the same engine. */
  char env[TG_ENVELOPE_MAX];
  const int ne = tg_core_render_envelope(state, env, int(sizeof env));
  if (ne > 0)
    SendFramed(kMsgEnvelope, env, ne);
}

/*
 * "<cols>:<cycleMs>:<head>:" and four raw bytes a column, every frame, in
 * place: column k is phase k / kScopeCols and `head` marks the write point.
 */
void TranceGate::SendScope()
{
  char scope[kScopeCols * 4 + 48];
  char* p = scope + snprintf(scope, sizeof scope, "%d:%d:%d:", kScopeCols,
                             int(tg_shell_cycle_ms(mShell)), mScope.Head());
  for (int i = 0; i < kScopeCols; i++)
    p = mScope.PutColumn(p, i, false);
  SendFramed(kMsgScope, scope, int(p - scope));
}

/*
 * The pattern's edits: none is a host parameter, so each is posted to the
 * engine, which applies it at the top of its next block.
 */
bool TranceGate::OnEditorMessage(int tag, const std::string& arg)
{
  using tg::patch::Edit;
  switch (tag)
  {
    case kMsgSetCursor: tg::patch::Post(mShell, Edit::Cursor, arg); return true;
    case kMsgSetStep: tg::patch::Post(mShell, Edit::Step, arg); return true;
    case kMsgSetDepth: tg::patch::Post(mShell, Edit::Depth, arg); return true;
    case kMsgSetOrder: tg::patch::Post(mShell, Edit::Order, arg); return true;
    case kMsgRandomize: tg::patch::Post(mShell, Edit::Randomize, arg); return true;
    case kMsgExportFile: ExportFile(arg == "bank"); return true;
    case kMsgImportFile: ImportFile(); return true;
    case kMsgCopySlot: CopySlot(); return true;
    case kMsgPasteSlot: PasteSlot(); return true;
    default:
      return false;
  }
}

/*
 * SLOT FILES. The panel is the system's, shown as a sheet on the editor; the
 * text is the engine's (tg_shell_export / tg_shell_import); Patch.cpp moves it
 * between the two and words the outcome, which the hint bar shows. An import
 * lands at the top of the next block, and the host's parameters follow it as
 * they follow a slot switch (OnHostIdle).
 */
void TranceGate::ExportFile(bool all)
{
  using tg::patch::FileKind;
  const FileKind kind = all ? FileKind::Bank : FileKind::Slot;
  const int slot = GetParam(kSlot)->Int();
  const bool shown = mFiles.Save(
    EditorView(), tg::patch::FileName(kind, slot), tg::patch::Extension(kind),
    [this, kind, slot](const std::string& path) {
      if (path.empty()) return; /* cancelled: nothing to say */
      std::string words;
      const bool ok = tg::patch::ExportFile(
        mShell, [this](int i) { return GetParam(i)->Value(); }, kind, slot, path, words);
      SendStatus(ok, words);
    });
  if (!shown)
    SendStatus(false, "Failed to show the save panel.");
}

void TranceGate::ImportFile()
{
  using tg::patch::FileKind;
  const bool shown = mFiles.Open(
    EditorView(), {tg::patch::Extension(FileKind::Slot), tg::patch::Extension(FileKind::Bank)},
    [this](const std::string& path) {
      if (path.empty()) return;
      std::string words;
      const bool ok = tg::patch::ImportFile(mShell, GetParam(kSlot)->Int(), path, words);
      SendStatus(ok, words);
    });
  if (!shown)
    SendStatus(false, "Failed to show the open panel.");
}

/*
 * THE CLIPBOARD IS THE PLUGIN'S, as the panels are: a WebView in a host can
 * neither read the clipboard nor receive ⌘V. Copy writes the current slot as a
 * slot file's text; Paste hands whatever is there to the engine, which decides
 * what it is (Patch.cpp, tg_shell_paste). A paste lands at the top of the next
 * block and the host's parameters follow it (OnHostIdle), as for an import.
 */
void TranceGate::CopySlot()
{
  std::string words;
  const bool ok = tg::patch::CopySlot(
    mShell, [this](int i) { return GetParam(i)->Value(); }, GetParam(kSlot)->Int(),
    ni::Clipboard::System(), words);
  SendStatus(ok, words);
}

void TranceGate::PasteSlot()
{
  std::string words;
  const bool ok = tg::patch::Paste(mShell, GetParam(kSlot)->Int(), ni::Clipboard::System(), words);
  SendStatus(ok, words);
}

void TranceGate::SendStatus(bool ok, const std::string& words)
{
  SendText(kMsgStatus, (ok ? "ok:" : "error:") + words);
}
