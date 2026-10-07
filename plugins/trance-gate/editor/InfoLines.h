// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What every control in the Trance Gate does, in one line each.
 *
 * The hint bar shows one of these while the pointer is over its control or
 * the keyboard focus is visibly on it, and each is also the control's
 * accessible description (ni::ui::setInfo). They are all here so they can be
 * read, reviewed and edited in one place, beside each other, in one voice --
 * the web editor's lib/info.js, whose strings these are.
 *
 * THE FORM: "Name — what it does." The name is drawn in ink and the rest in
 * ink-muted; the bar splits at the " — ". At most 72 characters, which an
 * InfoText holds at compile time.
 *
 * WHERE ONE DIFFERS FROM THE WEB EDITOR, its control changed with Ultraviolet
 * 1.1.0 and the name follows the control: the two-option selects became
 * switches (Out, Time in %), the buttons are sentence case and verb first
 * (Set order, Shuffle order, Randomize), and the window verbs are icons whose
 * names are the Actions card's (Paste slot, Export slot, Export all): two
 * words at most, as the Button card asks of a button's text. The Motion switch's
 * line is the frame's, the same in every window: it rings on the beat on
 * every platform now, not only on a Mac.
 *
 * THE CONVENTIONS sit here too: the Step card's three, and the three Set
 * order swaps in for them while it is on -- the bar holds three at most, so
 * the mode replaces them rather than adding a fourth.
 */
#pragma once

#include "Info.h"

#include <vector>

