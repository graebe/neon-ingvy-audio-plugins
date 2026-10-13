// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What every control in the editor does, in one line each.
 *
 * The hint bar shows one of these while the pointer is over its control or the
 * keyboard focus is on it, and each is also the control's accessible
 * description (the kit's `info`). They are all here so they can be read,
 * reviewed and edited in one place, beside each other, in one voice.
 *
 * THE FORM: "Name — what it does." The name is drawn in `ink` and the rest in
 * `ink-muted`, the hint bar's pattern; the kit splits at the " — ". At most 72
 * characters, as the native editor holds its own (InfoLines.h).
 *
 * THE NATIVE EDITOR'S WORDS, where the control is the same: the plugin's
 * editor in Live (plugins/trance-gate/editor/InfoLines.h) moved to Ultraviolet
 * 1.1.0 -- the two-option selects became switches (Out, Time in %), the
 * buttons are sentence case and verb first (Set order, Shuffle order,
 * Randomize) -- and this window follows it. What is the Move's own is said so:
 * the motion is the playhead's, and the publisher's work here is a module.
 */

/* A knob's readout: the module parses what is typed, in the readout's units. */
const typed = (name) => `${name} value — click to type one; Enter sets it, Escape cancels.`;
/* The stages accept either unit, and the one typed wins over Time. */
const typedStage = (name) => `${name} value — click to type ms or %; the unit you type wins.`;

export const INFO = {
  /* The panels' titles, up their left edges. */
  gatePanel: 'Gate — when the steps fall and how much of each one sounds.',
  envelopePanel: 'Envelope — the shape every step is played with.',
  fadePanel: 'Fade — bring the pattern in a step at a time, in an order you set.',

  /* GATE */
  rate: 'Rate — the length of one step, synced to the song tempo.',
  length: 'Length — 1 to 128 steps; holds on bar lengths, Page Up/Down jumps.',
  amount: 'Amount — how deep the gate cuts, as dry/wet; 0 % bypasses it.',
  width: 'Width — how much of each step stays open before it releases.',

  /* ENVELOPE -- measured against the gate, in whichever unit Time asks for. */
  attack: 'Attack — how long a step takes to open, in ms or % of the gate.',
  decay: 'Decay — how long a step takes to fall from full to Sustain.',
  sustain: 'Sustain — the level a step holds after Decay, until it releases.',
  release: 'Release — how long a step takes to close once its Width ends.',

  /* FADE */
  fade: 'Fade — how much of the pattern has arrived; automate it for a build-up.',
  out: 'Out — bring the holes in from a full gate; off brings your steps in.',
  soft: 'Soft — ramp each arriving step in on its own level instead of jumping.',
  order: 'Set order — tap steps in the order the fade brings them; press again.',
  shuffle: 'Shuffle order — deal a random arrival order; the pattern stays as it is.',

  /* The settings row. */
  slot: 'Slot — pick one of 8 slots; each keeps its own pattern and sound.',
  join: 'Join Neighbors — run consecutive steps together instead of retriggering.',
  curve: 'Curve — the stages\' shape: Linear, Exponential or S-Curve.',
  time: 'Time in % — read Attack, Decay and Release as % of the gate, not ms.',
  randomize: 'Randomize — fill this slot with a new pattern and arrival order.',

  /* The ring, its count, and the plot under it. */
  ring: 'Ring — click a wedge to toggle its step, sweep to paint; arrows: Length.',
  steps: 'Steps — the pattern\'s Length; focus the ring and use the arrows.',
  envelopePlot: 'Envelope plot — one gate as the engine plays it, on a ms axis.',

  /* The band. */
  patternPlot: 'Pattern plot — one cycle of the gate, rendered by the engine.',

  /* The pads, one string for all of them -- and another while Set order is
   * on, because a click there names an arrival instead of toggling. */
  pads: 'Pads — click toggles, shift-click ties, drag sets amount; Space, Alt+↑↓.',
  padsOrder: 'Pads — click the steps in the order the fade should bring them in.',
  arrival: 'Arrival — click to type a new place; a number in use swaps.',

  /* The window's own: the switch and the signature in the hint bar. */
  motion: 'Motion — let the playhead ripple the background; kept in this browser.',
  signature: 'Neon Ingvy — the publisher of this module.',

  /* Every knob's readout. */
  readout: {
    rate: 'Rate value — click to type a division, such as 1/16 or 1/8T.',
    length: typed('Length'),
    amount: typed('Amount'),
    width: typed('Width'),
    attack: typedStage('Attack'),
    decay: typedStage('Decay'),
    sustain: typed('Sustain'),
    release: typedStage('Release'),
    fade: typed('Fade'),
  },
};
