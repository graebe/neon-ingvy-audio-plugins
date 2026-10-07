// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The words the Side-Chain window writes: what every control does, the hint
 * bar's conventions, the header's state and its one warning, and the plot's
 * caption -- together, so they can be read, reviewed and edited in one place,
 * in one voice.
 *
 * THE INFO LINES are new with Ultraviolet 1.1.0: the web editor had none of
 * its own. Each is "Name — what it does", the name as the control is labelled,
 * at most 72 characters (an InfoText holds that at compile time), and checked
 * against what the control does -- Params.cpp, the engine, the manual
 * (plugins/side-chain/docs/live.md) -- rather than what it is called: a wrong
 * line is worse than none.
 *
 * THE REST IS THE WEB EDITOR'S, word for word -- the stage names are App.jsx's
 * STAGES, the warnings its warning() (the manual quotes them, so a change here
 * is a change there) -- but for the conventions, which are the layout
 * canvas's (Status.h), and the caption's span, which is the PlotWell card's.
 */
#pragma once

#include "Info.h"

namespace ni::sc::info
{

using ni::ui::InfoText;

/* THE PLOT: the well, and its four handles. Resetting is the conventions'
 * ("double-click to reset"), so no handle's line repeats it. */
inline constexpr InfoText plot { "Shape — the duck you asked for, over the audio it shaped." };
inline constexpr InfoText handleStart { "Delay — drag sideways for when the duck starts." };
inline constexpr InfoText handleBottom { "Attack and depth — drag sideways for how fast, up or down for how far." };
inline constexpr InfoText handleHoldEnd { "Hold — drag sideways for how long the duck stays down." };
inline constexpr InfoText handleEnd { "Release — drag sideways for how long it takes to come back." };

/* THE SHAPE'S KNOBS: the same parameters as the handles, as numbers. */
inline constexpr InfoText depth { "Depth — how far the signal is pushed down; 100 % is silence." };
inline constexpr InfoText delay { "Delay — when the duck starts; below 0 on Cycle, it starts early." };
inline constexpr InfoText attack { "Attack — how fast the duck reaches the bottom, in ms or % of the cycle." };
inline constexpr InfoText hold { "Hold — how long the duck stays at the bottom, in ms or % of the cycle." };
inline constexpr InfoText release { "Release — how long the signal takes to come back, in ms or %." };

/* THE TRIGGER'S, shown only for the source they belong to. Threshold, and
 * Source below, carry the remedy the web hint gave for each source's catch
 * (lib/text.js): Live routes no MIDI to an audio track, and a key is routed
 * in the device's sidechain section (docs/live.md). The header's warning
 * names the symptom; these say what to do about it. */
inline constexpr InfoText note { "Note — the MIDI note that fires the duck; C1 is 36, as on the pads." };
inline constexpr InfoText channel { "Channel — the MIDI channel the note must arrive on; Omni takes any." };
inline constexpr InfoText velSens { "Velocity — how much a note's velocity scales the depth." };
inline constexpr InfoText threshold { "Threshold — how loud the key must get; route it in Live's sidechain." };
inline constexpr InfoText lockout { "Lockout — how soon after one duck the key may fire the next." };

/* THE SELECTS AND THE SWITCHES. */
inline constexpr InfoText source { "Source — the tempo, a MIDI note or a key; MIDI needs a MIDI track." };
inline constexpr InfoText rate { "Rate — how often the duck fires on Cycle, synced to the song tempo." };
inline constexpr InfoText gate { "Gate — keep the duck down until the note ends; off, Hold times out." };
inline constexpr InfoText curve { "Curve — the stages' shape: Linear, Exponential or S-Curve." };
inline constexpr InfoText percent { "% of cycle — read the stage times in % of the cycle; off, in ms." };

/* Every knob's readout: the plugin reads what is typed, in its own units. */
inline constexpr InfoText depthValue { "Depth value — click to type a percentage; Enter sets it." };
inline constexpr InfoText delayValue { "Delay value — click to type ms or %; the unit you type wins." };
inline constexpr InfoText attackValue { "Attack value — click to type ms or %; the unit you type wins." };
inline constexpr InfoText holdValue { "Hold value — click to type ms or %; the unit you type wins." };
inline constexpr InfoText releaseValue { "Release value — click to type ms or %; the unit you type wins." };
inline constexpr InfoText noteValue { "Note value — click to type a note, such as C1 or F#2." };
inline constexpr InfoText channelValue { "Channel value — click to type a channel, 1 to 16, or Omni." };
inline constexpr InfoText velSensValue { "Velocity value — click to type a percentage; Enter sets it." };
inline constexpr InfoText thresholdValue { "Threshold value — click to type a level in dB, such as -24." };
inline constexpr InfoText lockoutValue { "Lockout value — click to type a time in ms; Enter sets it." };

/* The window's own: the switch and the signature in the hint bar. */
inline constexpr InfoText motion { "Motion — let the beat ripple the background; remembered here." };
inline constexpr InfoText signature { "Neon Ingvy — the publisher of this plugin." };

} // namespace ni::sc::info
