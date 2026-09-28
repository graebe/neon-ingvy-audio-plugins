/*
 * Trance Gate -- the Ableton Live plugin, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "TranceGate.h"
#include "IPlug_include_in_plug_src.h"

#include <algorithm>
#include <cstring>
#include <cassert>
#include <cmath>
#include <string>
#include <cstdlib>
#include <algorithm>

/*
 * The rate labels are the engine's, read from its own table rather than
 * retyped. A second copy would drift, and the drift would be silent: the
 * host stores an INDEX, so a list that disagrees by one entry re-points every
 * saved automation lane at the wrong division.
 */
static const char* const kRateLabels[] = {
  "1/1T", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T",
  "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/128"
};
static constexpr int kNumRates = int(sizeof(kRateLabels) / sizeof(kRateLabels[0]));
static constexpr int kRateDefault = 7;          /* 1/16 -- tg-core rates.rs */
static constexpr double kStageMaxPct = 200.0;   /* tg-core STAGE_MAX_PCT    */

/*
 * The percentage format, spelled out rather than left to the `label`
 * argument.
 *
 * iPlug2's AU wrapper prints a parameter with GetDisplay(value, false, str) --
 * the overload that does NOT append the label -- so a unit passed as `label`
 * reaches a VST3 host and never reaches an AU one. A host showing "3.83"
 * where it should show "3.83 %" is the sort of thing only a test that
 * compares displayed strings would catch.
 *
 * Two decimals everywhere, for the same reason: the step decides the
 * precision, and 0.1 rendered Sustain as "60.0" against the engine's "60.00".
 */
static const IParam::DisplayFunc kPctDisplay =
  [](double v, WDL_String& s) { s.SetFormatted(32, "%.2f %%", v); };

TranceGate::TranceGate(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  /*
   * Declared in the ENGINE's order, so the host index is the engine index and
   * there is no mapping table between them to get wrong. The build this
   * replaces carried one, because its declaration order had drifted.
   *
   * Two of these are one-based at the host and zero-based in the engine --
   * Slot and Length -- and that is deliberate: "slot 1" is what a musician
   * reads. The conversion happens once, in PushParams, and nowhere else.
   */
  GetParam(kSlot)->InitInt("Slot", 1, 1, TG_SLOTS);
  GetParam(kLength)->InitInt("Length", 16, 1, TG_MAX_STEPS, "steps");
  GetParam(kRate)->InitEnum("Rate", kRateDefault, kNumRates, "", 0, "", kRateLabels[0],
    kRateLabels[1], kRateLabels[2], kRateLabels[3], kRateLabels[4], kRateLabels[5],
    kRateLabels[6], kRateLabels[7], kRateLabels[8], kRateLabels[9], kRateLabels[10],
    kRateLabels[11], kRateLabels[12]);
  /* "Off"/"On", capitalised: iPlug2 defaults to lower case and the engine
   * prints "Off". */
  GetParam(kLegato)->InitBool("Join Neighbors", false, "", 0, "", "Off", "On");
  GetParam(kTimeMode)->InitEnum("Env Time", 0, {"ms", "% Step"});
  GetParam(kCurve)->InitEnum("Env Curve", 0, {"Linear", "Exponential", "S-Curve"});

  /* Shown as percentages because that is what they are; the engine takes
   * Amount, Width and Sustain as 0..1 and the three envelope stages as the
   * percent value itself, so only the first three are scaled in PushParams. */
  const auto pct = [](IParam* p, const char* name, double def, double lo, double hi) {
    p->InitDouble(name, def, lo, hi, 0.01, "%", 0, "",
                  IParam::ShapeLinear(), IParam::kUnitPercentage, kPctDisplay);
  };
  pct(GetParam(kAmount), "Amount", 100.0, 0.0, 100.0);
  pct(GetParam(kWidth), "Width", 100.0, 5.0, 100.0);
  pct(GetParam(kAttack), "Attack", 1.6, 0.0, kStageMaxPct);
  pct(GetParam(kDecay), "Decay", 16.0, 0.0, kStageMaxPct);
  pct(GetParam(kSustain), "Sustain", 100.0, 0.0, 100.0);
  pct(GetParam(kRelease), "Release", 16.0, 0.0, kStageMaxPct);

#ifdef WEBVIEW_EDITOR_DELEGATE
  /*
   * A CUSTOM SCHEME, NOT file://. A WKWebView loading from file:// treats
   * every asset as cross-origin and refuses the module script, so the editor
   * comes up blank with the reason only in Safari's inspector.
   */
  SetMaxJSStringLength(kMaxJSString);
  SetCustomUrlScheme("tgate");
  SetEnableDevTools(true);
  mEditorInitFunc = [&]() {
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
  };
#endif

  MakeDefaultPreset("Default", kNumPresets);

#if IPLUG_DSP
  mCore = tg_core_create(GetSampleRate() > 0.0 ? GetSampleRate() : 44100.0);
#endif
}

