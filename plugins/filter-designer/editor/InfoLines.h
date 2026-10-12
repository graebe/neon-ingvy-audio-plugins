// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The words NI Filter Designer's window writes: what every control does, the
 * picture, the panels and the hint bar's conventions -- together, so they can
 * be read, reviewed and edited in one place, in one voice.
 *
 * Each line is "Name — what it does", the name as the control is labelled, at
 * most 72 characters (an InfoText holds that at compile time), and checked
 * against what the engine does (engines/filter-designer) and what the manual
 * says (plugins/filter-designer/docs/live.md).
 */
#pragma once

#include "Info.h"

namespace ni::fd::info
{

using ni::ui::InfoText;

/* THE PICTURE AND THE ROW. */
inline constexpr InfoText response { "Response — double-click to add a filter; drag one, Alt for its Q." };
inline constexpr InfoText analyzerMode { "Analyzer — what is drawn behind the curve: in, out, both or none." };
inline constexpr InfoText range { "Range — how many dB the picture shows above and below 0." };
inline constexpr InfoText cell { "Filter — click to edit it; Alt-click switches it off and on." };
inline constexpr InfoText isoCell { "Isolator — click to edit it; Alt-click switches it off and on." };

/* THE INSPECTOR, A FILTER. */
inline constexpr InfoText type { "Type — what the filter does; Empty removes it." };
inline constexpr InfoText family { "Family — the cut's character: flat, smooth, steep or rippling." };
inline constexpr InfoText order { "Order — how steep: 6 dB an octave for each step." };
inline constexpr InfoText channel { "Channel — both sides, one, or only the middle or the sides." };
inline constexpr InfoText freq { "Freq — where it works; a cut's -3 dB point, a bell's centre." };
inline constexpr InfoText gain { "Gain — how far up or down, times Scale." };
inline constexpr InfoText q { "Q — how narrow: high is a sharp peak or notch." };
inline constexpr InfoText resonance { "Resonance — a peak at the cut's frequency; 0.71 has none." };
inline constexpr InfoText ripple { "Ripple — the passband's waves, traded for a steeper knee." };
inline constexpr InfoText stopband { "Stopband — how far down the stopband stays." };
inline constexpr InfoText on { "On — the filter heard or bypassed; its place stays." };

/* THE INSPECTOR, THE ISOLATOR. */
inline constexpr InfoText low { "Low — the band below Lower; at the bottom it is gone." };
inline constexpr InfoText mid { "Mid — the band between Lower and Upper." };
inline constexpr InfoText high { "High — the band above Upper." };
inline constexpr InfoText lower { "Lower — the crossover between Low and Mid." };
inline constexpr InfoText upper { "Upper — the crossover between Mid and High." };
inline constexpr InfoText slope { "Slope — the crossovers' steepness: 24 or 48 dB an octave." };
inline constexpr InfoText kill { "Kill — the band out, at once and without a click." };
inline constexpr InfoText isoOn { "On — the isolator in front of the filters, or out of the way." };

/* OUTPUT. */
inline constexpr InfoText output { "Output — the level after everything." };
inline constexpr InfoText scale { "Scale — every filter's gain at once; -100 % turns them over." };
inline constexpr InfoText adaptive { "Adaptive — a bell narrows as it boosts or cuts." };

/* Every knob's readout: the engine reads what is typed, in its own units. */
inline constexpr InfoText hzValue { "Value — click to type a frequency, such as 440 or 2.5k." };
inline constexpr InfoText dbValue { "Value — click to type a level in dB, such as -6." };
inline constexpr InfoText plainValue { "Value — click to type a number; Enter sets it." };

/* The window's own. */
inline constexpr InfoText motion { "Motion — let the beat ripple the background; remembered here." };
inline constexpr InfoText signature { "Neon Ingvy — the publisher of this plugin." };

} // namespace ni::fd::info
