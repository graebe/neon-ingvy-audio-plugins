// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The words the Side-Chain window writes on the Move: what every control does,
 * the hint bar's conventions and the stages' names -- together, so they can be
 * read, reviewed and edited in one place, in one voice.
 *
 * THEY ARE THE NATIVE EDITOR'S (plugins/side-chain/editor/InfoLines.h and
 * Status.cpp), word for word, except where the Move makes one untrue:
 *
 *   - Source names two sources, and its remedy for Live's MIDI routing does
 *     not apply to a module on the device's own chain;
 *   - the well holds no audio here (the Move streams none to the page), so it
 *     says what it does show rather than "the duck over the audio it shaped"
 *     and "double-click for full scale", which belonged to the audio's level;
 *   - Threshold and Lockout are absent, with the detector they tuned;
 *   - the signature names a module, not a plugin.
 *
 * Each info line is "Name — what it does", the name as the control is
 * labelled, at most 72 characters (test/status.test.mjs holds them to it, as
 * the native InfoText does at compile time).
 */

export const INFO = {
  /* The plot, and its four handles. Resetting is the conventions'. */
  plot: 'Shape — the duck across one cycle; drag a handle to change it.',
  handleStart: 'Delay — drag sideways for when the duck starts.',
  handleBottom: 'Attack and depth — drag sideways for how fast, up or down for how far.',
  handleHoldEnd: 'Hold — drag sideways for how long the duck stays down.',
  handleEnd: 'Release — drag sideways for how long it takes to come back.',

  /* The shape's knobs: the same parameters as the handles, as numbers. */
  depth: 'Depth — how far the signal is pushed down; 100 % is silence.',
  delay: 'Delay — when the duck starts; below 0 on Cycle, it starts early.',
  attack: 'Attack — how fast the duck reaches the bottom, in ms or % of the cycle.',
  hold: 'Hold — how long the duck stays at the bottom, in ms or % of the cycle.',
  release: 'Release — how long the signal takes to come back, in ms or %.',

  /* The trigger's, shown only for the source they belong to. */
  note: 'Note — the MIDI note that fires the duck; C1 is 36, as on the pads.',
  channel: 'Channel — the MIDI channel the note must arrive on; Omni takes any.',
  velSens: 'Velocity — how much a note\'s velocity scales the depth.',

  /* The selects and the switches. */
  source: 'Source — what fires the duck: the tempo, or a MIDI note.',
  rate: 'Rate — how often the duck fires on Cycle, synced to the song tempo.',
  gate: 'Gate — keep the duck down until the note ends; off, Hold times out.',
  curve: 'Curve — the stages\' shape: Linear, Exponential or S-Curve.',
  percent: '% of cycle — read the stage times in % of the cycle; off, in ms.',

  /* Every knob's readout: what is typed is read in the parameter's units. */
  depthValue: 'Depth value — click to type a percentage; Enter sets it.',
  delayValue: 'Delay value — click to type ms or %; the unit you type wins.',
  attackValue: 'Attack value — click to type ms or %; the unit you type wins.',
  holdValue: 'Hold value — click to type ms or %; the unit you type wins.',
  releaseValue: 'Release value — click to type ms or %; the unit you type wins.',
  noteValue: 'Note value — click to type a note, such as C1 or F#2.',
  channelValue: 'Channel value — click to type a channel, 1 to 16, or Omni.',
  velSensValue: 'Velocity value — click to type a percentage; Enter sets it.',

  /* The window's own: the switch and the signature in the hint bar. */
  motion: 'Motion — let the beat ripple the background; remembered here.',
  signature: 'Neon Ingvy — the publisher of this module.',
};

/*
 * THE HINT'S THREE CONVENTIONS, and only conventions (the Hint card): the
 * layout canvas's words, as the native editor states them.
 */
export const CONVENTIONS = [
  ['drag', 'a handle to shape the duck'],
  ['shift', 'for fine'],
  ['double-click', 'to reset'],
];

/* 0 idle, 1 delay, 2 attack, 3 hold, 4 release -- sc-core's stage_index. */
export const STAGES = ['idle', 'delay', 'attack', 'hold', 'release'];
