/*
 * NI Side-Chain -- the iPlug2 shell. See SideChain.h for what it may do.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "SideChain.h"
#include "IPlug_include_in_plug_src.h"
#include "Wire.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace iplug;

SideChain::SideChain(const InstanceInfo& info)
: ni::WebPlugin(info, MakeConfig(kNumParams, kNumPresets), {"nisidechain", __FILE__})
{
  /* Params.cpp, where a test can reach them. */
  sc::params::Declare([this](int i) { return GetParam(i); });
  mShell = sc_shell_create(GetSampleRate() > 0.0 ? GetSampleRate() : 44100.0);
}

SideChain::~SideChain()
{
  sc_shell_destroy(mShell);
  mShell = nullptr;
}

bool SideChain::SerializeState(IByteChunk& chunk) const
{
  return sc::params::Save(chunk, [this](IByteChunk& c) { return PutParams(c); });
}

int SideChain::UnserializeState(const IByteChunk& chunk, int startPos)
{
  return sc::params::Load(
    chunk, startPos,
    [this](const IByteChunk& c, int pos) { return CheckParams(c, pos); },
    [this](const IByteChunk& c, int pos) { return GetParams(c, pos); });
}

void SideChain::ResetAudio()
{
  if (!mShell)
    return;
  sc_shell_post_sample_rate(mShell, GetSampleRate());

  /* A host may hand over more than it announced, so ProcessAudio chunks
   * against this capacity; SC_MAX_BLOCK is the engine's own ceiling. */
  const size_t cap = size_t(std::max(64, std::min(GetBlockSize(), SC_MAX_BLOCK)));
  mL.assign(cap, 0.f);
  mR.assign(cap, 0.f);
  mDry.assign(cap, 0.f);
  mGain.assign(cap, 1.f);
  mSweep.assign(cap, 0.f);
  mKeyL.assign(cap, 0.f);
  mKeyR.assign(cap, 0.f);
  /* A column is a slice of a cycle, and the cycle just changed length. */
  mScope.Clear();
}

/*
 * EVERY PARAMETER, EVERY BLOCK. Pushing on change would trust the host to
 * report every path that moves a value -- automation, a preset, typed text, a
 * control surface. Fifteen stores is nothing.
 */
void SideChain::PushParams(sc_core_t* core)
{
  sc_core_set_num(core, SC_P_SOURCE, GetParam(kSource)->Value());
  sc_core_set_num(core, SC_P_RATE, GetParam(kRate)->Value());
  sc_core_set_num(core, SC_P_TIME_MODE, GetParam(kTimeMode)->Value());
  sc_core_set_num(core, SC_P_DELAY, GetParam(kDelay)->Value());
  sc_core_set_num(core, SC_P_ATTACK, GetParam(kAttack)->Value());
  sc_core_set_num(core, SC_P_HOLD, GetParam(kHold)->Value());
  sc_core_set_num(core, SC_P_RELEASE, GetParam(kRelease)->Value());
  /* The engine takes 0..1; the host shows a percentage. */
  sc_core_set_num(core, SC_P_DEPTH, GetParam(kDepth)->Value() / 100.0);
  sc_core_set_num(core, SC_P_CURVE, GetParam(kCurve)->Value());
  sc_core_set_num(core, SC_P_CHANNEL, GetParam(kChannel)->Value());
  sc_core_set_num(core, SC_P_NOTE, GetParam(kNote)->Value());
  sc_core_set_num(core, SC_P_MIDI_MODE, GetParam(kMidiMode)->Value());
  sc_core_set_num(core, SC_P_VEL_SENS, GetParam(kVelSens)->Value() / 100.0);
  sc_core_set_num(core, SC_P_THRESHOLD, GetParam(kThreshold)->Value());
  sc_core_set_num(core, SC_P_LOCKOUT, GetParam(kLockout)->Value());
}

/*
 * MIDI WITH ITS SAMPLE OFFSET: a duck lands on the note's sample rather than on
 * the top of whatever buffer the host uses -- jitter, which nothing downstream
 * could compensate. Not forwarded: a ducker echoing its trigger notes would arm
 * the instrument after it.
 */
void SideChain::ProcessMidiMsg(const IMidiMsg& msg)
{
  /* Ahead of the block the note belongs to; a second begin in one block is
   * harmless. */
  sc_core_t* core = sc_shell_begin(mShell);
  if (!core)
    return;
  const unsigned char bytes[3] = {
    (unsigned char) msg.mStatus,
    (unsigned char) msg.mData1,
    (unsigned char) msg.mData2,
  };
  sc_core_on_midi(core, bytes, 3, msg.mOffset);
}

