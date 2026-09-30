/*
 * Trance Gate -- the Ableton Live plugin, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "TranceGate.h"
#include "Patch.h"
#include "Wire.h"
#include "IPlug_include_in_plug_src.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
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
  /* "%", NOT "% Step". The long form did not fit the readout's 64px and read
   * as noise beside a 13-character rate label; what the percentage is OF is
   * said once, by the control's own name, rather than in every value it can
   * show. The engine accepts either spelling. */
  GetParam(kTimeMode)->InitEnum("Env Time", 0, {"ms", "%"});
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
  /*
   * 100% IS THE DEFAULT AND IT HAS TO BE.
   *
   * Fade is how much of the pattern has arrived, so zero is silence -- which
   * is exactly what a build-up wants and exactly what a fresh instance must
   * not do. At 100 the engine's weights are all 1.0 and the gate is what it
   * was before this existed, which is what keeps both golden renders valid.
   */
  pct(GetParam(kFade), "Fade", 100.0, 0.0, 100.0);
  /* "Hard"/"Soft" rather than Off/On: the switch does not turn the fade on, it
   * chooses whether a step arriving ramps or jumps. */
  GetParam(kFadeSoft)->InitBool("Fade Shape", false, "", 0, "", "Hard", "Soft");
  /*
   * WHICH END THE PATTERN IS BUILT UP FROM, and In is the default because it is
   * what the gate did before the direction existed. The knob means the same
   * thing either way -- how much of the drawn pattern is present -- so 100% is
   * the pattern in both and switching this at rest changes nothing.
   */
  GetParam(kFadeDir)->InitEnum("Fade Dir", 0, {"In", "Out"});

#ifdef WEBVIEW_EDITOR_DELEGATE
  /*
   * A CUSTOM SCHEME, NOT file://. A WKWebView loading from file:// treats
   * every asset as cross-origin and refuses the module script, so the editor
   * comes up blank with the reason only in Safari's inspector.
   */
  SetMaxJSStringLength(kMaxJSString);
  SetCustomUrlScheme("tgate");
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

  MakeDefaultPreset("Default", kNumPresets);

#if IPLUG_DSP
  mShell = tg_shell_create(GetSampleRate() > 0.0 ? GetSampleRate() : 44100.0);
#endif
}

TranceGate::~TranceGate()
{
#if IPLUG_DSP
  tg_shell_destroy(mShell);
  mShell = nullptr;
#endif
#ifdef WEBVIEW_EDITOR_DELEGATE
  gnd_free(mGround);
  mGround = nullptr;
#endif
}

/*
 * THE PATTERN IS SAVED FROM WHAT THE ENGINE PUBLISHED, not from a copy beside
 * it -- including an edit posted a moment ago and not yet applied, so a save
 * straight after an edit has it whether or not audio is running. The host calls
 * this on the main thread while ProcessBlock may be running; the published
 * state is the one read that is safe there. Patch.cpp has both halves.
 */
bool TranceGate::SerializeState(IByteChunk& chunk) const
{
  return tg::patch::Save(mShell, chunk,
                         [this](IByteChunk& c) { return SerializeParams(c); });
}

int TranceGate::UnserializeState(const IByteChunk& chunk, int startPos)
{
  /* The blob is posted, not applied: the engine belongs to the audio thread,
   * which picks it up at the top of the next block -- before PushParams, so the
   * host's parameters still win over the blob's copies of them. */
  return tg::patch::Load(mShell, chunk, startPos,
                         [this](const IByteChunk& c, int pos) { return UnserializeParams(c, pos); });
}

#if IPLUG_DSP

