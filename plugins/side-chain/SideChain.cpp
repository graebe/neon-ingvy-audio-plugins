/*
 * NI Side-Chain -- the iPlug2 shell. See SideChain.h for what it is allowed to do.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "SideChain.h"
#include "IPlug_include_in_plug_src.h"
#include "Wire.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace iplug;

/*
 * THE RATE LABELS ARE THE ENGINE'S TABLE, re-declared here because iPlug2's
 * InitEnum wants them at the call. They are `rates.rs`'s RATES in order, and
 * the engine will happily report its own via get_param("rate_label") -- which
 * is what the editor reads, so a drift here shows up as a host menu that
 * disagrees with the plugin's own readout rather than as a silent wrong rate.
 *
 * `1/1` IS HERE AND IS NOT IN THE TRANCE GATE'S TABLE. A bar-long step is not
 * a gate; a bar-long duck is the long swell under a build.
 */
static const char* kRateLabels[] = {
  "1/1", "1/1T", "1/2", "1/2T", "1/4", "1/4T",
  "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T",
};
static constexpr int kNumRates = int(sizeof kRateLabels / sizeof kRateLabels[0]);
static constexpr int kRateDefault = 4; /* 1/4 -- rates.rs's RATE_DEFAULT */

/*
 * A UNIT PASSED AS InitDouble's `label` NEVER REACHES AN AU HOST: iPlug2's AU
 * wrapper calls the no-label GetDisplay overload, so the unit silently vanishes
 * in Logic and appears everywhere else. An explicit DisplayFunc is the only
 * spelling that works in all four formats. The Trance Gate found this out.
 */
static const IParam::DisplayFunc kPctDisplay =
  [](double v, WDL_String& s) { s.SetFormatted(32, "%.1f %%", v); };
static const IParam::DisplayFunc kDbDisplay =
  [](double v, WDL_String& s) {
    /* The bottom of the range means "anything triggers", and printing it as
     * "-60.0 dB" invites the reader to look for a quieter setting. */
    if (v <= -60.0) s.Set("off");
    else s.SetFormatted(32, "%.1f dB", v);
  };
static const IParam::DisplayFunc kMsDisplay =
  [](double v, WDL_String& s) { s.SetFormatted(32, "%.0f ms", v); };

/*
 * A MIDI NOTE'S NAME IN LIVE'S OCTAVE NUMBERING, where 36 is C1 and 60 is C3 --
 * which is what the pads say.
 *
 * Generated rather than written out, because 128 string literals is 128 chances
 * to mistype one, and a wrong note name is a bug you only find by playing the
 * wrong drum.
 */
static void NoteName(int n, WDL_String& out)
{
  static const char* kNames[] = { "C", "C#", "D", "D#", "E", "F", "F#",
                                  "G", "G#", "A", "A#", "B" };
  out.SetFormatted(8, "%s%d", kNames[n % 12], (n / 12) - 2);
}

