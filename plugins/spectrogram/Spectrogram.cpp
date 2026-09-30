/*
 * Spectrogram -- a rolling analyzer for Ableton Live, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "Spectrogram.h"
#include "State.h"
#include "Wire.h"
#include "IPlug_include_in_plug_src.h"

#include <algorithm>
#include <cstring>

using namespace iplug;

/* The handoff frees through this; srecv_free's pointer type is not void*. */
static void FreeReceiver(void* r)
{
  srecv_free(static_cast<srecv_t*>(r));
}

Spectrogram::Spectrogram(const InstanceInfo& info)
: ni::WebPlugin(info, MakeConfig(kNumParams, kNumPresets), {"nispectrogram", __FILE__})
{
  /* Configured for real once the host has named a rate: see ResetAudio. */
  mRecv = srecv_new(48000.f, 8192, 1024, SPECTRO_BANDS,
                    SPECTRO_F_MIN, SPECTRO_F_MAX, SPECTRO_DB_FLOOR, SPECTRO_DB_CEIL);
  srecv_start(mRecv);
  mRecvLend = shell_handoff_new(FreeReceiver);
  shell_handoff_set(mRecvLend, mRecv);

  MakeDefaultPreset("Default", kNumPresets);

  /* Sized once for the largest tick; the band count never changes. */
  const size_t span = size_t(SPECTRO_BANDS) * kMaxColsPerTick;
  mSum.assign(span, 0);
  mClash.assign(span, 0);
  mPayload.reserve(span + 32);
  /* The picture opens on this track alone. */
  mView.assign(1, 0);
}

Spectrogram::~Spectrogram()
{
  /* The audio thread has stopped; this frees mRecv with the rest. */
  shell_handoff_free(mRecvLend);
  mRecvLend = nullptr;
  mRecv = nullptr;
}

/* Main thread only: it opens and closes readers, which allocates and mmaps. */
void Spectrogram::ApplySources()
{
  if (mRecv)
    srecv_set_sources(mRecv, mSources.empty() ? nullptr : mSources.data(),
                      int(mSources.size()));
}

/*
 * THE RECEIVER IS REBUILT WHERE IT IS DRAINED. The window is the engine's pick
 * for the rate, and the selection and clash settings survive the rebuild -- the
 * buses are reopened against the new rate, which is also where one that does
 * not match it starts being refused.
 */
void Spectrogram::ServiceReceiver()
{
  shell_handoff_collect(mRecvLend);
  if (!mRecvStale.load(std::memory_order_acquire))
    return;

  const float sr = mRecvRate.load(std::memory_order_relaxed);
  const int fftSize = spectro_pick_fft_size(sr);
  const int hop = spectro_pick_hop(sr, fftSize);
  srecv_t* fresh = srecv_new(sr, fftSize, hop, SPECTRO_BANDS,
                             SPECTRO_F_MIN, SPECTRO_F_MAX, SPECTRO_DB_FLOOR, SPECTRO_DB_CEIL);
  if (!fresh)
    return;
  srecv_set_clash(fresh, mClashFloorDb, mClashBalanceDb);
  srecv_set_range(fresh, mRangeLo, mRangeHi);

  /* The old one is freed -- its worker joined -- once the audio thread has let
   * go of it. */
  shell_handoff_set(mRecvLend, fresh);
  mRecv = fresh;
  ApplySources();
  /* Started once its sources are open, so choosing them waits for no thread. */
  srecv_start(fresh);
  /* Cleared last: the audio thread feeds the new receiver only now -- unless
   * ResetAudio asked again meanwhile, and then the next tick rebuilds. */
  if (mRecvRate.load(std::memory_order_relaxed) == sr)
    mRecvStale.store(false, std::memory_order_release);
  shell_handoff_collect(mRecvLend);

  /* The axis just changed under an editor that may be open. */
  SendAxis();
}

/*
 * The receiver's lifecycle runs whether or not a window is open. So does its
 * drain: with no editor the columns are dropped, so the rings do not fill with
 * a picture nobody will see. srecv_pump does the transforms here only if the
 * worker could not be started.
 */
void Spectrogram::OnHostIdle()
{
  ServiceReceiver();
  if (!mRecv)
    return;
  srecv_pump(mRecv);
  if (!EditorIsOpen())
    srecv_frame(mRecv, nullptr, 0, -1, -1, nullptr, nullptr, kMaxColsPerTick, nullptr);
}

void Spectrogram::OnEditorIdle()
{
  SendPicture();
}

void Spectrogram::OnEditorReady()
{
  /* First: the editor holds its pushes until it has this. */
  SendState();
  SendAxis();
  /* The picker's contents now, not when the slow timer comes round. */
  SendSources();
}

/*
 * THE HOST'S SAMPLE RATE REACHES THE FREQUENCY AXIS THROUGH HERE: a band's bin
 * range depends on it. The receiver is REPLACED rather than reconfigured --
 * every buffer's size depends on the configuration -- and that allocates, so it
 * happens in OnIdle; this may not be the main thread.
 */
