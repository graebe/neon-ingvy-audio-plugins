/*
 * Spectrogram -- a rolling analyzer for Ableton Live, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "Spectrogram.h"
#include "Wire.h"
#include "IPlug_include_in_plug_src.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

Spectrogram::Spectrogram(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  /* Configured for real in OnReset, once the host has named a rate. */
  mRecv = srecv_new(48000.f, 8192, 1024, SPECTRO_BANDS,
                    SPECTRO_F_MIN, SPECTRO_F_MAX, SPECTRO_DB_FLOOR, SPECTRO_DB_CEIL);

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* Before anything can be sent: see kMaxJSString. */
  SetMaxJSStringLength(kMaxJSString);

  /*
   * THE WEBVIEW LOADS NOTHING UNLESS IT IS TOLD TO, and the failure is silent:
   * a WKWebView with no page is a plain WHITE rectangle -- not an error, not a
   * blank editor in the plugin's own colours, just white. Which looks far more
   * like a broken build than like a missing call.
   *
   * mEditorInitFunc runs when the editor is created; LoadIndexHtml finds
   * Resources/web/index.html inside the bundle (that is what GetBundleID is
   * for, and __FILE__ is the development path it falls back to).
   *
   * EnableScroll(false) because a plugin window is not a document: a rubber-band
   * bounce on a drag inside the picture reads as a bug.
   */
  /* The ground's detector. Created here rather than in OnReset because OnReset
   * may run on a real-time thread in some hosts and this allocates; the rate it
   * is given now is corrected there. */
  mGround = gnd_new(GetSampleRate());

  mEditorInitFunc = [&]() {
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
  };
#endif

  MakeDefaultPreset("Default", kNumPresets);

  /* The drain buffer, sized once for the worst tick this code will ever send.
   * The analyzer's band count cannot change without a configure, and configure
   * only ever restates it. */
  const int bands = SPECTRO_BANDS;
  const size_t span = size_t(bands) * kMaxColsPerTick;
  mChanCols.assign(size_t(srecv_max_sources()), std::vector<unsigned char>(span, 0));
  mChanCount.assign(size_t(srecv_max_sources()), 0);
  mSum.assign(span, 0);
  mClash.assign(span, 0);
  /* The picture opens on this track alone. */
  mView.assign(1, 0);
  /* Two hex characters a byte, plus "<cols>:<bands>:". */
  mHex.reserve(size_t(bands) * kMaxColsPerTick * 2 + 32);
}

Spectrogram::~Spectrogram()
{
  srecv_free(mRecv);
  mRecv = nullptr;
#ifdef WEBVIEW_EDITOR_DELEGATE
  gnd_free(mGround);
  mGround = nullptr;
#endif
}

#if IPLUG_DSP

void Spectrogram::OnReset()
{
  /*
   * THE HOST'S SAMPLE RATE REACHES THE FREQUENCY AXIS THROUGH HERE, and it is
   * the reason configure is a separate call rather than an argument to
   * spectro_new: a band's bin range depends on the rate, so a session opened at
   * 44.1 kHz and a session opened at 96 kHz are different mappings. Get this
   * wrong and the picture is still a picture -- the numbers beside it are just
   * false, which is the worst kind of wrong for an analyzer.
   */
  const float sr = float(GetSampleRate());

  /*
   * THE WINDOW IS PICKED BY THE ENGINE, NOT WRITTEN DOWN HERE. An FFT's bins are
   * sample_rate / fft_size apart, so "the picture starts at 10 Hz" fixes the
   * window length once the rate is known -- 8192 points at 48 kHz, 16384 at
   * 96 kHz. A constant here would be right at one rate and silently wrong at
   * the others, which is exactly how this plugin shipped drawing from 47 Hz
   * while asking for 20.
   *
   * The hop comes from the same place and is tied to a COLUMN RATE rather than
   * to the window, so the picture holds the same thirteen seconds whatever the
   * session runs at.
   */
  const int fftSize = spectro_pick_fft_size(sr);
  const int hop = spectro_pick_hop(sr, fftSize);
  /* The editor spreads a catch-up batch across the positions it covers, and a
   * column's length in beats is this over the sample rate. Neither number is
   * anything the editor could derive on its own. */
  mHop = hop;

  /*
   * A RECEIVER IS REPLACED RATHER THAN RECONFIGURED, for the reason
   * spectro_configure gives: every buffer's size depends on the configuration,
   * so "reconfigure" and "reallocate" are the same act. The host guarantees
   * audio is stopped here, which is the only place that is safe.
   *
   * The selection survives it -- the sources are reopened against the new rate
   * below, which is also where a bus that does NOT match it starts being
   * refused rather than drawn.
   */
  srecv_free(mRecv);
  mRecv = srecv_new(sr, fftSize, hop, SPECTRO_BANDS,
                    SPECTRO_F_MIN, SPECTRO_F_MAX, SPECTRO_DB_FLOOR, SPECTRO_DB_CEIL);
  srecv_set_clash(mRecv, mClashFloorDb, mClashBalanceDb);
  ApplySources();

  /* Sized here, on the main thread, and never on the audio thread. iPlug2's
   * `sample` is double and the analyzer's path is float, so the block is
   * converted rather than the analyzer widened. */
  mMono.assign(size_t(std::max(GetBlockSize(), 1)), 0.0f);

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* The axis just changed. An editor that is already open would otherwise keep
   * the old scale until it is reopened. */
  SendAxis();
#endif

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* A rate change re-derives every coefficient; the reset stops a hump left over
   * from before the transport stopped firing an onset the moment it starts
   * again. Neither touches the onset COUNT -- see gnd_fires. */
  gnd_set_sample_rate(mGround, GetSampleRate());
  gnd_reset(mGround);
#endif
}

