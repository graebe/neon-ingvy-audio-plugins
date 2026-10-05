/*
 * The Trance Gate's fifteen host parameters: their order, and how each is
 * declared.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Apart from the plugin class so a test can reach them: tests/cpp/tg_params.cpp
 * declares these on bare iplug::IParams -- the class the plugin uses -- and
 * checks every value survives value -> text -> value, the round trip a CLAP
 * host (and clap-validator's param-conversions) makes.
 */
#pragma once

#include "IPlugParameter.h"

#include <functional>
#include <string>

/*
 * THE FIFTEEN AUTOMATABLE VALUES, IN THE ENGINE'S OWN WIRE ORDER.
 *
 * Deliberately tg_param_t's order, so the host index IS the engine index and
 * there is no mapping table between them to get wrong.
 */
enum EParams
{
  kSlot = 0,
  kLength,
  kRate,
  kLegato,
  kTimeMode,
  kCurve,
  kAmount,
  kWidth,
  kAttack,
  kDecay,
  kSustain,
  kRelease,
  /* APPENDED, like the engine's own two. A host that catalogued this plugin
   * stored these indices, so the fade's pair can only go on the end -- not
   * beside kAmount, which is where they belong by meaning. */
  kFade,
  kFadeSoft,
  kFadeDir,
  kNumParams
};

namespace tg {
namespace params {

/* Declares every parameter on the IParam `param(i)` returns for index i. */
void Declare(const std::function<iplug::IParam*(int)>& param);

/*
 * ONE CONVERSION EACH WAY between a host value and the engine's numeric wire
 * (tg_core_set_num). Slot and Length are one-based at the host and option
 * indices in the engine -- "slot 1" is what a musician reads -- and Amount,
 * Width, Sustain and Fade are percentages at the host and 0..1 in the engine;
 * everything else is the same number. The host's values go in through
 * ToEngine every block, and come back through FromEngine when a slot switch
 * tells the host the new slot's values.
 */
double ToEngine(int paramIdx, double hostValue);
double FromEngine(int paramIdx, double engineValue);
/* Whether the host's value already says what `engineValue` says, as the
 * engine would hold it: a float. A recall only moves a parameter that is
 * actually different, so a host is not handed a change for a rounding. */
bool SameInEngine(int paramIdx, double hostValue, double engineValue);

/*
 * THE STAGES IN WHICHEVER UNIT Env Time ASKS FOR -- the editor's readout and
 * the text typed into it.
 *
 * A stage is stored as a percentage of the gate's open time; milliseconds are
 * a second reading of the same number, `pct / 100 * width_ms`. The editor used
 * to build "12.5 ms" itself and hand what was typed back to the percent
 * parser, so typing 12.5 into an ms readout set 12.5 %. The plugin owns both
 * directions now.
 *
 * The HOST's text stays in percent: iPlug2 parses a host's typed value with
 * IParam::StringToValue, which cannot be told about the unit, and a display it
 * cannot read back breaks every host's value -> text -> value round trip.
 */
bool IsStage(int paramIdx);
/* "12.5 ms" (one decimal) when `ms` and the width is known, else "12.50 %". */
std::string FormatStage(double pct, bool ms, double widthMs);
/* Typed text -> the stage's percent, clamped to its range. An explicit unit
 * wins ("40 ms", "25 %"); a bare number is read in the current mode. */
double ParseStage(const char* text, bool ms, double widthMs);

} // namespace params
} // namespace tg