void Spectrogram::ResetAudio()
{
  const float sr = float(GetSampleRate());
  /* The hop ties the picture to a column RATE, so it holds the same seconds
   * at any sample rate; ppqPerCol is hop over rate, which the editor cannot
   * know. */
  mHop = spectro_pick_hop(sr, spectro_pick_fft_size(sr));
  mRecvRate.store(sr, std::memory_order_relaxed);
  mRecvStale.store(true, std::memory_order_release);
  mMono.assign(size_t(std::max(GetBlockSize(), 1)), 0.0f);
}

void Spectrogram::ProcessAudio(sample** inputs, sample** outputs, int nFrames)
{
  const int nIn = NInChansConnected();
  const int nOut = NOutChansConnected();
  if (nIn < 1 || nOut < 1)
    return;

  /* The input is read before the passthrough: a host may hand over one
   * buffer for both. */
  const bool stereoIn = nIn > 1 && inputs[1] != nullptr;
  /* Held for the block; nothing is fed while a rebuild for a new rate is
   * pending, since the receiver in hand was built for the old one. */
  auto* recv = static_cast<srecv_t*>(shell_handoff_acquire(mRecvLend));
  if (mRecvStale.load(std::memory_order_acquire))
    recv = nullptr;

  ni::wire::for_each_chunk(nFrames, int(mMono.size()), [&](int off, int n) {
    for (int i = 0; i < n; i++)
    {
      const float l = float(inputs[0][off + i]);
      const float r = float(stereoIn ? inputs[1][off + i] : inputs[0][off + i]);
      /* Halved: a centred mix summed without it reads 6 dB hot. */
      mMono[size_t(i)] = 0.5f * (l + r);
    }
    /* A copy into a ring and nothing else: the transforms run on the
     * receiver's worker, with every other source, in step. */
    if (recv)
      srecv_push_own(recv, mMono.data(), n);
  });
  shell_handoff_release(mRecvLend);

  /*
   * THE HOST'S CLOCK, and the two ways a host lies about it: a tempo of 0 from
   * a host that has not said, and a RUNNING transport at PPQ -1 (CLAP without
   * CLAP_TRANSPORT_HAS_BEATS_TIMELINE). Running, the host's position is the
   * authority -- alignment is what the bar view is for; stopped, the position
   * advances at the last tempo so the picture keeps filling.
   */
  const double hostBpm = GetTempo();
  if (hostBpm > 1.0 && hostBpm < 1000.0)
    mLastBpm = hostBpm;
  const double ppq = GetPPQPos();
  const bool running = GetTransportIsRunning() && ppq >= 0.0;
  const double sr = GetSampleRate();
  mPos = running ? ppq : ni::wire::advance_beats(mPos, nFrames, mLastBpm, sr);

  mPubPpq.store(mPos, std::memory_order_relaxed);
  mPubBpm.store(mLastBpm, std::memory_order_relaxed);
  mPubRunning.store(running, std::memory_order_relaxed);
  mPubPpqPerCol.store((mHop > 0 && sr > 0.0) ? (double(mHop) / sr) * (mLastBpm / 60.0) : 0.0,
                      std::memory_order_relaxed);
  /* Bars come from the position: VST3 copies mLastBar without its validity
   * flag, and AU leaves it unset without a downbeat. */
  int num = 4, denom = 4;
  GetTimeSig(num, denom);
  mPubSig.store(((num < 1 ? 4 : num) << 8) | (denom < 1 ? 4 : denom), std::memory_order_relaxed);

  ni::wire::passthrough(inputs, nIn, outputs, nOut, nFrames);
}

/*
 * The clock first and every tick -- the bar view's playhead crosses bar lines
 * on ticks with no column -- then one tick's columns: every channel drained in
 * step, the view summed, and the clash between the two channels the editor
 * named, not between whatever is on screen.
 */
void Spectrogram::SendPicture()
{
  if (!mRecv)
    return;
  SendSync();
  if (--mSourceTick <= 0)
  {
    mSourceTick = 25; /* twice a second at the idle timer's 20 ms */
    SendSources();
  }

  const int bands = srecv_bands(mRecv);
  if (bands <= 0 || bands > SPECTRO_BANDS)
    return;
  int clashCols = 0;
  const int cols = srecv_frame(mRecv, mView.data(), int(mView.size()),
                               mClashOn ? mCmpA : -1, mClashOn ? mCmpB : -1,
                               mSum.data(), mClash.data(), kMaxColsPerTick, &clashCols);
  if (cols > 0)
  {
    mPayload = spectro::wire::encode_columns(mSum.data(), cols, bands, 0);
    SendText(kMsgCols, mPayload);
  }
  if (clashCols > 0)
  {
    mPayload = spectro::wire::encode_columns(mClash.data(), clashCols, bands, 0);
    SendText(kMsgClashCols, mPayload);
  }
}

/* Every bus that exists, sending or not: a muted Listen-In is still where the
 * user put it. */
void Spectrogram::SendSources()
{
  char buf[ni::editor::kMaxJSString / 2];
  if (srecv_slots(reinterpret_cast<unsigned char*>(buf), int(sizeof buf)) < 0)
    return;
  SendFramed(kMsgSources, buf, int(strnlen(buf, sizeof buf)));
}