void TranceGate::OnReset()
{
  tg_shell_post_sample_rate(mShell, GetSampleRate());

  /* Sized here, on the main thread, and never on the audio thread. iPlug2's
   * `sample` is double and the engine's float path is the one its golden
   * render pins, so the block is converted rather than the engine widened. */
  const size_t n = size_t(std::max(GetBlockSize(), 1));
  mL.assign(n, 0.0f);
  mR.assign(n, 0.0f);
  mDry.assign(n, 0.0f);

  /*
   * THE SWEEP'S FALLBACK LENGTH, until an idle tick has measured the pattern's
   * own. A column is a slice of ONE CYCLE now rather than of wall time, so
   * there is no per-column sample count to keep here: the column a sample lands
   * in comes from the phase, and the phase comes from the engine.
   */
  mCycleSamples.store(GetSampleRate() * (kScopeFallbackMs / 1000.0),
                      std::memory_order_relaxed);
  mCapCol = -1;
  mCapCount = 0;
  mSweep = 0.0;
  /* Cleared, or the first window drawn after a rate change or a re-open is
   * whatever the last session left in the ring. */
  for (int i = 0; i < kScopeCols; i++)
  {
    mCap.dryLo[i].store(0.f, std::memory_order_relaxed);
    mCap.dryHi[i].store(0.f, std::memory_order_relaxed);
    mCap.wetLo[i].store(0.f, std::memory_order_relaxed);
    mCap.wetHi[i].store(0.f, std::memory_order_relaxed);
  }
  mCap.head.store(0, std::memory_order_release);

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* A rate change re-derives every coefficient; the reset stops a hump left over
   * from before the transport stopped firing an onset the moment it starts
   * again. Neither touches the onset COUNT -- see gnd_fires. */
  gnd_set_sample_rate(mGround, GetSampleRate());
  gnd_reset(mGround);
#endif
}

void TranceGate::OnParamChange(int)
{
  /* Nothing. Every value is pushed at the top of each block instead --
   * unconditionally, because set_num owns all the clamping and the side
   * effects, and pushing twelve doubles is cheaper than tracking which of
   * them moved. */
}

bool TranceGate::PushParams(tg_core_t* core)
{
  const int slot = int(GetParam(kSlot)->Value()) - 1;
  tg_core_set_num(core, TG_P_SLOT, double(slot));

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
  bool moved = false;
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
    moved = true;
  }
  pushLength = !moved && mSlotSync.load(std::memory_order_acquire) == 0;
#endif
  if (pushLength)
  {
    /* A length is in the saved blob, so a change to it is published at once
     * rather than on the cadence -- a save straight after it must have it. */
    const double length = GetParam(kLength)->Value() - 1.0;
    if (length != mLengthPushed)
    {
      mLengthPushed = length;
      tg_shell_touch(mShell);
    }
    tg_core_set_num(core, TG_P_LENGTH, length);
  }
  tg_core_set_num(core, TG_P_RATE, GetParam(kRate)->Value());
  tg_core_set_num(core, TG_P_LEGATO, GetParam(kLegato)->Value());
  tg_core_set_num(core, TG_P_TIME_MODE, GetParam(kTimeMode)->Value());
  tg_core_set_num(core, TG_P_CURVE, GetParam(kCurve)->Value());
  tg_core_set_num(core, TG_P_AMOUNT, GetParam(kAmount)->Value() / 100.0);
  tg_core_set_num(core, TG_P_HOLD, GetParam(kWidth)->Value() / 100.0);
  tg_core_set_num(core, TG_P_ATTACK, GetParam(kAttack)->Value());
  tg_core_set_num(core, TG_P_DECAY, GetParam(kDecay)->Value());
  tg_core_set_num(core, TG_P_SUSTAIN, GetParam(kSustain)->Value() / 100.0);
  tg_core_set_num(core, TG_P_RELEASE, GetParam(kRelease)->Value());
  /* The engine compares before it recomputes its weight table, so writing
   * these every block costs two float compares rather than two passes over
   * the pattern. */
  tg_core_set_num(core, TG_P_FADE, GetParam(kFade)->Value() / 100.0);
  tg_core_set_num(core, TG_P_FADE_SOFT, GetParam(kFadeSoft)->Value());
  tg_core_set_num(core, TG_P_FADE_DIR, GetParam(kFadeDir)->Value());
  return moved;
}

