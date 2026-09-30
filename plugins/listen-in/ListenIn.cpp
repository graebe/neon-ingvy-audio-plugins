/*
 * NI Listen-In -- a tap that other plugins can read.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "ListenIn.h"

#include "IPlug_include_in_plug_src.h"
#include "Wire.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace listenin;

/* The handoff frees through this; abus_pusher_release has the wrong pointer
 * type to be called through a void* function pointer. */
static void ReleasePusher(void* p)
{
  abus_pusher_release(static_cast<abus_pusher_t*>(p));
}

ListenIn::ListenIn(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  /* State.cpp, where a test can reach it. */
  state::Declare([this](int i) { return GetParam(i); });

  mStage.resize(size_t(kStageFrames) * abus_channels(), 0.f);
  mLabel.reserve(32);
  mBus = shell_handoff_new(ReleasePusher);

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* The ground's detector. Created here rather than in OnReset because OnReset
   * may run on a real-time thread in some hosts and this allocates; the rate it
   * is given now is corrected there. */
  mGround = gnd_new(GetSampleRate());

  mEditorInitFunc = [&]() {
    /* WITHOUT LoadIndexHtml THE WEBVIEW IS SILENTLY BLANK -- no error, no log,
     * a plain white rectangle. The Spectrogram's note, and it cost an evening
     * there. */
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
  };
#endif

  MakeDefaultPreset("Default", kNumPresets);
}

ListenIn::~ListenIn()
{
  /* The audio thread has stopped by now, so the live pusher goes too, and with
   * the writer the slot. */
  shell_handoff_free(mBus);
  mBus = nullptr;
  abus_writer_release(mWriter);
  mWriter = nullptr;
#ifdef WEBVIEW_EDITOR_DELEGATE
  gnd_free(mGround);
  mGround = nullptr;
#endif
}

/*
 * THE BUS FOLLOWS WHAT THE OTHER THREADS ASKED FOR, HERE AND NOWHERE ELSE.
 *
 * Main thread only: claiming maps shared memory and allocates. The rules are
 * the ones the bus always had -- nothing is claimed before the host's first
 * reset; a reset retunes a live bus and retries a refused one; a new slot or a
 * loaded state claims afresh -- only now they are applied where they are safe.
 */
void ListenIn::ServiceBus()
{
#if IPLUG_DSP
  /* A pusher replaced earlier is freed once the audio thread has let go. */
  shell_handoff_collect(mBus);

  const uint32_t rate = mRate.load(std::memory_order_acquire);
  if (rate == 0) return;

  const bool reset = mResetSeen.exchange(false, std::memory_order_acq_rel);
  const bool reload = mReclaim.exchange(false, std::memory_order_acq_rel);
  const int want = wire::clamp_slot(mWantSlot.load(std::memory_order_relaxed));

  /* A rate change makes the samples either side of it a different signal, so
   * the bus restarts rather than splicing. Posted: the pusher applies it. */
  if (reset && mWriter) abus_writer_set_sample_rate(mWriter, rate);

  if (!(mWaiting || reload || want != mTriedSlot || (reset && !mWriter))) return;

  /*
   * RELEASE FIRST, AND ONLY THEN CLAIM. Moving from 3 to 4 and back would
   * otherwise find slot 3 still held by this very instance and report it
   * taken. The writer goes at once; the slot goes with the pusher, once the
   * audio thread lets go of it -- within a block -- and until then the claim
   * waits for the next tick.
   */
  abus_writer_release(mWriter);
  mWriter = nullptr;
  shell_handoff_set(mBus, nullptr);
  mWaiting = shell_handoff_collect(mBus) > 0;
  if (mWaiting) return;

  mTriedSlot = want;
  abus_writer_t* w = nullptr;
  abus_pusher_t* p = nullptr;
  switch (abus_writer_claim(uint32_t(want), rate, &w, &p))
  {
    case ABUS_OK:
      mStatus = wire::kLive;
      mWriter = w;
      if (!mLabel.empty()) abus_writer_set_label(mWriter, mLabel.c_str());
      shell_handoff_set(mBus, p);
      break;
    case ABUS_ERR_TAKEN:
      /* NOT AN ERROR TO SWALLOW. Another Listen-In already publishes here, and
       * the editor has to say so -- a tap that silently does nothing is worse
       * than one that refuses out loud. */
      mStatus = wire::kTaken;
      break;
    default:
      mStatus = wire::kUnavailable;
      break;
  }
  SendState();
#endif
}