SideChain::SideChain(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kSource)->InitEnum("Source", 0, 3, "", 0, "",
                              "Cycle", "MIDI", "Sidechain");
  GetParam(kRate)->InitEnum("Rate", kRateDefault, kNumRates, "", 0, "",
    kRateLabels[0], kRateLabels[1], kRateLabels[2], kRateLabels[3],
    kRateLabels[4], kRateLabels[5], kRateLabels[6], kRateLabels[7],
    kRateLabels[8], kRateLabels[9], kRateLabels[10], kRateLabels[11]);

  /*
   * TIME MODE IS A DISPLAY CHOICE AND CHANGES NO SOUND, which is why it is an
   * enum with no arithmetic behind it here.
   *
   * The four stage lengths are percentages of the cycle, always -- see the long
   * note in engines/side-chain/crates/sc-core/src/lib.rs. Making the unit switch
   * change their MEANING would give the host four parameters that do something
   * different depending on a fifth, and a host caches parameter displays.
   *
   * So the ms reading is computed by the engine and pushed to the editor as
   * `stage_ms`, and this parameter tells the editor which of the two to show.
   */
  GetParam(kTimeMode)->InitEnum("Time", 0, 2, "", 0, "", "ms", "% of cycle");

  const auto pct = [](IParam* p, const char* name, double def, double hi) {
    p->InitDouble(name, def, 0.0, hi, 0.01, "%", 0, "",
                  IParam::ShapeLinear(), IParam::kUnitPercentage, kPctDisplay);
  };
  /*
   * DELAY RUNS BOTH WAYS, -100..+100, AND THE NEGATIVE HALF IS THE POINT.
   *
   * An early sidechain -- ducking slightly ahead of the beat so a mix breathes
   * into the kick rather than after it -- is a real thing to want, and on the
   * Cycle source it is possible because the cycle is PERIODIC: "20% early" is
   * "80% into the previous cycle", a position already passed rather than an
   * event anticipated.
   *
   * MIDI and Sidechain have nothing periodic to anticipate, so the engine
   * clamps a negative delay to no wait there. The parameter still travels the
   * whole range, because an automation lane is entitled to sweep through it.
   */
  GetParam(kDelay)->InitDouble("Delay", 0.0, -100.0, 100.0, 0.01, "%", 0, "",
                               IParam::ShapeLinear(),
                               IParam::kUnitPercentage, kPctDisplay);
  /* The other three run to twice the cycle, which is as far as a stage can go
   * and still finish before the trigger after next. */
  pct(GetParam(kAttack), "Attack", 2.0, 200.0);
  pct(GetParam(kHold), "Hold", 8.0, 200.0);
  pct(GetParam(kRelease), "Release", 35.0, 200.0);
  pct(GetParam(kDepth), "Depth", 100.0, 100.0);

  /* THREE, NOT FOUR. `Pump` was an asymmetric curve ported from ducker.c --
   * linear down, cubic ease-out up. It is gone, and the direction argument that
   * existed only to serve it went with it. */
  GetParam(kCurve)->InitEnum("Curve", 1, 3, "", 0, "",
                             "Linear", "Exponential", "S-Curve");

  /* Omni plus the sixteen channels. Declared as 17 enum values with the first
   * one named rather than as an int, so the host's own menu reads "Omni". */
  GetParam(kChannel)->InitEnum("Channel", 1, 17);
  GetParam(kChannel)->SetDisplayText(0, "Omni");
  for (int i = 1; i <= 16; i++)
  {
    WDL_String s;
    s.SetFormatted(8, "%d", i);
    GetParam(kChannel)->SetDisplayText(i, s.Get());
  }

  /*
   * A NOTE IS AN ADDRESS, NOT A POSITION ON A RANGE.
   *
   * ducker.c:508-512 records what happens otherwise: as a 0..127 int the Move's
   * grid drew an arc knob, so the cell said nothing and you had to open the
   * value to learn which note it was. As an enum of names it draws "C1" and
   * steps a semitone at a time. The wire value is still the note number.
   */
  GetParam(kNote)->InitEnum("Trigger", 36, 128);
  for (int i = 0; i < 128; i++)
  {
    WDL_String s;
    NoteName(i, s);
    GetParam(kNote)->SetDisplayText(i, s.Get());
  }

  GetParam(kMidiMode)->InitEnum("Mode", 0, 2, "", 0, "", "Trigger", "Gate");
  GetParam(kVelSens)->InitDouble("Vel", 0.0, 0.0, 100.0, 0.01, "%", 0, "",
                                 IParam::ShapeLinear(),
                                 IParam::kUnitPercentage, kPctDisplay);
  GetParam(kThreshold)->InitDouble("Threshold", -24.0, -60.0, 0.0, 0.1, "dB", 0, "",
                                   IParam::ShapeLinear(),
                                   IParam::kUnitDB, kDbDisplay);
  GetParam(kLockout)->InitDouble("Lockout", 20.0, 0.0, 200.0, 1.0, "ms", 0, "",
                                 IParam::ShapeLinear(),
                                 IParam::kUnitMilliseconds, kMsDisplay);

#if IPLUG_DSP
  mShell = sc_shell_create(GetSampleRate() > 0.0 ? GetSampleRate() : 44100.0);
#endif

