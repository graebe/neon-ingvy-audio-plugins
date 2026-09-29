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
  mSpectro = spectro_new();

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
  mEditorInitFunc = [&]() {
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
  };
#endif

  MakeDefaultPreset("Default", kNumPresets);

  /* The drain buffer, sized once for the worst tick this code will ever send.
   * The analyzer's band count cannot change without a configure, and configure
   * only ever restates it. */
  const int bands = spectro_bands(mSpectro);
  mCols.assign(size_t(bands) * kMaxColsPerTick, 0);
  /* Two hex characters a byte, plus "<cols>:<bands>:". */
  mHex.reserve(size_t(bands) * kMaxColsPerTick * 2 + 32);
}

Spectrogram::~Spectrogram()
{
  spectro_free(mSpectro);
  mSpectro = nullptr;
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

  spectro_configure(mSpectro, sr, fftSize, hop,
                    SPECTRO_BANDS, SPECTRO_F_MIN, SPECTRO_F_MAX,
                    SPECTRO_DB_FLOOR, SPECTRO_DB_CEIL);

  /* Sized here, on the main thread, and never on the audio thread. iPlug2's
   * `sample` is double and the analyzer's path is float, so the block is
   * converted rather than the analyzer widened. */
  mMono.assign(size_t(std::max(GetBlockSize(), 1)), 0.0f);

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* The axis just changed. An editor that is already open would otherwise keep
   * the old scale until it is reopened. */
  SendAxis();
#endif
}

void Spectrogram::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nIn = NInChansConnected();
  const int nOut = NOutChansConnected();

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
      spectro_push_f32(mSpectro, mMono.data(), n);
    }
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
void Spectrogram::OnIdle()
{
  if (!mSpectro)
    return;

  const int bands = spectro_bands(mSpectro);
  if (bands <= 0)
    return;

  const int cols = spectro_take_columns(mSpectro, mCols.data(), kMaxColsPerTick);
  if (cols <= 0)
    return;

  mHex = spectro::wire::encode_columns(mCols.data(), cols, bands);

  /* The guard, at the one call that can approach the cap. The static_assert in
   * Spectrogram.h is what actually holds the budget -- this one is gone under
   * -DNDEBUG, which is how the plugin ships. */
  assert(spectro::wire::framed_size(int(mHex.size())) < kMaxJSString);
  SendArbitraryMsgFromDelegate(kMsgCols, int(mHex.size()), mHex.c_str());
}

void Spectrogram::SendAxis()
{
  if (!mSpectro)
    return;

  const int bands = spectro_bands(mSpectro);
  if (bands <= 0)
    return;

  std::vector<float> hz(size_t(bands), 0.0f);
  const int n = spectro_band_hz(mSpectro, hz.data(), bands);

  const std::string axis = spectro::wire::encode_axis(hz.data(), n);

  assert(spectro::wire::framed_size(int(axis.size())) < kMaxJSString);
  SendArbitraryMsgFromDelegate(kMsgAxis, int(axis.size()), axis.c_str());
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

      spectro_set_range(mSpectro, lo, hi);

      /* The scale follows the range, and the engine reports it back rather than
       * the editor assuming what it asked for was honoured -- f_max is clamped
       * to Nyquist, so at 32 kHz "2 k to 20 k" really is "2 k to 16 k". */
      SendAxis();
      return true;
    }

    default:
      return false;
  }
}

#endif /* WEBVIEW_EDITOR_DELEGATE */
