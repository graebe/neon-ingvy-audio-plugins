// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The words NI Bit-Crusher's window writes: what every control does, the
 * panels and the plots, and the hint bar's conventions -- together, so they
 * can be read, reviewed and edited in one place, in one voice.
 *
 * Each line is "Name — what it does", the name as the control is labelled, at
 * most 72 characters (an InfoText holds that at compile time), and checked
 * against what the engine does (engines/bit-crusher/crates/bc-core) and what
 * the manual says (plugins/bit-crusher/docs/live.md): a wrong line is worse
 * than none.
 */
#pragma once

#include "Info.h"

namespace ni::bc::info
{

using ni::ui::InfoText;

/* THE PANELS: the three things a crusher does to a signal. */
inline constexpr InfoText stepsPanel { "Steps — how finely each level is kept: the amplitude made discrete." };
inline constexpr InfoText holdPanel { "Hold — how often a level is taken: time made discrete." };
inline constexpr InfoText levelPanel { "Level — how hard it is driven in, and what comes out." };

/* THE PLOTS. */
inline constexpr InfoText stepsPlot { "Steps — input across, output up, zoomed so steps show; grey: untouched." };
inline constexpr InfoText signalPlot { "Signal — the input in grey, the crushed output in front." };

/* STEPS. */
inline constexpr InfoText bits { "Bits — the number of steps, 2 to the power of Bits; fractions sweep." };
inline constexpr InfoText shape { "Shape — from even steps to fine ones near silence, as µ-law has." };
inline constexpr InfoText dither { "Dither — noise of up to a step before the steps; quiet detail survives." };
inline constexpr InfoText midRise { "Mid-Rise — no step at zero, so silence becomes a half-step buzz." };
inline constexpr InfoText antiAlias { "Anti-Alias — exact, band-limited steps: the same at every sample rate." };

/* HOLD. */
inline constexpr InfoText rate { "Rate — how many times a second a level is taken and held." };
inline constexpr InfoText jitter { "Jitter — let the hold's clock wander; noisier, and wider in stereo." };
inline constexpr InfoText octave { "Octave — where the post-filter sits, in octaves from half the Rate." };
inline constexpr InfoText preFilter { "Pre — a filter before the hold: what would fold back, 60 dB down." };
inline constexpr InfoText postFilter { "Post — a filter after the hold, at Octave: smooth its steps." };

/* LEVEL. */
inline constexpr InfoText drive { "Drive — gain into the crusher; past full scale the top step clips." };
inline constexpr InfoText mix { "Mix — the crushed signal against the dry one; 100 % is all crushed." };
inline constexpr InfoText output { "Output — the level after the mix." };

/* Every knob's readout: the engine reads what is typed, in its own units. */
inline constexpr InfoText bitsValue { "Bits value — click to type a bit depth, such as 7.5." };
inline constexpr InfoText percentValue { "Value — click to type a percentage; Enter sets it." };
inline constexpr InfoText rateValue { "Rate value — click to type a rate, such as 8000 or 8k." };
inline constexpr InfoText octaveValue { "Octave value — click to type octaves, such as -1 or 1.5." };
inline constexpr InfoText dbValue { "Value — click to type a level in dB, such as -6." };

/* The window's own: the switch and the signature in the hint bar. */
inline constexpr InfoText motion { "Motion — let the beat ripple the background; remembered here." };
inline constexpr InfoText signature { "Neon Ingvy — the publisher of this plugin." };

} // namespace ni::bc::info