TranceGate::~TranceGate()
{
#if IPLUG_DSP
  if (mCore) tg_core_destroy(mCore);
  mCore = nullptr;
#endif
}

/*
 * THE PATCH TEXT IS THE AUTHORITY, NOT THE ENGINE'S COPY OF IT.
 *
 * The engine is loaded FROM this string and never serialised back into it,
 * which is what keeps the save path off the audio thread: the engine's own
 * save allocates, and the only thread that may touch the engine is the one
 * that must not allocate. The host calls Serialize on the main thread while
 * ProcessBlock may be running, so reading the engine here would be a race as
 * well as a realtime violation.
 */
bool TranceGate::SerializeState(IByteChunk& chunk) const
{
  if (!SerializeParams(chunk)) return false;
  std::string patch;
  {
    std::lock_guard<std::mutex> lk(mPatchMx);
    patch = mPatch;
  }
  chunk.PutStr(patch.c_str());
  return true;
}

int TranceGate::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int pos = UnserializeParams(chunk, startPos);
  WDL_String patch;
  pos = chunk.GetStr(patch, pos);
  {
    std::lock_guard<std::mutex> lk(mPatchMx);
    mPatch.assign(patch.Get() ? patch.Get() : "");
  }
  /* Handed to the audio thread rather than applied here: the engine belongs
   * to that thread. It picks this up at the top of the next block. */
  mPatchDirty.store(true, std::memory_order_release);
  return pos;
}

#if IPLUG_DSP

void TranceGate::OnReset()
{
  const double sr = GetSampleRate();
  if (!mCore) mCore = tg_core_create(sr);
  else tg_core_set_sample_rate(mCore, sr);

  /* Sized here, on the main thread, and never on the audio thread. iPlug2's
   * `sample` is double and the engine's float path is the one its golden
   * render pins, so the block is converted rather than the engine widened. */
  const size_t n = size_t(std::max(GetBlockSize(), 1));
  mL.assign(n, 0.0f);
  mR.assign(n, 0.0f);
  mDry.assign(n, 0.0f);
}

void TranceGate::OnParamChange(int)
{
  /* Nothing. Every value is pushed at the top of each block instead --
   * unconditionally, because set_num owns all the clamping and the side
   * effects, and pushing twelve doubles is cheaper than tracking which of
   * them moved. */
}

void TranceGate::ApplyPendingPatch()
{
  if (!mPatchDirty.load(std::memory_order_acquire)) return;
  /* try_lock, never lock: the audio thread must not wait on the main thread.
   * A miss leaves the flag set and the patch arrives one block later, which
   * nobody can hear. */
  std::unique_lock<std::mutex> lk(mPatchMx, std::try_to_lock);
  if (!lk.owns_lock()) return;
  if (!mPatch.empty()) tg_core_set_param(mCore, "state", mPatch.c_str());
  mPatchDirty.store(false, std::memory_order_release);
}

