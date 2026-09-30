/*
 * Side-Chain's capture: six raw bytes a column, as SideChain::SendScope writes
 * them. Copyright (c) 2026 Torben Gräber. MIT.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeScope, COL, STRIDE } from '../src/lib/scope.js';

const bytes = (header, data) =>
  Uint8Array.from([...header].map((c) => c.charCodeAt(0)).concat(data));

test('a column is seen, dry, wet and a UNIPOLAR gain', () => {
  const s = decodeScope(bytes('2:', [1, 0, 255, 64, 191, 255, 0, 128, 128, 128, 128, 0]));
  assert.equal(s.count, 2);
  assert.equal(s.stride, STRIDE);
  assert.equal(s.data[COL.seen], 1);
  assert.equal(s.data[STRIDE + COL.seen], 0);
  assert.equal(s.data[COL.dryLo], -1);
  assert.equal(s.data[COL.dryHi], 1);
  assert.equal(s.data[COL.gain], 1, 'a gain byte of 255 is unity, not +1 of a bipolar');
  assert.equal(s.data[STRIDE + COL.gain], 0);
});

test('a short capture is dropped whole, and a buffer is reused', () => {
  assert.equal(decodeScope(bytes('2:', [1, 0, 0, 0, 0, 0, 0])), null);
  const one = decodeScope(bytes('1:', [1, 128, 128, 128, 128, 255]));
  assert.equal(decodeScope(bytes('1:', [0, 128, 128, 128, 128, 0]), one.data).data, one.data);
});