void Spectrogram::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nIn = NInChansConnected();
  const int nOut = NOutChansConnected();

#ifdef WEBVIEW_EDITOR_DELEGATE
  /*
   * THE GROUND'S DETECTOR SEES THE INPUT, and it is fed here -- at the top,
   * before anything writes to `outputs` -- because a host may hand over the same
   * buffer for in and out. No scratch buffer and no conversion: gnd_push takes
   * doubles, which is what `sample` already is.
   */
  {
    const int gndIn = NInChansConnected();
    const double* gndL = gndIn > 0 ? inputs[0] : nullptr;
    const double* gndR = (gndIn > 1 && inputs[1] != nullptr) ? inputs[1] : gndL;
    gnd_push(mGround, gndL, gndR, nFrames);
  }
#endif

  if (nIn < 1 || nOut < 1)
    return;

  /*
   * THE ANALYSIS READS THE INPUT FIRST, and the passthrough copy happens after.
   * Not because the order matters for the arithmetic -- nothing here modifies a
   * sample -- but because a host may hand us the same buffer for in and out, and
   * "read the input" has to mean the input under every host.
   */
  const bool stereoIn = nIn > 1 && inputs[1] != nullptr;
  const int cap = int(mMono.size());

  if (cap > 0)
  {
    /* A host may hand us a longer block than the one it announced, and
     * resizing a vector here would allocate on the audio thread. So the block
     * is analysed in chunks of what was reserved instead -- the Trance Gate's
     * approach, for the same reason. */
    for (int off = 0; off < nFrames; off += cap)
    {
      const int n = std::min(cap, nFrames - off);
      for (int i = 0; i < n; i++)
      {
        const float l = float(inputs[0][off + i]);
        const float r = float(stereoIn ? inputs[1][off + i] : inputs[0][off + i]);
        /* THE MONO SUM, halved. Summing without the halve makes a centred
         * mix read 6 dB hot and clip the top of the ramp on anything
         * mastered, which looks like the analyzer is wrong about level. */
        mMono[size_t(i)] = 0.5f * (l + r);
      }
      /*
       * A COPY INTO A RING, AND NOTHING ELSE ON THIS THREAD.
       *
       * The transform used to happen here. It now happens in OnIdle, with every
       * other source, because that is the only way they can be fed the same
       * number of frames and so be compared cell by cell -- and because an
       * analyzer that stutters draws a stuttering picture where one that
       * overran this thread would make a noise.
       */
      srecv_push_own(mRecv, mMono.data(), n);
    }
  }

  /*
   * THE HOST'S CLOCK, AND THE TWO WAYS A HOST LIES ABOUT IT.
   *
   * Both guards are the Trance Gate's, learned next door and worth spelling out
   * again because the failure is silent in each case:
   *
   *   - GetTempo() can be 0 in a host that has not said. Dividing by it, or
   *     advancing at it, parks the picture forever.
   *   - GetPPQPos() is -1.0 until a host fills it in, and a host can report a
   *     RUNNING transport while leaving it there -- CLAP without
   *     CLAP_TRANSPORT_HAS_BEATS_TIMELINE does exactly that. Trusting
   *     `running` alone would then place every column at bar -1.
   *
   * mLastBar is deliberately NOT used: VST3 copies it without checking its own
   * validity flag and AU leaves it unset unless the host offers a downbeat, so
   * bars are computed from the position instead.
   */
  const double hostBpm = GetTempo();
  if (hostBpm > 1.0 && hostBpm < 1000.0)
    mLastBpm = hostBpm;

  const double ppq = GetPPQPos();
  const bool running = GetTransportIsRunning() && ppq >= 0.0;

  /*
   * Running: take the host's position, because ALIGNMENT is what this view is
   * for and the host's number is the only authority on it. Stopped: keep
   * advancing at the last tempo, so the picture goes on filling instead of
   * freezing -- which is what makes it useful while auditioning a loop with the
   * transport parked.
   */
  mPos = running ? ppq
                 : spectro::wire::advance_beats(mPos, nFrames, mLastBpm, GetSampleRate());

  const double sr = GetSampleRate();
  mPubPpq.store(mPos, std::memory_order_relaxed);
  mPubBpm.store(mLastBpm, std::memory_order_relaxed);
  mPubRunning.store(running, std::memory_order_relaxed);
  mPubPpqPerCol.store(
      (mHop > 0 && sr > 0.0) ? (double(mHop) / sr) * (mLastBpm / 60.0) : 0.0,
      std::memory_order_relaxed);
  {
    int num = 4, denom = 4;
    GetTimeSig(num, denom);
    if (num < 1) num = 4;
    if (denom < 1) denom = 4;
    mPubSig.store((num << 8) | denom, std::memory_order_relaxed);
  }

  /* Bit for bit: a wire with a window in it. A mono input feeding a stereo
   * output is duplicated rather than left silent on the right. */
  for (int c = 0; c < nOut; c++)
  {
    const int src = std::min(c, nIn - 1);
    if (outputs[c] != inputs[src])
      std::memcpy(outputs[c], inputs[src], sizeof(sample) * size_t(nFrames));
  }
}