void TranceGate::PushParams()
{
  const int slot = int(GetParam(kSlot)->Value()) - 1;
  tg_core_set_num(mCore, TG_P_SLOT, double(slot));

  /*
   * LENGTH IS PER SLOT, SO IT IS NOT ALWAYS OURS TO PUSH.
   *
   * pat[slot].length belongs to the slot we just switched to, and the host's
   * Length parameter still holds the slot we left. Pushing it here overwrote
   * the new slot's length with the old one's -- the pattern's own length,
   * destroyed by changing slot and looking at it.
   *
   * So on the block the slot moves, the engine's length wins and the host is
   * told to catch up (SyncSlotParams, from OnIdle). Until it has, Length is
   * not pushed at all.
   */
  bool pushLength = true;
#ifdef WEBVIEW_EDITOR_DELEGATE
  /*
   * GUARDED, BECAUSE THE OTHER HALF OF THE HANDSHAKE IS. SyncSlotParams is
   * driven from OnIdle, which is compiled only with an editor delegate -- so
   * in a build without one there would be nothing to clear the flag, and
   * Length would become permanently unpushable. Suppressing only where the
   * release exists means the worst a future no-UI target can do is behave as
   * this did before the fix, rather than lock a parameter.
   */
  if (slot != mSlotPushed)
  {
    mSlotPushed = slot;
    mSlotSync.store(1, std::memory_order_release);
  }
  pushLength = mSlotSync.load(std::memory_order_acquire) == 0;
#endif
  if (pushLength)
    tg_core_set_num(mCore, TG_P_LENGTH, GetParam(kLength)->Value() - 1.0);
  tg_core_set_num(mCore, TG_P_RATE, GetParam(kRate)->Value());
  tg_core_set_num(mCore, TG_P_LEGATO, GetParam(kLegato)->Value());
  tg_core_set_num(mCore, TG_P_TIME_MODE, GetParam(kTimeMode)->Value());
  tg_core_set_num(mCore, TG_P_CURVE, GetParam(kCurve)->Value());
  tg_core_set_num(mCore, TG_P_AMOUNT, GetParam(kAmount)->Value() / 100.0);
  tg_core_set_num(mCore, TG_P_HOLD, GetParam(kWidth)->Value() / 100.0);
  tg_core_set_num(mCore, TG_P_ATTACK, GetParam(kAttack)->Value());
  tg_core_set_num(mCore, TG_P_DECAY, GetParam(kDecay)->Value());
  tg_core_set_num(mCore, TG_P_SUSTAIN, GetParam(kSustain)->Value() / 100.0);
  tg_core_set_num(mCore, TG_P_RELEASE, GetParam(kRelease)->Value());
}

void TranceGate::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nOut = NOutChansConnected();
  if (!mCore || nFrames <= 0 || nOut <= 0) return;

  /* The host may hand over a longer block than it announced. Growing the
   * vectors here would allocate on the audio thread, so the block is
   * processed in chunks of what was reserved instead. */
  const int cap = int(std::min(mL.size(), mR.size()));
  if (cap <= 0) return;

  ApplyPendingPatch();
  PushParams();

  /* A beat position of -1 is the engine's "no transport", which is what it
   * must see when the host is stopped -- not a stale position, which would
   * make the gate resume mid-pattern. */
  tg_transport_t t;
  t.running = GetTransportIsRunning() ? 1 : 0;
  t.bpm = float(GetTempo() > 0.0 ? GetTempo() : 120.0);
  t.beats = t.running ? GetPPQPos() : -1.0;
  if (t.running && t.beats < 0.0) { t.running = 0; t.beats = -1.0; }

  const bool stereo = nOut > 1 && outputs[1] != nullptr;

  for (int off = 0; off < nFrames; off += cap)
  {
    const int n = std::min(cap, nFrames - off);
    for (int i = 0; i < n; i++)
    {
      mL[size_t(i)] = float(inputs[0][off + i]);
      mR[size_t(i)] = float(stereo ? inputs[1][off + i] : inputs[0][off + i]);
    }

    /* The dry signal, before the engine overwrites it in place, and the pattern
   * phase on BOTH sides of the block -- a block is not a point in time, and
   * filing all of it at one end of itself makes the scope lag the gate it is
   * drawn under. */
  if ((int) mDry.size() >= n)
    std::memcpy(mDry.data(), mL.data(), sizeof(float) * size_t(n));
  const double phase0 = tg_core_phase01(mCore);

  tg_core_process_f32_split(mCore, mL.data(), mR.data(), n, &t);

  if ((int) mDry.size() >= n)
    CaptureBlock(mDry.data(), mL.data(), n, n, phase0, tg_core_phase01(mCore));

    for (int i = 0; i < n; i++)
    {
      outputs[0][off + i] = sample(mL[size_t(i)]);
      if (stereo) outputs[1][off + i] = sample(mR[size_t(i)]);
    }

    /* Each chunk is a continuation of the same block, so the transport has to
     * advance with it or the engine would gate the whole block on one
     * instant. Beats per sample = bpm / 60 / sampleRate. */
    if (t.running)
      t.beats += double(n) * (double(t.bpm) / 60.0) / GetSampleRate();
  }
}

#endif /* IPLUG_DSP */

#ifdef WEBVIEW_EDITOR_DELEGATE

