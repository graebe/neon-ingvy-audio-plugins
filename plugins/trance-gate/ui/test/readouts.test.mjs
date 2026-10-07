// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The `params` readout's Length detents, decoded.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeDetents, decodeEngineParams } from '../src/lib/readouts.js';
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
