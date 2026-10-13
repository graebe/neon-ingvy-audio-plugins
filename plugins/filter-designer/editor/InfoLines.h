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
inline constexpr InfoText response { "Response — magnitude in dB over log frequency; double-click adds one." };
inline constexpr InfoText analyzerMode { "Analyzer — the spectrum before (pre) and after (post) the filters." };
inline constexpr InfoText range { "Range — the magnitude axis: ±6, ±12, ±18 or ±30 dB." };
inline constexpr InfoText cell { "Filter — click to edit; Alt-click bypasses this filter." };
inline constexpr InfoText isoCell { "Isolator — click to edit; Alt-click switches the band split in or out." };

/* THE INSPECTOR, A FILTER. */
inline constexpr InfoText type { "Type — the response: pass, shelf, bell, notch, all-pass or tilt." };
inline constexpr InfoText family { "Family — the approximation: Butterworth, Bessel, Chebyshev, elliptic." };
inline constexpr InfoText order { "Order — the roll-off: 6 dB/oct per order, beyond the cutoff." };
inline constexpr InfoText channel { "Channel — filters L+R, L, R, mid (L+R)/2 or side (L-R)/2." };
inline constexpr InfoText freq { "Freq — the -3 dB cutoff, a bell's centre or a shelf's midpoint." };
inline constexpr InfoText gain { "Gain — the boost or cut at Freq, times Scale." };
inline constexpr InfoText q { "Q — centre frequency over the -3 dB bandwidth." };
inline constexpr InfoText passQ { "Q — of the highest-Q pole pair; 0.71 is the family's own response." };
inline constexpr InfoText ripple { "Ripple — passband ripple in dB (Chebyshev I, elliptic)." };
inline constexpr InfoText stopband { "Stopband — minimum stopband attenuation in dB (Chebyshev II, elliptic)." };
inline constexpr InfoText on { "On — the filter in the signal path, or bypassed." };

/* THE INSPECTOR, THE ISOLATOR. */
inline constexpr InfoText low { "Low — the level of the band below the low-mid crossover." };
inline constexpr InfoText mid { "Mid — the level of the band between the two crossovers." };
inline constexpr InfoText high { "High — the level of the band above the mid-high crossover." };
inline constexpr InfoText lower { "Low-Mid — the crossover frequency between the Low and Mid bands." };
inline constexpr InfoText upper { "Mid-High — the crossover frequency between the Mid and High bands." };
inline constexpr InfoText slope { "Slope — Linkwitz-Riley crossovers: LR4 24 dB/oct or LR8 48 dB/oct." };
inline constexpr InfoText kill { "Kill — mutes the band, with a 5 ms gain ramp." };
inline constexpr InfoText isoOn { "On — the three-band isolator in front of the filters." };

/* OUTPUT. */
inline constexpr InfoText output { "Output — the output gain in dB." };
inline constexpr InfoText scale { "Scale — multiplies every gain-type filter's gain; -100 % inverts it." };
inline constexpr InfoText adaptive { "Adaptive — proportional Q: a bell narrows as its gain grows." };
inline constexpr InfoText matched { "Matched — high-pass, low-pass: the analog magnitude up to Nyquist." };
inline constexpr InfoText bypass { "Bypass — the input to the output unprocessed, crossfaded in 10 ms." };

/* Every knob's readout: the engine reads what is typed, in its own units. */
inline constexpr InfoText hzValue { "Value — click to type a frequency, such as 440 or 2.5k." };
inline constexpr InfoText dbValue { "Value — click to type a level in dB, such as -6." };
inline constexpr InfoText plainValue { "Value — click to type a number; Enter sets it." };

/* The window's own. */
inline constexpr InfoText motion { "Motion — let the beat ripple the background; remembered here." };
inline constexpr InfoText signature { "Neon Ingvy — the publisher of this plugin." };

} // namespace ni::fd::info