void TranceGate::SendDisplay(int paramIdx)
{
  if (paramIdx < 0 || paramIdx >= kNumParams) return;
  WDL_String str;
  GetParam(paramIdx)->GetDisplay(str);
  /* The tag IS the parameter index, so the UI needs no table to route it. */
  SendArbitraryMsgFromDelegate(paramIdx, str.GetLength(), str.Get());
}

void TranceGate::OnParamChangeUI(int paramIdx, EParamSource source)
{
  SendDisplay(paramIdx);
}

/*
 * ONE FRAME'S WORTH OF EVERYTHING THE UI DRAWS THAT IS NOT A PARAMETER.
 *
 * `ui` is the engine's own single read -- steps, ties, length, phase, step
 * duration, whether the playhead is advancing, the cursor, and the per-step
 * depths -- and `params` is the twelve automatable values plus width_ms at
 * nine significant digits. Both exist precisely so a shell does not take one
 * lock per fact, thirty times a second.
 */
void TranceGate::OnIdle()
{
  if (!mCore) return;

  /* Before the readouts below, so the `params` push carries the length the
   * parameter has just been given rather than the one it is replacing. */
  SyncSlotParams();

  char buf[TG_STATE_MAX];
  if (tg_core_get_param(mCore, "ui", buf, int(sizeof buf)) > 0)
    SendArbitraryMsgFromDelegate(kMsgUiState, int(strlen(buf)), buf);
  if (tg_core_get_param(mCore, "params", buf, int(sizeof buf)) > 0)
    SendArbitraryMsgFromDelegate(kMsgParams, int(strlen(buf)), buf);

  /*
   * THE SCOPE, AS HEX BYTES — AND THE SIZE IS THE WHOLE POINT.
   *
   * SendArbitraryMsgFromDelegate formats through
   * WDL_String::SetFormatted(mMaxJSStringLength, ...), which TRUNCATES at
   * 8192 by default. Four floats per column at "%.3f" is ~28 bytes, so 256
   * columns is ~7.2 KB of text and ~9.6 KB once base64 has inflated it by a
   * third -- every frame was silently cut off part way through the sweep, and
   * the symptom was a signal plot that never reached its right-hand edge.
   *
   * Three decimals of float is absurd for a +/-1 waveform sampled at one
   * pixel per column. A byte per bound is finer than the plot can draw:
   * 4 bytes a column, 1 KB for the sweep, ~1.4 KB base64. Bounded by
   * construction rather than by a limit somebody has to remember.
   */
  const int filled = mCap.filled.load(std::memory_order_acquire);
  if (filled > 0)
  {
    static const char* kHex = "0123456789ABCDEF";
    /* 4 hex pairs per column, plus the count and a separator. */
    char scope[kScopeCols * 8 + 16];
    int n = snprintf(scope, sizeof scope, "%d:", filled);

    auto put = [&](float v) {
      /* -1..1 -> 0..255, clamped: the plot cannot draw past the well edge
       * anyway, and a NaN from an uninitialised column must not become a
       * random byte. */
      const float c = std::isfinite(v) ? std::fmin(1.f, std::fmax(-1.f, v)) : 0.f;
      const int b = int((c + 1.f) * 127.5f + 0.5f);
      scope[n++] = kHex[(b >> 4) & 0xF];
      scope[n++] = kHex[b & 0xF];
    };

    for (int i = 0; i < filled && i < kScopeCols; i++)
    {
      put(mCap.dryLo[i].load(std::memory_order_relaxed));
      put(mCap.dryHi[i].load(std::memory_order_relaxed));
      put(mCap.wetLo[i].load(std::memory_order_relaxed));
      put(mCap.wetHi[i].load(std::memory_order_relaxed));
    }
    scope[n] = '\0';

    /* The guard the old code lacked. Base64 costs a third on top, and the
     * transport truncates rather than fails -- so a payload that outgrows the
     * cap would go back to losing its tail in silence. */
    assert(n * 4 / 3 + 32 < kMaxJSString);
    SendArbitraryMsgFromDelegate(kMsgScope, n, scope);
  }
}

/*
 * Pad edits, and the patch. None of these is a host parameter -- the pattern
 * is 128 steps across 8 slots and exposing it would be 1024 of them -- so
 * they arrive here instead and go straight into the engine's string door.
 */