#endif /* IPLUG_DSP */

#ifdef WEBVIEW_EDITOR_DELEGATE

/*
 * THE COLUMNS, AS HEX BYTES -- the Trance Gate's scope encoding, and it is the
 * right one for the same two reasons.
 *
 * The transport TRUNCATES at mMaxJSStringLength rather than failing, so a
 * payload has to be bounded by construction rather than by a limit somebody has
 * to remember (kMaxColsPerTick does that). And a byte per band is finer than the
 * picture can draw: 128 bands is 256 characters a column, against roughly 28 for
 * the same column at "%.3f" per value.
 */
/*
 * One message per kick, and only when there has been one. The idiom, and why the
 * count is compared with != rather than >, is in ground_detect.h.
 */
void Spectrogram::SendGround()
{
  if (!mGround) return;
  const uint32_t fires = gnd_fires(mGround);
  if (fires == mGroundFires) return;
  mGroundFires = fires;

  char b[16];
  const int gn = snprintf(b, sizeof b, "%.3f", gnd_strength(mGround));
  if (gn > 0)
    SendArbitraryMsgFromDelegate(kMsgGround, gn, b);
}

void Spectrogram::OnIdle()
{
  /* One ring per kick the detector found since the last tick. FIRST, so no
   * early return below can starve the ground -- see ground_detect.h. */
  SendGround();

  if (!mRecv)
    return;

  /*
   * THE CLOCK GOES FIRST AND IT GOES EVERY TICK.
   *
   * Not folded in with the columns below, and not skipped when there are none:
   * the editor's bar view moves a playhead across the picture whether or not a
   * column finished in the last 20 ms, and the tick where the analyzer has
   * nothing ready is exactly the tick where the playhead is crossing a bar line
   * with nothing else to announce it.
   *
   * It is also what the editor needs to place the columns that DO arrive, so it
   * has to be in hand before them rather than after.
   */
  SendSync();

  /*
   * THE SOURCE LIST, ON A SLOW TIMER. A Listen-In appears when somebody inserts
   * one, which is a human-speed event -- probing sixteen slots fifty times a
   * second to learn nothing would be a syscall storm in aid of a dropdown.
   */
  if (--mSourceTick <= 0)
  {
    mSourceTick = 25;   /* twice a second at the idle timer's 20 ms */
    SendSources();
  }

  /*
   * AND THIS IS WHERE THE TRANSFORMS HAPPEN, for every source at once.
   *
   * One pump feeds them all the same number of frames, so column k of each is
   * the same moment. That is what lets the clash below be read cell by cell
   * rather than being a coincidence between two clocks.
   */
  srecv_pump(mRecv);

  const int bands = srecv_bands(mRecv);
  if (bands <= 0)
    return;

  const int channels = srecv_channels(mRecv);

  /*
   * DRAIN EVERY CHANNEL FIRST, THEN DECIDE WHAT TO SAY ABOUT THEM.
   *
   * A drained column is gone -- `srecv_take_columns` is destructive -- and both
   * the view (a sum) and the comparison (a pair) need several channels alive at
   * the same instant. Taking them one at a time and sending as we went is what
   * forced the clash to always be "this bus against channel 0" whatever the
   * editor was actually asking for.
   */
  int common = -1;
  for (int ch = 0; ch < channels && ch < int(mChanCols.size()); ch++)
  {
    const int cols = srecv_take_columns(mRecv, ch, mChanCols[size_t(ch)].data(),
                                        kMaxColsPerTick);
    mChanCount[size_t(ch)] = cols;
    /* They are fed from one pump, so they agree -- but a channel refused for a
     * sample-rate mismatch returns 0 forever, and must not drag the rest to 0. */
    if (cols > 0)
      common = (common < 0) ? cols : std::min(common, cols);
  }
  if (common <= 0)
    return;

  /*
   * THE VIEW IS ONE STREAM, and the editor never learns it was several. A
   * channel that produced nothing this tick is simply left out of the sum
   * rather than contributing silence, which would pull the picture down.
   */
  const unsigned char* srcs[8];
  int nSrc = 0;
  for (int ch : mView)
  {
    if (ch < 0 || ch >= channels || ch >= int(mChanCols.size()))
      continue;
    if (mChanCount[size_t(ch)] < common)
      continue;
    if (nSrc < int(sizeof srcs / sizeof srcs[0]))
      srcs[nSrc++] = mChanCols[size_t(ch)].data();
  }
  if (nSrc == 0)
    return;

  srecv_sum(mRecv, srcs, nSrc, mSum.data(), common);
  mHex = spectro::wire::encode_columns(mSum.data(), common, bands, 0);
  assert(spectro::wire::framed_size(int(mHex.size())) < kMaxJSString);
  SendArbitraryMsgFromDelegate(kMsgCols, int(mHex.size()), mHex.c_str());

  /*
   * AND THE COMPARISON IS THE OTHER STREAM, between the two channels the editor
   * named -- not between whatever happens to be on screen. That separation is
   * the point of the pair existing.
   */
  if (!mClashOn || mCmpA == mCmpB)
    return;
  if (mCmpA < 0 || mCmpA >= channels || mCmpB < 0 || mCmpB >= channels)
    return;
  if (mChanCount[size_t(mCmpA)] < common || mChanCount[size_t(mCmpB)] < common)
    return;

  srecv_clash(mRecv, mChanCols[size_t(mCmpA)].data(), mChanCols[size_t(mCmpB)].data(),
              mClash.data(), common);
  mHex = spectro::wire::encode_columns(mClash.data(), common, bands, 0);
  assert(spectro::wire::framed_size(int(mHex.size())) < kMaxJSString);
  SendArbitraryMsgFromDelegate(kMsgClashCols, int(mHex.size()), mHex.c_str());
}