#if IPLUG_DSP

void ListenIn::OnReset()
{
  /* Recorded, not acted on: this may not be the main thread. OnIdle retunes or
   * claims the bus to match. */
  mRate.store(uint32_t(std::lround(GetSampleRate() > 0.0 ? GetSampleRate() : 48000.0)),
              std::memory_order_release);
  mResetSeen.store(true, std::memory_order_release);

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* A rate change re-derives every coefficient; the reset is what stops a hump
   * left over from before the transport stopped firing an onset the moment it
   * starts again. Neither touches the onset COUNT -- the editor compares that
   * against its own last value, so rewinding it would draw a phantom ring. */
  gnd_set_sample_rate(mGround, GetSampleRate());
  gnd_reset(mGround);
#endif
}

/* The audio thread, under VST3 and CLAP automation: records the slot and
 * nothing else. OnIdle moves the bus. */
void ListenIn::OnParamChange(int paramIdx)
{
  if (paramIdx == kSlot)
    mWantSlot.store(wire::clamp_slot(GetParam(kSlot)->Int()), std::memory_order_relaxed);
}

void ListenIn::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nIn = NInChansConnected();
  const int nOut = NOutChansConnected();

  if (nIn < 1 || nOut < 1)
    return;

  /*
   * THE TAP READS THE INPUT FIRST, and the passthrough copy happens after.
   *
   * Not because the order matters for the arithmetic -- nothing here modifies
   * a sample -- but because a host may hand us the same buffer for in and out,
   * and "publish the input" has to mean the input under every host. The
   * Spectrogram's note, and it is the same hazard.
   */
  const bool stereoIn = nIn > 1 && inputs[1] != nullptr;
  const int cap = kStageFrames;
  float peak = 0.f;
  /* Held for the block; null when no slot is claimed, which push ignores. */
  auto* bus = static_cast<abus_pusher_t*>(shell_handoff_acquire(mBus));

  for (int off = 0; off < nFrames; off += cap)
  {
    const int n = std::min(cap, nFrames - off);
    for (int i = 0; i < n; i++)
    {
      const float l = float(inputs[0][off + i]);
      /* A MONO SOURCE IS DUPLICATED, not left silent on the right. The bus is
       * always stereo so that a receiver never has to ask what arrived. */
      const float r = float(stereoIn ? inputs[1][off + i] : inputs[0][off + i]);
      mStage[size_t(i) * 2] = l;
      mStage[size_t(i) * 2 + 1] = r;
      peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    /* A no-op when the slot was taken, which is why there is no branch here. */
    abus_pusher_push(bus, mStage.data(), uint32_t(n));
  }
  shell_handoff_release(mBus);

  /*
   * PEAK DECAYS RATHER THAN RESETTING. The editor reads this at 60 Hz and the
   * audio thread writes it far more often; a plain store would mean the meter
   * shows whichever block happened to land under the read, and a quiet block
   * inside a loud passage would make it flicker to nothing.
   */
  const float prev = mPeak.load(std::memory_order_relaxed);
  mPeak.store(std::max(peak, prev * 0.85f), std::memory_order_relaxed);

#ifdef WEBVIEW_EDITOR_DELEGATE
  /*
   * THE GROUND'S DETECTOR SEES THE INPUT, and it has to be read before the
   * passthrough copy below for the reason the tap is: a host may hand us the
   * same buffer for in and out, so "the input" is only the input until that
   * memcpy runs. Here it makes no arithmetic difference -- nothing modifies a
   * sample -- but the ordering is the habit that keeps it true when something
   * does.
   *
   * No conversion and no scratch buffer: gnd_push takes doubles, which is what
   * `sample` already is. A mono source is passed twice, as the bus does.
   */
  gnd_push(mGround, inputs[0], stereoIn ? inputs[1] : inputs[0], nFrames);
#endif

  /* Bit for bit: a wire with a tap on it. */
  for (int c = 0; c < nOut; c++)
  {
    const int src = std::min(c, nIn - 1);
    if (outputs[c] != inputs[src])
      std::memcpy(outputs[c], inputs[src], sizeof(sample) * size_t(nFrames));
  }
}