bool TranceGate::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  if (!mCore) return false;
  std::string arg(static_cast<const char*>(pData), size_t(dataSize > 0 ? dataSize : 0));

  switch (msgTag)
  {
    case kMsgSetCursor:
      tg_core_set_param(mCore, "cursor", arg.c_str());
      return true;

    /* "<index>:<mode>" -- the cursor moves first because `step` edits
     * whatever the cursor is on. Off/On/Tie is three-state rather than two
     * because a tie is not a separate property of a step, it is the third
     * thing a step can be. */
    case kMsgSetStep:
    case kMsgSetDepth:
    {
      const auto colon = arg.find(':');
      if (colon == std::string::npos) return true;
      const std::string idx = arg.substr(0, colon);
      const std::string val = arg.substr(colon + 1);
      tg_core_set_param(mCore, "cursor", idx.c_str());
      tg_core_set_param(mCore, msgTag == kMsgSetStep ? "step" : "step_amount",
                        val.c_str());
      return true;
    }

    case kMsgPatch:               /* paste */
      if (!arg.empty()) tg_core_set_param(mCore, "state", arg.c_str());
      return true;

    /*
     * TYPING IN A READOUT. The UI holds normalised values and no units, so it
     * cannot parse "40 ms" -- the plugin owns the format in both directions
     * and is the only side that can. StringToValue is the same parser the
     * host uses for a typed automation value.
     */
    case kMsgSetText:
    {
      const auto colon = arg.find(':');
      if (colon == std::string::npos) return true;
      const int idx = std::atoi(arg.substr(0, colon).c_str());
      if (idx < 0 || idx >= kNumParams) return true;
      const double v = GetParam(idx)->StringToValue(arg.substr(colon + 1).c_str());
      /* Through the host, not straight into the parameter: a typed value is
       * an edit like any other and belongs in the undo history and the
       * automation lane. */
      BeginInformHostOfParamChangeFromUI(idx);
      SendParameterValueFromUI(idx, GetParam(idx)->ToNormalized(v));
      EndInformHostOfParamChangeFromUI(idx);
      SendDisplay(idx);
      return true;
    }

    /*
     * THE WINDOW GROWS WITH LENGTH. The grid wraps at 16, so 128 steps is
     * eight rows; without this, seven of them are below the bottom edge.
     * The UI reports the row count because it is the side that lays the grid
     * out, and the arithmetic here is the JUCE editor's heightFor().
     */
    case kMsgRows:
    {
      /* The UI sends the height it needs, already in the viewport's own
       * pixels -- it is the side that knows both the row count and the scale
       * it had to apply to fit the width it was given. */
      const int h = std::atoi(arg.c_str());
      if (h > 100 && h < 4000 && h != GetEditorHeight())
        EditorResizeFromUI(GetEditorWidth(), h, true);
      return true;
    }

    /*
     * THE PAGE IS LIVE. Everything OnUIOpen tried to send before the module
     * script existed, sent again now that there is something to receive it.
     */
    case kMsgReady:
      SendFullState();
      return true;

    case kMsgRequestPatch:        /* copy */
    {
      char blob[TG_STATE_MAX];
      if (tg_core_get_param(mCore, "state", blob, int(sizeof blob)) > 0)
        SendArbitraryMsgFromDelegate(kMsgPatch, int(strlen(blob)), blob);
      return true;
    }
    default: return false;   /* not ours -- let the base class see it */
  }
}

/*
 * EVERY VALUE AND EVERY STRING. The values because the UI holds nothing but
 * normalised numbers, and the strings because it cannot format one -- it does
 * not know a unit, a precision or an enum's labels, by design.
 */
void TranceGate::SendFullState()
{
  SendCurrentParamValuesFromDelegate();
  for (int i = 0; i < kNumParams; i++)
    SendDisplay(i);
}

void TranceGate::OnUIOpen()
{
  /* QUALIFIED for the same reason the constructor is: under the CLAP target
   * an unqualified `Plugin` is clap::helpers::Plugin, which has no OnUIOpen. */
  iplug::Plugin::OnUIOpen();

  /*
   * SENT HERE TOO, THOUGH IT IS USUALLY TOO EARLY TO BE HEARD.
   *
   * This fires from didFinishNavigation, and the editor's module script has
   * not evaluated yet, so globalThis.SPVFD does not exist and these go
   * nowhere. kMsgReady is what actually delivers them. It stays because it
   * costs twenty-four small messages and covers the case where the page is
   * already live -- a reload, or a host that reopens the same WebView.
   */
  SendFullState();
}