#ifdef WEBVIEW_EDITOR_DELEGATE
  /*
   * 65536, UP FROM iPlug2'S 8192 DEFAULT, because the scope push is the one
   * message that can approach it and the transport TRUNCATES rather than fails.
   * The Trance Gate's tail silently vanished from every frame before it found
   * this; there is an assert at the push here for the same reason.
   */
  SetMaxJSStringLength(kMaxJSString);
  /* file:// blocks the module script, so the page is served over a custom
   * scheme instead. */
  SetCustomUrlScheme("nisidechain");
  SetEnableDevTools(true);
  /* The ground's detector. Created here rather than in OnReset because OnReset
   * may run on a real-time thread in some hosts and this allocates; the rate it
   * is given now is corrected there. */
  mGround = gnd_new(GetSampleRate());

  mEditorInitFunc = [&]() {
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
  };
#endif
}

SideChain::~SideChain()
{
  sc_shell_destroy(mShell);
  mShell = nullptr;
#ifdef WEBVIEW_EDITOR_DELEGATE
  gnd_free(mGround);
  mGround = nullptr;
#endif
}

bool SideChain::SerializeState(IByteChunk& chunk) const
{
  const int at = shell::state::Begin(chunk, kChunkVersion);
  return SerializeParams(chunk) && shell::state::End(chunk, at);
}

int SideChain::UnserializeState(const IByteChunk& chunk, int startPos)
{
  const shell::state::Header h = shell::state::Read(chunk, startPos);
  if (h.body < 0)
    return -1;
  const int pos = UnserializeParams(chunk, h.body);
  return pos < 0 ? pos : shell::state::Finish(h, pos);
}

#if IPLUG_DSP

void SideChain::OnReset()
{
  if (!mShell) return;
  sc_shell_post_sample_rate(mShell, GetSampleRate());

  /*
   * SIZED HERE AND NEVER ON THE AUDIO THREAD.
   *
   * GetBlockSize() is what the host announced, but a host is allowed to hand
   * over more than it announced -- so ProcessBlock chunks against this capacity
   * rather than trusting the frame count. SC_MAX_BLOCK is the engine's own
   * ceiling for the key buffer and there is no reason to reserve past it.
   */
  const int cap = std::max(64, std::min(GetBlockSize(), SC_MAX_BLOCK));
  mL.assign(size_t(cap), 0.f);
  mR.assign(size_t(cap), 0.f);
  mDry.assign(size_t(cap), 0.f);
  mGain.assign(size_t(cap), 1.f);
  mSweep.assign(size_t(cap), 0.f);
  mKeyL.assign(size_t(cap), 0.f);
  mKeyR.assign(size_t(cap), 0.f);

  /*
   * A rate change invalidates every column, because a column is a slice of a
   * cycle and the cycle has just changed length in samples. Bumping the
   * generation retires them all without touching the values.
   *
   * THE WHOLE CAPTURE IS WRITTEN HERE, and `gain` alone is not enough.
   * `std::atomic<float>` and `std::atomic<uint32_t>` members are DEFAULT-
   * INITIALISED, which for these leaves them indeterminate -- reading one
   * before it has been stored to is undefined behaviour, not "reads as zero".
   *
   * `seen` was the dangerous one: it is compared against the generation, so an
   * indeterminate value that happened to match would publish a column of
   * indeterminate audio as real. The window fills within one cycle and hides it,
   * which is exactly the kind of bug that surfaces once, on someone else's
   * machine, as a spike that reads as a transient.
   */
  mCapGen.fetch_add(1, std::memory_order_relaxed);
  mCapCol = -1;
  for (int i = 0; i < kScopeCols; i++)
  {
    mCap.dryLo[i].store(0.f, std::memory_order_relaxed);
    mCap.dryHi[i].store(0.f, std::memory_order_relaxed);
    mCap.wetLo[i].store(0.f, std::memory_order_relaxed);
    mCap.wetHi[i].store(0.f, std::memory_order_relaxed);
    mCap.gain[i].store(1.f, std::memory_order_relaxed);
    /* Zero is never a live generation: mCapGen starts at 1 and only rises. */
    mCap.seen[i].store(0u, std::memory_order_relaxed);
  }

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* A rate change re-derives every coefficient; the reset stops a hump left over
   * from before the transport stopped firing an onset the moment it starts
   * again. Neither touches the onset COUNT -- see gnd_fires. */
  gnd_set_sample_rate(mGround, GetSampleRate());
  gnd_reset(mGround);
#endif
}

