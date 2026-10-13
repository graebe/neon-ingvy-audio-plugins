// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Attack, Decay and Release as text (src/lib/stages.js), held to what the
 * plugin's formatStage and parseStage do in Live (plugins/trance-gate/
 * Params.cpp): the same number reads and types the same on both.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { formatStage, parseStage, withStages, STAGE_MAX_PCT } from '../src/lib/stages.js';

test('a stage reads in ms against the gate, or in % of it', () => {
  assert.equal(formatStage(1.6, true, 93.75), '1.5 ms');
  assert.equal(formatStage(16, true, 93.75), '15.0 ms');
  assert.equal(formatStage(16, false, 93.75), '16.00 %');
  /* Before the gate's width is known there is no ms to show. */
  assert.equal(formatStage(16, true, 0), '16.00 %');
});

test('the unit typed wins over the mode', () => {
  assert.equal(parseStage('15 ms', false, 93.75), 16);
  assert.equal(parseStage('25 %', true, 93.75), 25);
  assert.equal(parseStage('15', true, 93.75), 16, 'bare, in the mode shown');
  assert.equal(parseStage('15', false, 93.75), 15);
  assert.equal(parseStage(' .5 %', true, 100), 0.5);
});

test('typing is clamped to the engine\'s range, and words are no number', () => {
  assert.equal(parseStage('900 %', false, 100), STAGE_MAX_PCT);
  assert.equal(parseStage('-5', false, 100), 0);
  assert.equal(parseStage('9 ms', true, 0), 0, 'no width, no ms');
  assert.equal(parseStage('fast', false, 100), null);
  assert.equal(parseStage('', false, 100), null);
});

test('the store shows and types the stages, and passes the rest through', () => {
  const values = { 4: 0, 8: 0.008 };
  const sent = [];
  const store = {
    value: (i) => values[i] ?? 0,
    text: (i) => `t${i}`,
    commit: (i, v) => sent.push(['commit', i, v]),
    sendText: (i, t) => sent.push(['text', i, t]),
  };
  let ms = true;
  const host = withStages(store, { stages: [8], timeMs: () => ms, widthMs: () => 93.75 });
  assert.equal(host.text(8), '1.5 ms');
  ms = false;
  assert.equal(host.text(8), '1.60 %');
  assert.equal(host.text(4), 't4');
  host.sendText(8, '15 ms');
  host.sendText(8, 'nonsense');
  host.sendText(4, '7');
  assert.deepEqual(sent, [['commit', 8, 16 / STAGE_MAX_PCT], ['text', 4, '7']]);
});