#ifdef WEBVIEW_EDITOR_DELEGATE
/*
 * THE GATE ACROSS ONE CYCLE, RENDERED THROUGH A SCRATCH ENGINE.
 *
 * The editor used to MODEL this -- walk the pattern in JavaScript and rebuild
 * the envelope from the parameters -- and it got the case that matters wrong: a
 * release outliving its step was drawn as an instant cut at the step edge, and
 * the next step's attack started from silence instead of the tail it should
 * have continued from. Release runs to 200% of the gate's Width, so a release
 * that crosses an edge is not an edge case, it is most settings.
 *
 * This is the JUCE build's answer, and it is the right one: run the REAL patch
 * through a throwaway engine with a DC input, and the output samples ARE the
 * gate. "The only way for it to disagree with the audio is for the engine to
 * disagree with itself." There is no model left to be wrong.
 *
 * AMOUNT IS OVERRIDDEN TO 1, and that is two things at once.
 *
 *     m = 1 - amount*(1 - g) = floor + (1 - floor)*g,   floor = 1 - amount
 *
 * is AFFINE in g, so Amount is a transform the editor applies when it paints
 * and never a reason to re-render. It also steps around the `amount <= 0` short
 * circuit, which leaves the buffer untouched -- and would therefore store the
 * input DC, drawing a solid OPEN gate for what is in fact a true bypass.
 *
 * THE TIME BASE IS EXACT AND NEEDS NO SEARCH. The JUCE version hunted the rate
 * ladder for a (rate, bpm) pair whose step duration matched, because it had
 * only the duration. We can choose the scratch's SAMPLE RATE instead:
 * ms_per_step cancels the sample rate out -- recalc_ms_per_step says so in as
 * many words -- so loading the patch, reading the duration back and then
 * setting the rate to perStep*1000/msStep makes a step exactly perStep samples
 * at 120 BPM. Simpler than the original, and not an approximation.
 *
 * Two cycles are rendered and the first discarded, so step 0's carry-in comes
 * from the last step rather than from silence -- the warm-up the envelope
 * oracle uses, and the reason a pattern whose last step bleeds into its first
 * draws correctly.
 */
void TranceGate::RenderGate(const char* state)
{
  mGatePayload.clear();
  if (state == nullptr || *state == '\0') return;

  tg_core_t* sc = tg_core_create(44100.0);
  if (sc == nullptr) return;

  char buf[TG_STATE_MAX];
  tg_core_set_param(sc, "state", state);
  /* Length is the OPTION INDEX, as every setter here takes it. */
  int length = 16;
  if (tg_core_get_param(sc, "length", buf, int(sizeof buf)) > 0)
    length = std::atoi(buf) + 1;
  if (length < 1) length = 1;
  if (length > TG_MAX_STEPS) length = TG_MAX_STEPS;

  double msStep = 0.0;
  if (tg_core_get_param(sc, "ms_per_step", buf, int(sizeof buf)) > 0)
    msStep = std::atof(buf);
  if (!(msStep > 0.0)) { tg_core_destroy(sc); return; }

  const int perStep = tg::wire::gate_per_step(length);
  tg_core_set_sample_rate(sc, double(perStep) * 1000.0 / msStep);
  tg_core_set_param(sc, "amount", "1.0");

  const int frames = perStep * length;
  /* Its own buffers: the audio path's mL/mR belong to the audio thread, and
   * this runs on the message thread. Two cycles, DC at full scale. */
  std::vector<float> l(size_t(frames * 2), 1.0f);
  std::vector<float> r(size_t(frames * 2), 1.0f);

  tg_transport_t t;
  t.running = 1;
  t.bpm = 120.0f;
  t.beats = 0.0;
  const double sr = double(perStep) * 1000.0 / msStep;
  /* One block per step keeps the transport anchored where the boundaries are;
   * the engine's own PLL then has nothing to chase. */
  for (int i = 0; i < length * 2; i++)
  {
    const int off = i * perStep;
    t.beats = tg::wire::advance_beats(0.0, off, 120.0, sr);
    tg_core_process_f32_split(sc, l.data() + off, r.data() + off, perStep, &t);
  }

  /* The SECOND cycle: the first is the warm-up. */
  mGatePayload.reserve(size_t(frames) * 2 + 32);
  char head[64];
  std::snprintf(head, sizeof head, "%d:%d:", length, perStep);
  mGatePayload = head;
  static const char* kHex = "0123456789ABCDEF";
  for (int i = 0; i < frames; i++)
  {
    const unsigned char b = tg::wire::encode_gain(l[size_t(frames + i)]);
    mGatePayload.push_back(kHex[(b >> 4) & 0xF]);
    mGatePayload.push_back(kHex[b & 0xF]);
  }
  tg_core_destroy(sc);
}
#endif