/*
 * EVERY PARAMETER, EVERY BLOCK, UNCONDITIONALLY.
 *
 * OnParamChange is deliberately not implemented. Pushing on change means
 * maintaining a record of what was pushed and trusting the host to tell us
 * about every path that can move a value -- automation, a preset, a typed
 * entry, a control surface. Fifteen stores per block is nothing, and it removes
 * a whole class of "the host changed it and we missed it".
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
  /* The engine takes 0..1; the host shows a percentage. One division, in one
   * place, rather than a percentage inside the DSP. */
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
 * MIDI, WITH ITS SAMPLE OFFSET KEPT.
 *
 * mOffset is the frame within the block the message belongs on, and honouring
 * it is the difference between a duck that lands where the note is and one that
 * lands at the top of whatever buffer the host happens to be using. At 256
 * frames that is up to 5 ms, and it is JITTER rather than latency -- the same
 * note lands on a different sample depending on where in the buffer it fell --
 * so it cannot be compensated for anywhere downstream.
 */
void SideChain::ProcessMidiMsg(const IMidiMsg& msg)
{
  /* The audio thread, ahead of the block the note belongs to -- so the engine
   * is taken here too; a second begin in the same block is harmless. */
  sc_core_t* core = sc_shell_begin(mShell);
  if (!core) return;
  const unsigned char bytes[3] = {
    (unsigned char) msg.mStatus,
    (unsigned char) msg.mData1,
    (unsigned char) msg.mData2,
  };
  sc_core_on_midi(core, bytes, 3, msg.mOffset);
  /* NOT forwarded: PLUG_DOES_MIDI_OUT is 0, and a ducker that echoed its
   * trigger notes would arm the instrument after it. */
}

void SideChain::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nOut = NOutChansConnected();
  if (!mShell || nFrames <= 0 || nOut <= 0) return;
  const int cap = int(std::min(mL.size(), mR.size()));
  if (cap <= 0) return;

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

  sc_core_t* core = sc_shell_begin(mShell);
  PushParams(core);

  const bool stereoOut = nOut > 1;
  const int nIn = NInChansConnected();
  const bool stereoIn = nIn > 1;

  /*
   * THE AUX BUS, AND THE ONE THING THIS SIDE KNOWS THAT THE ENGINE CANNOT.
   *
   * Inputs 2 and 3 are the sidechain. Whether they are CONNECTED is a question
   * only the host can answer: an unpatched bus and a silent one are the same
   * block of zeroes, and "no key" is a different message to the user from
   * "nothing is playing".
   */
  const bool keyL = IsChannelConnected(ERoute::kInput, 2);
  const bool keyR = IsChannelConnected(ERoute::kInput, 3);
  const bool haveKey = keyL || keyR;
  mKeyConnected.store(haveKey ? 1 : 0, std::memory_order_relaxed);
  sc_core_set_key_connected(core, haveKey ? 1 : 0);

  for (int off = 0; off < nFrames; off += cap)
  {
    const int n = std::min(cap, nFrames - off);

    for (int i = 0; i < n; i++)
    {
      mL[i] = float(inputs[0][off + i]);
      mR[i] = float(stereoIn ? inputs[1][off + i] : inputs[0][off + i]);
    }
    std::memcpy(mDry.data(), mL.data(), sizeof(float) * size_t(n));

    if (haveKey)
    {
      for (int i = 0; i < n; i++)
      {
        mKeyL[i] = keyL ? float(inputs[2][off + i]) : 0.f;
        mKeyR[i] = keyR ? float(inputs[3][off + i]) : mKeyL[i];
      }
      /*
       * REPORTED, NOT WORKED AROUND. Logic and GarageBand copy bus 1 into the
       * sidechain bus when nothing is patched. iPlug2's own example papers over
       * it with a memcmp and a comment calling the workaround imperfect, saying
       * the better answer is an explicit enable -- which Side-Chain has, in the
       * Source parameter. So this only tells the editor, which is the one thing
       * that would otherwise be actively misleading: a key meter moving in time
       * with the track's own audio reads as a working sidechain.
       */
      mKeyIsMain.store(
        sc::wire::key_is_duplicate(mDry.data(), mKeyL.data(), n) ? 1 : 0,
        std::memory_order_relaxed);
      sc_core_push_key_f32(core, mKeyL.data(), mKeyR.data(), n);
    }
    else
    {
      mKeyIsMain.store(0, std::memory_order_relaxed);
    }

    /*
     * THE TRANSPORT. `-1` beats means NO TRANSPORT, which is not beat zero: a
     * stopped host must never be handed a stale position. And a running host
     * that reports a negative position is not running as far as we are
     * concerned -- that combination is a host bug, and believing it would make
     * the phase-locked loop chase a target behind the start of time.
     */
    sc_transport_t t;
    t.running = GetTransportIsRunning() ? 1 : 0;
    t.bpm = float(GetTempo() > 0.0 ? GetTempo() : 120.0);
    t.beats = t.running ? GetPPQPos() : -1.0;
    if (t.running && t.beats < 0.0) { t.running = 0; t.beats = -1.0; }
    /* A block longer than the reserved capacity is processed in chunks, and
     * every chunk is a CONTINUATION -- without advancing the position here the
     * engine would place a whole block's triggers on one instant, which at a
     * long block is a stutter locked to the buffer size rather than the grid. */
    if (off > 0 && t.running)
      t.beats = sc::wire::advance_beats(t.beats, off, double(t.bpm), GetSampleRate());

    sc_core_process_f32_split_tap(core, mL.data(), mR.data(), mGain.data(),
                                    mSweep.data(), n, &t);

    CaptureBlock(mDry.data(), mL.data(), mGain.data(), mSweep.data(), n);

    for (int i = 0; i < n; i++)
    {
      outputs[0][off + i] = sample(mL[i]);
      if (stereoOut) outputs[1][off + i] = sample(mR[i]);
    }
  }

  sc_shell_end(mShell, nFrames);
}

