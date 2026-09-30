/*
 * Trance Gate -- the host parameters. See Params.h.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "Params.h"
#include "tg_shell.h"

using iplug::IParam;

namespace tg {
namespace params {

static constexpr double kStageMaxPct = TG_STAGE_MAX_PCT;

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
 * AND THEN NO LABEL AT ALL. The CLAP wrapper appends the label to whatever
 * the display function wrote, so "%" as a label as well printed "3.83 % %"
 * there -- the unit is said once, here, and reaches every format.
 *
 * Two decimals everywhere, for the same reason: the step decides the
 * precision, and 0.1 rendered Sustain as "60.0" against the engine's "60.00".
 */
static const IParam::DisplayFunc kPctDisplay =
  [](double v, WDL_String& s) { s.SetFormatted(32, "%.2f %%", v); };

void Declare(const std::function<IParam*(int)>& param)
{
  /*
   * Declared in the ENGINE's order, so the host index is the engine index.
   * Slot and Length are one-based at the host and zero-based in the engine --
   * "slot 1" is what a musician reads -- and converted once, in PushParams.
   */
  param(kSlot)->InitInt("Slot", 1, 1, TG_SLOTS);
  param(kLength)->InitInt("Length", 16, 1, TG_MAX_STEPS, "steps");
  /* The rate labels are the engine's own table: the host stores an INDEX, so
   * a list that disagreed by one entry would re-point every automation lane. */
  IParam* rate = param(kRate);
  rate->InitEnum("Rate", tg_core_rate_default(), TG_NUM_RATES);
  for (int i = 0; i < TG_NUM_RATES; i++)
  {
    char label[MAX_PARAM_DISPLAY_LEN];
    if (tg_core_rate_label(i, label, int(sizeof label)) > 0)
      rate->SetDisplayText(i, label);
  }
  /* "Off"/"On", capitalised: iPlug2 defaults to lower case and the engine
   * prints "Off". */
  param(kLegato)->InitBool("Join Neighbors", false, "", 0, "", "Off", "On");
  /* "%", NOT "% Step". The long form did not fit the readout's 64px and read
   * as noise beside a 13-character rate label; what the percentage is OF is
   * said once, by the control's own name, rather than in every value it can
   * show. The engine accepts either spelling. */
  param(kTimeMode)->InitEnum("Env Time", 0, {"ms", "%"});
  param(kCurve)->InitEnum("Env Curve", 0, {"Linear", "Exponential", "S-Curve"});

  /* Shown as percentages because that is what they are; the engine takes
   * Amount, Width and Sustain as 0..1 and the three envelope stages as the
   * percent value itself, so only the first three are scaled in PushParams. */
  const auto pct = [](IParam* p, const char* name, double def, double lo, double hi) {
    p->InitDouble(name, def, lo, hi, 0.01, "", 0, "",
                  IParam::ShapeLinear(), IParam::kUnitPercentage, kPctDisplay);
  };
  pct(param(kAmount), "Amount", 100.0, 0.0, 100.0);
  pct(param(kWidth), "Width", 100.0, 5.0, 100.0);
  pct(param(kAttack), "Attack", 1.6, 0.0, kStageMaxPct);
  pct(param(kDecay), "Decay", 16.0, 0.0, kStageMaxPct);
  pct(param(kSustain), "Sustain", 100.0, 0.0, 100.0);
  pct(param(kRelease), "Release", 16.0, 0.0, kStageMaxPct);
  /*
   * 100% IS THE DEFAULT AND IT HAS TO BE.
   *
   * Fade is how much of the pattern has arrived, so zero is silence -- which
   * is exactly what a build-up wants and exactly what a fresh instance must
   * not do. At 100 the engine's weights are all 1.0 and the gate is what it
   * was before this existed, which is what keeps both golden renders valid.
   */
  pct(param(kFade), "Fade", 100.0, 0.0, 100.0);
  /* "Hard"/"Soft" rather than Off/On: the switch does not turn the fade on, it
   * chooses whether a step arriving ramps or jumps. */
  param(kFadeSoft)->InitBool("Fade Shape", false, "", 0, "", "Hard", "Soft");
  /*
   * WHICH END THE PATTERN IS BUILT UP FROM, and In is the default because it is
   * what the gate did before the direction existed. The knob means the same
   * thing either way -- how much of the drawn pattern is present -- so 100% is
   * the pattern in both and switching this at rest changes nothing.
   */
  param(kFadeDir)->InitEnum("Fade Dir", 0, {"In", "Out"});
}

} // namespace params
} // namespace tg
