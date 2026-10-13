// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ground's rings, found in the playhead (src/lib/beats.js).
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { beatsPerStep, createBeats } from '../src/lib/beats.js';

test('a Rate is so many quarter notes a step', () => {
  assert.equal(beatsPerStep('1/16'), 0.25);
  assert.equal(beatsPerStep('1/4'), 1);
  assert.equal(beatsPerStep('1/8T'), 1 / 3);
  assert.equal(beatsPerStep('1/1T'), 8 / 3);
  assert.equal(beatsPerStep('fast'), null);
  assert.equal(beatsPerStep(undefined), null);
});

/* Walk the playhead from `from` to `to` in `dt` steps and collect the rings. */
function walk(b, from, to, dt, length, rate) {
  const out = [];
  for (let p = from; p <= to + 1e-9; p += dt) {
    const r = b.next(p % length, length, rate);
    if (r !== null) out.push([Math.round((p % length) * 100) / 100, r]);
  }
  return out;
}

test('at 1/16, a ring every fourth step, full at the cycle\'s start', () => {
  const b = createBeats();
  const rings = walk(b, 0, 16.5, 0.25, 16, '1/16');
  assert.deepEqual(rings, [[4, 0.4], [8, 0.4], [12, 0.4], [0, 1]]);
});

test('a correction backwards does not ring again', () => {
  const b = createBeats();
  b.next(3.9, 16, '1/16');
  assert.equal(b.next(4.05, 16, '1/16'), 0.4);
  assert.equal(b.next(3.95, 16, '1/16'), null, 'the anchor landed a little behind');
  assert.equal(b.next(4.1, 16, '1/16'), null, 'and that beat has rung');
});

test('a stop forgets the place, so the next start does not ring at once', () => {
  const b = createBeats();
  b.next(1, 16, '1/16');
  b.reset();
  assert.equal(b.next(9, 16, '1/16'), null);
  assert.equal(b.next(12.1, 16, '1/16'), 0.4);
});

test('no Rate, no rings', () => {
  const b = createBeats();
  assert.equal(b.next(1, 16, undefined), null);
  assert.equal(b.next(5, 16, undefined), null);
});
