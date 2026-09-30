/*
 * NI Listen-In -- a tap that other plugins can read.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "ListenIn.h"
#include "Wire.h"
#include "IPlug_include_in_plug_src.h"

#include <algorithm>
#include <cmath>

using namespace iplug;
using namespace listenin;

/* The handoff frees through this; abus_pusher_release's pointer type is not
 * void*. */
static void ReleasePusher(void* p)
{
  abus_pusher_release(static_cast<abus_pusher_t*>(p));
}

ListenIn::ListenIn(const InstanceInfo& info)
: ni::WebPlugin(info, MakeConfig(kNumParams, kNumPresets), {"nilistenin", __FILE__})
{
  /* State.cpp, where a test can reach it. */
  state::Declare([this](int i) { return GetParam(i); });
  mStage.resize(size_t(kStageFrames) * abus_channels(), 0.f);
  mLabel.reserve(32);
  mBus = shell_handoff_new(ReleasePusher);
  MakeDefaultPreset("Default", kNumPresets);
}

ListenIn::~ListenIn()
{
  /* The audio thread has stopped, so the live pusher goes too, and with the
   * writer the slot. */
  shell_handoff_free(mBus);
  mBus = nullptr;
  abus_writer_release(mWriter);
  mWriter = nullptr;
}

/*
 * THE BUS FOLLOWS WHAT THE OTHER THREADS ASKED FOR, here and nowhere else.
 * Nothing is claimed before the host's first reset; a reset retunes a live bus
 * and retries a refused one; a new slot or a loaded state claims afresh.
 */
void ListenIn::ServiceBus()
{
  /* A pusher replaced earlier is freed once the audio thread has let go. */
  shell_handoff_collect(mBus);
  ServiceLabel();

  const uint32_t rate = mRate.load(std::memory_order_acquire);
  if (rate == 0)
    return;

  const bool reset = mResetSeen.exchange(false, std::memory_order_acq_rel);
  const bool reload = mReclaim;
  mReclaim = false;
  const int want = wire::clamp_slot(mWantSlot.load(std::memory_order_relaxed));

  /* Either side of a rate change is a different signal, so the bus restarts
   * rather than splicing. Posted: the pusher applies it. */
  if (reset && mWriter)
    abus_writer_set_sample_rate(mWriter, rate);

  if (!(mWaiting || reload || want != mTriedSlot || (reset && !mWriter)))
    return;

  /*
   * RELEASE FIRST, AND ONLY THEN CLAIM: moving 3 -> 4 -> 3 would otherwise find
   * slot 3 still held by this instance and call it taken. The writer goes at
   * once; the slot goes with the pusher once the audio thread lets go -- within
   * a block -- and until then the claim waits for the next tick.
   */
  abus_writer_release(mWriter);
  mWriter = nullptr;
  shell_handoff_set(mBus, nullptr);
  mWaiting = shell_handoff_collect(mBus) > 0;
  if (mWaiting)
    return;

  mTriedSlot = want;
  abus_writer_t* w = nullptr;
  abus_pusher_t* p = nullptr;
  switch (abus_writer_claim(uint32_t(want), rate, &w, &p))
  {
    case ABUS_OK:
      mStatus = wire::kLive;
      mWriter = w;
      if (!mLabel.empty())
        abus_writer_set_label(mWriter, mLabel.c_str());
      shell_handoff_set(mBus, p);
      break;
    case ABUS_ERR_TAKEN:
      /* Another Listen-In publishes here, and the editor says so: a tap that
       * silently does nothing is worse than one that refuses out loud. */
      mStatus = wire::kTaken;
      break;
    default:
      mStatus = wire::kUnavailable;
      break;
  }
  SendState();
}

void ListenIn::ServiceLabel()
{
  bool loaded = false;
  if (!mSession.Take(mLabel, loaded))
    return;
  if (mWriter)
    abus_writer_set_label(mWriter, mLabel.c_str());
  if (loaded)
  {
    /* The slot and the label changed underneath the bus: claim afresh. The
     * editor typed neither, so it is told. */
    mReclaim = true;
    if (EditorIsOpen())
      SendText(kMsgLabel, mLabel);
  }
}

/* Recorded, not acted on: this may not be the main thread. */
void ListenIn::ResetAudio()
{
  mRate.store(uint32_t(std::lround(GetSampleRate() > 0.0 ? GetSampleRate() : 48000.0)),
              std::memory_order_release);
  mResetSeen.store(true, std::memory_order_release);
}

void ListenIn::OnParamChange(int paramIdx)
{
  if (paramIdx == kSlot)
    mWantSlot.store(wire::clamp_slot(GetParam(kSlot)->Int()), std::memory_order_relaxed);
}

void ListenIn::ProcessAudio(sample** inputs, sample** outputs, int nFrames)
{
  const int nIn = NInChansConnected();
  const int nOut = NOutChansConnected();
  if (nIn < 1 || nOut < 1)
    return;

  /* The tap reads the input before the passthrough: a host may hand over one
   * buffer for both. The bus is always stereo; a mono source is duplicated. */
  const bool stereoIn = nIn > 1 && inputs[1] != nullptr;
  float peak = 0.f;
  /* Held for the block; null when no slot is claimed, which push ignores. */
  auto* bus = static_cast<abus_pusher_t*>(shell_handoff_acquire(mBus));

  ni::wire::for_each_chunk(nFrames, kStageFrames, [&](int off, int n) {
    for (int i = 0; i < n; i++)
    {
      const float l = float(inputs[0][off + i]);
      const float r = float(stereoIn ? inputs[1][off + i] : inputs[0][off + i]);
      mStage[size_t(i) * 2] = l;
      mStage[size_t(i) * 2 + 1] = r;
      peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    abus_pusher_push(bus, mStage.data(), uint32_t(n));
  });
  shell_handoff_release(mBus);

  /* The peak decays rather than resets, or a quiet block landing under a
   * 60 Hz read would flicker the meter to nothing. */
  const float prev = mPeak.load(std::memory_order_relaxed);
  mPeak.store(std::max(peak, prev * 0.85f), std::memory_order_relaxed);

  ni::wire::passthrough(inputs, nIn, outputs, nOut, nFrames);
}

/* The chunk is State.cpp's: parameters, then the label. */
bool ListenIn::SerializeState(IByteChunk& chunk) const
{
  return state::Save(chunk, [this](IByteChunk& c) { return PutParams(c); }, mSession.Label());
}

int ListenIn::UnserializeState(const IByteChunk& chunk, int startPos)
{
  /* Recorded, not applied: the host picks this thread. The next idle tick
   * hands the label to the writer and claims the bus afresh. */
  return mSession.Load(
    chunk, startPos,
    [this](const IByteChunk& c, int p) { return CheckParams(c, p); },
    [this](const IByteChunk& c, int p) { return GetParams(c, p); });
}

void ListenIn::SendState()
{
  const int slot = wire::clamp_slot(GetParam(kSlot)->Int());
  SendText(kMsgState, wire::encode_state(slot, mStatus, mPeak.load(std::memory_order_relaxed)));
}

/* The name is not in the state string: it changes rarely, and the state is
 * parsed sixty times a second. */
void ListenIn::OnEditorReady()
{
  /* A load not yet applied is what the editor should open on. */
  ServiceLabel();
  SendState();
  SendText(kMsgLabel, mLabel);
}

bool ListenIn::OnEditorMessage(int tag, const std::string& arg)
{
  if (tag != kMsgLabel)
    return false;
  char clean[32];
  wire::parse_label(arg.c_str(), clean, int(sizeof clean));
  mSession.Edit(clean);
  ServiceLabel();
  return true;
}
