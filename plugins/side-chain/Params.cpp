/*
 * NI Side-Chain -- the host parameters and the state chunk. See Params.h.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "Params.h"
#include "sc_core.h"
#include "shell_state.h"

using iplug::IParam;

namespace sc {
namespace params {

/*
 * A UNIT PASSED AS InitDouble's `label` NEVER REACHES AN AU HOST: iPlug2's AU
 * wrapper calls the no-label GetDisplay overload, so the unit silently vanishes
 * in Logic and appears everywhere else. An explicit DisplayFunc is the only
 * spelling that works in all four formats. The Trance Gate found this out.
 *
 * AND THEN NO LABEL AT ALL: the CLAP wrapper appends the label after the
 * display text, so a unit in both printed "35.0 % %". Said once, here.
 */
static const IParam::DisplayFunc kPctDisplay =
  [](double v, WDL_String& s) { s.SetFormatted(32, "%.1f %%", v); };
static const IParam::DisplayFunc kDbDisplay =
  [](double v, WDL_String& s) {
    /*
     * The bottom of the range means "anything triggers", and printing it as
     * "-60.0 dB" invites the reader to look for a quieter setting.
     *
     * "-inf dB", NOT "off". A host parses what it displays back through
     * StringToValue, which is atof: "off" read as 0 dB -- the top of the range,
     * the setting that almost never triggers. "-inf" is what strtod reads as
     * minus infinity, which clamps to this bottom; and it says the same thing,
     * a threshold nothing can fall below.
     */
    if (v <= -60.0) s.Set("-inf dB");
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

void Declare(const std::function<IParam*(int)>& param)
{
  param(kSource)->InitEnum("Source", 0, 3, "", 0, "",
                              "Cycle", "MIDI", "Sidechain");
  /* The rate labels are the engine's own table (`1/1` is in it, where the
   * Trance Gate's has none: a bar-long duck is the swell under a build). */
  IParam* rate = param(kRate);
  int nRates = 0;
  char label[MAX_PARAM_DISPLAY_LEN];
  while (sc_core_rate_label(nRates, label, int(sizeof label)) > 0)
    nRates++;
  rate->InitEnum("Rate", sc_core_rate_default(), nRates);
  for (int i = 0; i < nRates; i++)
    if (sc_core_rate_label(i, label, int(sizeof label)) > 0)
      rate->SetDisplayText(i, label);

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
  param(kTimeMode)->InitEnum("Time", 0, 2, "", 0, "", "ms", "% of cycle");

  const auto pct = [](IParam* p, const char* name, double def, double hi) {
    p->InitDouble(name, def, 0.0, hi, 0.01, "", 0, "",
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
  param(kDelay)->InitDouble("Delay", 0.0, -100.0, 100.0, 0.01, "", 0, "",
                               IParam::ShapeLinear(),
                               IParam::kUnitPercentage, kPctDisplay);
  /* The other three run to twice the cycle, which is as far as a stage can go
   * and still finish before the trigger after next. */
  pct(param(kAttack), "Attack", 2.0, 200.0);
  pct(param(kHold), "Hold", 8.0, 200.0);
  pct(param(kRelease), "Release", 35.0, 200.0);
  pct(param(kDepth), "Depth", 100.0, 100.0);

  /* THREE, NOT FOUR. `Pump` was an asymmetric curve ported from ducker.c --
   * linear down, cubic ease-out up. It is gone, and the direction argument that
   * existed only to serve it went with it. */
  param(kCurve)->InitEnum("Curve", 1, 3, "", 0, "",
                             "Linear", "Exponential", "S-Curve");

  /* Omni plus the sixteen channels. Declared as 17 enum values with the first
   * one named rather than as an int, so the host's own menu reads "Omni". */
  param(kChannel)->InitEnum("Channel", 1, 17);
  param(kChannel)->SetDisplayText(0, "Omni");
  for (int i = 1; i <= 16; i++)
  {
    WDL_String s;
    s.SetFormatted(8, "%d", i);
    param(kChannel)->SetDisplayText(i, s.Get());
  }

  /*
   * A NOTE IS AN ADDRESS, NOT A POSITION ON A RANGE.
   *
   * ducker.c:508-512 records what happens otherwise: as a 0..127 int the Move's
   * grid drew an arc knob, so the cell said nothing and you had to open the
   * value to learn which note it was. As an enum of names it draws "C1" and
   * steps a semitone at a time. The wire value is still the note number.
   */
  param(kNote)->InitEnum("Trigger", 36, 128);
  for (int i = 0; i < 128; i++)
  {
    WDL_String s;
    NoteName(i, s);
    param(kNote)->SetDisplayText(i, s.Get());
  }

  param(kMidiMode)->InitEnum("Mode", 0, 2, "", 0, "", "Trigger", "Gate");
  param(kVelSens)->InitDouble("Vel", 0.0, 0.0, 100.0, 0.01, "", 0, "",
                                 IParam::ShapeLinear(),
                                 IParam::kUnitPercentage, kPctDisplay);
  param(kThreshold)->InitDouble("Threshold", -24.0, -60.0, 0.0, 0.1, "", 0, "",
                                   IParam::ShapeLinear(),
                                   IParam::kUnitDB, kDbDisplay);
  param(kLockout)->InitDouble("Lockout", 20.0, 0.0, 200.0, 1.0, "", 0, "",
                                 IParam::ShapeLinear(),
                                 IParam::kUnitMilliseconds, kMsDisplay);
}

bool Save(iplug::IByteChunk& chunk, const PutParams& params)
{
  const int at = shell::state::Begin(chunk, kChunkVersion);
  return params(chunk) && shell::state::End(chunk, at);
}

int Load(const iplug::IByteChunk& chunk, int startPos,
         const GetParams& check, const GetParams& apply)
{
  const shell::state::Header h = shell::state::Read(chunk, startPos);
  if (h.body < 0) return -1;
  const int pos = check(chunk, h.body);
  if (pos < 0 || apply(chunk, h.body) != pos) return -1;
  return shell::state::Finish(h, pos);
}

} // namespace params
} // namespace sc