#endif /* IPLUG_DSP */

/* The chunk is State.cpp's: parameters, then the label. */
bool ListenIn::SerializeState(IByteChunk& chunk) const
{
  return state::Save(chunk, [this](IByteChunk& c) { return SerializeParams(c); }, mLabel);
}

int ListenIn::UnserializeState(const IByteChunk& chunk, int startPos)
{
  const int pos = state::Load(
    chunk, startPos,
    [this](const IByteChunk& c, int p) { return shell::state::CheckParams(c, p, *this); },
    [this](const IByteChunk& c, int p) { return UnserializeParams(c, p); },
    mLabel);
  /* The slot and the label just changed underneath the bus; OnIdle claims
   * afresh. */
  if (pos >= 0)
    mReclaim.store(true, std::memory_order_release);
  return pos;
}

#ifdef WEBVIEW_EDITOR_DELEGATE

void ListenIn::SendState()
{
  const int slot = wire::clamp_slot(GetParam(kSlot)->Int());
  const std::string s =
    wire::encode_state(slot, mStatus, mPeak.load(std::memory_order_relaxed));
  SendArbitraryMsgFromDelegate(kMsgState, int(s.size()), s.c_str());
}

void ListenIn::OnUIOpen()
{
  iplug::Plugin::OnUIOpen();
  /* The ground's detector runs only while an editor is open to show it. */
  gnd_set_active(mGround, 1);
  SendState();
}

/*
 * The ground's detector only drives the editor, so it stops with it. Here and
 * not in OnUIClose, which WebViewEditorDelegate::CloseWindow never calls.
 */
void ListenIn::CloseWindow()
{
  gnd_set_active(mGround, 0);
  iplug::Plugin::CloseWindow();
}

/*
 * One message per kick, and only when there has been one.
 *
 * The count is compared with != rather than > so that its eventual wrap is a
 * non-event; see gnd_fires in the header. The strength is read after the count,
 * which is the order that cannot report a kick the editor has already seen.
 */
void ListenIn::SendGround()
{
  const uint32_t fires = gnd_fires(mGround);
  if (fires == mGroundFires)
    return;
  mGroundFires = fires;

  char buf[16];
  const int n = snprintf(buf, sizeof(buf), "%.3f", gnd_strength(mGround));
  if (n > 0)
    SendArbitraryMsgFromDelegate(kMsgGround, n, buf);
}


bool ListenIn::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  (void) ctrlTag;

  if (msgTag == kMsgReady)
  {
    /*
     * The editor asks rather than being told, because OnUIOpen can run before
     * the editor's module has finished evaluating -- the Spectrogram's test
     * harness reproduces that race on purpose. A state sent into a page with
     * no listener yet is a state nobody receives.
     */
    SendState();
    /* The name is not in the state string: it changes rarely and would cost a
     * parse sixty times a second for nothing. */
    SendArbitraryMsgFromDelegate(kMsgLabel, int(mLabel.size()), mLabel.c_str());
    return true;
  }

  if (msgTag == kMsgLabel)
  {
    /* pData IS NOT NUL-TERMINATED -- it is dataSize bytes out of a base64
     * decode, and reading it as a C string runs off the end of the buffer into
     * whatever the WebView bridge left there. */
    const std::string arg(static_cast<const char*>(pData),
                          size_t(dataSize > 0 ? dataSize : 0));
    char clean[32];
    wire::parse_label(arg.c_str(), clean, int(sizeof(clean)));
    mLabel = clean;
    if (mWriter) abus_writer_set_label(mWriter, mLabel.c_str());
    return true;
  }

  return false;
}

#else

void ListenIn::SendState() {}

#endif /* WEBVIEW_EDITOR_DELEGATE */

void ListenIn::OnIdle()
{
#ifdef WEBVIEW_EDITOR_DELEGATE
  /* One ring per kick the detector found since the last tick. FIRST, so no
   * early return -- ServiceBus waiting on the audio thread, say -- can starve
   * the ground; see ground_detect.h. */
  SendGround();
#endif
  ServiceBus();
#ifdef WEBVIEW_EDITOR_DELEGATE
  SendState();
#endif
}