/*
 * THE CAPTURE, BINNED BY THE ENGINE'S OWN SWEEP.
 *
 * The column index comes from `mSweep[i]`, which the engine wrote -- so there
 * is no copy of the phase arithmetic here to drift out of step with the sound.
 * See SideChain.h for why the window is phase-locked rather than rolling.
 */
void SideChain::CaptureBlock(const float* dry, const float* wet, const float* gain,
                        const float* sweep, int frames)
{
  const uint32_t gen = mCapGen.load(std::memory_order_relaxed);

  const auto flush = [&](int col) {
    if (col < 0) return;
    mCap.dryLo[col].store(mCapDryLo, std::memory_order_relaxed);
    mCap.dryHi[col].store(mCapDryHi, std::memory_order_relaxed);
    mCap.wetLo[col].store(mCapWetLo, std::memory_order_relaxed);
    mCap.wetHi[col].store(mCapWetHi, std::memory_order_relaxed);
    mCap.gain[col].store(mCapGain, std::memory_order_relaxed);
    /* Published LAST, and with release, so a reader that sees this column as
     * seen also sees the five values above it. */
    mCap.seen[col].store(gen, std::memory_order_release);
  };

  for (int i = 0; i < frames; i++)
  {
    /* The sweep is 0..1 inclusive, so 1.0 would index one past the end. */
    int col = int(sweep[i] * float(kScopeCols));
    col = std::min(kScopeCols - 1, std::max(0, col));

    if (col != mCapCol)
    {
      flush(mCapCol);
      mCapCol = col;
      /* A new column starts FROM THIS SAMPLE rather than widening the last
       * one's bounds -- otherwise every column would eventually hold the
       * maximum of the whole cycle and the picture would be a solid block. */
      mCapDryLo = mCapDryHi = dry[i];
      mCapWetLo = mCapWetHi = wet[i];
      mCapGain = gain[i];
      continue;
    }

    if (dry[i] < mCapDryLo) mCapDryLo = dry[i];
    if (dry[i] > mCapDryHi) mCapDryHi = dry[i];
    if (wet[i] < mCapWetLo) mCapWetLo = wet[i];
    if (wet[i] > mCapWetHi) mCapWetHi = wet[i];
    /* The MINIMUM gain: the deepest the duck got in this column, which is the
     * thing being looked at. */
    if (gain[i] < mCapGain) mCapGain = gain[i];
  }

  /*
   * AND THE COLUMN STILL BEING FILLED, so the picture is at most one block
   * behind rather than waiting for the sweep to move on.
   *
   * This is not cosmetic: when the sweep SATURATES at 1.0 -- a MIDI or
   * sidechain source that has not fired again -- the index stops changing, and
   * without this the last column would never be written at all.
   */
  flush(mCapCol);
}