void SideChain::ProcessAudio(sample** inputs, sample** outputs, int nFrames)
{
  const int nOut = NOutChansConnected();
  const int cap = int(std::min(mL.size(), mR.size()));
  if (!mShell || nFrames <= 0 || nOut <= 0 || cap <= 0)
    return;

  /* Which channel is what -- per channel, never by counting them. */
  const sc::wire::InputMap in = sc::wire::map_inputs(
    IsChannelConnected(ERoute::kInput, 0), IsChannelConnected(ERoute::kInput, 1),
    IsChannelConnected(ERoute::kInput, 2), IsChannelConnected(ERoute::kInput, 3));

  sc_core_t* core = sc_shell_begin(mShell);
  PushParams(core);

  const bool haveKey = in.keyL >= 0;
  mKeyConnected.store(haveKey ? 1 : 0, std::memory_order_relaxed);
  sc_core_set_key_connected(core, haveKey ? 1 : 0);
  const bool stereoOut = nOut > 1;
  const bool capture = EditorIsOpen();

  ni::wire::for_each_chunk(nFrames, cap, [&](int off, int n) {
    ni::wire::to_float(inputs[in.mainL] + off, mL.data(), n);
    ni::wire::to_float(inputs[in.mainR] + off, mR.data(), n);
    std::memcpy(mDry.data(), mL.data(), sizeof(float) * size_t(n));

    if (haveKey)
    {
      ni::wire::to_float(inputs[in.keyL] + off, mKeyL.data(), n);
      ni::wire::to_float(inputs[in.keyR] + off, mKeyR.data(), n);
      mKeyIsMain.store(sc::wire::key_is_duplicate(mDry.data(), mKeyL.data(), n) ? 1 : 0,
                       std::memory_order_relaxed);
      sc_core_push_key_f32(core, mKeyL.data(), mKeyR.data(), n);
    }
    else
    {
      mKeyIsMain.store(0, std::memory_order_relaxed);
    }

    /* The host's position at the block, advanced to this chunk. */
    const ni::wire::Transport host = HostTransport();
    sc_transport_t t = {host.running, host.beats, host.bpm};
    if (off > 0 && t.running)
      t.beats = ni::wire::advance_beats(t.beats, off, double(t.bpm), GetSampleRate());

    sc_core_process_f32_split_tap(core, mL.data(), mR.data(), mGain.data(), mSweep.data(), n, &t);
    if (capture)
      mScope.Push(mDry.data(), mL.data(), mGain.data(), mSweep.data(), n);

    ni::wire::from_float(mL.data(), outputs[0] + off, n);
    if (stereoOut)
      ni::wire::from_float(mR.data(), outputs[1] + off, n);
  });

  sc_shell_end(mShell, nFrames);
}

/* What the audio thread last published -- never the engine itself. */
void SideChain::OnEditorIdle()
{
  char buf[SC_STATE_MAX];
  if (sc_shell_read(mShell, "ui", buf, int(sizeof buf)) > 0)
    SendFramed(kMsgUiState, buf, int(strlen(buf)));
  if (sc_shell_read(mShell, "params", buf, int(sizeof buf)) > 0)
    SendFramed(kMsgParams, buf, int(strlen(buf)));
  if (sc_shell_read(mShell, "stage_ms", buf, int(sizeof buf)) > 0)
    SendFramed(kMsgStageMs, buf, int(strlen(buf)));

  char buses[16];
  const int n = snprintf(buses, sizeof buses, "%d:%d",
                         mKeyConnected.load(std::memory_order_relaxed),
                         mKeyIsMain.load(std::memory_order_relaxed));
  SendFramed(kMsgBuses, buses, n);

  SendScope();
}

/*
 * "<cols>:" a seen flag per column ":" then five hex bytes a column -- dry
 * low/high, wet low/high, the gain. The flags are how the editor tells a
 * column the sweep has not reached from one holding silence.
 */
void SideChain::SendScope()
{
  char scope[kScopeCols * 11 + 64];
  char* p = scope + snprintf(scope, sizeof scope, "%d:", kScopeCols);
  for (int i = 0; i < kScopeCols; i++)
    *p++ = mScope.Seen(i) ? '1' : '0';
  *p++ = ':';
  for (int i = 0; i < kScopeCols; i++)
    p = mScope.PutColumn(p, i, true);
  *p = '\0';
  SendFramed(kMsgScope, scope, int(p - scope));
}
