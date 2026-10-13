// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * EVERY CONTROL THE WINDOW SHOWS, as data: which parameter, how it is drawn,
 * and for which source. App.jsx renders from these tables and nothing else,
 * so test/bindings.test.mjs can hold every control that can appear to the
 * module's declaration -- a key it has, of the type the control needs -- and
 * every parameter the module declares to a control. A control bound to
 * nothing, or a parameter with no way to reach it, fails there rather than on
 * the device.
 *
 * THE NATIVE EDITOR'S CONTROLS (plugins/side-chain/editor/SideChainEditor.cpp),
 * in its order, with its labels and widths, less the two the Move has no
 * parameter for (Threshold, Lockout):
 *
 *   the knobs   Depth, Delay, Attack, Hold, Release, then MIDI's own --
 *               Note and Channel as stepped knobs (a Select holds twelve
 *               options at most, Select card) and Velocity
 *   the trigger Source, then Rate on Cycle or the Gate switch on MIDI
 *   the shape   Curve and the "% of cycle" switch
 *
 * `when` is the source a control belongs to; absent, it always shows.
 * `kind` on a knob: 'stage' reads in the unit Time asks for, 'choice' steps
 * through options, plain is the parameter's own text.
 */
import { P } from './msg.js';
import { CYCLE, MIDI } from './status.js';
import { INFO } from './text.js';

export const KNOBS = [
  { idx: P.depth, label: 'Depth', info: INFO.depth, readoutInfo: INFO.depthValue },
  /* Bipolar, as the plugin's: -20 % is an early duck, drawn left of centre. */
  { idx: P.delay, label: 'Delay', kind: 'stage', bipolar: true, info: INFO.delay, readoutInfo: INFO.delayValue },
  { idx: P.attack, label: 'Attack', kind: 'stage', info: INFO.attack, readoutInfo: INFO.attackValue },
  { idx: P.hold, label: 'Hold', kind: 'stage', info: INFO.hold, readoutInfo: INFO.holdValue },
  { idx: P.release, label: 'Release', kind: 'stage', info: INFO.release, readoutInfo: INFO.releaseValue },
  { idx: P.note, label: 'Note', kind: 'choice', when: MIDI, info: INFO.note, readoutInfo: INFO.noteValue },
  { idx: P.channel, label: 'Channel', kind: 'choice', when: MIDI, info: INFO.channel, readoutInfo: INFO.channelValue },
  { idx: P.velSens, label: 'Velocity', when: MIDI, info: INFO.velSens, readoutInfo: INFO.velSensValue },
];

/* Two rows split by subject -- the trigger, then the shape every source has
 * -- so the shape's controls do not move when the source changes. Widths are
 * the native editor's: the label column, then the field. */
export const TRIGGER_ROW = [
  { kind: 'select', idx: P.source, label: 'Source', labelWidth: 52, width: 128, info: INFO.source },
  { kind: 'select', idx: P.rate, label: 'Rate', labelWidth: 38, width: 84, when: CYCLE, info: INFO.rate },
  { kind: 'toggle', idx: P.midiMode, label: 'Gate', when: MIDI, info: INFO.gate },
];

export const SHAPE_ROW = [
  /* 136, which "Exponential" fits in the system's own face. */
  { kind: 'select', idx: P.curve, label: 'Curve', labelWidth: 42, width: 136, info: INFO.curve },
  { kind: 'toggle', idx: P.timeMode, label: '% of cycle', info: INFO.percent },
];

/* Whether a control shows for `source`. */
export const showsFor = (control, source) => control.when === undefined || control.when === source;

/*
 * The four handles in their order along the cycle: the parameter the sideways
 * drag moves, the stage of the shape that parameter is (shape.js), and on the
 * bottom corner the parameter up and down moves.
 */
export const HANDLES = [
  { key: 'start', name: 'Delay', xIdx: P.delay, stage: 'delay', info: INFO.handleStart },
  { key: 'bottom', name: 'Attack and depth', xIdx: P.attack, stage: 'attack', yIdx: P.depth,
    info: INFO.handleBottom },
  { key: 'holdEnd', name: 'Hold', xIdx: P.hold, stage: 'hold', info: INFO.handleHoldEnd },
  { key: 'end', name: 'Release', xIdx: P.release, stage: 'release', info: INFO.handleEnd },
];