/*
 * THE OTHER HALF OF THE SLOT HANDSHAKE, on the main thread.
 *
 * The engine's "length" readout is the OPTION INDEX (pat[slot].length - 1),
 * which is the same convention PushParams pushes back, so the parameter's
 * value is that plus one.
 *
 * Through the host rather than straight into the parameter: switching slot
 * genuinely changes Length, and a host that is automating or recording it has
 * to see that happen.
 */
void TranceGate::SyncSlotParams()
{
  if (mSlotSync.load(std::memory_order_acquire) == 0) return;

  char buf[64];
  if (tg_core_get_param(mCore, "length", buf, int(sizeof buf)) > 0)
  {
    const double want = double(std::atoi(buf)) + 1.0;
    if (GetParam(kLength)->Value() != want)
    {
      /* TO THE HOST. SendParameterValueFromUI also does the SetNormalized on
       * our own parameter, which is what actually lets PushParams resume. */
      BeginInformHostOfParamChangeFromUI(kLength);
      SendParameterValueFromUI(kLength, GetParam(kLength)->ToNormalized(want));
      EndInformHostOfParamChangeFromUI(kLength);

      /*
       * AND TO THE EDITOR, which is not optional.
       *
       * "FromUI" means the UI is where the change came from, so iPlug2 sends
       * it onward to the host and no further -- the UI is assumed to know
       * already. Here the change came from the ENGINE, so the editor knows
       * nothing about it: without this the host and the DSP would agree on
       * the new slot's length while the Length knob still showed the old
       * slot's, which is a worse confusion than the bug this fixes.
       */
      SendParameterValueFromDelegate(kLength, want, false);
    }
  }
  /* Cleared even if the read failed: a stuck flag would leave Length
   * permanently unpushable, which is a worse fault than the one it guards. */
  mSlotSync.store(0, std::memory_order_release);
}

#endif

/*
 * One block into the sweep. The column comes from the engine's own pattern
 * phase, so the sweep is locked to the music rather than to wall time -- a
 * scope triggered by the pattern.
 *
 * EACH SAMPLE IS PLACED WHERE IT HAPPENED. The phase is linear in time across
 * a block, so interpolating between the two ends is exact. Filing the whole
 * block under one column instead made every value arrive a block late, and on
 * a rising edge a late reading is a lower one -- the band climbed visibly
 * slower than the gate curve drawn over it, by 0.4 of full scale at a 512
 * sample buffer.
 */
void TranceGate::CaptureBlock(const float* dry, const float* wet, int frames,
                              int totalFrames, double phase0, double phase1)
{
  if (frames <= 0 || totalFrames <= 0) return;

  double span = phase1 - phase0;
  if (span < 0.0) span += 1.0;              /* the pattern wrapped */
  if (span < 0.0 || span > 1.0) span = 0.0;
  const double step = span / double(totalFrames);

  for (int i = 0; i < frames; i++)
  {
    double p = phase0 + step * double(i);
    if (p >= 1.0) p -= 1.0;
    int col = int(p * double(kScopeCols));
    if (col < 0) col = 0;
    if (col >= kScopeCols) col = kScopeCols - 1;

    if (col != mCapCol)
    {
      if (mCapCol >= 0)
      {
        mCap.dryLo[mCapCol].store(mCapDryLo, std::memory_order_relaxed);
        mCap.dryHi[mCapCol].store(mCapDryHi, std::memory_order_relaxed);
        mCap.wetLo[mCapCol].store(mCapWetLo, std::memory_order_relaxed);
        mCap.wetHi[mCapCol].store(mCapWetHi, std::memory_order_relaxed);
        mCap.filled.store(mCapCol + 1, std::memory_order_release);
      }
      if (col < mCapCol)                    /* a new sweep: show the whole one */
        mCap.filled.store(kScopeCols, std::memory_order_release);
      mCapCol = col;
      mCapDryLo = mCapDryHi = dry[i];
      mCapWetLo = mCapWetHi = wet[i];
    }
    if (dry[i] < mCapDryLo) mCapDryLo = dry[i];
    if (dry[i] > mCapDryHi) mCapDryHi = dry[i];
    if (wet[i] < mCapWetLo) mCapWetLo = wet[i];
    if (wet[i] > mCapWetHi) mCapWetHi = wet[i];
  }
}
