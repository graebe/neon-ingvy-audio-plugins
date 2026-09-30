/*
 * One message per key per frame, and nothing lost at the end of a gesture.
 * Copyright (c) 2026 Torben Gräber. MIT.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createCoalescer } from '../src/lib/coalesce.js';

test('many moves in one frame send the last value once', () => {
  const frames = [];
  const sent = [];
  const c = createCoalescer((k, v) => sent.push([k, v]),
                            { raf: (f) => frames.push(f), caf: () => {} });
  for (const v of [0.1, 0.2, 0.3, 0.4]) c.push(5, v);
  c.push(6, 0.9);
  assert.deepEqual(sent, [], 'nothing leaves before the frame');
  assert.equal(frames.length, 1, 'one frame requested, not one per move');
  frames[0]();
  assert.deepEqual(sent, [[5, 0.4], [6, 0.9]]);
});

test('flush sends what is waiting, once', () => {
  const sent = [];
  let cancelled = 0;
  const c = createCoalescer((k, v) => sent.push(v), { raf: () => 7, caf: () => cancelled++ });
  c.push(1, 0.5);
  c.flush();
  c.flush();
  assert.deepEqual(sent, [0.5]);
  assert.equal(cancelled, 1);
  assert.equal(c.pending(), 0);
});