void TranceGate::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nOut = NOutChansConnected();
  if (!mShell || nFrames <= 0 || nOut <= 0) return;

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

  /* The host may hand over a longer block than it announced. Growing the
   * vectors here would allocate on the audio thread, so the block is
   * processed in chunks of what was reserved instead. */
  const int cap = int(std::min(mL.size(), mR.size()));
  if (cap <= 0) return;

  /* Every edit posted since the last block lands here, before the host's
   * parameters are pushed over it. */
  tg_core_t* core = tg_shell_begin(mShell);
  const bool slotMoved = PushParams(core);

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

    /* The dry signal, kept before the engine overwrites it in place. The
   * capture rolls on wall time now, so the block needs no phase either side
   * of it -- a sample's column is simply where the clock had got to. */
  if ((int) mDry.size() >= n)
    std::memcpy(mDry.data(), mL.data(), sizeof(float) * size_t(n));

    /*
     * THE PHASE EITHER SIDE OF THE BLOCK, because the sweep needs to know where
     * in the pattern each sample fell. Cheap on purpose -- tg_core_phase01 is
     * arithmetic, where the formatted readout's snprintf has no business on this
     * thread.
     */
    const double ph0 = tg_core_phase01(core);
    tg_core_process_f32_split(core, mL.data(), mR.data(), n, &t);
    const double ph1 = tg_core_phase01(core);

    if ((int) mDry.size() >= n)
      CaptureBlock(mDry.data(), mL.data(), n, ph0, ph1, t.running != 0);

    for (int i = 0; i < n; i++)
    {
      outputs[0][off + i] = sample(mL[size_t(i)]);
      if (stereo) outputs[1][off + i] = sample(mR[size_t(i)]);
    }

    /* Each chunk is a continuation of the same block, so the transport has to
     * advance with it or the engine would gate the whole block on one
     * instant. Beats per sample = bpm / 60 / sampleRate. */
    if (t.running)
      t.beats = tg::wire::advance_beats(t.beats, n, double(t.bpm),
                                        GetSampleRate());
  }

  /*
   * A SLOT SWITCH IS PUBLISHED BEFORE IT IS ANNOUNCED. SyncSlotParams reads the
   * new slot's length from the published readout the moment it sees the flag,
   * so the flag goes up only once a frame with that slot in it is out -- or it
   * would read the slot being left and write that length onto the new one,
   * which is the bug the handshake exists to prevent.
   */
  if (slotMoved) tg_shell_touch(mShell);
  tg_shell_end(mShell, nFrames);
  if (slotMoved) mSlotSync.store(1, std::memory_order_release);
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
 * duration, whether the playhead is advancing, the cursor, the per-step depths
 * and their arrival order -- and `params` is the fourteen automatable values
 * plus width_ms at nine significant digits. Both exist precisely so a shell
 * does not take one lock per fact, thirty times a second.
 */
/*
 * One message per kick, and only when there has been one. The idiom, and why the
 * count is compared with != rather than >, is in ground_detect.h.
 */
void TranceGate::SendGround()
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

