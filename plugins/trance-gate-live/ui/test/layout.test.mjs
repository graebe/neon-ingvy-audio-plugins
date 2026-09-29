/*
 * The grid's layout and hit-testing.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * These are the parts of the M4L grid that can be wrong without looking
 * wrong: a hit-test off by one pad, a drag inverted, a row of pads that ends
 * two pixels short of the edge. Each is a fault you would chase in Live with
 * a mouse rather than read in a diff, which is why they live in layout.js
 * with no Max object in reach and are checked here.
 *
 *   node --test ui/test/layout.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { padBoxes, stepAt, depthAt, playheadAt, PADS } from '../layout.js';

test('the pad row ends exactly on the right edge', () => {
  /* 169px of height is fixed; the width is whatever the device is given, and
   * these are awkward on purpose -- 407 and 33 share no factor. */
  for (const [w, n] of [[760, 16], [407, 33], [300, 32], [128, 13], [99, 7]]) {
    const b = padBoxes(w, n);
    assert.equal(b.length, n, `${w}/${n}: one box per step`);
    const last = b[b.length - 1];
    assert.equal(last.x + last.w + PADS.gap, w,
      `${w}/${n}: the last pad must reach the edge`);
    for (let i = 1; i < n; i++)
      assert.ok(b[i].x >= b[i - 1].x + b[i - 1].w, `${w}/${n}: pads overlap at ${i}`);
  }
});

test('every pixel of the strip hits a step, gutters included', () => {
  const w = 407, n = 33;
  const seen = new Set();
  for (let x = 0; x < w; x++) {
    const i = stepAt(x, w, n);
    assert.ok(i >= 0 && i < n, `x=${x} fell through to ${i}`);
    seen.add(i);
  }
  assert.equal(seen.size, n, 'every step must be reachable');
});

test('a hit outside the strip is no step at all', () => {
  assert.equal(stepAt(-1, 100, 16), -1);
  assert.equal(stepAt(100, 100, 16), -1);
  assert.equal(stepAt(50, 0, 16), -1);
});

test('hits land on the step they look like', () => {
  /* 160 wide, 16 steps: exactly 10px each, so the boundaries are unambiguous
   * and an off-by-one has nowhere to hide. */
  assert.equal(stepAt(0, 160, 16), 0);
  assert.equal(stepAt(9, 160, 16), 0);
  assert.equal(stepAt(10, 160, 16), 1);
  assert.equal(stepAt(159, 160, 16), 15);
});

/*
 * THE INVERSION. y counts down and the pad lights from the bottom, so the
 * top of the pad is full depth. Getting this backwards still produces a
 * working drag, which is why it is asserted rather than trusted.
 */
test('dragging up raises the depth', () => {
  assert.equal(depthAt(0, 44), 1, 'the top of the pad is full');
  assert.ok(Math.abs(depthAt(22, 44) - 0.5) < 1e-9, 'the middle is half');
  assert.ok(depthAt(10, 44) > depthAt(30, 44), 'higher is louder');
});

test('a step dragged to the floor still sounds', () => {
  /* Depth 0 and "step off" are different states; a drag must not perform the
   * other one. Turning a step off is a click. */
  assert.equal(depthAt(44, 44), 1 / 255);
  assert.equal(depthAt(9999, 44), 1 / 255);
  assert.equal(depthAt(-50, 44), 1, 'above the pad clamps to full');
});

test('a stopped transport has no playhead', () => {
  assert.equal(playheadAt(null, 0), -1);
  assert.equal(playheadAt({ moving: false, phase: 3, msStep: 125, length: 16, at: 0 }, 0), -1);
});

test('the playhead advances by wall time between readouts', () => {
  const a = { moving: true, phase: 0, msStep: 100, length: 16, at: 1000 };
  assert.equal(playheadAt(a, 1000), 0, 'at the anchor it is the anchor');
  assert.equal(playheadAt(a, 1150), 1, 'one and a half steps on is step 1');
  assert.equal(playheadAt(a, 1350), 3);
  /* And it wraps rather than running off the end of the pattern. */
  assert.equal(playheadAt(a, 1000 + 100 * 16), 0, 'a full cycle is back to 0');
  assert.equal(playheadAt(a, 1000 + 100 * 17), 1);
});

test('no tempo yet means the anchor is the only honest answer', () => {
  const a = { moving: true, phase: 5, msStep: 0, length: 16, at: 0 };
  assert.equal(playheadAt(a, 99999), 5);
});
