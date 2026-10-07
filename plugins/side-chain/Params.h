// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's fifteen host parameters: their table, their text, and the
 * conversion from a host value to the engine's.
 *
 * THE SAME FIFTEEN AS THE iPlug2 BUILD, OR ITS SETS DO NOT OPEN. A host keeps
 * a parameter by its ID -- with JUCE_FORCE_USE_LEGACY_PARAM_IDS that is the
 * index here, the index iPlug2 used -- and a saved chunk holds the plain
 * values in this order. Names, ranges, steps, defaults and texts are that
 * build's too: tests/fixtures/iplug2/NISideChain/parameters.json is what the
 * host saw, and the processor's tests hold this table to it.
 *
 * DELIBERATELY sc_param_t's ORDER: the host index is the engine index, so
 * there is no mapping table to get wrong.
 *
 * EVERYTHING THE SIDE-CHAIN HOLDS IS IN THIS LIST. The shape's handles are
 * these parameters, so dragging one lands in the host's undo history and its
 * automation lane, and the saved state is the fifteen values and nothing else
 * (FORMAT.md: no strings).
 *
 * Plain C++, so a test links it without JUCE.
 */
#pragma once

#include "Nist.h"
#include "ParamSpec.h"
#include "sc_core.h"

#include <string>

namespace ni::sc
{

enum Param : int
{
    kSource = 0,
    kRate,
    kTimeMode,
    kDelay,
    kAttack,
    kHold,
    kRelease,
    kDepth,
    kCurve,
    kChannel,
    kNote,
    kMidiMode,
    kVelSens,
    kThreshold,
    kLockout,
    kNumParams
};
static_assert ((int) kNumParams == (int) SC_P_COUNT, "the host's parameters are the engine's, one for one");

/* The rate table's length and the rate a fresh instance plays: the engine's
 * (sc_core_rate_label, sc_core_rate_default), which the tests hold these to.
 * A host stores Rate's INDEX, so the table is the engine's own. */
constexpr int numRates = 12;
constexpr int defaultRate = 4;

/* The table, by index. Rate's and Trigger's words are given at run time
 * (rateLabel, noteName). */
const ParamSpec& specOf (int index);
const ParamSpec* specs();

/* FORMAT.md's Side-Chain: fifteen parameters, no strings, and headerless
 * chunks that always had fifteen. */
const nist::Layout& layout();

/* A host value to the engine's numeric wire (sc_core_set_num), as the iPlug2
 * build converted: Depth and Vel are percentages at the host, 0..1 in the
 * engine; everything else is the same number. */
double toEngine (int index, double plain);

/* The engine's word for rate `index` ("1/4", "1/8T"), or its number. */
std::string rateLabel (int index);

/* A note's name in Live's octave numbering, where 36 is C1 and 60 is C3 --
 * what the pads say. */
std::string noteName (int note);

/*
 * A CONTINUOUS PARAMETER'S TEXT, as the iPlug2 build printed it, with '.'
 * whatever the locale: "35.0 %", "-24.0 dB", "20 ms". Threshold's bottom is
 * "-inf dB": it means "anything triggers", and a host reads the text back --
 * "off" would read as 0 dB, the setting that almost never triggers.
 */
std::string valueText (int index, double plain);

/* Typed text back to a plain value, the unit optional; "-inf" is Threshold's
 * bottom. False for text that is no number, which leaves the value alone. */
bool parseValue (int index, const std::string& text, double& plain);

} // namespace ni::sc