void TranceGate::OnIdle()
{
  if (!mShell) return;

  /* Before the readouts below, so the `params` push carries the length the
   * parameter has just been given rather than the one it is replacing. */
  SyncSlotParams();

  char buf[TG_STATE_MAX];
  if (tg_shell_read(mShell, "ui", buf, int(sizeof buf)) > 0)
  {
    SendArbitraryMsgFromDelegate(kMsgUiState, int(strlen(buf)), buf);

    /*
     * THE SWEEP'S LENGTH, MEASURED HERE AND HANDED OVER AS ONE DOUBLE.
     *
     * One cycle is the step duration times the length, and both are fields of
     * the readout that has just been formatted -- so this costs a pair of
     * atof()s on a string we already have, on the thread that can afford them.
     * The audio thread only ever reads the number. Doing it the other way round
     * would put an snprintf on the audio callback to recover two values the
     * engine had already computed.
     */
    /* steps : ties : LENGTH : phase : MS_STEP : advancing : cursor : depths
     *   0       1       2        3       4                                     */
    const char* f = buf;
    double len = 16.0, msStep = 0.0;
    for (int i = 0; i <= 4 && f; i++)
    {
      if (i == 2) len = atof(f);
      else if (i == 4) msStep = atof(f);
      const char* colon = strchr(f, ':');
      f = colon ? colon + 1 : nullptr;
    }
    if (!(len >= 1.0)) len = 1.0;
    const double cycleMs = msStep * len;
    mScopeCycleMs = (cycleMs > 1.0) ? cycleMs : kScopeFallbackMs;
    mCycleSamples.store(GetSampleRate() * (mScopeCycleMs / 1000.0),
                        std::memory_order_relaxed);
  }
  /*
   * THE GATE CURVE, RENDERED ONLY WHEN THE PATCH MOVES.
   *
   * The state string is the whole patch, so comparing it is comparing every
   * input the render has. Amount is in it, so an Amount drag re-renders --
   * which is harmless: a render is ~1024 samples through the engine, and the
   * alternative is parsing the blob to exclude one field.
   */
  if (tg_shell_read(mShell, "state", buf, int(sizeof buf)) > 0)
  {
    if (mGateState != buf)
    {
      mGateState = buf;
      RenderGate(buf);
      /* ONLY WHEN IT MOVES. The curve is ~2 KB of hex and the patch is still
       * between two keystrokes for most of a session, so pushing it every tick
       * would double the editor's traffic to say nothing. An editor that opens
       * later gets it from SendFullState, which is what the kMsgReady handshake
       * is for. */
      if (!mGatePayload.empty())
        SendArbitraryMsgFromDelegate(kMsgGate, int(mGatePayload.size()),
                                     mGatePayload.c_str());
    }
  }

  if (tg_shell_read(mShell, "params", buf, int(sizeof buf)) > 0)
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
  {
    /*
     * THE WHOLE SWEEP, EVERY FRAME, IN PLACE.
     *
     * NOT ROTATED. Column k is pattern phase k / kScopeCols and the reader draws
     * it at that x, which is the whole point of the axis standing still. `head`
     * is where the trace is being written -- a mark, not an origin -- so the
     * picture reads as filling left to right and the reader can show the fill
     * point.
     */
    const int head = mCap.head.load(std::memory_order_acquire);
    static const char* kHex = "0123456789ABCDEF";
    /* 4 hex pairs per column, plus the head, the cycle length and separators. */
    char scope[kScopeCols * 8 + 48];
    int n = snprintf(scope, sizeof scope, "%d:%d:%d:", kScopeCols,
                     int(mScopeCycleMs), head);

    auto put = [&](float v) {
      const int b = tg::wire::encode_sample(v);
      scope[n++] = kHex[(b >> 4) & 0xF];
      scope[n++] = kHex[b & 0xF];
    };

    for (int i = 0; i < kScopeCols; i++)
    {
      put(mCap.dryLo[i].load(std::memory_order_relaxed));
      put(mCap.dryHi[i].load(std::memory_order_relaxed));
      put(mCap.wetLo[i].load(std::memory_order_relaxed));
      put(mCap.wetHi[i].load(std::memory_order_relaxed));
    }
    scope[n] = '\0';

    /* The guard the old code lacked. Base64 costs a third on top, and the
     * transport truncates rather than fails -- so a payload that outgrows the
     * cap would go back to losing its tail in silence. The worst case is the
     * buffer's own size, so it is checked where it cannot be compiled out. */
    static_assert(tg::wire::framed_size(int(sizeof scope)) < kMaxJSString,
                  "the scope push no longer fits the WebView's string cap");
    SendArbitraryMsgFromDelegate(kMsgScope, n, scope);
  }

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* One ring per kick the detector found since the last tick. */
  SendGround();
#endif
}

/*
 * Pad edits, and the patch. None of these is a host parameter -- the pattern
 * is 128 steps across 8 slots and exposing it would be 1024 of them -- so they
 * arrive here and are posted to the engine, which applies them at the top of
 * its next block. Patch.cpp turns each into the engine's keys.
 */
