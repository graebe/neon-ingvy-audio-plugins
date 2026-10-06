/*
 * The Trance Gate's fifteen host parameters. See Params.h.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 */
#include "Params.h"

namespace ni::tg {

namespace {

const char* const kOffOn[] = {"Off", "On"};
const char* const kEnvTime[] = {"ms", "%"};
const char* const kCurves[] = {"Linear", "Exponential", "S-Curve"};
const char* const kHardSoft[] = {"Hard", "Soft"};
const char* const kInOut[] = {"In", "Out"};

/* plugins/trance-gate/Params.cpp's Declare, row for row. */
const Spec kSpecs[kNumParams] = {
  {"slot", "Slot", Kind::Int, 1, TG_SLOTS, 1, "", nullptr, 0},
  {"length", "Length", Kind::Int, 1, TG_MAX_STEPS, 16, "steps", nullptr, 0},
  {"rate", "Rate", Kind::Choice, 0, TG_NUM_RATES - 1, TG_RATE_DEFAULT, "", nullptr, TG_NUM_RATES},
  {"legato", "Join Neighbors", Kind::Bool, 0, 1, 0, "", kOffOn, 2},
  {"timeMode", "Env Time", Kind::Choice, 0, 1, 0, "", kEnvTime, 2},
  {"curve", "Env Curve", Kind::Choice, 0, 2, 0, "", kCurves, 3},
  {"amount", "Amount", Kind::Percent, 0, 100, 100, "", nullptr, 0},
  {"width", "Width", Kind::Percent, 5, 100, 100, "", nullptr, 0},
  {"attack", "Attack", Kind::Percent, 0, TG_STAGE_MAX_PCT, 1.6, "", nullptr, 0},
  {"decay", "Decay", Kind::Percent, 0, TG_STAGE_MAX_PCT, 16, "", nullptr, 0},
  {"sustain", "Sustain", Kind::Percent, 0, 100, 100, "", nullptr, 0},
  {"release", "Release", Kind::Percent, 0, TG_STAGE_MAX_PCT, 16, "", nullptr, 0},
  /* 100 is the default and has to be: Fade is how much of the pattern has
   * arrived, so zero is silence. */
  {"fade", "Fade", Kind::Percent, 0, 100, 100, "", nullptr, 0},
  {"fadeSoft", "Fade Shape", Kind::Bool, 0, 1, 0, "", kHardSoft, 2},
  {"fadeDir", "Fade Dir", Kind::Choice, 0, 1, 0, "", kInOut, 2},
};

bool IsPercent(int index)
{
  return index == kAmount || index == kWidth || index == kSustain || index == kFade;
}

} // namespace

const Spec& SpecOf(int index)
{
  return kSpecs[index];
}

double Default(int index)
{
  return kSpecs[index].def;
}

double ToEngine(int index, double plain)
{
  if (index == kSlot || index == kLength)
    return plain - 1.0;
  return IsPercent(index) ? plain / 100.0 : plain;
}

double FromEngine(int index, double engine)
{
  if (index == kSlot || index == kLength)
    return engine + 1.0;
  return IsPercent(index) ? engine * 100.0 : engine;
}

/*
 * EXACTLY, AS FLOATS, and on purpose: the engine holds a float, and a recall
 * moves a host parameter only when the float it would hold differs. A
 * tolerance would leave a real difference unfollowed.
 */
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
bool SameInEngine(int index, double plain, double engine)
{
  return float(ToEngine(index, plain)) == float(engine);
}
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

} // namespace ni::tg
