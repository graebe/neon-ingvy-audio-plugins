// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The words a single filter's window writes: what every control does, the
 * picture and the hint bar's conventions -- together, so they can be read,
 * reviewed and edited in one place, in one voice, and in NI Filter
 * Designer's (plugins/filter-designer/editor/InfoLines.h), whose node each
 * product is.
 *
 * Each line is "Name — what it does", the name as the control is labelled, at
 * most 72 characters (an InfoText holds that at compile time), and checked
 * against what the engine does (engines/single-filter) and what the manuals
 * say (plugins/<product>/docs/live.md).
 */
#pragma once

#include "Info.h"

namespace ni::sf::info
{

using ni::ui::InfoText;

/* THE PICTURE. */
inline constexpr InfoText response { "Response — the filter's magnitude in dB over log frequency, with Output." };
inline constexpr InfoText phase { "Response — the all-pass's phase over log frequency, -180° to +180°." };
inline constexpr InfoText handle { "Handle — drag: Freq across, Gain or Q up and down; Shift for fine." };
inline constexpr InfoText analyzerMode { "Analyzer — the spectrum before (pre) and after (post) the filter." };
inline constexpr InfoText range { "Range — the magnitude axis: ±6, ±12, ±18 or ±30 dB." };

/* THE FILTER. */
inline constexpr InfoText family { "Family — the approximation: Butterworth, Bessel, Chebyshev, elliptic." };
inline constexpr InfoText slope { "Slope — the roll-off: 6 dB/oct per order, beyond the cutoff." };
inline constexpr InfoText order { "Order — the all-pass's sections: each turns the phase further." };
inline constexpr InfoText channel { "Channel — filters L+R, L, R, mid (L+R)/2 or side (L-R)/2." };
inline constexpr InfoText freq { "Freq — the -3 dB cutoff, a bell's centre or a shelf's midpoint." };
inline constexpr InfoText gain { "Gain — the boost or cut at Freq." };
inline constexpr InfoText q { "Q — centre frequency over the -3 dB bandwidth." };
inline constexpr InfoText passQ { "Q — of the highest-Q pole pair; 0.71 is the family's own response." };
inline constexpr InfoText shelfQ { "Q — the steepest section's; above 0.71 the shelf overshoots." };
inline constexpr InfoText allPassQ { "Q — the steepest section's: higher turns the phase faster at Freq." };
inline constexpr InfoText ripple { "Ripple — passband ripple in dB (Chebyshev I, elliptic)." };
inline constexpr InfoText stopband { "Stopband — minimum stopband attenuation in dB (Chebyshev II, elliptic)." };
inline constexpr InfoText output { "Output — the output gain in dB." };
inline constexpr InfoText adaptive { "Adaptive — proportional Q: the bell narrows as its gain grows." };
inline constexpr InfoText matched { "Matched — the analog magnitude up to Nyquist, not the bilinear one." };
inline constexpr InfoText bypass { "Bypass — the input to the output unprocessed, crossfaded in 10 ms." };

/* Every knob's readout: the engine reads what is typed, in its own units. */
inline constexpr InfoText hzValue { "Value — click to type a frequency, such as 440 or 2.5k." };
inline constexpr InfoText dbValue { "Value — click to type a level in dB, such as -6." };
inline constexpr InfoText plainValue { "Value — click to type a number; Enter sets it." };

/* The window's own. */
inline constexpr InfoText signature { "Neon Ingvy — the publisher of this plugin." };

} // namespace ni::sf::info