bool TranceGate::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  if (!mShell) return false;
  std::string arg(static_cast<const char*>(pData), size_t(dataSize > 0 ? dataSize : 0));

  using tg::patch::Edit;
  switch (msgTag)
  {
    case kMsgSetCursor:  tg::patch::Post(mShell, Edit::Cursor, arg);    return true;
    case kMsgSetStep:    tg::patch::Post(mShell, Edit::Step, arg);      return true;
    case kMsgSetDepth:   tg::patch::Post(mShell, Edit::Depth, arg);     return true;
    case kMsgSetOrder:   tg::patch::Post(mShell, Edit::Order, arg);     return true;
    case kMsgRandomize:  tg::patch::Post(mShell, Edit::Randomize, arg); return true;
    case kMsgPatch:      tg::patch::Post(mShell, Edit::Paste, arg);     return true;

    /*
     * TYPING IN A READOUT. The UI holds normalised values and no units, so it
     * cannot parse "40 ms" -- the plugin owns the format in both directions
     * and is the only side that can. StringToValue is the same parser the
     * host uses for a typed automation value.
     */
    case kMsgSetText:
    {
      std::string idxText, valText;
      if (!tg::wire::split_pair(arg, idxText, valText)) return true;
      const int idx = std::atoi(idxText.c_str());
      if (idx < 0 || idx >= kNumParams) return true;
      const double v = GetParam(idx)->StringToValue(valText.c_str());
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
      const int h = tg::wire::clamp_editor_height(std::atoi(arg.c_str()));
      if (h && h != GetEditorHeight())
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
      if (tg_shell_read(mShell, "state", blob, int(sizeof blob)) > 0)
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

  /*
   * AND THE GATE CURVE, because OnIdle only pushes it when the patch moves.
   *
   * Without this an editor opened on a patch nobody then touches would draw an
   * empty Pattern tab until the first edit -- which looks exactly like the plot
   * being broken. Rendered here if the cache is cold, because there is no
   * guarantee an idle tick has run before the editor asks.
   */
  if (mShell)
  {
    char buf[TG_STATE_MAX];
    if (tg_shell_read(mShell, "state", buf, int(sizeof buf)) > 0)
    {
      if (mGatePayload.empty() || mGateState != buf)
      {
        mGateState = buf;
        RenderGate(buf);
      }
      if (!mGatePayload.empty())
        SendArbitraryMsgFromDelegate(kMsgGate, int(mGatePayload.size()),
                                     mGatePayload.c_str());
    }
  }
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
  if (tg_shell_read(mShell, "length", buf, int(sizeof buf)) > 0)
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
                              double ph0, double ph1, bool advancing)
{
  if (frames <= 0) return;

  /*
   * HOW FAR THE PATTERN MOVED ACROSS THIS BLOCK, unwrapped.
   *
   * A cycle boundary inside the block arrives as ph1 < ph0, which is a span of
   * span+1 and not of span-1; a SEEK arrives as a jump in either direction and
   * must not be drawn as a sweep across the whole axis. Half a cycle is the
   * dividing line, and past it the block is treated as a jump: the columns it
   * would have smeared across are left holding the audio they already have.
   */
  double span = ph1 - ph0;
  bool jumped = false;
  if (span < -0.5) span += 1.0;
  else if (span > 0.5) { span -= 1.0; jumped = true; }
  if (span < 0.0) jumped = true;

  /*
   * A STOPPED TRANSPORT HAS NO PHASE, so the sweep runs on its own at the same
   * length. Without this every sample lands in column 0 and the plot shows one
   * pixel of a signal that is passing through audibly -- the gate holds open
   * when the transport is stopped, so there is something real to look at.
   */
  double cyc = mCycleSamples.load(std::memory_order_relaxed);
  if (!(cyc > 0.0)) cyc = GetSampleRate() * (kScopeFallbackMs / 1000.0);
  if (!advancing || jumped)
  {
    ph0 = mSweep;
    span = double(frames) / cyc;
  }
  mSweep = ph0 + span;
  mSweep -= std::floor(mSweep);

  const auto flush = [&]() {
    if (mCapCount <= 0 || mCapCol < 0) return;
    mCap.dryLo[mCapCol].store(mCapDryLo, std::memory_order_relaxed);
    mCap.dryHi[mCapCol].store(mCapDryHi, std::memory_order_relaxed);
    mCap.wetLo[mCapCol].store(mCapWetLo, std::memory_order_relaxed);
    mCap.wetHi[mCapCol].store(mCapWetHi, std::memory_order_relaxed);
    /* Published AFTER the column is written, so a reader never sees the write
     * point pointing at a column that has not been filled in yet. */
    mCap.head.store(mCapCol, std::memory_order_release);
  };

  for (int i = 0; i < frames; i++)
  {
    double ph = ph0 + span * (double(i) / double(frames));
    ph -= std::floor(ph);
    int col = int(ph * double(kScopeCols));
    if (col < 0) col = 0;
    else if (col >= kScopeCols) col = kScopeCols - 1;

    if (col != mCapCol)
    {
      flush();
      mCapCol = col;
      mCapCount = 0;
    }

    if (mCapCount == 0)
    {
      mCapDryLo = mCapDryHi = dry[i];
      mCapWetLo = mCapWetHi = wet[i];
    }
    else
    {
      if (dry[i] < mCapDryLo) mCapDryLo = dry[i];
      if (dry[i] > mCapDryHi) mCapDryHi = dry[i];
      if (wet[i] < mCapWetLo) mCapWetLo = wet[i];
      if (wet[i] > mCapWetHi) mCapWetHi = wet[i];
    }
    mCapCount++;
  }

  /*
   * THE PARTIAL COLUMN IS PUBLISHED TOO, and that is the fix for the complaint
   * the wall-time rewrite was trying to answer.
   *
   * Publishing only completed columns meant the write point lagged by up to a
   * whole column, and -- far worse at sixteen steps -- that a level change was
   * invisible until the sweep had passed. Flushing here makes the newest column
   * current to within one buffer. It is overwritten, never blended, so there is
   * no state in this function that could behave like a filter.
   */
  flush();
}