/*
 * Every bus that exists, whether or not anything is sending on it -- an idle
 * slot is worth showing, because a Listen-In that has been muted is still where
 * the user put it. PROBING CREATES NOTHING.
 */
void Spectrogram::SendSources()
{
  char buf[kMaxJSString / 2];
  const int n = srecv_slots(reinterpret_cast<unsigned char*>(buf), int(sizeof buf));
  if (n < 0)
    return;

  const int len = int(strnlen(buf, sizeof buf));
  assert(spectro::wire::framed_size(len) < kMaxJSString);
  SendArbitraryMsgFromDelegate(kMsgSources, len, buf);
}

/* Main thread only: it opens and closes readers, which allocates and mmaps. */
void Spectrogram::ApplySources()
{
  if (!mRecv)
    return;
  srecv_set_sources(mRecv, mSources.empty() ? nullptr : mSources.data(),
                    int(mSources.size()));
}

void Spectrogram::SendAxis()
{
  if (!mRecv)
    return;

  const int bands = srecv_bands(mRecv);
  if (bands <= 0)
    return;

  /* ONE axis for every source. They share a configuration, which is exactly
   * what lets their columns be compared cell by cell. */
  std::vector<float> hz(size_t(bands), 0.0f);
  const int n = srecv_band_hz(mRecv, hz.data(), bands);

  const std::string axis = spectro::wire::encode_axis(hz.data(), n);

  assert(spectro::wire::framed_size(int(axis.size())) < kMaxJSString);
  SendArbitraryMsgFromDelegate(kMsgAxis, int(axis.size()), axis.c_str());
}

