/*
 * Listen-In -- a tap that other plugins can read.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "ListenIn.h"

#include "IPlug_include_in_plug_src.h"
#include "Wire.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace listenin;

ListenIn::ListenIn(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kSlot)->InitInt("Bus", 1, 1, int(abus_max_slot()), "");

  mStage.resize(size_t(kStageFrames) * abus_channels(), 0.f);
  mLabel.reserve(32);

#ifdef WEBVIEW_EDITOR_DELEGATE
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
  abus_writer_release(mBus);
  mBus = nullptr;
}

void ListenIn::Reclaim()
{
  /*
   * MAIN THREAD ONLY. This maps shared memory and allocates; ProcessBlock must
   * never reach it. Releasing first means a slot change hands the old bus back
   * before asking for the new one -- otherwise moving from 3 to 4 and back
   * would find slot 3 still held by this very instance and report it taken.
   */
  abus_writer_release(mBus);
  mBus = nullptr;
  mClaimedSlot = 0;

  const int slot = wire::clamp_slot(GetParam(kSlot)->Int());
  const uint32_t sr = uint32_t(std::lround(GetSampleRate() > 0.0 ? GetSampleRate() : 48000.0));

  const int rc = abus_writer_claim(uint32_t(slot), sr, &mBus);
  switch (rc)
  {
    case ABUS_OK:
      mStatus = wire::kLive;
      mClaimedSlot = slot;
      if (!mLabel.empty()) abus_writer_set_label(mBus, mLabel.c_str());
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
}

#if IPLUG_DSP

void ListenIn::OnReset()
{
  /* A rate change makes the samples either side of it a different signal, so
   * the bus restarts rather than splicing. A fresh claim does that implicitly;
   * an existing one is told. */
  if (mBus != nullptr)
  {
    const uint32_t sr = uint32_t(std::lround(GetSampleRate()));
    abus_writer_set_sample_rate(mBus, sr);
  }
  else
  {
    Reclaim();
  }
}

void ListenIn::OnParamChange(int paramIdx)
{
  if (paramIdx == kSlot && wire::clamp_slot(GetParam(kSlot)->Int()) != mClaimedSlot)
    Reclaim();
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
    abus_writer_push(mBus, mStage.data(), uint32_t(n));
  }

  /*
   * PEAK DECAYS RATHER THAN RESETTING. The editor reads this at 60 Hz and the
   * audio thread writes it far more often; a plain store would mean the meter
   * shows whichever block happened to land under the read, and a quiet block
   * inside a loud passage would make it flicker to nothing.
   */
  const float prev = mPeak.load(std::memory_order_relaxed);
  mPeak.store(std::max(peak, prev * 0.85f), std::memory_order_relaxed);

  /* Bit for bit: a wire with a tap on it. */
  for (int c = 0; c < nOut; c++)
  {
    const int src = std::min(c, nIn - 1);
    if (outputs[c] != inputs[src])
      std::memcpy(outputs[c], inputs[src], sizeof(sample) * size_t(nFrames));
  }
}

#endif /* IPLUG_DSP */

/*
 * THE STATE CHUNK: parameters, then the label.
 *
 * The slot is a parameter and SerializeParams handles it. The name is text and
 * cannot be a parameter, so it is appended -- and read back defensively,
 * because a chunk written by a future version may hold more than this one
 * knows how to want.
 */
bool ListenIn::SerializeState(IByteChunk& chunk) const
{
  if (!SerializeParams(chunk))
    return false;
  return chunk.PutStr(mLabel.c_str()) > 0;
}

int ListenIn::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int pos = UnserializeParams(chunk, startPos);

  WDL_String label;
  const int after = chunk.GetStr(label, pos);
  if (after > pos)
  {
    char clean[32];
    wire::parse_label(label.Get(), clean, int(sizeof(clean)));
    mLabel = clean;
    pos = after;
  }

#if IPLUG_DSP
  /* The slot just changed underneath us, so the claim has to follow it. */
  Reclaim();
#endif
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
  SendState();
}

void ListenIn::OnIdle()
{
  SendState();
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
    if (mBus != nullptr)
      abus_writer_set_label(mBus, mLabel.c_str());
    return true;
  }

  return false;
}

#else

void ListenIn::SendState() {}

#endif /* WEBVIEW_EDITOR_DELEGATE */
