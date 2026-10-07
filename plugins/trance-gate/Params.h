// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's fifteen host parameters: their table, and the conversions
 * between a host value and the engine's.
 *
 * THE SAME FIFTEEN AS THE iPlug2 BUILD, OR ITS SETS DO NOT OPEN. A host keeps
 * a parameter by its ID -- with JUCE_FORCE_USE_LEGACY_PARAM_IDS that is the
 * index here, the index iPlug2 used -- and a saved chunk holds the plain
 * values in this order. Names, ranges, steps, defaults and units are that
 * build's too: tests/fixtures/iplug2/NITranceGate/parameters.json is what the
 * host saw, and the processor's tests hold this table to it.
 *
 * DELIBERATELY tg_param_t's ORDER: the host index is the engine index, so
 * there is no mapping table to get wrong. Fade and its two switches were
 * APPENDED -- a host that catalogued the plugin stored these indices -- which
 * is why they are not beside Amount.
 *
 * Plain C++, so the state's tests link it without JUCE.
 */
#pragma once

#include "Nist.h"
#include "ParamSpec.h"
#include "trance_gate_core.h"

#include <string>

namespace ni::tg
{

enum Param : int
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
static_assert ((int) kNumParams == (int) TG_P_COUNT, "the host's parameters are the engine's, one for one");

/* The table, by index. Rate's words are the engine's (tg_core_rate_label),
 * given at run time. */
const ParamSpec& specOf (int index);
const ParamSpec* specs();

/*
 * THE SAVED STATE: FORMAT.md's Trance Gate -- fifteen parameters and one
 * string, the engine's blob -- with headerless chunks from the builds that had
 * twelve (up to trance-gate v1.0.1) and fourteen (before Fade Dir).
 */
const nist::Layout& layout();

/*
 * ONE CONVERSION EACH WAY between a host value and the engine's numeric wire
 * (tg_core_set_num), exactly as the iPlug2 build converts: Slot and Length are
 * one-based at the host, Amount, Width, Sustain and Fade percentages.
 */
double toEngine (int index, double plain);
double fromEngine (int index, double engine);
/* Whether the host's value already says what `engine` says, as the engine
 * holds it -- a float. A slot switch only moves a parameter that differs. */
bool sameInEngine (int index, double plain, double engine);

/* A percentage as the host shows it, "100.00 %" (iPlug2's "%.2f %%"), with
 * '.' whatever the locale; and typed text read back, the unit optional. */
std::string percentText (double plain);
bool parsePercent (const std::string& text, double& plain);

/*
 * THE STAGES IN WHICHEVER UNIT Env Time ASKS FOR -- the editor's readout and
 * the text typed into it. A stage is a percentage of the gate's open time;
 * milliseconds are a second reading of the same number, pct / 100 * widthMs.
 * The HOST's text stays in percent: a display it cannot read back would break
 * every host's value -> text -> value round trip.
 */
bool isStage (int index);
/* "12.5 ms" (one decimal) when `ms` and the width is known, else "12.50 %". */
std::string formatStage (double pct, bool ms, double widthMs);
/* Typed text -> the stage's percent, clamped to its range. An explicit unit
 * wins ("40 ms", "25 %"); a bare number is read in the current mode. False
 * for text that is no number. */
bool parseStage (const std::string& text, bool ms, double widthMs, double& pct);

} // namespace ni::tg