/*
 * The transport, as the editor reads it. Everything here was measured on the
 * audio thread; this only formats it.
 */
void Spectrogram::SendSync()
{
  const int sig = mPubSig.load(std::memory_order_relaxed);

  const std::string s = spectro::wire::encode_sync(
      mPubPpq.load(std::memory_order_relaxed),
      mPubBpm.load(std::memory_order_relaxed),
      (sig >> 8) & 0xFF,
      sig & 0xFF,
      mPubRunning.load(std::memory_order_relaxed),
      mPubPpqPerCol.load(std::memory_order_relaxed),
      int(GetSampleRate()));

  assert(spectro::wire::framed_size(int(s.size())) < kMaxJSString);
  SendArbitraryMsgFromDelegate(kMsgSync, int(s.size()), s.c_str());
}

/*
 * WHAT A SESSION HAS TO REMEMBER: which buses this window was looking at, and
 * what it was calling a clash. Neither is a parameter -- nobody automates which
 * picture they are looking at -- so the host cannot save them for us.
 *
 * Written as ONE STRING rather than as a count and a loop, because a reader
 * that trusts a count it did not write is a reader that can be made to walk off
 * the end of a chunk. parse_slots already skips anything unreadable.
 */
bool Spectrogram::SerializeState(IByteChunk& chunk) const
{
  if (!SerializeParams(chunk))
    return false;

  std::string sel;
  for (size_t i = 0; i < mSources.size(); i++)
  {
    if (i)
      sel += ',';
    char num[16];
    snprintf(num, sizeof num, "%u", mSources[i]);
    sel += num;
  }
  if (chunk.PutStr(sel.c_str()) <= 0)
    return false;

  char clash[64];
  snprintf(clash, sizeof clash, "%.2f:%.2f", mClashFloorDb, mClashBalanceDb);
  if (chunk.PutStr(clash) <= 0)
    return false;

  /* The view and the comparison: what the window was showing, and what it was
   * measuring. Two separate settings, saved separately. */
  std::string view;
  for (size_t i = 0; i < mView.size(); i++)
  {
    if (i)
      view += ',';
    char num[16];
    snprintf(num, sizeof num, "%d", mView[i]);
    view += num;
  }
  if (chunk.PutStr(view.c_str()) <= 0)
    return false;

  char cmp[48];
  snprintf(cmp, sizeof cmp, "%d:%d:%d", mCmpA, mCmpB, mClashOn ? 1 : 0);
  return chunk.PutStr(cmp) > 0;
}

int Spectrogram::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int pos = UnserializeParams(chunk, startPos);

  /*
   * READ BACK DEFENSIVELY. This plugin's chunk was EMPTY until this version, so
   * every session saved before it will arrive here with nothing after the
   * parameters -- and a chunk written by a later version may hold more than
   * this one knows how to want. Each field is taken only if it is there.
   */
  WDL_String sel;
  int after = chunk.GetStr(sel, pos);
  if (after > pos)
  {
    mSources.clear();
    spectro::wire::parse_slots(sel.Get(), mSources);
    pos = after;
  }

  WDL_String clash;
  after = chunk.GetStr(clash, pos);
  if (after > pos)
  {
    float floorDb = 0.f, balanceDb = 0.f;
    if (spectro::wire::parse_range(clash.Get(), floorDb, balanceDb))
    {
      mClashFloorDb = floorDb;
      mClashBalanceDb = balanceDb;
    }
    pos = after;
  }

  WDL_String view;
  after = chunk.GetStr(view, pos);
  if (after > pos)
  {
    mView.clear();
    spectro::wire::parse_channels(view.Get(), mView);
    if (mView.empty())
      mView.assign(1, 0);
    pos = after;
  }

  WDL_String cmp;
  after = chunk.GetStr(cmp, pos);
  if (after > pos)
  {
    int a = 0, b = 0;
    bool on = false;
    if (spectro::wire::parse_compare(cmp.Get(), a, b, on))
    {
      mCmpA = a;
      mCmpB = b;
      mClashOn = on;
    }
    pos = after;
  }

