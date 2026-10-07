// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * One message per key per window, and nothing lost at the end of a gesture.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createCoalescer, WINDOW_MS } from '../src/lib/coalesce.js';

test('many moves in one window send the last value once', () => {
  const timers = [];
  const sent = [];
  const c = createCoalescer((k, v) => sent.push([k, v]),
                            { after: (f, ms) => timers.push([f, ms]), cancel: () => {} });
  for (const v of [0.1, 0.2, 0.3, 0.4]) c.push(5, v);
  c.push(6, 0.9);
  assert.deepEqual(sent, [], 'nothing leaves before the window closes');
  assert.equal(timers.length, 1, 'one timer, not one per move');
  assert.equal(timers[0][1], WINDOW_MS);
  timers[0][0]();
  assert.deepEqual(sent, [[5, 0.4], [6, 0.9]]);
});

test('flush sends what is waiting, once', () => {
  const sent = [];
  let cancelled = 0;
  const c = createCoalescer((k, v) => sent.push(v), { after: () => 7, cancel: () => cancelled++ });
  c.push(1, 0.5);
  c.flush();
  c.flush();
  assert.deepEqual(sent, [0.5]);
  assert.equal(cancelled, 1);
  assert.equal(c.pending(), 0);
});

test('the real timer sends a drag with no display frame', async () => {
  const sent = [];
  const c = createCoalescer((k, v) => sent.push(v));
  c.push(1, 0.25);
  c.push(1, 0.75);
  await new Promise((r) => setTimeout(r, WINDOW_MS * 4));
  assert.deepEqual(sent, [0.75]);
});
