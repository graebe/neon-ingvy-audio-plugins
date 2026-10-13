// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The engine's readouts, decoded, and a step edit drawn ahead of the next.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeDetents, decodeEngineParams, decodeUi, applyEdit } from '../src/lib/readouts.js';
import { lengthNorm, MAX_LENGTH } from '../src/lib/msg.js';

const LINE = '0:0:0:0:1/32:15:0.9:0.75:1.6:16:1:16:93.75:1:0:0';

test('the detents are the readout\'s last field, as the engine lists them', () => {
  assert.deepEqual(decodeEngineParams(`${LINE}:16,32,64,128`).detents, [16, 32, 64, 128]);
  assert.deepEqual(decodeEngineParams(`${LINE}:`).detents, [], 'none whole');
  assert.deepEqual(decodeEngineParams(LINE).detents, [], 'an older plugin sends none');
  assert.equal(decodeEngineParams('0:1:2'), null);
});

test('junk in the list is dropped, not guessed at', () => {
  assert.deepEqual(decodeDetents('8,x,,-4,16'), [8, 16]);
  assert.deepEqual(decodeDetents(undefined), []);
});

test('a length is normalised over the parameter\'s 127 intervals', () => {
  assert.equal(lengthNorm(1), 0);
  assert.equal(lengthNorm(MAX_LENGTH), 1);
  assert.equal(lengthNorm(32), 31 / 127);
});

/* steps:ties:length:phase:ms_step:advancing:cursor:depths:orders:slot */
const UI = '5:4:4:1.500:125.00:1:2:FF80FFFF:01000200:3';

test('the ui readout: the pattern, the playhead and the slot', () => {
  const u = decodeUi(UI);
  assert.deepEqual(u.steps, [true, false, true, false]);
  assert.deepEqual(u.ties, [false, false, true, false]);
  assert.equal(u.length, 4);
  assert.equal(u.phase, 1.5);
  assert.equal(u.msStep, 125);
  assert.equal(u.moving, true);
  assert.equal(u.cursor, 2);
  assert.equal(u.depths[1], 128 / 255);
  assert.deepEqual(u.orders, [1, 0, 2, 0]);
  assert.equal(u.slot, 3);
  assert.equal(decodeUi(UI.split(':').slice(0, 9).join(':')).slot, 0, 'an older engine: slot 1');
  assert.equal(decodeUi('1:2:3'), null);
});

test('a step edit is drawn ahead of the readout, and only inside the pattern', () => {
  const u = decodeUi(UI);
  const on = applyEdit(u, { i: 1, mode: 1 });
  assert.equal(on.steps[1], true);
  assert.equal(on.ties[1], false);
  assert.equal(u.steps[1], false, 'the readout itself is not touched');
  const tie = applyEdit(u, { i: 0, mode: 2 });
  assert.equal(tie.steps[0], true);
  assert.equal(tie.ties[0], true);
  const off = applyEdit(u, { i: 2, mode: 0 });
  assert.equal(off.steps[2], false);
  assert.equal(off.ties[2], false);
  /* An amount is held as the engine holds it: a byte. */
  assert.equal(applyEdit(u, { i: 0, amount: 0.5 }).depths[0], 128 / 255);
  assert.equal(applyEdit(u, { i: 9, mode: 1 }), u);
  assert.equal(applyEdit(u, { i: -1, amount: 1 }), u);
});
