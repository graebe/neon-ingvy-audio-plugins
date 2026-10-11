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

/* THE PANELS: the three things a crusher does to a signal -- it makes the
 * amplitude discrete, it makes time discrete, and it is driven and mixed. */
inline constexpr InfoText amplitudePanel { "Amplitude Crush — amplitude discretization: how finely levels are kept." };
inline constexpr InfoText timePanel { "Time Crush — time discretization: how often a level is taken and held." };
inline constexpr InfoText levelPanel { "Level — how hard it is driven in, and what comes out." };
inline constexpr InfoText filterPanel { "Filter — a resonant low-pass on the crushed signal, before the Mix." };

/* THE PLOTS, one over each panel, and what holds them still. */
inline constexpr InfoText amplitudePlot { "Amplitude Crush — level in across, out up; lit dots: the signal now." };
inline constexpr InfoText timePlot { "Time Crush — what the hold takes in grey, the hold dim; a dot per take." };
inline constexpr InfoText filterPlot { "Filter — its response: grey is untouched; lit while the Filter is on." };
inline constexpr InfoText signalPlot { "Signal — the input in grey, the crushed output in front." };
inline constexpr InfoText refresh { "Refresh — how often the three pictures take a new look; 1/s is calm." };
inline constexpr InfoText pause { "Pause — hold the three pictures still; the sound goes on." };
inline constexpr InfoText resume { "Pause — let the three pictures move again." };

/* The accessible titles of the pause button. */
inline constexpr InfoText pauseTitle { "Pause the pictures" };
inline constexpr InfoText resumeTitle { "Resume the pictures" };

/* AMPLITUDE CRUSH. */
inline constexpr InfoText bits { "Bits — 2^Bits steps: 257 levels at 8 bits, 256 with Mid-Rise." };
inline constexpr InfoText shape { "Shape — from even steps to fine ones near silence, as µ-law has." };
inline constexpr InfoText dither { "Dither — noise of up to a step before the steps; quiet detail survives." };
inline constexpr InfoText midRise { "Mid-Rise — no step at zero, so silence becomes a half-step buzz." };
inline constexpr InfoText antiAlias { "Anti-Alias — exact, band-limited steps: the same at every sample rate." };

/* TIME CRUSH. */
inline constexpr InfoText rate { "Rate — how many times a second a level is taken and held." };
inline constexpr InfoText jitter { "Jitter — let the hold's clock wander; noisier, and wider in stereo." };
inline constexpr InfoText octave { "Octave — where the post-filter sits, in octaves from half the Rate." };
inline constexpr InfoText preFilter { "Pre — a filter before the hold: what would fold back, 60 dB down." };
inline constexpr InfoText postFilter { "Post — a filter after the hold, at Octave: smooth its steps." };

/* LEVEL. */
inline constexpr InfoText drive { "Drive — gain into the crusher; past full scale the top step clips." };
inline constexpr InfoText mix { "Mix — the crushed signal against the dry one; 100 % is all crushed." };
inline constexpr InfoText output { "Output — the level after the mix." };

/* FILTER. */
inline constexpr InfoText filter { "Filter — switch the low-pass in or out; off, the crush is unfiltered." };
inline constexpr InfoText cutoff { "Cutoff — where the filter starts to cut, 20 Hz to 20 kHz." };
inline constexpr InfoText peak { "Peak — a resonant rise at the cutoff, up to +24 dB; 0 is flat." };
inline constexpr InfoText slope { "Slope — how steeply it cuts above the cutoff: 12, 24 or 48 dB/octave." };
inline constexpr InfoText cutoffValue { "Cutoff value — click to type a frequency, such as 800 or 1.2k." };
inline constexpr InfoText slopeValue { "Slope value — turn for 12, 24 or 48 dB per octave." };

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