#endif /* IPLUG_DSP */

#if IPLUG_EDITOR

void SideChain::SendDisplay(int paramIdx)
{
  if (paramIdx < 0 || paramIdx >= kNumParams) return;
  WDL_String str;
  GetParam(paramIdx)->GetDisplay(str);
  SendArbitraryMsgFromDelegate(paramIdx, str.GetLength(), str.Get());
}

/*
 * EVERY VALUE AND EVERY STRING. The values because the editor holds nothing but
 * normalised numbers, and the strings because it cannot format one -- it does
 * not know a unit, a precision or an enum's labels, by design.
 */
void SideChain::SendFullState()
{
  SendCurrentParamValuesFromDelegate();
  for (int i = 0; i < kNumParams; i++)
    SendDisplay(i);
}

void SideChain::OnParamChangeUI(int paramIdx, EParamSource source)
{
  SendDisplay(paramIdx);
}

void SideChain::OnUIOpen()
{
  /* QUALIFIED because under the CLAP target an unqualified `Plugin` is
   * clap::helpers::Plugin, which has no OnUIOpen. */
  iplug::Plugin::OnUIOpen();

  /*
   * SENT HERE TOO, THOUGH IT IS USUALLY TOO EARLY TO BE HEARD.
   *
   * This fires from didFinishNavigation, and a <script type="module"> is
   * DEFERRED -- it evaluates after the document is done, so globalThis.SPVFD
   * does not exist yet and every one of these goes nowhere. kMsgReady is what
   * actually delivers them. It stays because it costs thirty small messages and
   * covers the case where the page is already live: a reload, or a host that
   * reopens the same WebView.
   */
  SendFullState();
}

/*
 * ONE FRAME'S WORTH OF EVERYTHING THE EDITOR DRAWS THAT IS NOT A PARAMETER.
 *
 * OnIdle runs off a timer created in the API wrapper's constructor at
 * IDLE_TIMER_RATE, so ~50 Hz at best and not tied to the editor being open.
 * That is why the editor treats the phase it receives as an ANCHOR and
 * interpolates from it on requestAnimationFrame: reading this directly stutters
 * and drifts audibly against the sound.
 */
/*
 * One message per kick, and only when there has been one. The idiom, and why the
 * count is compared with != rather than >, is in ground_detect.h.
 */
void SideChain::SendGround()
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