#if IPLUG_DSP
  /* The selection just changed underneath the receiver, so it has to follow. */
  if (mRecv)
  {
    srecv_set_clash(mRecv, mClashFloorDb, mClashBalanceDb);
    ApplySources();
  }
#endif
  return pos;
}

void Spectrogram::OnUIOpen()
{
  /* QUALIFIED for the same reason the constructor is: under the CLAP target an
   * unqualified `Plugin` is clap::helpers::Plugin, which has no OnUIOpen. */
  iplug::Plugin::OnUIOpen();

  /* Usually too early to be heard -- see kMsgReady. It stays because it costs
   * one small message and covers the case where the page is already live: a
   * reload, or a host that reopens the same WebView. */
  SendAxis();
}

bool Spectrogram::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgReady:
      SendAxis();
      /* And the picker's contents, so an editor that has just mounted does not
       * show an empty source list until the slow timer comes round. */
      SendSources();
      return true;

    case kMsgRange:
    {
      /*
       * "<f_min>:<f_max>" -- the zoom, and it reaches the analyzer while audio
       * is running. spectro_set_range is built for exactly that: it stores a
       * request rather than reallocating, and the audio thread adopts it at its
       * next frame. A malformed payload leaves the range alone, which the engine
       * also enforces for anything undrawable.
       */
      const std::string arg(static_cast<const char*>(pData),
                            size_t(dataSize > 0 ? dataSize : 0));
      float lo = 0.f, hi = 0.f;
      if (!spectro::wire::parse_range(arg, lo, hi))
        return true;

      srecv_set_range(mRecv, lo, hi);

      /* The scale follows the range, and the engine reports it back rather than
       * the editor assuming what it asked for was honoured -- f_max is clamped
       * to Nyquist, so at 32 kHz "2 k to 20 k" really is "2 k to 16 k". */
      SendAxis();
      return true;
    }

    case kMsgSelect:
    {
      /*
       * "<slot>,<slot>,..." -- which buses to listen in on, in the order they
       * should appear. Empty is the own channel alone, and is the normal state
       * rather than an error.
       *
       * pData IS NOT NUL-TERMINATED: it is dataSize bytes out of a base64
       * decode, which is the trap Listen-In's label parser documents.
       */
      const std::string arg(static_cast<const char*>(pData),
                            size_t(dataSize > 0 ? dataSize : 0));
      mSources.clear();
      spectro::wire::parse_slots(arg, mSources);
      ApplySources();
      return true;
    }

    case kMsgView:
    {
      /*
       * "<ch>,<ch>,..." -- which channels are added into the picture. Empty is
       * refused rather than obeyed: a spectrogram showing nothing at all is a
       * broken plugin, not a view, and the editor has no way to say so.
       */
      const std::string arg(static_cast<const char*>(pData),
                            size_t(dataSize > 0 ? dataSize : 0));
      mView.clear();
      spectro::wire::parse_channels(arg, mView);
      if (mView.empty())
        mView.assign(1, 0);
      return true;
    }

    case kMsgCompare:
    {
      /* "<a>:<b>:<on>" -- which two channels, and whether the mask is wanted.
       * Independent of the view: comparing two things you are not looking at is
       * a legitimate thing to ask for. */
      const std::string arg(static_cast<const char*>(pData),
                            size_t(dataSize > 0 ? dataSize : 0));
      int a = 0, b = 0;
      bool on = false;
      if (!spectro::wire::parse_compare(arg, a, b, on))
        return true;
      mCmpA = a;
      mCmpB = b;
      mClashOn = on;
      return true;
    }

    case kMsgClash:
    {
      /* "<floor_db>:<balance_db>" -- what counts as a clash. */
      const std::string arg(static_cast<const char*>(pData),
                            size_t(dataSize > 0 ? dataSize : 0));
      float floorDb = 0.f, balanceDb = 0.f;
      if (!spectro::wire::parse_range(arg, floorDb, balanceDb))
        return true;
      mClashFloorDb = floorDb;
      mClashBalanceDb = balanceDb;
      srecv_set_clash(mRecv, floorDb, balanceDb);
      return true;
    }

    default:
      return false;
  }
}

#endif /* WEBVIEW_EDITOR_DELEGATE */