void Spectrogram::SendState()
{
  SendText(kMsgState, spectro::wire::encode_state(mRangeLo, mRangeHi, mView, mCmpA, mCmpB,
                                                  mClashOn, mClashFloorDb, mClashBalanceDb));
}

void Spectrogram::SendAxis()
{
  if (!mRecv)
    return;
  const int bands = srecv_bands(mRecv);
  if (bands <= 0)
    return;
  /* One axis for every source: they share a configuration. */
  std::vector<float> hz(size_t(bands), 0.0f);
  const int n = srecv_band_hz(mRecv, hz.data(), bands);
  SendText(kMsgAxis, spectro::wire::encode_axis(hz.data(), n));
}

void Spectrogram::SendSync()
{
  const int sig = mPubSig.load(std::memory_order_relaxed);
  SendText(kMsgSync, spectro::wire::encode_sync(
    mPubPpq.load(std::memory_order_relaxed), mPubBpm.load(std::memory_order_relaxed),
    (sig >> 8) & 0xFF, sig & 0xFF, mPubRunning.load(std::memory_order_relaxed),
    mPubPpqPerCol.load(std::memory_order_relaxed), int(GetSampleRate())));
}

/* The chunk is State.cpp's: what the session was looking at, as strings. */
bool Spectrogram::SerializeState(IByteChunk& chunk) const
{
  spectro::state::Fields f;
  f.sources = mSources;
  f.clashFloorDb = mClashFloorDb;
  f.clashBalanceDb = mClashBalanceDb;
  f.view = mView;
  f.cmpA = mCmpA;
  f.cmpB = mCmpB;
  f.clashOn = mClashOn;
  f.rangeLo = mRangeLo;
  f.rangeHi = mRangeHi;
  return spectro::state::Save(chunk, [this](IByteChunk& c) { return PutParams(c); }, f);
}

int Spectrogram::UnserializeState(const IByteChunk& chunk, int startPos)
{
  /* What the chunk does not carry keeps its current value. */
  spectro::state::Fields f;
  f.sources = mSources;
  f.clashFloorDb = mClashFloorDb;
  f.clashBalanceDb = mClashBalanceDb;
  f.view = mView;
  f.cmpA = mCmpA;
  f.cmpB = mCmpB;
  f.clashOn = mClashOn;
  f.rangeLo = mRangeLo;
  f.rangeHi = mRangeHi;
  const int pos = spectro::state::Load(
    chunk, startPos,
    [this](const IByteChunk& c, int p) { return CheckParams(c, p); },
    [this](const IByteChunk& c, int p) { return GetParams(c, p); }, f);
  if (pos < 0)
    return -1;
  mSources = f.sources;
  mClashFloorDb = f.clashFloorDb;
  mClashBalanceDb = f.clashBalanceDb;
  mView = f.view;
  mCmpA = f.cmpA;
  mCmpB = f.cmpB;
  mClashOn = f.clashOn;
  mRangeLo = f.rangeLo;
  mRangeHi = f.rangeHi;

  /* The selection changed underneath the receiver, so it follows. */
  if (mRecv)
  {
    srecv_set_clash(mRecv, mClashFloorDb, mClashBalanceDb);
    srecv_set_range(mRecv, mRangeLo, mRangeHi);
    ApplySources();
  }
  return pos;
}

bool Spectrogram::OnEditorMessage(int tag, const std::string& arg)
{
  switch (tag)
  {
    /* The zoom, while audio runs: the engine stores a request and adopts it at
     * its next frame, and refuses anything undrawable. The axis goes back as
     * the engine has it -- f_max is clamped to Nyquist. */
    case kMsgRange:
    {
      float lo = 0.f, hi = 0.f;
      if (spectro::wire::parse_range(arg, lo, hi))
      {
        mRangeLo = lo;
        mRangeHi = hi;
        srecv_set_range(mRecv, lo, hi);
        SendAxis();
      }
      return true;
    }

    case kMsgSelect:
      mSources.clear();
      spectro::wire::parse_slots(arg, mSources);
      ApplySources();
      return true;

    /* An empty view is refused: a spectrogram showing nothing is a broken
     * plugin, not a view. */
    case kMsgView:
      mView.clear();
      spectro::wire::parse_channels(arg, mView);
      if (mView.empty())
        mView.assign(1, 0);
      return true;

    /* Independent of the view: comparing two things not on screen is a fair
     * request. */
    case kMsgCompare:
    {
      int a = 0, b = 0;
      bool on = false;
      if (spectro::wire::parse_compare(arg, a, b, on))
      {
        mCmpA = a;
        mCmpB = b;
        mClashOn = on;
      }
      return true;
    }

    case kMsgClash:
    {
      float floorDb = 0.f, balanceDb = 0.f;
      if (spectro::wire::parse_range(arg, floorDb, balanceDb))
      {
        mClashFloorDb = floorDb;
        mClashBalanceDb = balanceDb;
        srecv_set_clash(mRecv, floorDb, balanceDb);
      }
      return true;
    }

    default:
      return false;
  }
}
