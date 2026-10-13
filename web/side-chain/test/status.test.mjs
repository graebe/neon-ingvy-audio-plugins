// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the window says, case by case: status.js against the native editor's
 * Status.cpp, which it ports, and the words in text.js against their limits.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  CYCLE, MIDI, QUIET_AFTER_MS, TriggerWatch, warningFor, stateLine, captionFor, readsInMs,
  stageText, stageEdit, playheadOf, landmarkMs, stepFrom,
} from '../src/lib/status.js';
import { INFO, CONVENTIONS, STAGES } from '../src/lib/text.js';

test('the trigger goes quiet after most of a second without a fire', () => {
  const w = new TriggerWatch();
  assert.equal(w.isQuiet(10_000), false, 'nothing seen is no verdict');
  w.observe(3, 0);
  assert.equal(w.isQuiet(QUIET_AFTER_MS), false);
  assert.equal(w.isQuiet(QUIET_AFTER_MS + 1), true);
  w.observe(3, 2000);
  assert.equal(w.isQuiet(2000), true, 'the same count again is not a fire');
  w.observe(4, 2000);
  assert.equal(w.isQuiet(2500), false, 'a new count is');
});

test('the warnings the Move can give, and only when the trigger is quiet', () => {
  assert.equal(warningFor({ source: CYCLE, advancing: 0 }, false), '');
  assert.equal(warningFor({ source: CYCLE, advancing: 0 }, true), 'transport stopped');
  /* A running transport on a slow rate is quiet between fires, and fine. */
  assert.equal(warningFor({ source: CYCLE, advancing: 1 }, true), '');
  assert.equal(warningFor({ source: MIDI, advancing: 1 }, true), 'no midi');
  assert.equal(warningFor({ source: MIDI, advancing: 0 }, false), '');
  /* The plugin's Sidechain source cannot be reported by the Move. */
  assert.equal(warningFor({ source: 2, advancing: 0 }, true), '');
});

test('the state line: source, its rate on Cycle, and the stage', () => {
  assert.equal(stateLine('Cycle', '1/4', CYCLE, 4), 'Cycle 1/4 · release');
  assert.equal(stateLine('MIDI', '1/4', MIDI, 0), 'MIDI · idle');
  assert.equal(stateLine(undefined, undefined, 7, 9), ' · ');
  assert.deepEqual(STAGES, ['idle', 'delay', 'attack', 'hold', 'release']);
});

test('the caption names the span once a tempo has given it one', () => {
  assert.equal(captionFor(0), 'SHAPE   ONE CYCLE');
  assert.equal(captionFor(500), 'SHAPE   ONE CYCLE, 500 MS');
  assert.equal(captionFor(499.6, 224.6), 'SHAPE   ONE CYCLE, 500 MS   STAGES 225 MS');
  assert.equal(captionFor(500, 0), 'SHAPE   ONE CYCLE, 500 MS   STAGES 0 MS');
});

test('the stages read in ms on the first Time option, once a tempo is known', () => {
  assert.equal(readsInMs(0, 500), true);
  assert.equal(readsInMs(1, 500), false);
  assert.equal(readsInMs(0, 0), false, 'no cycle length, nothing to read ms in');
  assert.equal(stageText(39.6, true, '7.9 %'), '40 ms');
  assert.equal(stageText(39.6, false, '7.9 %'), '7.9 %');
});

test('typing a stage: the unit you type wins, a bare number is in the unit shown', () => {
  assert.deepEqual(stageEdit('40 ms', false, 500), { percent: 8 });
  assert.deepEqual(stageEdit('40', true, 500), { percent: 8 });
  assert.deepEqual(stageEdit('40MS', true, 500), { percent: 8 });
  assert.deepEqual(stageEdit('12,5 ms', true, 500), { percent: 2.5 });
  assert.deepEqual(stageEdit('20 %', true, 500), { text: '20 %' });
  assert.deepEqual(stageEdit(' 20 ', false, 500), { text: '20' });
  assert.equal(stageEdit('40 ms', true, 0), null, 'ms before the cycle has a length');
  assert.equal(stageEdit('fast', true, 500), null);
  assert.equal(stageEdit('', true, 500), null);
});

test('the playhead wraps on Cycle and stops at the end of a one-shot', () => {
  assert.equal(playheadOf(0.25, CYCLE), 0.25);
  assert.ok(Math.abs(playheadOf(1.25, CYCLE) - 0.25) < 1e-12);
  assert.equal(playheadOf(1.25, MIDI), 1);
  assert.equal(playheadOf(-1, MIDI), 0);
  assert.equal(playheadOf(NaN, CYCLE), 0);
});

test('the ruler\'s landmark is the floor, inside the cycle only', () => {
  assert.equal(landmarkMs(2, 500), 10);
  assert.equal(landmarkMs(0, 500), 0);
  assert.equal(landmarkMs(150, 500), 0);
});

test('a choice knob moves at least one option, as ParamChoiceKnob does', () => {
  /* Channel: 17 options, so an arrow's 1 % is a sixth of one. */
  const ch = (i) => i / 16;
  assert.equal(stepFrom(ch(1), ch(1) + 0.01, 17), ch(2));
  assert.equal(stepFrom(ch(1), ch(1) - 0.01, 17), ch(0));
  assert.equal(stepFrom(ch(16), ch(16) - 0.01, 17), ch(15));
  /* A page is a tenth of the range, in whole options. */
  assert.equal(stepFrom(ch(4), ch(4) + 0.1, 17), ch(6));
  /* The ends stay the ends. */
  assert.equal(stepFrom(ch(5), 0, 17), 0);
  assert.equal(stepFrom(ch(5), 1.2, 17), 1);
  /* No movement is no step. */
  assert.equal(stepFrom(ch(5), ch(5), 17), ch(5));
  /* Notes: an arrow is a semitone. */
  assert.equal(stepFrom(36 / 127, 36 / 127 + 0.01, 128), 37 / 127);
  /* A one-option list has nowhere to go. */
  assert.equal(stepFrom(0, 0.4, 1), 0.4);
});

test('every info line is "Name — what it does", within 72 characters', () => {
  for (const [name, line] of Object.entries(INFO)) {
    assert.ok([...line].length <= 72, `${name} is ${[...line].length} characters`);
    assert.ok(line.includes(' — '), `${name} has no " — "`);
  }
});

test('the hint bar states three conventions, as the native editor does', () => {
  assert.deepEqual(CONVENTIONS, [
    ['drag', 'a handle to shape the duck'], ['shift', 'for fine'], ['double-click', 'to reset'],
  ]);
});
