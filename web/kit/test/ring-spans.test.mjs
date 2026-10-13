// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A batch of columns is put back in one span, or two when the ring wraps.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ringSpans } from '../src/lib/ring-spans.js';

test('a batch that fits is one span', () => {
  assert.deepEqual(ringSpans(10, 3, 606), [[10, 12]]);
  assert.deepEqual(ringSpans(603, 3, 606), [[603, 605]]);
});

test('a batch across the end is two', () => {
  assert.deepEqual(ringSpans(604, 5, 606), [[604, 605], [0, 2]]);
});

test('nothing written is nothing to put, and a batch never exceeds the ring', () => {
  assert.deepEqual(ringSpans(5, 0, 606), []);
  assert.deepEqual(ringSpans(0, 700, 606), [[0, 605]]);
});
