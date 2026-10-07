// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's fifteen host parameters, as the iPlug2 build declared them.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 *
 * THE SAME FIFTEEN, OR THE SETS DO NOT OPEN. A host stores a parameter by
 * its ID -- with JUCE_FORCE_USE_LEGACY_PARAM_IDS that is the index here, the
 * index iPlug2 used (plugins/trance-gate/Params.h) -- and a saved chunk holds
 * the plain values in this order. Names, ranges and defaults are iPlug2's
 * too: tests/fixtures/iplug2/NITranceGate/parameters.json is what the host
 * saw, and test/HostTest.cpp holds this build to it.
 *
 * Plain C++, so test/ChunkTest.cpp links it without JUCE.
 */
#pragma once

#include "trance_gate_core.h"

namespace ni::tg {

/* Deliberately tg_param_t's order: the host index IS the engine index. */
enum Param
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
  kFade,
  kFadeSoft,
  kFadeDir,
  kNumParams
};
static_assert(int(kNumParams) == int(TG_P_COUNT), "the host's parameters are the engine's, one for one");

/* iPlug2's parameter types, which decide how a value is stored and checked: a
 * stepped one (all but Percent) only ever holds a whole number. */
enum class Kind
{
  Int,
  Choice,
  Bool,
  Percent,
};

struct Spec
{
  const char* id;           /* the JUCE parameter ID; the VST3 ID is the index */
  const char* name;         /* iPlug2's name, which is what Live shows */
  Kind kind;
  double min, max, def;     /* plain values, in the parameter's own units */
  const char* label;        /* the VST3 units string ("steps" or nothing) */
  const char* const* texts; /* a Choice's or a Bool's labels; null for Rate,
                               whose labels are the engine's own table */
  int numTexts;
};

const Spec& SpecOf(int index);

inline bool Stepped(int index) { return SpecOf(index).kind != Kind::Percent; }

/* The plain values the defaults are, for a chunk too old to carry them all. */
double Default(int index);

/*
 * ONE CONVERSION EACH WAY between a host value and the engine's numeric wire
 * (tg_core_set_num), exactly as the iPlug2 build converts: Slot and Length
 * are one-based at the host, Amount, Width, Sustain and Fade percentages.
 */
double ToEngine(int index, double plain);
double FromEngine(int index, double engine);
/* Whether the host's value already says what `engine` says, as the engine
 * holds it -- a float. A recall only moves a parameter that differs. */
bool SameInEngine(int index, double plain, double engine);

} // namespace ni::tg