namespace ni::tg::info
{

using ni::ui::InfoText;

/* The panels' titles, up their left edges. */
inline constexpr InfoText gatePanel { "Gate — when the steps fall and how much of each one sounds." };
inline constexpr InfoText envelopePanel { "Envelope — the shape every step is played with." };
inline constexpr InfoText fadePanel { "Fade — bring the pattern in a step at a time, in an order you set." };

/* GATE */
inline constexpr InfoText rate { "Rate — the length of one step, synced to the song tempo." };
inline constexpr InfoText length { "Length — 1 to 128 steps; holds on bar lengths, Page Up/Down jumps." };
inline constexpr InfoText amount { "Amount — how deep the gate cuts, as dry/wet; 0 % bypasses it." };
inline constexpr InfoText width { "Width — how much of each step stays open before it releases." };

/* ENVELOPE -- measured against the gate, in whichever unit Time asks for. */
inline constexpr InfoText attack { "Attack — how long a step takes to open, in ms or % of the gate." };
inline constexpr InfoText decay { "Decay — how long a step takes to fall from full to Sustain." };
inline constexpr InfoText sustain { "Sustain — the level a step holds after Decay, until it releases." };
inline constexpr InfoText release { "Release — how long a step takes to close once its Width ends." };

/* FADE */
inline constexpr InfoText fade { "Fade — how much of the pattern has arrived; automate it for a build-up." };
inline constexpr InfoText out { "Out — bring the holes in from a full gate; off brings your steps in." };
inline constexpr InfoText soft { "Soft — ramp each arriving step in on its own level instead of jumping." };
inline constexpr InfoText order { "Set order — tap steps in the order the fade brings them; press again." };
inline constexpr InfoText shuffle { "Shuffle order — deal a random arrival order; the pattern stays as it is." };

/* The settings row. */
inline constexpr InfoText slot { "Slot — pick one of 8 slots; each keeps its own pattern and sound." };
inline constexpr InfoText join { "Join Neighbors — run consecutive steps together instead of retriggering." };
inline constexpr InfoText curve { "Curve — the stages' shape: Linear, Exponential or S-Curve." };
inline constexpr InfoText time { "Time in % — read Attack, Decay and Release as % of the gate, not ms." };

/* The window's verbs, one joined column of icons right of the panels (the
 * Actions card, Verbs.h): each line names its button's full verb, which the
 * icon replaces. */
inline constexpr InfoText copy { "Copy slot — put this slot, pattern and sound, on the clipboard." };
inline constexpr InfoText paste { "Paste slot — replace this slot from the clipboard; a bank, all 8." };
inline constexpr InfoText exportSlot { "Export slot — save this slot to a .nitgslot file." };
inline constexpr InfoText exportAll { "Export all — save all 8 slots to one .nitgbank file." };
inline constexpr InfoText import { "Import — load a slot file into this slot, or a bank into all 8." };
inline constexpr InfoText randomize { "Randomize — fill this slot with a new pattern and arrival order." };

/* The ring, its count, and the plot under it. */
inline constexpr InfoText ring { "Ring — click a wedge to toggle its step, sweep to paint; arrows: Length." };
inline constexpr InfoText steps { "Steps — the pattern's Length; focus the ring and use the arrows." };
inline constexpr InfoText envelopePlot { "Envelope plot — one gate as the engine plays it, on a ms axis." };

/* The band: the two plots, one at a time, and the tabs over its right edge. */
inline constexpr InfoText patternTab { "Pattern — show one cycle of the gate as the engine renders it." };
inline constexpr InfoText signalTab { "Signal — show your audio before and after the gate, one cycle." };
inline constexpr InfoText patternPlot { "Pattern plot — one cycle of the gate, rendered by the engine." };
inline constexpr InfoText signalPlot { "Signal plot — the dry input in grey, the gated output in violet." };

/* The pads, one line for all of them -- and another while ORDER is on,
 * because a click there names an arrival instead of toggling. */
inline constexpr InfoText pads { "Pads — click toggles, shift-click ties, drag sets amount; Space, Alt+↑↓." };
inline constexpr InfoText padsOrder { "Pads — click the steps in the order the fade should bring them in." };
inline constexpr InfoText arrival { "Arrival — click to type a new place; a number in use swaps." };

/* The window's own: the signature in the hint bar. The Motion switch's line
 * is the frame's, the same in every editor (EditorFrame). */
inline constexpr InfoText signature { "Neon Ingvy — the publisher of this plugin." };

/* Every knob's readout: the plugin parses what is typed, in the readout's
 * units; the stages take either unit, and the one typed wins over Time. */
namespace readout
{
inline constexpr InfoText rate { "Rate value — click to type a division, such as 1/16 or 1/8T." };
inline constexpr InfoText length { "Length value — click to type one; Enter sets it, Escape cancels." };
inline constexpr InfoText amount { "Amount value — click to type one; Enter sets it, Escape cancels." };
inline constexpr InfoText width { "Width value — click to type one; Enter sets it, Escape cancels." };
inline constexpr InfoText attack { "Attack value — click to type ms or %; the unit you type wins." };
inline constexpr InfoText decay { "Decay value — click to type ms or %; the unit you type wins." };
inline constexpr InfoText sustain { "Sustain value — click to type one; Enter sets it, Escape cancels." };
inline constexpr InfoText release { "Release value — click to type ms or %; the unit you type wins." };
inline constexpr InfoText fade { "Fade value — click to type one; Enter sets it, Escape cancels." };
} // namespace readout

/* ------------------------------------------------------- conventions -- */

/* The window's three at rest: the Step card's, word for word. */
inline std::vector<ni::ui::Clause> conventions()
{
    return { { "click", "a step to toggle" },
             { "shift-click", "for a tie" },
             { "drag", "up or down for its amount" } };
}

/* While Set order is on: what a click names now, how to type one, how to
 * finish. `holes` under Fade Out, whose arrivals are the gaps. The web bar's
 * three said more ("... they should arrive", "-- they swap") and ran past the
 * Motion switch with "Set order" for "ORDER"; what a taken number does is the
 * arrival's own line now. */
inline std::vector<ni::ui::Clause> orderConventions (bool holes)
{
    return { { "click", juce::String ("the ") + (holes ? "gaps" : "steps") + " in arrival order" },
             { "a number", "to type one" },
             { "Set order", "again to finish" } };
}

} // namespace ni::tg::info
