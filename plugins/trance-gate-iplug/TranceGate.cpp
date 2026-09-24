/*
 * Trance Gate -- the Ableton Live plugin, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "TranceGate.h"
#include "IPlug_include_in_plug_src.h"

#include <algorithm>
#include <cstring>

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
  GetParam(kLegato)->InitBool("Join Neighbors", false);
  GetParam(kTimeMode)->InitEnum("Env Time", 0, {"ms", "% Step"});
  GetParam(kCurve)->InitEnum("Env Curve", 0, {"Linear", "Exponential", "S-Curve"});

  /* Shown as percentages because that is what they are; the engine takes
   * Amount, Width and Sustain as 0..1 and the three envelope stages as the
   * percent value itself, so only the first three are scaled in PushParams. */
  GetParam(kAmount)->InitDouble("Amount", 100.0, 0.0, 100.0, 0.1, "%");
  GetParam(kWidth)->InitDouble("Width", 100.0, 5.0, 100.0, 0.1, "%");
  GetParam(kAttack)->InitDouble("Attack", 1.6, 0.0, kStageMaxPct, 0.01, "%");
  GetParam(kDecay)->InitDouble("Decay", 16.0, 0.0, kStageMaxPct, 0.01, "%");
  GetParam(kSustain)->InitDouble("Sustain", 100.0, 0.0, 100.0, 0.1, "%");
  GetParam(kRelease)->InitDouble("Release", 16.0, 0.0, kStageMaxPct, 0.01, "%");

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
  tg_core_set_num(mCore, TG_P_SLOT, GetParam(kSlot)->Value() - 1.0);
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

    tg_core_process_f32_split(mCore, mL.data(), mR.data(), n, &t);

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