void SideChain::OnIdle()
{
  if (!mShell) return;

  /* What the audio thread last published -- never the engine itself. */
  char buf[SC_STATE_MAX];
  if (sc_shell_read(mShell, "ui", buf, int(sizeof buf)) > 0)
    SendArbitraryMsgFromDelegate(kMsgUiState, int(strlen(buf)), buf);
  if (sc_shell_read(mShell, "params", buf, int(sizeof buf)) > 0)
    SendArbitraryMsgFromDelegate(kMsgParams, int(strlen(buf)), buf);
  if (sc_shell_read(mShell, "stage_ms", buf, int(sizeof buf)) > 0)
    SendArbitraryMsgFromDelegate(kMsgStageMs, int(strlen(buf)), buf);

  {
    /* "<keyConnected>:<keyIsMain>" -- the two facts only this side knows. */
    char b[16];
    const int n = snprintf(b, sizeof b, "%d:%d",
                           mKeyConnected.load(std::memory_order_relaxed),
                           mKeyIsMain.load(std::memory_order_relaxed));
    SendArbitraryMsgFromDelegate(kMsgBuses, n, b);
  }

  /*
   * THE SCOPE, AS HEX BYTES -- AND THE SIZE IS THE WHOLE POINT.
   *
   * SendArbitraryMsgFromDelegate formats through
   * WDL_String::SetFormatted(mMaxJSStringLength, ...), which TRUNCATES rather
   * than fails, and base64 inflates by a third on top. The Trance Gate sent
   * four "%.3f" per column and lost the right-hand edge of its picture every
   * single frame without any error anywhere.
   *
   * A byte per bound is finer than the plot can draw. Five bytes a column here:
   * the dry's low and high, the wet's low and high, and the gain that was
   * applied -- 2560 hex characters, plus one seen-flag per column.
   *
   * THE SEEN FLAGS ARE NOT PADDING. With a phase-locked sweep there is no head
   * cursor to orient the window by, so nothing otherwise distinguishes a column
   * holding silence from one the sweep has not reached yet -- and the first
   * cycle after loading would draw a flat line across the rest of the window,
   * which reads as a signal that stopped.
   */
  {
    static const char* kHex = "0123456789ABCDEF";
    const uint32_t gen = mCapGen.load(std::memory_order_relaxed);

    char scope[kScopeCols * 11 + 64];
    int n = snprintf(scope, sizeof scope, "%d:", kScopeCols);

    for (int i = 0; i < kScopeCols; i++)
      scope[n++] = (mCap.seen[i].load(std::memory_order_acquire) == gen) ? '1' : '0';
    scope[n++] = ':';

    const auto put = [&](unsigned char b) {
      scope[n++] = kHex[(b >> 4) & 0xF];
      scope[n++] = kHex[b & 0xF];
    };
    for (int i = 0; i < kScopeCols; i++)
    {
      put(sc::wire::encode_sample(mCap.dryLo[i].load(std::memory_order_relaxed)));
      put(sc::wire::encode_sample(mCap.dryHi[i].load(std::memory_order_relaxed)));
      put(sc::wire::encode_sample(mCap.wetLo[i].load(std::memory_order_relaxed)));
      put(sc::wire::encode_sample(mCap.wetHi[i].load(std::memory_order_relaxed)));
      /* The gain is unipolar, so it gets the unipolar encoder -- through the
       * bipolar one every value would land in 128..255 and the trace would
       * arrive at seven bits, reading as a coarse meter rather than a bug. */
      put(sc::wire::encode_unipolar(mCap.gain[i].load(std::memory_order_relaxed)));
    }
    scope[n] = '\0';

    /* The guard the Trance Gate lacked until the symptom was traced -- on the
     * buffer's own size, so it holds in a release build too. */
    static_assert(sc::wire::framed_size(int(sizeof scope)) < kMaxJSString,
                  "the scope push no longer fits the WebView's string cap");
    SendArbitraryMsgFromDelegate(kMsgScope, n, scope);
  }

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* One ring per kick the detector found since the last tick. */
  SendGround();
#endif
}

bool SideChain::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  std::string arg(static_cast<const char*>(pData),
                  size_t(dataSize > 0 ? dataSize : 0));

  switch (msgTag)
  {
    /*
     * TYPING IN A READOUT. The editor holds normalised values and no units, so
     * it cannot parse "-18 dB" or "1/8" -- this side owns the format in both
     * directions and is the only one that can. StringToValue is the same parser
     * the host uses for a typed automation value.
     */
    case kMsgSetText:
    {
      std::string idxText, valText;
      if (!sc::wire::split_pair(arg, idxText, valText)) return true;
      const int idx = std::atoi(idxText.c_str());
      if (idx < 0 || idx >= kNumParams) return true;
      const double v = GetParam(idx)->StringToValue(valText.c_str());
      /* Through the host, not straight into the parameter: a typed value is an
       * edit like any other and belongs in the undo history and the automation
       * lane. */
      BeginInformHostOfParamChangeFromUI(idx);
      SendParameterValueFromUI(idx, GetParam(idx)->ToNormalized(v));
      EndInformHostOfParamChangeFromUI(idx);
      SendDisplay(idx);
      return true;
    }

    /*
     * THE EDITOR REPORTS THE HEIGHT IT NEEDS, already in the viewport's own
     * pixels -- it is the side that knows both its content and the scale it had
     * to apply to fit the width it was given.
     *
     * Which makes the number UNTRUSTED here: an arithmetic slip in the editor
     * arrives as a request for a 40 000 pixel window, and the host will honour
     * it. clamp_editor_height is the refusal.
     */
    case kMsgHeight:
    {
      const int h = sc::wire::clamp_editor_height(std::atoi(arg.c_str()));
      if (h && h != GetEditorHeight())
        EditorResizeFromUI(GetEditorWidth(), h, true);
      return true;
    }

    /* THE PAGE IS LIVE. Everything OnUIOpen tried to send before the module
     * script existed, sent again now that there is something to receive it. */
    case kMsgReady:
      SendFullState();
      return true;

    default:
      return false; /* not ours -- let the base class see it */
  }
}

#endif /* IPLUG_EDITOR */
